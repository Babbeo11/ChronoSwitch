// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/TimelineActors/CausalActor.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerState.h"

ACausalActor::ACausalActor()
{
	// Enable ticking to handle physics synchronization.
	PrimaryActorTick.bCanEverTick = true;
	
	// Update BEFORE physics to ensure passengers can react to the moving base in the same frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	
	// High priority for replication as this is an interactive physics object.
	NetPriority = 5.0f;

	// Force the actor timeline mode to exist in both timelines (Causal).
	ActorTimeline = EActorTimeline::Both_Causal;

	// Physics Defaults
	DesyncThreshold = 50.0f;
	MaxAllowedDistance = 20.0f; 
	SpringStiffness = 30.0f;    
	SpringDamping = 5.0f;
	InteractedComponent = nullptr;
	FutureMeshVelocity = FVector::ZeroVector;
	
	// Configure Ghost Mesh
	GhostMesh = CreateDefaultSubobject<UStaticMeshComponent>("GhostMesh");
	GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostMesh->SetHiddenInGame(true);
	GhostMesh->SetCastShadow(false);
	
	// Configure Past Mesh (Master)
	if (PastMesh)
	{
		// Set PastMesh as Root to ensure correct movement replication.
		SetRootComponent(PastMesh);
		
		if (FutureMesh) FutureMesh->SetupAttachment(PastMesh);
		if (GhostMesh) GhostMesh->SetupAttachment(PastMesh);

		PastMesh->SetSimulatePhysics(true);
		PastMesh->SetEnableGravity(true);

		AActor::SetReplicateMovement(true);
	}
	
	// Configure Future Mesh (Slave)
	if (FutureMesh)
	{
		FutureMesh->SetSimulatePhysics(true);
		FutureMesh->SetEnableGravity(true);
		FutureMesh->BodyInstance.bUseCCD = true; 

		// Note: FutureMesh movement is driven locally by logic, not directly replicated.
	}
	
	bReplicates = true;
}

void ACausalActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACausalActor, InteractedComponent);
}

void ACausalActor::OnRep_InteractedComponent()
{
	// Trigger interaction logic on clients when the state changes.
	if (InteractedComponent)
	{
		NotifyOnGrabbed(InteractedComponent, nullptr); 
	}
	else
	{
		NotifyOnReleased(nullptr, nullptr);
	}
}

void ACausalActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Detach FutureMesh from PastMesh at startup.
	// This ensures FutureMesh is driven purely by custom logic (UpdateSlaveMesh) rather than scene hierarchy inheritance.
	if (FutureMesh)
	{
		FutureMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	
	// Ensure physics settings are correct at runtime start.
	if (HasAuthority())
	{
		if (PastMesh) PastMesh->SetSimulatePhysics(true);
		if (FutureMesh) FutureMesh->SetSimulatePhysics(true);
	}
}

void ACausalActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateSlaveMesh(DeltaTime);
	UpdateGhostVisuals();
}

void ACausalActor::Interact_Implementation(ACharacter* Interactor)
{
	// Base implementation is empty.
}

FText ACausalActor::GetInteractPrompt_Implementation()
{
	if (IsHeld())
		return FText::FromString("Press F to Release");
	return FText::FromString("Press F to Grab");
}

bool ACausalActor::CanBeGrabbed(UPrimitiveComponent* MeshToGrab) const
{
	// Enforce mutual exclusion: if any part is held, nothing else can be grabbed.
	return InteractedComponent == nullptr;
}

void ACausalActor::NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	InteractedComponent = Mesh;

	// Ensure this actor ticks AFTER the character holding it to prevent vertical jitter (1-frame lag).
	if (Grabber)
	{
		AddTickPrerequisiteActor(Grabber);
	}

	// If PastMesh is grabbed, FutureMesh must become kinematic to follow it precisely.
	if (FutureMesh && Mesh == PastMesh)
	{
		FutureMesh->SetSimulatePhysics(false);
		FutureMesh->SetEnableGravity(false);
		FutureMeshVelocity = FVector::ZeroVector;
	}
}

void ACausalActor::NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	InteractedComponent = nullptr;

	// Remove tick dependency.
	if (Grabber)
	{
		RemoveTickPrerequisiteActor(Grabber);
	}

	// Restore FutureMesh to physics mode.
	if (FutureMesh)
	{
		FutureMesh->SetSimulatePhysics(true);
		FutureMesh->SetEnableGravity(true);

		// Apply calculated velocity to preserve momentum based on actual movement (respecting collisions).
		FutureMesh->SetPhysicsLinearVelocity(FutureMeshVelocity);

		// Preserve angular momentum from PastMesh (rotation is synced directly).
		if (PastMesh)
		{
			FutureMesh->SetPhysicsAngularVelocityInDegrees(PastMesh->GetPhysicsAngularVelocityInDegrees());
		}
	}
}

void ACausalActor::UpdateSlaveMesh(float DeltaTime)
{
	if (!PastMesh || !FutureMesh) return;

	const FVector TargetLocation = PastMesh->GetComponentLocation();
	const FRotator TargetRotation = PastMesh->GetComponentRotation();

	// Case 1: PastMesh is being held.
	// FutureMesh must follow kinematically (Sweep) to allow lifting other players.
	if (InteractedComponent == PastMesh)
	{
		const FVector CurrentLoc = FutureMesh->GetComponentLocation();
		const FVector MoveDelta = TargetLocation - CurrentLoc;
		const bool bIsLifting = MoveDelta.Z > 0.1f;

		// Ignore collision with players standing on the mesh.
		// This allows the mesh to move UP into them, letting their CharacterMovementComponent resolve the lift.
		// Iterate over PlayerArray instead of all world actors.
		if (AGameStateBase* GameState = GetWorld()->GetGameState())
		{
			for (APlayerState* PS : GameState->PlayerArray)
			{
				if (ACharacter* Char = Cast<ACharacter>(PS->GetPawn()))
				{
						// Geometric Check: Ensure player is physically ABOVE the mesh, not just touching it from side/bottom.
						const float CharBottomZ = Char->GetActorLocation().Z - Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
						const FBoxSphereBounds MeshBounds = FutureMesh->CalcBounds(FutureMesh->GetComponentTransform());
						const float MeshTopZ = MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z;
						
						// Tolerance (e.g. 15 units) allows for slight penetration or step-downs, but rejects side/bottom hits.
						const bool bIsPhysicallyAbove = CharBottomZ >= (MeshTopZ - 15.0f);

						if (Char->GetMovementBase() == FutureMesh && bIsPhysicallyAbove && bIsLifting)
					{
						FutureMesh->IgnoreActorWhenMoving(Char, true);
					}
				}
			}
		}
		
		FHitResult Hit;
		FutureMesh->SetWorldLocationAndRotation(TargetLocation, TargetRotation, true, &Hit);

		if (DeltaTime > KINDA_SMALL_NUMBER)
		{
			FutureMeshVelocity = (FutureMesh->GetComponentLocation() - CurrentLoc) / DeltaTime;
		}
		
		// Clear ignores immediately after the move.
		FutureMesh->ClearMoveIgnoreActors();
	}
	// Case 2: Object is free (Released).
	// FutureMesh uses physics forces to follow PastMesh, respecting gravity and collisions.
	else if (InteractedComponent == nullptr)
	{
		const FVector CurrentLocation = FutureMesh->GetComponentLocation();
		const float Distance = FVector::Dist(TargetLocation, CurrentLocation);

		// Only apply correction forces if the mesh has drifted beyond the allowed threshold.
		if (Distance > MaxAllowedDistance)
		{
			FVector Delta = TargetLocation - CurrentLocation;
			
			// Clamp the pull vector to prevent excessive force generation when far away.
			const float MaxPullDistance = 1000.0f;
			if (Delta.SizeSquared() > MaxPullDistance * MaxPullDistance)
			{
				Delta = Delta.GetSafeNormal() * MaxPullDistance;
			}
			
			FVector GravityComp = FVector::ZeroVector;
			FVector VelocityForDamping = FutureMesh->GetComponentVelocity(); 

			// --- Obstacle Detection ---
			FHitResult Hit;
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(this);

			// Construct query params once.
			static const FCollisionObjectQueryParams ObjectParams = []()
			{
				FCollisionObjectQueryParams P;
				P.AddObjectTypesToQuery(ECC_WorldStatic);
				P.AddObjectTypesToQuery(ECC_WorldDynamic);
				P.AddObjectTypesToQuery(ECC_PhysicsBody);
				return P;
			}();
			
			// Sweep towards the target to detect walls or floors.
			bool bBlocked = GetWorld()->SweepSingleByObjectType(
				Hit,
				CurrentLocation,
				CurrentLocation + (Delta.GetSafeNormal() * 10.0f), // Short sweep
				FQuat::Identity,
				ObjectParams,
				FutureMesh->GetCollisionShape(),
				Params
			);

			if (bBlocked)
			{
				const float NormalProj = Delta | Hit.ImpactNormal;

				// Determine if we hit a Wall (Vertical) or Floor (Horizontal).
				if (Hit.ImpactNormal.Z < 0.7f)
				{
					// --- WALL LOGIC (Sticky) ---
					// If pulling INTO the wall, lock the object to prevent sliding.
					if (NormalProj < 0.0f)
					{
						// Remove tangential pull force.
						Delta = Hit.ImpactNormal * NormalProj;

						// Counteract gravity to hold position.
						if (FutureMesh->IsGravityEnabled())
						{
							GravityComp = FVector(0.0f, 0.0f, -GetWorld()->GetGravityZ());
						}

						// Kill tangential velocity (Infinite Friction).
						const FVector NormalVel = Hit.ImpactNormal * (VelocityForDamping | Hit.ImpactNormal);
						FutureMesh->SetPhysicsLinearVelocity(NormalVel);
						
						// Update damping velocity to avoid erroneous upward force.
						VelocityForDamping = NormalVel;
					}
				}
				else
				{
					// --- FLOOR LOGIC (Sliding) ---
					// Allow sliding along the floor by removing only the downward pull component.
					if (NormalProj < 0.0f)
					{
						Delta -= Hit.ImpactNormal * NormalProj;
					}
				}
			}
		
			// Calculate Spring Force 
			const FVector SpringForce = Delta * SpringStiffness;
		
			// Calculate Damping Force 
			const FVector DampingForce = -VelocityForDamping * SpringDamping;
		
			const FVector TotalForce = SpringForce + DampingForce + GravityComp;

			// Apply the net force to the physics body.
			FutureMesh->AddForce(TotalForce, NAME_None, true); // true = Acceleration change (mass independent)
		}

		// Always sync rotation directly for stability.
		FutureMesh->SetWorldRotation(TargetRotation);
	}
}

void ACausalActor::UpdateGhostVisuals()
{
	if (!GhostMesh || !PastMesh || !FutureMesh) return;

	const float Distance = FVector::Dist(PastMesh->GetComponentLocation(), FutureMesh->GetComponentLocation());

	if (Distance > DesyncThreshold)
	{
		// Show Ghost at the "True" location (where the PastMesh is).
		GhostMesh->SetHiddenInGame(false);
		GhostMesh->SetWorldLocationAndRotation(PastMesh->GetComponentLocation(), PastMesh->GetComponentRotation());
	}
	else
	{
		GhostMesh->SetHiddenInGame(true);
	}
}