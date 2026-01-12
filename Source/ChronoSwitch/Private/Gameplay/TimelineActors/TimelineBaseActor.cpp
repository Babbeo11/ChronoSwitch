#include "Gameplay/TimelineActors/TimelineBaseActor.h"

ATimelineBaseActor::ATimelineBaseActor()
{
	// Disable ticking by default to improve performance as this base class doesn't require per-frame logic.
	PrimaryActorTick.bCanEverTick = false;

	// Initialize the observer component.
	TimelineObserver = CreateDefaultSubobject<UTimelineObserverComponent>(TEXT("TimelineObserver"));
	
	// Default state.
	TargetTimeline = ETimelineType::Past;
}

void ATimelineBaseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	// Ensure the component reflects the actor's timeline state when placed or moved in the editor.
	if (TimelineObserver)
	{
		TimelineObserver->SetTargetTimeline(TargetTimeline);
	}
}

void ATimelineBaseActor::SetTargetTimeline(ETimelineType NewTimeline)
{
	TargetTimeline = NewTimeline;

	// Propagate the change to the component to update collisions and visual states.
	if (TimelineObserver)
	{
		TimelineObserver->SetTargetTimeline(TargetTimeline);
	}
}