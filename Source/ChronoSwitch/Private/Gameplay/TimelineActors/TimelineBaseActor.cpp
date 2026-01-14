#include "Gameplay/TimelineActors/TimelineBaseActor.h"

ATimelineBaseActor::ATimelineBaseActor()
{
	// Disable ticking by default to improve performance as this base class doesn't require per-frame logic.
	PrimaryActorTick.bCanEverTick = false;
	
	SceneRoot = CreateDefaultSubobject<USceneComponent>("SceneComponent");
	RootComponent = SceneRoot;
	
	PastRoot = CreateDefaultSubobject<USceneComponent>("PastRoot");
	FutureRoot = CreateDefaultSubobject<USceneComponent>("FutureRoot");
	
	PastRoot->SetupAttachment(SceneRoot);
	FutureRoot->SetupAttachment(SceneRoot);
	
	PastMesh = CreateDefaultSubobject<UStaticMeshComponent>("PastMesh");
	FutureMesh = CreateDefaultSubobject<UStaticMeshComponent>("FutureMesh");
	
	PastMesh->SetupAttachment(PastRoot);
	FutureMesh->SetupAttachment(FutureRoot);

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

void ATimelineBaseActor::SetActorTimeline(EActorTimeline NewTimeline)
{
	ActorTimeline = NewTimeline;

	// Propagate the change to the component to update collisions and visual states.
	
	switch (NewTimeline)
	{
	case EActorTimeline::PastOnly:
		
		break;
		
	case EActorTimeline::FutureOnly:
		
		break;
		
	case EActorTimeline::Both_Static:
		
		break;
		
	case EActorTimeline::Both_Causal:
		
		break;
		
	default:
		
		break;
	}
}
