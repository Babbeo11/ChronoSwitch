// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/TimelineActors/CausalActor.h"
#include "Characters/ChronoSwitchCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerState.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Chaos/ChaosEngineInterface.h"

ACausalActor::ACausalActor()
{
	PrimaryActorTick.bCanEverTick = true;
	
	// Update BEFORE physics to ensure passengers can react to the moving base in the same frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	
	// High priority for replication as this is an interactive physics object.
	NetPriority = 5.0f;
	ActorTimeline = EActorTimeline::Both_Causal;

	// Physics Defaults
	DesyncThreshold = 50.0f;
	SpringStiffness = 30.0f;    
	SpringDamping = 5.0f;
	MaxPullDistance = 1000.0f;
	HeldInterpSpeed = 20.0f;
	LiftVerticalTolerance = 15.0f;
	InteractedComponent = nullptr;
	FutureMeshVelocity = FVector::ZeroVector;
	InteractingCharacter = nullptr;
	
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
	}
	
	// Configure Future Mesh (Slave)
	if (FutureMesh)
	{
		FutureMesh->SetSimulatePhysics(true);
		FutureMesh->SetEnableGravity(true);
		FutureMesh->SetIsReplicated(false);
		FutureMesh->BodyInstance.bUseCCD = true; 

		// Note: FutureMesh movement is driven locally by logic, not directly replicated.
	}
}


void ACausalActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Detach FutureMesh from PastMesh at startup.
	if (FutureMesh)
	{
		FutureMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	
	// Ensure physics settings are correct at runtime start.
	if (PastMesh) PastMesh->SetSimulatePhysics(true);
	if (FutureMesh) FutureMesh->SetSimulatePhysics(true);
	
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
	// Determine if the local player is the one holding the object.
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
	
	if (InteractedComponent)
	{
		if (InteractingCharacter == LocalPawn)
		{
			return FText::FromString("Press F to Release");
		}
		// If they are already holding the PastMesh (Priority), we cannot steal it.
		if (InteractedComponent == PastMesh)
		{
			// Already held by someone with max priority. Cannot grab.
			return FText();
		}
			
		// If they hold FutureMesh, we can steal it by grabbing PastMesh.
		return FText::FromString("Press F to Grab");
	}

	return FText::FromString("Press F to Grab");
}

bool ACausalActor::CanBeGrabbed(UPrimitiveComponent* MeshToGrab) const
{
	// If no one is holding it, it can be grabbed.
	if (!InteractedComponent)
	{
		return true;
	}

	// Priority Logic: A grab on the PastMesh succeeds ONLY if the FutureMesh is held (Steal).
	// If PastMesh is already held, we cannot steal it.
	if (MeshToGrab == PastMesh)
	{
		if (InteractedComponent == PastMesh) return false;
		return true;
	}

	// Otherwise (trying to grab FutureMesh), the grab fails if anything is already held.
	return false;
}

void ACausalActor::NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	// If the PastMesh is being grabbed while the FutureMesh is held by another player, force the other player to release.
	if (InteractedComponent && InteractingCharacter && InteractingCharacter != Grabber)
	{
		if (AChronoSwitchCharacter* OtherChar = Cast<AChronoSwitchCharacter>(InteractingCharacter))
		{
			OtherChar->Release();
		}
	}
	
	// Call base implementation to handle state assignment and tick prerequisites.
	// NOTE: Must be called AFTER the force drop logic above, otherwise InteractedComponent is overwritten.
	Super::NotifyOnGrabbed(Mesh, Grabber);

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
	// Call base implementation to handle state cleanup.
	Super::NotifyOnReleased(Mesh, Grabber);

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

		// Optimization: Only iterate over players if we are actually lifting (moving up).
		if (bIsLifting)
		{
			// Ignore collision with players standing on the mesh.
			// This allows the mesh to move UP into them, letting their CharacterMovementComponent resolve the lift.
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
						const bool bIsPhysicallyAbove = CharBottomZ >= (MeshTopZ - LiftVerticalTolerance);

						if (Char->GetMovementBase() == FutureMesh && bIsPhysicallyAbove)
						{
							FutureMesh->IgnoreActorWhenMoving(Char, true);
						}
					}
				}
			}
		}
		
		// Smoothly interpolate towards the target to prevent teleporting when unblocked.
		const FVector NextLoc = FMath::VInterpTo(CurrentLoc, TargetLocation, DeltaTime, HeldInterpSpeed);
		const FRotator NextRot = FMath::RInterpTo(FutureMesh->GetComponentRotation(), TargetRotation, DeltaTime, HeldInterpSpeed);

		FHitResult Hit;
		FutureMesh->SetWorldLocationAndRotation(NextLoc, NextRot, true, &Hit);

		if (DeltaTime > UE_KINDA_SMALL_NUMBER)
		{
			FutureMeshVelocity = (FutureMesh->GetComponentLocation() - CurrentLoc) / DeltaTime;
		}
		
		// Clear ignores immediately after the move.
		FutureMesh->ClearMoveIgnoreActors();
	}
	// Case 2: Object is free (Released).
	// Use AddCustomPhysics to run spring logic on the Physics Thread for stability.
	else if (InteractedComponent == nullptr && FutureMesh)
	{
		if (FBodyInstance* BodyInst = FutureMesh->GetBodyInstance())
		{
			// Capture parameters by value for thread safety.
			float Stiffness = SpringStiffness;
			float Damping = SpringDamping;
			float MaxDist = MaxPullDistance;
			
			// Capture Master's BodyInstance to read real-time physics state on the Physics Thread.
			FBodyInstance* MasterBodyInst = PastMesh ? PastMesh->GetBodyInstance() : nullptr;
			FVector FallbackTarget = TargetLocation;
			FQuat FallbackRotation = TargetRotation.Quaternion();

			FCalculateCustomPhysics CalculateCustomPhysics = FCalculateCustomPhysics::CreateLambda([Stiffness, Damping, MaxDist, MasterBodyInst, FallbackTarget, FallbackRotation](float PhysicsDeltaTime, FBodyInstance* BI)
			{
				if (!BI || !BI->IsValidBodyInstance()) return;

				// Get physics state safely (Thread-safe)
				const FTransform BodyTransform = BI->GetUnrealWorldTransform_AssumesLocked();
				const FVector CurrentLocation = BodyTransform.GetLocation();
				const FVector CurrentVelocity = BI->GetUnrealWorldVelocity_AssumesLocked();

				// Determine Target from Master's real-time state to avoid lag.
				FVector RealTimeTarget = FallbackTarget;
				FQuat RealTimeRotation = FallbackRotation;
				FVector RealTimeLinearVelocity = FVector::ZeroVector;
				FVector RealTimeAngularVelocity = FVector::ZeroVector;

				if (MasterBodyInst && MasterBodyInst->IsValidBodyInstance())
				{
					FTransform MasterTransform = MasterBodyInst->GetUnrealWorldTransform_AssumesLocked();
					RealTimeTarget = MasterTransform.GetLocation();
					RealTimeRotation = MasterTransform.GetRotation();
					RealTimeLinearVelocity = MasterBodyInst->GetUnrealWorldVelocity_AssumesLocked();
					RealTimeAngularVelocity = MasterBodyInst->GetUnrealWorldAngularVelocityInRadians_AssumesLocked();
				}

				// Calculate Spring Force
				FVector Delta = RealTimeTarget - CurrentLocation;
				
				if (Delta.SizeSquared() > MaxDist * MaxDist)
				{
					Delta = Delta.GetSafeNormal() * MaxDist;
				}

				const FVector SpringForce = Delta * Stiffness;
				// Damping based on RELATIVE velocity prevents drag when matching speed.
				const FVector DampingForce = -(CurrentVelocity - RealTimeLinearVelocity) * Damping;
				
				const FVector TotalForce = SpringForce + DampingForce;

				// Apply Force on Physics Thread (bAccelChange=true, bWake=true)
				// We use bAccelChange=true (Acceleration) to make the spring independent of mass.
				FPhysicsInterface::AddForce_AssumesLocked(BI->GetPhysicsActorHandle(), TotalForce, true, true);

				// --- Angular Spring (Torque) ---
				
				const FQuat CurrentRotation = BodyTransform.GetRotation();
				
				// Calculate error quaternion (difference between target and current)
				FQuat ErrorRot = RealTimeRotation * CurrentRotation.Inverse();
				FVector Axis;
				float Angle;
				ErrorRot.ToAxisAndAngle(Axis, Angle);

				// Normalize angle.
				if (Angle > UE_PI) Angle -= 2.0f * UE_PI;
				
				// Apply Torque: Stiffness * Angle - Damping * AngularVelocity
				const FVector AngularVelocity = BI->GetUnrealWorldAngularVelocityInRadians_AssumesLocked();
				const FVector Torque = (Axis * Angle * Stiffness) - ((AngularVelocity - RealTimeAngularVelocity) * Damping);
				
				FPhysicsInterface::AddTorque_AssumesLocked(BI->GetPhysicsActorHandle(), Torque, true, true);
			});

			BodyInst->AddCustomPhysics(CalculateCustomPhysics);
		}
	}
}

void ACausalActor::UpdateGhostVisuals() const
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
