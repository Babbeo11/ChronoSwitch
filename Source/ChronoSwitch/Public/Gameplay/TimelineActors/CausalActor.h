// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimelineBaseActor.h"
#include "GameFramework/Actor.h"
#include "CausalActor.generated.h"

/**
 * A CausalActor is a physics-enabled object that exists in both timelines simultaneously.
 * It implements a "Master-Slave" relationship where the PastMesh (Master) drives the FutureMesh (Slave).
 * 
 * Key Behaviors:
 * - Kinematic Sync: When the PastMesh is held, the FutureMesh follows kinematically to allow lifting other players.
 * - Physics Sync: When released, the FutureMesh uses spring forces to follow the PastMesh, allowing for physical interactions (gravity, collisions).
 * - Ghost Visualization: Displays a visual cue when the two meshes desynchronize due to obstacles.
 */
UCLASS()
class CHRONOSWITCH_API ACausalActor : public ATimelineBaseActor
{
	GENERATED_BODY()

public:
	ACausalActor();
	
	// --- IInteractable Interface ---
	virtual void Interact_Implementation(ACharacter* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() override;

	// --- Interaction Hooks ---
	virtual void NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber) override;
	virtual void NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber) override;

	/** Checks if the specific component can be grabbed (e.g., prevents grabbing Future if Past is held). */
	bool CanBeGrabbed(UPrimitiveComponent* MeshToGrab) const;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	virtual void Tick(float DeltaTime) override;

protected:
	// --- Components ---

	/** Visual-only mesh that appears when the Past and Future meshes desynchronize. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> GhostMesh;

	// --- Physics Configuration ---

	/** Distance threshold (in units) between Past and Future meshes before the Ghost appears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Causal Physics")
	float DesyncThreshold;
	
	/** Distance threshold (in units) before the spring force activates to pull the meshes together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Causal Physics")
	float MaxAllowedDistance;

	/** Strength of the spring force pulling the FutureMesh towards the PastMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Causal Physics")
	float SpringStiffness;

	/** Damping factor to reduce oscillation when the FutureMesh moves towards the PastMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Causal Physics")
	float SpringDamping;

	// --- State ---

	/** The component currently being held by a player, if any. Replicated to sync state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_InteractedComponent, Category = "Causal State")
	TObjectPtr<UPrimitiveComponent> InteractedComponent;

	/** The character currently holding this actor. Replicated to handle state changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Causal State")
	TObjectPtr<ACharacter> InteractingCharacter;

	/** Handles changes to the interaction state on clients. */
	UFUNCTION()
	void OnRep_InteractedComponent();

private:
	/** Updates the position of the FutureMesh based on the PastMesh's state. */
	void UpdateSlaveMesh(float DeltaTime);

	/** Updates the visibility and location of the GhostMesh based on desync distance. */
	void UpdateGhostVisuals();

	/** Tracks the velocity of the FutureMesh during kinematic movement to apply upon release. */
	FVector FutureMeshVelocity;
};