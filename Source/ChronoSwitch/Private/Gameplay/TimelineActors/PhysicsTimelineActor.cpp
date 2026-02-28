// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/TimelineActors/PhysicsTimelineActor.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"

APhysicsTimelineActor::APhysicsTimelineActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// High priority for replication as this is an interactive physics object.
	NetPriority = 3.0f;

	// Default to Past-only existence, but can be changed in Editor.
	ActorTimeline = EActorTimeline::PastOnly;

	InteractedComponent = nullptr;
	InteractingCharacter = nullptr;

	// Ensure movement is replicated for physics sync.
	bReplicates = true;
	AActor::SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(2.0f);
	NetDormancy = DORM_Awake;
	SetPhysicsReplicationMode(EPhysicsReplicationMode::PredictiveInterpolation);
	
	
	// Default behavior: PastMesh is the Root (Standard for PastOnly and CausalActor).
	if (PastMesh)
	{
		SetRootComponent(PastMesh);
		if (FutureMesh)
		{
			FutureMesh->SetupAttachment(PastMesh);
		}
	}
}

void APhysicsTimelineActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APhysicsTimelineActor, InteractedComponent);
	DOREPLIFETIME(APhysicsTimelineActor, InteractingCharacter);
}

void APhysicsTimelineActor::BeginPlay()
{
	Super::BeginPlay();

	// Configure physics based on the selected timeline.
	if (ActorTimeline == EActorTimeline::PastOnly && PastMesh)
	{
		PastMesh->SetSimulatePhysics(true);
		PastMesh->SetEnableGravity(true);
	}
	else if (ActorTimeline == EActorTimeline::FutureOnly && FutureMesh)
	{
		FutureMesh->SetSimulatePhysics(true);
		FutureMesh->SetEnableGravity(true);
	}
}

void APhysicsTimelineActor::OnRep_InteractedComponent()
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

void APhysicsTimelineActor::Interact_Implementation(ACharacter* Interactor)
{
	// Base implementation is empty.
}

FText APhysicsTimelineActor::GetInteractPrompt_Implementation()
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
		
		// If held by someone else, we can't grab it.
		return FText();
	}

	return FText::FromString("Press F to Grab");
}

void APhysicsTimelineActor::NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	InteractedComponent = Mesh;
	InteractingCharacter = Grabber;

	// Ensure this actor ticks AFTER the character holding it to prevent vertical jitter.
	if (Grabber)
	{
		AddTickPrerequisiteActor(Grabber);
	}
}

void APhysicsTimelineActor::NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber)
{
	InteractedComponent = nullptr;
	InteractingCharacter = nullptr;

	// Remove tick dependency.
	if (Grabber)
	{
		RemoveTickPrerequisiteActor(Grabber);
	}
}

bool APhysicsTimelineActor::CanBeGrabbed(UPrimitiveComponent* MeshToGrab) const
{
	// Default logic: Can only be grabbed if not currently held by anyone.
	return InteractedComponent == nullptr;
}



