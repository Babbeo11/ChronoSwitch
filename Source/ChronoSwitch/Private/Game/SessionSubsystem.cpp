// Fill out your copyright notice in the Description page of Project Settings.

#include "Game/SessionSubsystem.h"

#include "OnlineSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemNames.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"

#ifndef SEARCH_PRESENCE
#define SEARCH_PRESENCE FName(TEXT("PRESENCESEARCH"))
#endif

#ifndef SEARCH_LOBBIES
#define SEARCH_LOBBIES FName(TEXT("LOBBIESSEARCH"))
#endif


USessionSubsystem::USessionSubsystem()
	: bCreateSessionAfterDestroy(false)
	, bReturnToMenuAfterDestroy(false)
	, bJoinInviteAfterDestroy(false)
	, PendingInviteLocalUserNum(0)
{
}

void USessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("SessionSubsystem Initialize failed: SessionInterface invalid"));
		return;
	}

	InviteAcceptedDelegateHandle = SessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
		FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &USessionSubsystem::OnInviteAccepted));

	InviteReceivedDelegateHandle = SessionInterface->AddOnSessionInviteReceivedDelegate_Handle(
		FOnSessionInviteReceivedDelegate::CreateUObject(this, &USessionSubsystem::OnInviteReceived));

	LoginIfNeeded();
}

void USessionSubsystem::Deinitialize()
{
	if (IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld()); Subsystem && LoginCompleteDelegateHandle.IsValid())
	{
		if (IOnlineIdentityPtr Identity = Subsystem->GetIdentityInterface(); Identity.IsValid())
		{
			Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginCompleteDelegateHandle);
		}
	}

	if (IOnlineSessionPtr SessionInterface = GetSessionInterface(); SessionInterface.IsValid())
	{
		SessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedDelegateHandle);
		SessionInterface->ClearOnSessionInviteReceivedDelegate_Handle(InviteReceivedDelegateHandle);
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}

	Super::Deinitialize();
}

IOnlineSessionPtr USessionSubsystem::GetSessionInterface() const
{
	if (const UWorld* World = GetWorld())
	{
		if (IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(World))
		{
			return OnlineSubsystem->GetSessionInterface();
		}
	}

	return nullptr;
}

bool USessionSubsystem::IsUsingEOS() const
{
	const IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	return Subsystem && Subsystem->GetSubsystemName() == EOS_SUBSYSTEM;
}

bool USessionSubsystem::IsOnlineServiceReady() const
{
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	if (!Subsystem)
	{
		return false;
	}

	// Steam and Null authenticate implicitly; only EOS needs an explicit login first
	if (Subsystem->GetSubsystemName() != EOS_SUBSYSTEM)
	{
		return true;
	}

	const IOnlineIdentityPtr Identity = Subsystem->GetIdentityInterface();
	return Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn;
}

bool USessionSubsystem::IsExternalInviteAvailable() const
{
	const IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetWorld());
	return Subsystem && Subsystem->GetSubsystemName() == STEAM_SUBSYSTEM;
}

void USessionSubsystem::LoginIfNeeded()
{
	if (!IsUsingEOS() || IsOnlineServiceReady() || bLoginInProgress)
	{
		return;
	}

	IOnlineIdentityPtr Identity = Online::GetSubsystem(GetWorld())->GetIdentityInterface();
	if (!Identity.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("EOS login failed: Identity interface is invalid"));
		return;
	}

	// Catch unconfigured credentials early with a clear message instead of opaque EOS SDK errors
	TArray<FString> Artifacts;
	GConfig->GetArray(TEXT("/Script/OnlineSubsystemEOS.EOSSettings"), TEXT("Artifacts"), Artifacts, GEngineIni);
	for (const FString& Artifact : Artifacts)
	{
		if (Artifact.Contains(TEXT("PLACEHOLDER")))
		{
			UE_LOG(LogTemp, Error, TEXT("EOS credentials in DefaultEngine.ini are still placeholders. Fill in the Epic Dev Portal values under [/Script/OnlineSubsystemEOS.EOSSettings] before running with -CustomConfig=EOS"));
			OnLoginFinishedSignal.Broadcast(false, TEXT("EOS credentials are not configured"));
			return;
		}
	}

	if (!LoginCompleteDelegateHandle.IsValid())
	{
		LoginCompleteDelegateHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
			FOnLoginCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnLoginComplete));
	}

	// Anonymous per-device account; Id carries the display name other players see as host name.
	// EOS caps Device ID display names at 32 characters.
	FOnlineAccountCredentials Credentials;
	Credentials.Type = TEXT("externalauth:DeviceIdAccessToken");
	Credentials.Id = FString(FPlatformProcess::ComputerName()).Left(32);

	UE_LOG(LogTemp, Log, TEXT("Starting EOS Device ID login as '%s'"), *Credentials.Id);
	bLoginInProgress = true;
	Identity->Login(0, Credentials);
}

void USessionSubsystem::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	bLoginInProgress = false;
	UE_LOG(LogTemp, Log, TEXT("EOS login complete: success=%s %s"), bWasSuccessful ? TEXT("true") : TEXT("false"), *Error);
	OnLoginFinishedSignal.Broadcast(bWasSuccessful, Error);
}


void USessionSubsystem::HostSession(int32 MaxPlayers, bool bIsLAN)
{
	FSessionCreateSettings SessionCreateSettings;
	SessionCreateSettings.MaxPlayers = MaxPlayers;
	SessionCreateSettings.bIsLAN = bIsLAN;

	HostSessionWithSettings(SessionCreateSettings);
}

void USessionSubsystem::HostSessionWithSettings(const FSessionCreateSettings& SessionCreateSettings)
{
	CreateSessionWithSettings(SessionCreateSettings);
}

void USessionSubsystem::CreateSession(int32 NumPublicConnections, bool bIsLAN)
{
	FSessionCreateSettings SessionCreateSettings;
	SessionCreateSettings.MaxPlayers = NumPublicConnections;
	SessionCreateSettings.bIsLAN = bIsLAN;

	CreateSessionWithSettings(SessionCreateSettings);
}

void USessionSubsystem::CreateSessionWithSettings(const FSessionCreateSettings& SessionCreateSettings)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSession failed: SessionInterface invalid"));
		OnCreateSessionFinishedSignal.Broadcast(false, TEXT("Session interface is invalid"));
		return;
	}

	if (!IsOnlineServiceReady())
	{
		LoginIfNeeded(); // retry the startup login in case it failed
		OnCreateSessionFinishedSignal.Broadcast(false, TEXT("Online login not completed yet, try again in a moment"));
		return;
	}

	LastSessionCreateSettings = SessionCreateSettings;

	if (SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Existing session found, destroying it before creating a new one"));

		bCreateSessionAfterDestroy = true;
		bReturnToMenuAfterDestroy = false;
		bJoinInviteAfterDestroy = false;

		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DestroySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnDestroySessionComplete));

		if (!SessionInterface->DestroySession(NAME_GameSession))
		{
			UE_LOG(LogTemp, Warning, TEXT("DestroySession returned false while trying to recreate session"));
			bCreateSessionAfterDestroy = false;
			OnDestroySessionFinishedSignal.Broadcast(false, TEXT("DestroySession returned false while trying to recreate session"));
			OnCreateSessionFinishedSignal.Broadcast(false, TEXT("Could not destroy existing session before creating a new one"));
		}

		return;
	}

	FOnlineSessionSettings OnlineSessionSettings;
	OnlineSessionSettings.bIsLANMatch = SessionCreateSettings.bIsLAN;
	OnlineSessionSettings.NumPublicConnections = SessionCreateSettings.MaxPlayers;
	OnlineSessionSettings.bAllowJoinInProgress = SessionCreateSettings.bAllowJoinInProgress;
	OnlineSessionSettings.bAllowJoinViaPresence = SessionCreateSettings.bAllowJoinViaPresence;
	OnlineSessionSettings.bShouldAdvertise = SessionCreateSettings.bShouldAdvertise;
	OnlineSessionSettings.bUsesPresence = SessionCreateSettings.bUsesPresence;
	OnlineSessionSettings.bUseLobbiesIfAvailable = SessionCreateSettings.bUseLobbiesIfAvailable;
	OnlineSessionSettings.BuildUniqueId = (SessionCreateSettings.BuildUniqueId != 0)
		? SessionCreateSettings.BuildUniqueId
		: GetBuildUniqueId();

	if (IsUsingEOS())
	{
		// Presence requires Epic accounts (EAS); with anonymous Device ID login it must stay off.
		// Lobbies still work without presence.
		OnlineSessionSettings.bUsesPresence = false;
		OnlineSessionSettings.bAllowJoinViaPresence = false;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== HOSTING SESSION INFO ==="));
	UE_LOG(LogTemp, Warning, TEXT("Name: %s | Project: %s"), *SessionCreateSettings.SessionName, *SESSION_SETTING_PROJECT_NAME.ToString());
	UE_LOG(LogTemp, Warning, TEXT("BuildUniqueId: %d | LAN: %s"), OnlineSessionSettings.BuildUniqueId, OnlineSessionSettings.bIsLANMatch ? TEXT("YES") : TEXT("NO"));

	OnlineSessionSettings.Set(SESSION_SETTING_SERVER_NAME, SessionCreateSettings.SessionName, EOnlineDataAdvertisementType::ViaOnlineService);
	OnlineSessionSettings.Set(SESSION_SETTING_PROJECT_NAME, SESSION_SETTING_PROJECT_NAME.ToString(), EOnlineDataAdvertisementType::ViaOnlineService);

	SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	CreateSessionCompleteDelegateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnCreateSessionComplete));

	if (!SessionInterface->CreateSession(0, NAME_GameSession, OnlineSessionSettings))
	{
		UE_LOG(LogTemp, Warning, TEXT("CreateSession returned false"));
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
		OnCreateSessionFinishedSignal.Broadcast(false, TEXT("CreateSession returned false"));
	}
}

void USessionSubsystem::FindSessions(bool bIsLAN, int32 MaxSearchResults)
{
	FindSession(MaxSearchResults, bIsLAN);
}

void USessionSubsystem::FindSession(int32 MaxSearchResults, bool bIsLAN)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FindSession failed: SessionInterface invalid"));
		OnFindSessionsFinishedSignal.Broadcast(false, 0);
		return;
	}

	if (!IsOnlineServiceReady())
	{
		LoginIfNeeded(); // retry the startup login in case it failed
		OnFindSessionsFinishedSignal.Broadcast(false, 0);
		return;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->bIsLanQuery = bIsLAN;
	SessionSearch->MaxSearchResults = MaxSearchResults;
	if (!IsUsingEOS())
	{
		// Steam-only flag: on EOS it would be treated as a lobby attribute filter
		// that no host advertises, returning zero results
		SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	}
	if (!bIsLAN)
	{
		SessionSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}
	SessionSearch->QuerySettings.Set(SESSION_SETTING_PROJECT_NAME, SESSION_SETTING_PROJECT_NAME.ToString(), EOnlineComparisonOp::Equals);

	SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	FindSessionsCompleteDelegateHandle = SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnFindSessionsComplete));

	if (!SessionInterface->FindSessions(0, SessionSearch.ToSharedRef()))
	{
		UE_LOG(LogTemp, Warning, TEXT("FindSessions returned false"));
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
		OnFindSessionsFinishedSignal.Broadcast(false, 0);
	}
}

void USessionSubsystem::JoinSessionByIndex(int32 SessionIndex)
{
	if (!SessionSearch.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("JoinSessionByIndex failed: SessionSearch invalid"));
		OnJoinSessionFinishedSignal.Broadcast(false, TEXT("Session search is invalid"));
		return;
	}

	if (!SessionSearch->SearchResults.IsValidIndex(SessionIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("JoinSessionByIndex failed: invalid index %d"), SessionIndex);
		OnJoinSessionFinishedSignal.Broadcast(false, TEXT("Invalid session index"));
		return;
	}

	JoinSession(SessionSearch->SearchResults[SessionIndex]);
}

int32 USessionSubsystem::GetFoundSessionsCount() const
{
	if (!SessionSearch.IsValid())
	{
		return 0;
	}

	return SessionSearch->SearchResults.Num();
}

FSessionSearchResultInfo USessionSubsystem::MakeSessionInfoFromSearchResult(const FOnlineSessionSearchResult& SearchResult, int32 SessionIndex) const
{
	FSessionSearchResultInfo SessionInfo;

	if (!SearchResult.IsValid())
	{
		return SessionInfo;
	}

	const FOnlineSession& Session = SearchResult.Session;
	const FOnlineSessionSettings& Settings = Session.SessionSettings;

	const int32 MaxPlayers = Settings.NumPublicConnections;
	const int32 OpenPublicConnections = Session.NumOpenPublicConnections;
	const int32 CurrentPlayers = FMath::Clamp(MaxPlayers - OpenPublicConnections, 0, MaxPlayers);

	FString CustomSessionName;
	if (Settings.Get(SESSION_SETTING_SERVER_NAME, CustomSessionName))
	{
		SessionInfo.SessionName = CustomSessionName;
	}
	else
	{
		SessionInfo.SessionName = TEXT("Default Session");
	}

	SessionInfo.SessionIndex = SessionIndex;
	SessionInfo.HostName = Session.OwningUserName.IsEmpty() ? TEXT("Unknown Host") : Session.OwningUserName;
	SessionInfo.CurrentPlayers = CurrentPlayers;
	SessionInfo.MaxPlayers = MaxPlayers;
	SessionInfo.OpenPublicConnections = OpenPublicConnections;
	SessionInfo.PingInMs = SearchResult.PingInMs;
	SessionInfo.bIsLAN = Settings.bIsLANMatch;
	SessionInfo.bIsValid = true;

	return SessionInfo;
}

FSessionSearchResultInfo USessionSubsystem::GetFoundSessionInfo(int32 SessionIndex) const
{
	if (!SessionSearch.IsValid() || !SessionSearch->SearchResults.IsValidIndex(SessionIndex))
	{
		return FSessionSearchResultInfo();
	}

	return MakeSessionInfoFromSearchResult(SessionSearch->SearchResults[SessionIndex], SessionIndex);
}

TArray<FSessionSearchResultInfo> USessionSubsystem::GetFoundSessionsInfo() const
{
	TArray<FSessionSearchResultInfo> FoundSessionsInfo;

	if (!SessionSearch.IsValid())
	{
		return FoundSessionsInfo;
	}

	FoundSessionsInfo.Reserve(SessionSearch->SearchResults.Num());

	for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
	{
		FoundSessionsInfo.Add(MakeSessionInfoFromSearchResult(SessionSearch->SearchResults[Index], Index));
	}

	return FoundSessionsInfo;
}

FString USessionSubsystem::GetFoundSessionDisplayString(int32 SessionIndex) const
{
	const FSessionSearchResultInfo SessionInfo = GetFoundSessionInfo(SessionIndex);

	if (!SessionInfo.bIsValid)
	{
		return TEXT("Invalid Session");
	}

	return FString::Printf(
		TEXT("%s (%s) - Players: %d/%d - Ping: %d ms"),
		*SessionInfo.SessionName,
		*SessionInfo.HostName,
		SessionInfo.CurrentPlayers,
		SessionInfo.MaxPlayers,
		SessionInfo.PingInMs
	);
}

void USessionSubsystem::JoinSession(const FOnlineSessionSearchResult& SessionSearchResult)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("JoinSession failed: SessionInterface invalid"));
		OnJoinSessionFinishedSignal.Broadcast(false, TEXT("Session interface is invalid"));
		return;
	}

	SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnJoinSessionComplete));

	if (!SessionInterface->JoinSession(0, NAME_GameSession, SessionSearchResult))
	{
		UE_LOG(LogTemp, Warning, TEXT("JoinSession returned false"));
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
		OnJoinSessionFinishedSignal.Broadcast(false, TEXT("JoinSession returned false"));
	}
}

void USessionSubsystem::QuitSession()
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("QuitSession failed: SessionInterface invalid"));
		OnLeaveSessionFinishedSignal.Broadcast(false, TEXT("Session interface is invalid"));
		return;
	}

	if (SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
	{
		bCreateSessionAfterDestroy = false;
		bReturnToMenuAfterDestroy = true;
		bJoinInviteAfterDestroy = false;

		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DestroySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnDestroySessionComplete));

		if (!SessionInterface->DestroySession(NAME_GameSession))
		{
			UE_LOG(LogTemp, Warning, TEXT("DestroySession returned false while quitting session"));
			bReturnToMenuAfterDestroy = false;
			OnDestroySessionFinishedSignal.Broadcast(false, TEXT("DestroySession returned false while quitting session"));
			OnLeaveSessionFinishedSignal.Broadcast(false, TEXT("DestroySession returned false while quitting session"));
		}

		return;
	}

	OnLeaveSessionFinishedSignal.Broadcast(true, FString());
}

void USessionSubsystem::OpenExternalInviteDialog()
{
	if (const UWorld* World = GetWorld())
	{
		if (IOnlineSubsystem* Subsystem = Online::GetSubsystem(World))
		{
			IOnlineExternalUIPtr ExternalUI = Subsystem->GetExternalUIInterface();
			if (ExternalUI.IsValid())
			{
				ExternalUI->ShowInviteUI(0, NAME_GameSession);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("OpenExternalInviteDialog failed: ExternalUI interface is invalid"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("OpenExternalInviteDialog failed: OnlineSubsystem is invalid"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("OpenExternalInviteDialog failed: World is invalid"));
	}
}

void USessionSubsystem::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("OnDestroySessionComplete: %s, success=%s"), *SessionName.ToString(), bWasSuccessful ? TEXT("true") : TEXT("false"));

	const bool bShouldCreateSession = bCreateSessionAfterDestroy;
	const bool bShouldReturnToMenu = bReturnToMenuAfterDestroy;
	const bool bShouldJoinInvite = bJoinInviteAfterDestroy;

	bCreateSessionAfterDestroy = false;
	bReturnToMenuAfterDestroy = false;
	bJoinInviteAfterDestroy = false;

	if (!bWasSuccessful)
	{
		OnDestroySessionFinishedSignal.Broadcast(false, TEXT("Failed to destroy session"));

		if (bShouldReturnToMenu)
		{
			OnLeaveSessionFinishedSignal.Broadcast(false, TEXT("Failed to destroy session while leaving"));
		}

		return;
	}

	OnDestroySessionFinishedSignal.Broadcast(true, FString());

	if (bShouldCreateSession)
	{
		CreateSessionWithSettings(LastSessionCreateSettings);
		return;
	}

	if (bShouldJoinInvite)
	{
		JoinSession(PendingInviteResult);
		return;
	}

	if (bShouldReturnToMenu)
	{
		OnLeaveSessionComplete(SessionName, bWasSuccessful);
	}
}

void USessionSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteDelegateHandle);
	}

	UE_LOG(LogTemp, Warning, TEXT("OnCreateSessionComplete: %s, success=%s"), *SessionName.ToString(), bWasSuccessful ? TEXT("true") : TEXT("false"));

	if (!bWasSuccessful)
	{
		OnCreateSessionFinishedSignal.Broadcast(false, TEXT("Failed to create session"));
		return;
	}

	OnCreateSessionFinishedSignal.Broadcast(true, FString());
}

void USessionSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteDelegateHandle);
	}
	
	if (SessionSearch.IsValid())
	{
		for (const FOnlineSessionSearchResult& RawResult : SessionSearch->SearchResults)
		{
			FString FoundProject;
			RawResult.Session.SessionSettings.Get(SESSION_SETTING_PROJECT_NAME, FoundProject);
			UE_LOG(LogTemp, Warning, TEXT("RAW SESSION FOUND: Host: %s | Project: %s | BuildID: %d"), 
				*RawResult.Session.OwningUserName, *FoundProject, RawResult.Session.SessionSettings.BuildUniqueId);
		}
	}

	const int32 NumResults = SessionSearch.IsValid() ? SessionSearch->SearchResults.Num() : 0;

	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FindSessions failed"));
		OnFindSessionsFinishedSignal.Broadcast(false, 0);
		return;
	}

	if (NumResults == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FindSessions completed successfully, but no sessions were found"));
		OnFindSessionsFinishedSignal.Broadcast(true, 0);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("FindSessions completed successfully. Found %d session(s)"), NumResults);
	OnFindSessionsFinishedSignal.Broadcast(true, NumResults);
}

void USessionSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		const FString ErrorMessage = FString::Printf(TEXT("JoinSession failed with result: %d"), static_cast<int32>(Result));
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ErrorMessage);
		OnJoinSessionFinishedSignal.Broadcast(false, ErrorMessage);
		return;
	}

	FString JoinURL;
	if (SessionInterface.IsValid() && SessionInterface->GetResolvedConnectString(SessionName, JoinURL))
	{
		UE_LOG(LogTemp, Warning, TEXT("JoinSession succeeded. Resolved connect string: %s"), *JoinURL);
		OnJoinSessionFinishedSignal.Broadcast(true, JoinURL);
		return;
	}

	const FString ErrorMessage = FString::Printf(TEXT("Failed to resolve connect string for session: %s"), *SessionName.ToString());
	UE_LOG(LogTemp, Warning, TEXT("%s"), *ErrorMessage);
	OnJoinSessionFinishedSignal.Broadcast(false, ErrorMessage);
}

void USessionSubsystem::OnLeaveSessionComplete(FName SessionName, bool bWasSuccessful)
{
	OnLeaveSessionFinishedSignal.Broadcast(bWasSuccessful, bWasSuccessful ? FString() : TEXT("Failed to leave session"));
}

void USessionSubsystem::OnInviteAccepted(const bool bWasSuccessful, const int32 LocalUserNum, TSharedPtr<const FUniqueNetId> PersonInviting, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !InviteResult.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Invite accepted callback failed or invite is invalid"));
		return;
	}

	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		return;
	}

	if (SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
	{
		PendingInviteLocalUserNum = LocalUserNum;
		PendingInviteResult = InviteResult;

		bCreateSessionAfterDestroy = false;
		bReturnToMenuAfterDestroy = false;
		bJoinInviteAfterDestroy = true;

		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteDelegateHandle);
		DestroySessionCompleteDelegateHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnDestroySessionComplete));

		if (!SessionInterface->DestroySession(NAME_GameSession))
		{
			UE_LOG(LogTemp, Warning, TEXT("DestroySession returned false before joining invite"));
			bJoinInviteAfterDestroy = false;
		}

		return;
	}

	SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	JoinSessionCompleteDelegateHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &USessionSubsystem::OnJoinSessionComplete));

	if (!SessionInterface->JoinSession(LocalUserNum, NAME_GameSession, InviteResult))
	{
		UE_LOG(LogTemp, Warning, TEXT("JoinSession from invite returned false"));
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteDelegateHandle);
	}
}

void USessionSubsystem::OnInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult)
{
	OnInviteReceivedSignal.Broadcast();
}