#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/ActorComponents/TimelineObserverComponent.h" 
#include "TimelineBaseActor.generated.h"

/**
 * Base actor class that synchronizes its state with a TimelineObserverComponent.
 */
UCLASS(PrioritizeCategories = "00 | Timeline")
class CHRONOSWITCH_API ATimelineBaseActor : public AActor
{
	GENERATED_BODY()

public:
	ATimelineBaseActor();

	/** Updates the target timeline and synchronizes it with the observer component. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void SetTargetTimeline(ETimelineType NewTimeline);

protected:
	/** Component responsible for handling timeline-specific logic and collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TObjectPtr<UTimelineObserverComponent> TimelineObserver;
	
	/** The specific timeline this actor belongs to. Synchronization is handled via Setter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline", Setter = SetTargetTimeline, meta = (DisplayPriority = "0"))
	ETimelineType TargetTimeline;
	
	/** Used to ensure the observer is correctly initialized with the chosen timeline in the editor. */
	virtual void OnConstruction(const FTransform& Transform) override;
};