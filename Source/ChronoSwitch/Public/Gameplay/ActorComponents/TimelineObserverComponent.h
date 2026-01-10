#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimelineObserverComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimelineStateChanged, bool, bActiveInTimeline);

UENUM(BlueprintType)
enum class ETimelineType : uint8
{
	Past    UMETA(DisplayName = "Past (Timeline 0)"),
	Future  UMETA(DisplayName = "Future (Timeline 1)")
};

/**
 * Component that observes timeline changes from the PlayerState and updates 
 * the owner's collision and broadcast state accordingly.
 */
UCLASS(meta=(BlueprintSpawnableComponent), HideCategories=(Timeline))
class CHRONOSWITCH_API UTimelineObserverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTimelineObserverComponent();
	
	UPROPERTY(BlueprintAssignable, Category = "Timeline")
	FOnTimelineStateChanged OnTimelineStateChanged;
	
	/** Updates the component's target timeline and refreshes actor collision settings. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void SetTargetTimeline(ETimelineType NewTimeline);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	ETimelineType TargetTimeline;
	
private:
	/** Handle for the PlayerState delegate subscription. */
	FDelegateHandle TimelineDelegateHandle;

	/** Constants for project-specific collision channels. */
	static constexpr ECollisionChannel CHANNEL_PAST = ECC_GameTraceChannel1;
	static constexpr ECollisionChannel CHANNEL_FUTURE = ECC_GameTraceChannel2;

	/** Attempts to find the local PlayerState and bind to timeline changes. */
	void InitializeBinding();
	
	/** Callback for when the global timeline ID changes in the PlayerState. */
	void HandleTimelineChanged(uint8 NewTimelineID);
	
	/** Compares the current ID with the target and broadcasts the result. */
	void UpdateTimelineState(uint8 CurrentTimelineID);
	
	/** Configures owner's primitive components collision filtering. */
	void SetupActorCollision();
};