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
class UInteractPromptWidget;
class AChronoSwitchPlayerState;

UCLASS()
class CHRONOSWITCH_API AChronoSwitchCharacter : public ACharacter
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
#pragma endregion
	
#pragma region UI
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UInteractPromptWidget> InteractWidgetClass;
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
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMeshComponent;
#pragma endregion

#pragma region UI
	UPROPERTY()
	UInteractPromptWidget* InteractWidget;
#pragma endregion

#pragma region Input
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
	
	/** Handles interaction input (Release > Interact > Grab). */
	UFUNCTION()
	void Interact();
	
	/** Called from the Input Action. This is intended to be implemented in Blueprint to start an animation sequence. */
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

protected:
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
	
	/** Handles symmetrical player-vs-player collision logic. */
	void UpdatePlayerCollision(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS);
	
	/** Handles asymmetrical visibility logic for rendering the other player. */
	void UpdatePlayerVisibility(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS, float DeltaTime);
#pragma endregion

private:
#pragma region Internal State
	/** Timer handle for retrying the PlayerState binding if it's not immediately available. */
	FTimerHandle PlayerStateBindTimer;

	/** Current blend value for the timeline material transition (0.0 to 1.0). */
	float CurrentTimelineBlend;

	/** Current blend value for the visibility transition (0.0 = Visible, 1.0 = Invisible). */
	float CurrentVisibilityBlend;

	/** Cached pointer to the dynamic material instance of the other player to avoid casting every frame. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CachedBodyMID;

	/** Reusable array for trace object types to avoid heap allocation every frame. */
	TArray<TEnumAsByte<EObjectTypeQuery>> ReusableTraceObjectTypes;
#pragma endregion
};
