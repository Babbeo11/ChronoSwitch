// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ChronoSwitchCharacter.generated.h"

class UTimelineComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class AChronoSwitchPlayerState;

UCLASS()
class CHRONOSWITCH_API AChronoSwitchCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** Sets default values for this character's properties. */
	AChronoSwitchCharacter();

	

protected:
	/** Called when the game starts or when spawned. */
	virtual void BeginPlay() override;

	// ========================================================================
	// COMPONENTS
	// ========================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMeshComponent;
	
	// ========================================================================
	// INPUT
	// ========================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> MoveAction;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> LookAction;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> JumpAction;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> InteractAction;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> TimeSwitchAction;
	
	UFUNCTION()
	void Move(const FInputActionValue& Value);
	
	UFUNCTION()
	void Look(const FInputActionValue& Value);
	
	UFUNCTION()
	void JumpStart();
	
	UFUNCTION()
	void JumpStop();
	
	UFUNCTION()
	void Interact();
	
	/** Called from the Input Action. This is intended to be implemented in Blueprint to start an animation sequence. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimeSwitchPressed();

	// ========================================================================
	// NETWORK & REPLICATION
	// ========================================================================

	/** Server RPC: Requests a timeline switch for the other player (CrossPlayer mode). */
	UFUNCTION(Server, Reliable)
	void Server_RequestOtherPlayerSwitch();

	// ========================================================================
	// TIMELINE LOGIC
	// ========================================================================

	/** Executes the core time switch logic based on the current game mode. Designed to be called from an Anim Notify in Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void ExecuteTimeSwitchLogic();

	/** Binds this character to its associated PlayerState to listen for timeline changes. */
	void BindToPlayerState();
	
	/** Handler for the PlayerState's OnTimelineIDChanged delegate. Updates collision and triggers cosmetic effects. */
	void HandleTimelineUpdate(uint8 NewTimelineID);

	/** Updates the character's collision ObjectType. This contains only the core gameplay logic. */
	void UpdateCollisionChannel(uint8 NewTimelineID);
	
	// ========================================================================
	// COSMETIC EVENTS
	// ========================================================================

	/** Blueprint event called when a timeline switch occurs, for triggering VFX, SFX, and animations. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimelineSwitched(uint8 NewTimelineID);

	/** Cosmetic event called on all clients (Owner and Proxies) to trigger VFX/SFX. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimelineChangedCosmetic(uint8 NewTimelineID);
	
	// ========================================================================
	// INTERACTION & PHYSICS
	// ========================================================================

	/** Attempts to grab a physics object in front of the character. */
	void AttemptGrab();
	
	/** Releases the currently held object. */
	void Release();

	/** Server RPC: Validates and executes the grab logic. */
	UFUNCTION(Server, Reliable)
	void Server_Grab();
	
	/** Server RPC: Validates and executes the release logic. */
	UFUNCTION(Server, Reliable)
	void Server_Release();

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float ReachDistance = 300.0f;
	
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float HoldDistance = 200.0f;
	
public:
	// ========================================================================
	// NETWORK & REPLICATION (PUBLIC)
	// ========================================================================
	/** Client RPC: Forces a timeline change and flushes prediction to prevent rubber banding. */
	UFUNCTION(Client, Reliable)
	void Client_ForcedTimelineChange(uint8 NewTimelineID);

	// ========================================================================
	// ENGINE OVERRIDES
	// ========================================================================
	/** Called every frame. */
	virtual void Tick(float DeltaTime) override;

	/** Called to bind functionality to input. */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	/** Required for replicating properties like IsGrabbing. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Timer handle for retrying the PlayerState binding if it's not immediately available. */
	FTimerHandle PlayerStateBindTimer;
	
	// ========================================================================
	// INTERNAL HELPERS
	// ========================================================================
	
	/** Finds and caches the other player character in the world. */
	void CacheOtherPlayerCharacter();
	
	/** Handles symmetrical player-vs-player collision logic. */
	void UpdatePlayerCollision(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS);
	
	/** Configures physics settings for remote players (Simulated Proxies) to prevent jitter or allow dragging. */
	void ConfigureSimulatedProxyPhysics(AChronoSwitchCharacter* ProxyChar, AChronoSwitchPlayerState* ProxyPS, bool bIsOnPhysicsObject);

	/** Handles asymmetrical visibility logic for rendering the other player. */
	void UpdatePlayerVisibility(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS);

	TWeakObjectPtr<class ACharacter> CachedOtherPlayerCharacter;
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedMyPlayerState;
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedOtherPlayerState;
	
	/** The component currently being held. Replicated to handle client-side physics toggling. */
	UPROPERTY(ReplicatedUsing = OnRep_GrabbedComponent)
	TObjectPtr<UPrimitiveComponent> GrabbedComponent;

	/** Handles client-side physics toggling to prevent jitter when holding objects. */
	UFUNCTION()
	void OnRep_GrabbedComponent(UPrimitiveComponent* OldComponent);

	/** Updates the position and rotation of the held object every frame (Kinematic update). */
	void UpdateHeldObjectTransform();

	/** Performs a trace from the camera to find interactable objects in the world. */
	bool BoxTraceFront(FHitResult& OutHit, const float DrawDistance = 200, const EDrawDebugTrace::Type Type = EDrawDebugTrace::Type::ForDuration);
};
