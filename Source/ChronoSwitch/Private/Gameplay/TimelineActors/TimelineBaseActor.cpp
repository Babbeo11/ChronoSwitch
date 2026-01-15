#include "Gameplay/TimelineActors/TimelineBaseActor.h"

ATimelineBaseActor::ATimelineBaseActor()
{
	// This actor does not need to tick. Its state is updated via events.
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

void ATimelineBaseActor::Interact_Implementation()
{
	// This function is called when the player interacts with this actor.
	// The base implementation is empty, intended to be overridden in Blueprints or child C++ classes.
}

void ATimelineBaseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Update visuals in the editor whenever a property is changed.
	UpdateEditorVisuals();
}

void ATimelineBaseActor::UpdateEditorVisuals()
{
	// This function provides immediate visual feedback in the editor.
	// It shows or hides the meshes based on the selected ActorTimeline state.
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
	
	// Set up the permanent collision profiles for the meshes. This is done once.
	SetupCollisionProfiles();

	// Subscribe to the observer component's delegate. When the local player's timeline
	// state changes, HandlePlayerTimelineUpdate will be called.
	if (TimelineObserver)
	{
		TimelineObserver->OnPlayerTimelineStateUpdated.AddDynamic(this, &ATimelineBaseActor::HandlePlayerTimelineUpdate);
	}
}

void ATimelineBaseActor::SetupCollisionProfiles()
{
	// This function sets the permanent physical identity of each mesh. This approach is multiplayer-friendly,
	// as the collision profiles are static and the interacting character's own collision channel
	// determines the outcome, preventing network desynchronization.

	// A helper lambda to configure a mesh's collision, avoiding code duplication.
	auto ConfigureMeshCollision = [](UStaticMeshComponent* Mesh, uint8 MeshTimelineID)
	{
		if (!Mesh) return;

		const ECollisionChannel MyObjectChannel = UTimelineObserverComponent::GetCollisionChannelForTimeline(MeshTimelineID);
		const ECollisionChannel MyTraceChannel = UTimelineObserverComponent::GetCollisionTraceChannelForTimeline(MeshTimelineID);

		Mesh->SetCollisionObjectType(MyObjectChannel);

		// Use the "Ignore All, Enable Selectively" pattern for robust collision.
		// This prevents default mesh settings from causing unexpected interactions.
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);

		// 1. Enable physical collision with characters/objects in its OWN timeline.
		Mesh->SetCollisionResponseToChannel(MyObjectChannel, ECR_Block);

		// 2. Enable collision with standard world geometry (like the floor).
		Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

		// 3. Enable collision for interaction traces from its OWN timeline.
		// This allows the player's interaction trace to detect this object.
		Mesh->SetCollisionResponseToChannel(MyTraceChannel, ECR_Block);
	};
	
	switch (ActorTimeline)
	{
	case EActorTimeline::PastOnly:
		ConfigureMeshCollision(PastMesh, 0);
		if (FutureMesh) FutureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		break;
	case EActorTimeline::FutureOnly:
		if (PastMesh) PastMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ConfigureMeshCollision(FutureMesh, 1);
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
	// Determine which timeline the local player is currently in.
	const bool bPlayerIsInPast = (PlayerTimelineID == 0);

	// Determine the visibility for each mesh based on the actor's state and the player's state.
	bool bPastVisible = false;
	bool bFutureVisible = false;

	switch (ActorTimeline)
	{
	case EActorTimeline::PastOnly:
		// Visible if player is in past, or as a ghost if player is in future with visor.
		bPastVisible = bPlayerIsInPast || (!bPlayerIsInPast && bIsVisorActive);
		bFutureVisible = false;
		break;

	case EActorTimeline::FutureOnly:
		bPastVisible = false;
		// Visible if player is in future, or as a ghost if player is in past with visor.
		bFutureVisible = !bPlayerIsInPast || (bPlayerIsInPast && bIsVisorActive);
		break;

	case EActorTimeline::Both_Static:
	case EActorTimeline::Both_Causal:
		// For 'Both' states, there is no ghost effect; just show the solid mesh
		// corresponding to the player's current timeline.
		bPastVisible = bPlayerIsInPast;
		bFutureVisible = !bPlayerIsInPast;
		break;
	}

	// Apply the calculated visibility to each mesh.
	if(PastMesh) PastMesh->SetHiddenInGame(!bPastVisible);
	if(FutureMesh) FutureMesh->SetHiddenInGame(!bFutureVisible);
}
