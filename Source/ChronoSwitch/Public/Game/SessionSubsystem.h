// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "SessionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSessionInviteReceivedSignal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCreateSessionFinishedSignal, bool, bWasSuccessful, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFindSessionsFinishedSignal, bool, bWasSuccessful, int32, NumResults);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJoinSessionFinishedSignal, bool, bWasSuccessful, FString, ConnectStringOrErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDestroySessionFinishedSignal, bool, bWasSuccessful, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLeaveSessionFinishedSignal, bool, bWasSuccessful, FString, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoginFinishedSignal, bool, bWasSuccessful, FString, ErrorMessage);

#define SESSION_SETTING_PROJECT_NAME FName(TEXT("CHRONO:SWITCH"))
#define SESSION_SETTING_SERVER_NAME FName(TEXT("SERVER_NAME"))


USTRUCT(BlueprintType)
struct FSessionCreateSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	FString SessionName = TEXT("New Session");
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	int32 MaxPlayers = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	bool bIsLAN = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	bool bAllowJoinInProgress = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	bool bAllowJoinViaPresence = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	bool bShouldAdvertise = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	bool bUsesPresence = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	bool bUseLobbiesIfAvailable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Session")
	int32 BuildUniqueId = 0;
};

USTRUCT(BlueprintType)
struct FSessionSearchResultInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 SessionIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	FString SessionName;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 OpenPublicConnections = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 PingInMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	bool bIsLAN = false;

	UPROPERTY(BlueprintReadOnly, Category = "Session")
	bool bIsValid = false;
};

/**
 * Handles Online Subsystem session logic for hosting, finding, joining, leaving and Steam invites.
 */
UCLASS()
class CHRONOSWITCH_API USessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	USessionSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Session")
	void HostSession(int32 MaxPlayers = 2, bool bIsLAN = false);

	UFUNCTION(BlueprintCallable, Category = "Session")
	void HostSessionWithSettings(const FSessionCreateSettings& SessionCreateSettings);

	UFUNCTION(BlueprintCallable, Category = "Session")
	void FindSessions(bool bIsLAN = false, int32 MaxSearchResults = 100);

	UFUNCTION(BlueprintCallable, Category = "Session")
	void JoinSessionByIndex(int32 SessionIndex);

	UFUNCTION(BlueprintCallable, Category = "Session")
	int32 GetFoundSessionsCount() const;

	UFUNCTION(BlueprintCallable, Category = "Session")
	FSessionSearchResultInfo GetFoundSessionInfo(int32 SessionIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Session")
	TArray<FSessionSearchResultInfo> GetFoundSessionsInfo() const;

	UFUNCTION(BlueprintCallable, Category = "Session")
	FString GetFoundSessionDisplayString(int32 SessionIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Session")
	void QuitSession();

	UFUNCTION(BlueprintCallable, Category = "Session")
	void OpenExternalInviteDialog();

	/** True when session calls can be made. Always true on Steam/LAN; on EOS, true once the automatic Device ID login has completed. */
	UFUNCTION(BlueprintPure, Category = "Session")
	bool IsOnlineServiceReady() const;

	/** True when the platform has an invite overlay (Steam). Bind the invite button's visibility to this. */
	UFUNCTION(BlueprintPure, Category = "Session")
	bool IsExternalInviteAvailable() const;

	void CreateSession(int32 NumPublicConnections, bool bIsLAN);
	void CreateSessionWithSettings(const FSessionCreateSettings& SessionCreateSettings);
	void FindSession(int32 MaxSearchResults, bool bIsLAN);
	void JoinSession(const FOnlineSessionSearchResult& SessionSearchResult);

	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnSessionInviteReceivedSignal OnInviteReceivedSignal;

	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnCreateSessionFinishedSignal OnCreateSessionFinishedSignal;

	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnFindSessionsFinishedSignal OnFindSessionsFinishedSignal;

	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnJoinSessionFinishedSignal OnJoinSessionFinishedSignal;

	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnDestroySessionFinishedSignal OnDestroySessionFinishedSignal;

	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnLeaveSessionFinishedSignal OnLeaveSessionFinishedSignal;

	/** Broadcast when the EOS Device ID login finishes. Never fires on Steam (no explicit login needed). */
	UPROPERTY(BlueprintAssignable, Category = "Session|Events")
	FOnLoginFinishedSignal OnLoginFinishedSignal;

private:
	void LoginIfNeeded();
	void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	bool IsUsingEOS() const;
	IOnlineSessionPtr GetSessionInterface() const;

	FSessionSearchResultInfo MakeSessionInfoFromSearchResult(const FOnlineSessionSearchResult& SearchResult, int32 SessionIndex) const;

	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnLeaveSessionComplete(FName SessionName, bool bWasSuccessful);

	void OnInviteAccepted(const bool bWasSuccessful, const int32 LocalUserNum, TSharedPtr<const FUniqueNetId> PersonInviting, const FOnlineSessionSearchResult& InviteResult);
	void OnInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult);

	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	FSessionCreateSettings LastSessionCreateSettings;

	bool bCreateSessionAfterDestroy;
	bool bReturnToMenuAfterDestroy;
	bool bJoinInviteAfterDestroy;

	int32 PendingInviteLocalUserNum;
	FOnlineSessionSearchResult PendingInviteResult;

	bool bLoginInProgress = false;
	FDelegateHandle LoginCompleteDelegateHandle;

	FDelegateHandle DestroySessionCompleteDelegateHandle;
	FDelegateHandle CreateSessionCompleteDelegateHandle;
	FDelegateHandle FindSessionsCompleteDelegateHandle;
	FDelegateHandle JoinSessionCompleteDelegateHandle;
	FDelegateHandle InviteAcceptedDelegateHandle;
	FDelegateHandle InviteReceivedDelegateHandle;
};