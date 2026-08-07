// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Game/ChronoSwitchGameState.h"
#include "GameFramework/Character.h"
#include "Interfaces/CanForceRelease.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ChronoSwitchCharacter.generated.h"

class USpringArmComponent;
class UTimelineComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class UPlayerVisorWidget;
class AChronoSwitchPlayerState;
class USkeletalMesh;
class UAnimInstance;

UCLASS()
class CHRONOSWITCH_API AChronoSwitchCharacter : public ACharacter, public ICanForceRelease
{
	GENERATED_BODY()

public:
#pragma region Lifecycle
	/** Sets default values for this character's properties. */
	AChronoSwitchCharacter();

	/** Called every frame. */
	virtual void Tick(float DeltaTime) override;

	/** Called to bind functionality to input. */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
#pragma endregion

#pragma region Timeline System Public
	/** Client RPC: Forces a timeline change and flushes prediction to prevent rubber banding. */
	UFUNCTION(Client, Reliable)
	void Client_ForcedTimelineChange(uint8 NewTimelineID);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite,Replicated, Category = "Timeline")
	bool Endgame;
#pragma endregion
	
#pragma region UI
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UPlayerVisorWidget> PlayerVisorWidgetClass;
#pragma endregion


protected:
#pragma region Lifecycle
	/** Called when the game starts or when spawned. */
	virtual void BeginPlay() override;

	/** Required for replicating properties. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#pragma endregion

#pragma region Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraSpringArm;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess")
	UMaterialInterface* PostProcessBaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess")
	UMaterialInterface* GhostOverlayMaterial;

	UPROPERTY(BlueprintReadOnly, Category = "PostProcess")
	UMaterialInstanceDynamic* PostProcessDynamicMaterial;
#pragma endregion

#pragma region Character Appearance
	/** True if this character is Player 1. Replicated so all clients can apply the correct visuals. */
	UPROPERTY(ReplicatedUsing = OnRep_IsPlayer1, BlueprintReadOnly, Category = "Appearance")
	bool bIsPlayer1 = false;

	/** Mesh used for Player 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<USkeletalMesh> Player1SkeletalMesh;

	/** AnimBP used for Player 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSubclassOf<UAnimInstance> Player1AnimClass;

	/** Mesh used for Player 2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TObjectPtr<USkeletalMesh> Player2SkeletalMesh;

	/** AnimBP used for Player 2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSubclassOf<UAnimInstance> Player2AnimClass;

	/** Applies the correct mesh + anim instance based on bIsPlayer1. */
	UFUNCTION()
	void ApplyCharacterAppearance();

	/** RepNotify for bIsPlayer1. */
	UFUNCTION()
	void OnRep_IsPlayer1();

	/** Updates emissive behavior for the character's own materials. */
	void ApplyCharacterEmissive();
#pragma endregion

#pragma region UI
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
	bool bShouldHaveWidget = true;
	
	UPROPERTY(BlueprintReadWrite, Category = "UI")
	UPlayerVisorWidget* PlayerVisorWidget;
	
	UFUNCTION()
	void HandleModeChanged(ETimeSwitchMode NewMode);
	
	UFUNCTION()
	void HandleCanSwitchTimelineChanged(bool bCanSwitchTimeline);
	
	UFUNCTION()
	void HandleSelfTimelineChanged(uint8 NewTimeline);
	
	UFUNCTION()
	void HandleOtherTimelineChanged(uint8 NewTimeline);
	
#pragma endregion

#pragma region Input
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category=Input)
	bool bIsPaused = false;
	
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
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> SprintAction;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Input)
	TObjectPtr<UInputAction> TogglePauseAction;
	
	UPROPERTY(EditDefaultsOnly, Category = Input)
	float LookPitchMin = -55.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = Input)
	float LookPitchMax = 80.0f;
	
	/** Handles movement input. */
	UFUNCTION()
	void Move(const FInputActionValue& Value);
	
	/** Handles look input. Sensitivity is reduced when holding objects. */
	UFUNCTION()
	void Look(const FInputActionValue& Value);
	
	UFUNCTION()
	void JumpStart();
	
	UFUNCTION()
	void JumpStop();
	
	/** Called when the sprint input is started. */
	UFUNCTION()
	void StartSprinting();

	/** Called when the sprint input is completed. */
	UFUNCTION()
	void StopSprinting();
	
	UFUNCTION(BlueprintCallable)
	void TogglePause();
	
	/** Handles interaction input (Release > Interact > Grab). */
	UFUNCTION()
	void Interact();
	
	UFUNCTION()
	void OnTimeSwitch();
	
	UFUNCTION(Server, Reliable)
	void Server_OnTimeSwitch();
	
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnTimeSwitch();
	
	/** This is intended to be implemented in Blueprint to start an animation sequence. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimeSwitchPressed();
#pragma endregion

#pragma region Timeline System
	/** Executes the core time switch logic based on the current game mode. Designed to be called from an Anim Notify in Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void ExecuteTimeSwitchLogic();

	/** Binds this character to its associated PlayerState to listen for timeline changes. */
	void BindToPlayerState();
	
	/** Handler for the PlayerState's OnTimelineIDChanged delegate. Updates collision and triggers cosmetic effects. */
	void HandleTimelineUpdate(uint8 NewTimelineID);

	/** Handler for the PlayerState's OnVisorStateChanged delegate. Triggers cosmetic effects. */
	void HandleVisorStateUpdate(bool bIsVisorActive);

	/** Updates the character's collision ObjectType based on the timeline. */
	void UpdateCollisionChannel(uint8 NewTimelineID);
	
	/** Checks if switching to the other timeline would cause a collision. */
	bool CheckTimelineOverlap();
	
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnAntiPhasingTriggered();

	/** Cosmetic event called on all clients (Owner and Proxies) to trigger VFX/SFX. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimelineChangedCosmetic(uint8 NewTimelineID);

	/** Cosmetic event called when the visor state changes to trigger VFX/SFX (e.g. MPC transitions). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnVisorStateChangedCosmetic(bool bIsVisorActive);

	/** Server RPC: Requests a timeline switch for the other player (CrossPlayer mode). */
	UFUNCTION(Server, Reliable)
	void Server_RequestOtherPlayerSwitch();
#pragma endregion

#pragma region Interaction System
public:
	/** Initiates the grab logic. */
	void AttemptGrab();
	
	/** Initiates the release logic. */
	void Release();
	
	/** Initiates the release logic from outside source. */
	virtual void ForceRelease_Implementation() override;

protected:
	/** Server RPC: Validates and executes the grab logic. */
	UFUNCTION(Server, Reliable)
	void Server_Grab();
	
	/** Server RPC: Validates and executes the release logic. */
	UFUNCTION(Server, Reliable)
	void Server_Release();
	
	/** Server RPC: Sets the character's max walk speed. */
	UFUNCTION(Server, Reliable)
	void Server_SetMaxWalkSpeed(float NewSpeed);
	
	/** Server RPC: Validates and executes the release logic. */
	UFUNCTION(Server, Reliable)
	void Server_Interact(UObject* Object, ACharacter* Interactor);
	
	UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
	void OnInteracted(bool bSucceded);
	
	UFUNCTION(BlueprintImplementableEvent, Category = "Interaction")
	void OnObjectGrabbed();

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float ReachDistance = 300.0f;
	
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float HoldDistance = 200.0f;

	/** Maximum distance the object can be from the camera before being forcibly dropped (e.g. when stuck behind a wall). */
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float MaxHoldDistance = 350.0f;

	/** The component currently being held. Replicated to handle client-side physics state. */
	UPROPERTY(ReplicatedUsing = OnRep_GrabbedComponent)
	TObjectPtr<UPrimitiveComponent> GrabbedComponent;

	/** Handles client-side physics state changes when holding objects. */
	UFUNCTION()
	void OnRep_GrabbedComponent(UPrimitiveComponent* OldComponent);

	/** Updates the position and rotation of the held object every frame (Kinematic update). */
	void UpdateHeldObjectTransform(float DeltaTime);

	/** Tracks the local position of the held object to prevent network jitter. */
	FVector HeldObjectPos;

	/** Tracks the velocity of the held object to impart momentum upon release. */
	FVector HeldObjectVelocity;

	/** Stores the rotation of the object relative to the camera at the moment of grabbing. */
	UPROPERTY(Replicated)
	FRotator GrabbedRelativeRotation;
	
	/** Stores the original collision channel of the grabbed object to restore it upon release. */
	UPROPERTY(Replicated)
	TEnumAsByte<ECollisionChannel> GrabbedMeshOriginalCollision;
#pragma endregion

#pragma region Interaction Sensing System
	/** Stores pointer to actor in front of the player. */
	UPROPERTY()
	AActor* SensedActor;
	
	/** Checks for interactable objects in front of the player. */
	void OnTickSenseInteractable();
	
	/** Validates new traced candidate*/
	AActor* ValidateInteractable(AActor* HitActor, UPrimitiveComponent* HitComponent);
	
	/** Sets visibility and text values only on SensedActor change*/
	void UpdateInteractWidget();
	
	/** Performs a trace from the camera to find interactable objects in the world. */
	bool BoxTraceFront(FHitResult& OutHit, const float DrawDistance = 200, const EDrawDebugTrace::Type Type = EDrawDebugTrace::Type::ForDuration);
	
#pragma endregion
	
#pragma region Player Management
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TWeakObjectPtr<class ACharacter> CachedOtherPlayerCharacter;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedMyPlayerState;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedOtherPlayerState;

	/** Finds and caches the other player character in the world. */
	void CacheOtherPlayerCharacter();
	
	/** Attempts to cache the other player and states. */
	void TryCachePlayers();

	/** Handles symmetrical player-vs-player collision logic. */
	void UpdatePlayerCollision();

	/** Handler for the other player's timeline update to refresh collisions dynamically. */
	void HandleOtherPlayerTimelineUpdate(uint8 NewTimelineID);
	
	/** Handles asymmetrical visibility logic for rendering the other player. */
	void UpdatePlayerVisibility(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS, float DeltaTime);
#pragma endregion 

#pragma region Player Movement
	/** The character's normal walking speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerMovement")
	float WalkSpeed = 300.0f;

	/** The character's sprinting speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerMovement")
	float SprintSpeed = 600.0f;
	
	/** How fast the character transitions between walking and sprinting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerMovement")
	float SprintAccelerationSpeed = 8.0f;

	/** Internal target speed for smooth interpolation. */
	float TargetWalkSpeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PlayerMovement")
	float CoyoteTimeWindow;
	
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PrevCustomMode) override;
	
	virtual bool CanJumpInternal_Implementation() const override;
	
	virtual void OnJumped_Implementation() override;
	
#pragma endregion
	
private:
#pragma region Internal State
	/** Timer handle for retrying the PlayerState binding if it's not immediately available. */
	FTimerHandle PlayerStateBindTimer;

	/** Timer handle for finding and caching the other player and their state. */
	FTimerHandle PlayerCachingTimer;

	/** Current blend value for the timeline material transition (0.0 to 1.0). */
	float CurrentTimelineBlend;

	/** Current blend value for the visibility transition (0.0 = Visible, 1.0 = Invisible). */
	float CurrentVisibilityBlend;

	/** Cached pointers to the dynamic material instances of the other player to avoid casting every frame. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CachedBodyMIDs;
	
	UPROPERTY(Transient)
	float JumpGraceTimeExpiration;

	/** Reusable array for trace object types to avoid heap allocation every frame. */
	TArray<TEnumAsByte<EObjectTypeQuery>> ReusableTraceObjectTypes;
#pragma endregion
};
