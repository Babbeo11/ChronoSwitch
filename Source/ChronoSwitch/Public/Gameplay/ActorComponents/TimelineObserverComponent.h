#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimelineObserverComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimelineStateChanged, bool, bActiveInTimeline);

/**
 * Defines the available timelines in the game world.
 */
UENUM(BlueprintType)
enum class ETimelineType : uint8
{
	Past    UMETA(DisplayName = "Past (Timeline 0)"),
	Future  UMETA(DisplayName = "Future (Timeline 1)")
};

/**
 * UTimelineObserverComponent
 * 
 * This component listens to timeline and visor state changes from the local PlayerState.
 * It automatically manages the owner's collision channels and visibility based on 
 * whether the owner's assigned 'TargetTimeline' matches the world's current timeline.
 */
UCLASS(meta=(BlueprintSpawnableComponent), HideCategories=(Timeline))
class CHRONOSWITCH_API UTimelineObserverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTimelineObserverComponent();
	
	/** Broadcasts when the active status of this component in the current timeline changes. */
	UPROPERTY(BlueprintAssignable, Category = "Timeline")
	FOnTimelineStateChanged OnTimelineStateChanged;
	
	/** Sets the timeline this actor belongs to and refreshes its world state. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void SetTargetTimeline(ETimelineType NewTimeline);

	/** Maps a timeline enum to its corresponding collision channel. */
	static ECollisionChannel GetCollisionChannelForTimeline(ETimelineType Timeline);
	static ECollisionChannel GetCollisionTraceChannelForTimeline(ETimelineType Timeline);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The timeline assigned to this component/actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	ETimelineType TargetTimeline;
	
private:
	/** Subscription handles for PlayerState delegates. */
	FDelegateHandle OnTimelineIDChangedHandle;
	FDelegateHandle OnVisorStateChangedHandle;
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedPlayerState;

	/** Project-specific collision channels defined in DefaultEngine.ini. */
	static constexpr ECollisionChannel CHANNEL_PAST = ECC_GameTraceChannel1;
	static constexpr ECollisionChannel CHANNEL_FUTURE = ECC_GameTraceChannel2;
	static constexpr ECollisionChannel CHANNEL_TRACE_PAST = ECC_GameTraceChannel3;
	static constexpr ECollisionChannel CHANNEL_TRACE_FUTURE = ECC_GameTraceChannel4;
	static constexpr float BINDING_RETRY_DELAY = 0.2f;

	/** Subscribes to the local PlayerState events. Retries if PlayerState is not yet available. */
	void InitializeBinding();
	
	/** Event handler for global timeline shifts. */
	void HandleTimelineChanged(uint8 NewTimelineID);
	
	/** Event handler for visor toggle (allows seeing across timelines). */
	void HandleVisorStateChanged(bool bNewState);
	
	/** Updates visibility and broadcasts state based on the current player context. */
	void UpdateTimelineState(uint8 CurrentTimelineID);
	
	/** Configures the owner's collision filtering for the assigned timeline. */
	void SetupActorCollision();
};