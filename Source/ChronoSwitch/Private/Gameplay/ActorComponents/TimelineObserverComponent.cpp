#include "Gameplay/ActorComponents/TimelineObserverComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Kismet/GameplayStatics.h"

UTimelineObserverComponent::UTimelineObserverComponent()
{
	// This component does not need to tick. It operates entirely on events.
	PrimaryComponentTick.bCanEverTick = false;
}

ECollisionChannel UTimelineObserverComponent::GetCollisionChannelForTimeline(uint8 Timeline)
{
	return (Timeline == 0) ? CHANNEL_PAST : CHANNEL_FUTURE;
}

ECollisionChannel UTimelineObserverComponent::GetCollisionTraceChannelForTimeline(uint8 Timeline)
{
	return (Timeline == 0) ? CHANNEL_TRACE_PAST : CHANNEL_TRACE_FUTURE;
}

bool UTimelineObserverComponent::IsVisorActive() const
{
	if (CachedPlayerState.IsValid())
	{
		return CachedPlayerState->IsVisorActive();
	}
	// Default to false if the PlayerState is not yet cached or has been destroyed.
	return false;
}

void UTimelineObserverComponent::BeginPlay()
{
	Super::BeginPlay();

	// Start the process of binding to the local player's PlayerState.
	InitializeBinding();
}

void UTimelineObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from delegates to prevent memory leaks and dangling pointers when the component is destroyed.
	if (CachedPlayerState.IsValid())
	{
		CachedPlayerState->OnTimelineIDChanged.Remove(OnTimelineIDChangedHandle);
		CachedPlayerState->OnVisorStateChanged.Remove(OnVisorStateChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UTimelineObserverComponent::InitializeBinding()
{
	// Clear any pending retry timers to prevent multiple timers running.
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RetryTimerHandle);
	}

	const APlayerController* LocalPC = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	// Ensure we only bind on the local client/player observing the timeline
	if (LocalPC && LocalPC->IsLocalController() && LocalPC->PlayerState)
	{
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(LocalPC->PlayerState))
		{
			CachedPlayerState = PS;

			OnTimelineIDChangedHandle = PS->OnTimelineIDChanged.AddUObject(this, &UTimelineObserverComponent::HandleTimelineChanged);
			OnVisorStateChangedHandle = PS->OnVisorStateChanged.AddUObject(this, &UTimelineObserverComponent::HandleVisorStateChanged);
			
			// Defer the initial broadcast to the next frame. This ensures all listeners (like ATimelineBaseActor)
			// have had time to subscribe in their own BeginPlay, resolving initialization race conditions.
			FTimerHandle TempHandle;
			GetWorld()->GetTimerManager().SetTimer(TempHandle, this, &UTimelineObserverComponent::DeferredInitialBroadcast, 0.001f, false);
			return;
		}
	}
	
	// If PlayerState isn't ready yet (common during initial spawn), retry shortly
	// This component has no purpose on a dedicated server, as there is no local player to observe.
	if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		GetWorld()->GetTimerManager().SetTimer(RetryTimerHandle, this, &UTimelineObserverComponent::InitializeBinding, BINDING_RETRY_DELAY, false);
	}
}

void UTimelineObserverComponent::HandleTimelineChanged(uint8 NewTimelineID)
{
	UpdateTimelineState(NewTimelineID);
}

void UTimelineObserverComponent::HandleVisorStateChanged(bool bNewState)
{
	// The new state is implicitly handled by UpdateTimelineState, which reads the latest state from the PlayerState.
	if (CachedPlayerState.IsValid())
	{
		UpdateTimelineState(CachedPlayerState->GetTimelineID());
	}
}

void UTimelineObserverComponent::DeferredInitialBroadcast()
{
	if (CachedPlayerState.IsValid())
	{
		UpdateTimelineState(CachedPlayerState->GetTimelineID());
	}
}

void UTimelineObserverComponent::UpdateTimelineState(uint8 CurrentTimelineID)
{
	if (!CachedPlayerState.IsValid()) 
		return;
	
	// This component's only job is to get the latest state from the PlayerState and broadcast it.
	// It has no knowledge of what the owner will do with this information.
	const bool bIsVisorActive = CachedPlayerState->IsVisorActive();
	
	// Broadcast the full state to any subscribed listeners (e.g., the owner actor).
	OnPlayerTimelineStateUpdated.Broadcast(CurrentTimelineID, bIsVisorActive);
}