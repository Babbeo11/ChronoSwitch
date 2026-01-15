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
	ActorTimeline = EActorTimeline::PastOnly;
}

void ATimelineBaseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	// Ensure the component reflects the actor's timeline state when placed or moved in the editor.
	if (TimelineObserver)
	{
		TimelineObserver->SetTargetTimeline(GetCurrentTimelineID());
	}
}

void ATimelineBaseActor::SetActorTimeline(EActorTimeline NewTimeline)
{
	ActorTimeline = NewTimeline;

	if (TimelineObserver)
	{
		TimelineObserver->SetTargetTimeline(GetCurrentTimelineID());
	}
	
}

uint8 ATimelineBaseActor::GetCurrentTimelineID()
{
	switch (ActorTimeline)
	{
	case EActorTimeline::PastOnly:
		return 0;
	case EActorTimeline::FutureOnly:
		return 1;
	case EActorTimeline::Both_Static:
		return 2;
	case EActorTimeline::Both_Causal:
		return 2;
	default: 
		return 2;
	}
}
