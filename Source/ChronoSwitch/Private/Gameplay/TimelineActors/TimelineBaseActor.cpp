#include "Gameplay/TimelineActors/TimelineBaseActor.h"

ATimelineBaseActor::ATimelineBaseActor()
{
	// This actor does not need to tick. Its state is updated via event delegates.
	PrimaryActorTick.bCanEverTick = false;
	
	// A simple scene component as the root provides a clean attachment point.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	// Create the two meshes that represent this actor in the Past and Future.
	PastMesh = CreateDefaultSubobject<UStaticMeshComponent>("PastMesh");
	FutureMesh = CreateDefaultSubobject<UStaticMeshComponent>("FutureMesh");
	PastMesh->SetupAttachment(GetRootComponent());
	FutureMesh->SetupAttachment(GetRootComponent());

	// Create the component that listens for the local player's timeline state changes.
	TimelineObserver = CreateDefaultSubobject<UTimelineObserverComponent>(TEXT("TimelineObserver"));
	
	// Set a default state for the actor.
	ActorTimeline = EActorTimeline::PastOnly;
}

void ATimelineBaseActor::Interact_Implementation(ACharacter* Interactor)
{
	// Base implementation is empty. Override in Blueprints or child classes to add logic.
}

bool ATimelineBaseActor::IsGrabbable_Implementation()
{
	// By default, timeline actors are not grabbable. Override if needed.
	return false;
}

void ATimelineBaseActor::Release_Implementation()
{
	// Base implementation is empty.
}

void ATimelineBaseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Update visuals in the editor whenever a property is changed to provide WYSIWYG feedback.
	UpdateEditorVisuals();
}

void ATimelineBaseActor::UpdateEditorVisuals()
{
	// Provides immediate WYSIWYG feedback in the Unreal Editor viewport.
	const bool bShowPast = (ActorTimeline == EActorTimeline::PastOnly || ActorTimeline == EActorTimeline::Both_Static || ActorTimeline == EActorTimeline::Both_Causal);
	const bool bShowFuture = (ActorTimeline == EActorTimeline::FutureOnly || ActorTimeline == EActorTimeline::Both_Static || ActorTimeline == EActorTimeline::Both_Causal);

	if (PastMesh)
	{
		PastMesh->SetVisibility(bShowPast);
	}
	if (FutureMesh)
	{
		FutureMesh->SetVisibility(bShowFuture);
	}
}

void ATimelineBaseActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Set up the permanent collision profiles for the meshes. 
	// This is done once at startup to ensure deterministic physical behavior.
	SetupCollisionProfiles();

	// Subscribe to the observer component's delegate. 
	// When the local player's timeline state changes, HandlePlayerTimelineUpdate will be called.
	if (TimelineObserver)
	{
		TimelineObserver->OnPlayerTimelineStateUpdated.AddDynamic(this, &ATimelineBaseActor::HandlePlayerTimelineUpdate);
	}
}

void ATimelineBaseActor::SetupCollisionProfiles()
{
	// Network Optimization: Static Collision Profiles.
	// Instead of changing collision channels at runtime (which can cause desyncs),
	// we set permanent profiles. The Player's own collision channel determines what they hit.
	// This ensures that a player in the Past walks on Past objects, while a player in the Future walks on Future objects,
	// even if they are in the same server instance.

	// Helper lambda to configure a mesh's collision.
	auto ConfigureMeshCollision = [](UStaticMeshComponent* Mesh, uint8 MeshTimelineID)
	{
		if (!Mesh) return;

		const ECollisionChannel MyObjectChannel = UTimelineObserverComponent::GetCollisionChannelForTimeline(MeshTimelineID);
		const ECollisionChannel MyTraceChannel = UTimelineObserverComponent::GetCollisionTraceChannelForTimeline(MeshTimelineID);

		Mesh->SetCollisionObjectType(MyObjectChannel);
		
		// Ensure collision is enabled for queries (traces) and physics.
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		// "Ignore All, Enable Selectively" pattern for robust collision control.
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);

		// 1. Block characters/objects that exist in the SAME timeline.
		Mesh->SetCollisionResponseToChannel(MyObjectChannel, ECR_Block);

		// 2. Block standard world geometry (Floor, Walls).
		Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

		// 3. Block interaction traces from the SAME timeline.
		Mesh->SetCollisionResponseToChannel(MyTraceChannel, ECR_Block);
	};
	
	// Apply configuration based on the actor's timeline mode.
	switch (ActorTimeline)
	{
	case EActorTimeline::PastOnly:
		ConfigureMeshCollision(PastMesh, 0); // 0 = Past
		if (FutureMesh) FutureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		break;
		
	case EActorTimeline::FutureOnly:
		if (PastMesh) PastMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ConfigureMeshCollision(FutureMesh, 1); // 1 = Future
		break;
		
	case EActorTimeline::Both_Static:
	case EActorTimeline::Both_Causal:
		ConfigureMeshCollision(PastMesh, 0);
		ConfigureMeshCollision(FutureMesh, 1);
		break;
	}
}

void ATimelineBaseActor::HandlePlayerTimelineUpdate(uint8 PlayerTimelineID, bool bIsVisorActive)
{
	// This function manages VISUALS only. Collision is handled by the static profiles and the player's channel.
	
	const bool bPlayerIsInPast = (PlayerTimelineID == 0);

	// Determine visibility based on the actor's mode and the player's state.
	bool bPastVisible = false;
	bool bFutureVisible = false;

	switch (ActorTimeline)
	{
	case EActorTimeline::PastOnly:
		// Visible if player is in Past, OR if player is in Future but using the Visor (Ghost view).
		bPastVisible = bPlayerIsInPast || (!bPlayerIsInPast && bIsVisorActive);
		bFutureVisible = false;
		break;

	case EActorTimeline::FutureOnly:
		bPastVisible = false;
		// Visible if player is in Future, OR if player is in Past but using the Visor (Ghost view).
		bFutureVisible = !bPlayerIsInPast || (bPlayerIsInPast && bIsVisorActive);
		break;

	case EActorTimeline::Both_Static:
	case EActorTimeline::Both_Causal:
		// If the object exists in both timelines, show the version corresponding to the player's current timeline.
		// No ghost effect is needed here as the object is always "there".
		bPastVisible = bPlayerIsInPast;
		bFutureVisible = !bPlayerIsInPast;
		break;
	}

	if(PastMesh) PastMesh->SetHiddenInGame(!bPastVisible);
	if(FutureMesh) FutureMesh->SetHiddenInGame(!bFutureVisible);
}
