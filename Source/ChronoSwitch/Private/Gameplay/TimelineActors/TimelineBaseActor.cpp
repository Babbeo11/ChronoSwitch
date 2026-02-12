#include "Gameplay/TimelineActors/TimelineBaseActor.h"

ATimelineBaseActor::ATimelineBaseActor()
{
	// Disable ticking for performance; updates are event-driven.
	PrimaryActorTick.bCanEverTick = false;
	
	// Use a SceneComponent as root for clean attachment.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	// Initialize meshes.
	PastMesh = CreateDefaultSubobject<UStaticMeshComponent>("PastMesh");
	FutureMesh = CreateDefaultSubobject<UStaticMeshComponent>("FutureMesh");
	PastMesh->SetupAttachment(GetRootComponent());
	FutureMesh->SetupAttachment(GetRootComponent());

	// Initialize timeline observer.
	TimelineObserver = CreateDefaultSubobject<UTimelineObserverComponent>(TEXT("TimelineObserver"));
	
	// Default to Past-only existence.
	ActorTimeline = EActorTimeline::PastOnly;
	bShowStaticGhost = false;
}

#pragma region Interaction

void ATimelineBaseActor::Interact_Implementation(ACharacter* Interactor)
{
	// Override in derived classes.
}

FText ATimelineBaseActor::GetInteractPrompt_Implementation()
{
	// Override in derived classes
	return FText();
}

void ATimelineBaseActor::NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	// Override in derived classes.
}

void ATimelineBaseActor::NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	// Override in derived classes.
}

#pragma endregion

#pragma region Lifecycle

void ATimelineBaseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateEditorVisuals();
}

void ATimelineBaseActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Initialize collision settings once at startup.
	SetupCollisionProfiles();

	// Bind to timeline updates.
	if (TimelineObserver)
	{
		TimelineObserver->OnPlayerTimelineStateUpdated.AddDynamic(this, &ATimelineBaseActor::HandlePlayerTimelineUpdate);
	}
}

#pragma endregion

#pragma region Timeline Logic

void ATimelineBaseActor::UpdateEditorVisuals()
{
	// Logic to display the correct meshes in the Editor Viewport.
	const bool bShowPast = (ActorTimeline != EActorTimeline::FutureOnly);
	const bool bShowFuture = (ActorTimeline != EActorTimeline::PastOnly);

	if (PastMesh) PastMesh->SetVisibility(bShowPast);
	if (FutureMesh) FutureMesh->SetVisibility(bShowFuture);
}

void ATimelineBaseActor::SetupCollisionProfiles()
{
	// Apply collision settings based on the actor's timeline mode.
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

void ATimelineBaseActor::ConfigureMeshCollision(UStaticMeshComponent* Mesh, uint8 MeshTimelineID)
{
	if (!Mesh) return;

	const ECollisionChannel MyObjectChannel = UTimelineObserverComponent::GetCollisionChannelForTimeline(MeshTimelineID);
	const ECollisionChannel MyTraceChannel = UTimelineObserverComponent::GetCollisionTraceChannelForTimeline(MeshTimelineID);
	
	// Identify Player Channels.
	const ECollisionChannel MyPlayerChannel = (MeshTimelineID == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;
	const ECollisionChannel OtherPlayerChannel = (MeshTimelineID == 0) ? ECC_GameTraceChannel2 : ECC_GameTraceChannel1;

	Mesh->SetCollisionObjectType(MyObjectChannel);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Reset all responses to Ignore, then whitelist specific interactions.
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	// 1. Block objects in the same timeline.
	Mesh->SetCollisionResponseToChannel(MyObjectChannel, ECR_Block);

	// 2. Block world geometry and physics bodies.
	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

	// 3. Block interaction traces from the same timeline.
	Mesh->SetCollisionResponseToChannel(MyTraceChannel, ECR_Block);
	
	// 4. Player Collision: Block own timeline player, ignore other timeline player.
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // Ignore generic pawns
	Mesh->SetCollisionResponseToChannel(MyPlayerChannel, ECR_Block);
	Mesh->SetCollisionResponseToChannel(OtherPlayerChannel, ECR_Ignore);
}

void ATimelineBaseActor::HandlePlayerTimelineUpdate(uint8 PlayerTimelineID, bool bIsVisorActive)
{
	// Updates visual visibility based on the player's state.
	// Collision is handled statically by SetupCollisionProfiles.
	
	const bool bPlayerIsInPast = (PlayerTimelineID == 0);
	bool bPastVisible = false;
	bool bFutureVisible = false;

	switch (ActorTimeline)
	{
	case EActorTimeline::PastOnly:
		// Visible if in Past, or in Future with Visor (Ghost).
		bPastVisible = bPlayerIsInPast || (!bPlayerIsInPast && bIsVisorActive);
		break;

	case EActorTimeline::FutureOnly:
		// Visible if in Future, or in Past with Visor (Ghost).
		bFutureVisible = !bPlayerIsInPast || (bPlayerIsInPast && bIsVisorActive);
		break;

	case EActorTimeline::Both_Static:
		// Visible in respective timeline.
		// If bShowStaticGhost is true, also visible in other timeline if Visor is active.
		bPastVisible = bPlayerIsInPast || (bShowStaticGhost && bIsVisorActive);
		bFutureVisible = !bPlayerIsInPast || (bShowStaticGhost && bIsVisorActive);
		break;

	case EActorTimeline::Both_Causal:
		// Always visible in the respective timeline.
		bPastVisible = bPlayerIsInPast;
		bFutureVisible = !bPlayerIsInPast;
		break;
	}

	if (PastMesh) PastMesh->SetHiddenInGame(!bPastVisible);
	if (FutureMesh) FutureMesh->SetHiddenInGame(!bFutureVisible);
}

#pragma endregion