// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimelineBaseActor.h"
#include "PhysicsTimelineActor.generated.h"


/**
 * A physics-enabled actor that exists in a single timeline (Past or Future).
 * Unlike CausalActor, it does not have a dual-timeline link or ghost mesh.
 * It can be grabbed and moved by players.
 */
UCLASS()
class CHRONOSWITCH_API APhysicsTimelineActor : public ATimelineBaseActor
{
	GENERATED_BODY()
	
public:
	APhysicsTimelineActor();

	// --- IInteractable Interface ---
	virtual void Interact_Implementation(ACharacter* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() override;

	// --- Interaction Hooks ---
	virtual void NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber) override;
	virtual void NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber) override;

	/** Checks if the specific component can be grabbed. Virtual to allow complex logic in derived classes. */
	virtual bool CanBeGrabbed(UPrimitiveComponent* MeshToGrab) const;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- State ---

	/** The component currently being held by a player, if any. Replicated to sync state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_InteractedComponent, Category = "Physics State")
	TObjectPtr<UPrimitiveComponent> InteractedComponent;

	/** The character currently holding this actor. Replicated to handle state changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Physics State")
	TObjectPtr<ACharacter> InteractingCharacter;

	/** Handles changes to the interaction state on clients. */
	UFUNCTION()
	void OnRep_InteractedComponent();
};