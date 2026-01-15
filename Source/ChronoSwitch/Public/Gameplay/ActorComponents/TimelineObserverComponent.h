#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimelineObserverComponent.generated.h"

// Forward declaration
class AChronoSwitchPlayerState;

/** Broadcasts the local player's full timeline state (ID and visor status). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlayerTimelineStateUpdated, uint8, PlayerTimelineID, bool, bIsVisorActive);

/**
 * Observes the local player's state (TimelineID, Visor) and broadcasts changes.
 * This component acts as a "Messenger," decoupling its owner from the PlayerState.
 * It performs no logic itself, leaving the reaction to its owner.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CHRONOSWITCH_API UTimelineObserverComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	/** Sets default values for this component's properties. */
	UTimelineObserverComponent();

	/** Broadcasts whenever the local player's timeline or visor state is updated. */
	UPROPERTY(BlueprintAssignable, Category = "Timeline")
	FOnPlayerTimelineStateUpdated OnPlayerTimelineStateUpdated;

	/** Static utility function to get the collision object channel for a given timeline. */
	UFUNCTION(BlueprintPure, Category = "Timeline|Utility")
	static ECollisionChannel GetCollisionChannelForTimeline(uint8 Timeline);

	/** Static utility function to get the collision trace channel for a given timeline. */
	UFUNCTION(BlueprintPure, Category = "Timeline|Utility")
	static ECollisionChannel GetCollisionTraceChannelForTimeline(uint8 Timeline);

	/** Returns true if the observed player state reports the visor is active. */
	UFUNCTION(BlueprintPure, Category = "Timeline")
	bool IsVisorActive() const;

protected:
	/** Called when the game starts. */
	virtual void BeginPlay() override;
	/** Called when the component is being destroyed. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Attempts to find the local PlayerState and bind to its delegates. Retries on failure. */
	void InitializeBinding();

	/** Delegate handler called when the observed PlayerState's TimelineID changes. */
	void HandleTimelineChanged(uint8 NewTimelineID);

	/** Delegate handler called when the observed PlayerState's Visor state changes. */
	void HandleVisorStateChanged(bool bNewState);

	/** Fetches the latest state from the cached PlayerState and broadcasts it via OnPlayerTimelineStateUpdated. */
	void UpdateTimelineState(uint8 CurrentTimelineID);

	/** Broadcasts the initial state after a minor delay to prevent race conditions during initialization. */
	void DeferredInitialBroadcast();

	/** A weak pointer to the observed PlayerState to prevent dangling references. */
	TWeakObjectPtr<AChronoSwitchPlayerState> CachedPlayerState;

	/** Handles for the bound delegates, used for proper cleanup in EndPlay. */
	FDelegateHandle OnTimelineIDChangedHandle;
	FDelegateHandle OnVisorStateChangedHandle;
	/** Timer handle for the PlayerState binding retry mechanism. */
	FTimerHandle RetryTimerHandle;

	/** How long to wait before retrying to bind to the PlayerState. */
	static constexpr float BINDING_RETRY_DELAY = 0.1f;

	/** Project-specific collision channels defined in DefaultEngine.ini. */
	static constexpr ECollisionChannel CHANNEL_PAST = ECC_GameTraceChannel1;
	static constexpr ECollisionChannel CHANNEL_FUTURE = ECC_GameTraceChannel2;
	static constexpr ECollisionChannel CHANNEL_TRACE_PAST = ECC_GameTraceChannel3;
	static constexpr ECollisionChannel CHANNEL_TRACE_FUTURE = ECC_GameTraceChannel4;
};