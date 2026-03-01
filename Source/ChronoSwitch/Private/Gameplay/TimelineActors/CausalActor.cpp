// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/TimelineActors/CausalActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerState.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Net/UnrealNetwork.h"
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
	MaxPullDistance = 800.0f;
	MaxAcceleration = 8000.0f; 
	MaxVelocity = 1500.0f; 
	HeldInterpSpeed = 20.0f;
	LiftVerticalTolerance = 15.0f;
	InteractedComponent = nullptr;
	FutureMeshVelocity = FVector::ZeroVector;
	InteractingCharacter = nullptr;
	FutureInteractingCharacter = nullptr;
	
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
		PastMesh->BodyInstance.bUseCCD = true; 
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

void ACausalActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACausalActor, FutureInteractingCharacter);
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
	
	// Check if we are holding either part of the actor.
	if (InteractingCharacter == LocalPawn || FutureInteractingCharacter == LocalPawn)
	{
		return FText::FromString("Press F to Release");
	}

	return FText::FromString("Press F to Grab");
}

bool ACausalActor::CanBeGrabbed(UPrimitiveComponent* MeshToGrab) const
{
	// Allow independent grabbing.
	if (MeshToGrab == PastMesh)
	{
		// Can grab PastMesh if it's not already held (InteractedComponent tracks PastMesh).
		return InteractedComponent == nullptr;
	}
	if (MeshToGrab == FutureMesh)
	{
		// Can grab FutureMesh if it's not already held.
		return FutureInteractingCharacter == nullptr;
	}
	
	return false;
}

void ACausalActor::NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	if (Mesh == FutureMesh)
	{
		// Track Future holder separately.
		FutureInteractingCharacter = Grabber;
		
		// Manually add tick dependency since we aren't calling Super for FutureMesh.
		if (Grabber)
		{
			AddTickPrerequisiteActor(Grabber);
		}
	}
	else if (Mesh == PastMesh)
	{
		// Call base implementation to handle state assignment (InteractedComponent) for PastMesh.
		Super::NotifyOnGrabbed(Mesh, Grabber);

		// If PastMesh is grabbed, FutureMesh must become kinematic to follow it precisely (Case 1).
		// UNLESS FutureMesh is also held by a player, in which case that player controls it.
		if (FutureMesh && !FutureInteractingCharacter)
		{
			FutureMesh->SetSimulatePhysics(false);
			FutureMesh->SetEnableGravity(false);
			FutureMeshVelocity = FVector::ZeroVector;
		}
	}
}

void ACausalActor::NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	if (Mesh == FutureMesh)
	{
		FutureInteractingCharacter = nullptr;
		if (Grabber)
		{
			RemoveTickPrerequisiteActor(Grabber);
		}
		
		// We don't immediately restore physics here because UpdateSlaveMesh will handle state
		// based on whether PastMesh is held (Case 1) or free (Case 2).
	}
	else if (Mesh == PastMesh)
	{
		// Call base implementation to handle state cleanup for PastMesh.
		Super::NotifyOnReleased(Mesh, Grabber);
	}

	// Restore FutureMesh to physics mode ONLY if it is not held by anyone.
	if (FutureMesh && !FutureInteractingCharacter)
	{
		// Check if PastMesh is held (Case 1). If so, FutureMesh must remain kinematic to follow smoothly.
		if (InteractedComponent == PastMesh)
		{
			FutureMesh->SetSimulatePhysics(false);
			FutureMesh->SetEnableGravity(false);
			FutureMeshVelocity = FVector::ZeroVector;
		}
		else
		{
			// Case 2: Past is free. Future uses physics spring.
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
}

void ACausalActor::UpdateSlaveMesh(float DeltaTime)
{
	if (!PastMesh || !FutureMesh) return;

	// If FutureMesh is held by a player, they have full control. 
	// Disable Master-Slave logic so we don't fight the player's movement.
	if (FutureInteractingCharacter) return;

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
			float MaxAccel = MaxAcceleration;
			
			// Capture Master's BodyInstance to read real-time physics state on the Physics Thread.
			FBodyInstance* MasterBodyInst = PastMesh ? PastMesh->GetBodyInstance() : nullptr;
			FVector FallbackTarget = TargetLocation;
			FQuat FallbackRotation = TargetRotation.Quaternion();

			FCalculateCustomPhysics CalculateCustomPhysics = FCalculateCustomPhysics::CreateLambda([Stiffness, Damping, MaxDist, MaxAccel, MasterBodyInst, FallbackTarget, FallbackRotation](float PhysicsDeltaTime, FBodyInstance* BI)
			{
				if (!BI || !BI->IsValidBodyInstance()) return;

				// Get physics state safely (Thread-safe)
				const FTransform BodyTransform = BI->GetUnrealWorldTransform_AssumesLocked();
				const FVector CurrentLocation = BodyTransform.GetLocation();
				FVector CurrentVelocity = BI->GetUnrealWorldVelocity_AssumesLocked();

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

				// Clamp the total acceleration to prevent physics explosions/tunneling against walls.
				const FVector ClampedForce = TotalForce.GetClampedToMaxSize(MaxAccel);

				// Apply Force on Physics Thread (bAccelChange=true, bWake=true)
				FPhysicsInterface::AddForce_AssumesLocked(BI->GetPhysicsActorHandle(), ClampedForce, true, true);

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
