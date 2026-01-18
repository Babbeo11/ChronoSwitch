// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
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

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMeshComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = PhysicsHandle, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsHandleComponent> ObjectPhysicsHandle;
	
	// --- Input ---

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
	
	// --- Input Callbacks ---

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

	// --- Network & Replication ---

	/** Server RPC: Requests a timeline switch for the other player (CrossPlayer mode). */
	UFUNCTION(Server, Reliable)
	void Server_RequestOtherPlayerSwitch();

	// --- Timeline Logic ---

	/** Executes the core time switch logic based on the current game mode. Designed to be called from an Anim Notify in Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void ExecuteTimeSwitchLogic();

	/** Binds this character to its associated PlayerState to listen for timeline changes. */
	void BindToPlayerState();
	
	/** Handler for the PlayerState's OnTimelineIDChanged delegate. Updates collision and triggers cosmetic effects. */
	void HandleTimelineUpdate(uint8 NewTimelineID);

	/** Updates the character's collision ObjectType. This contains only the core gameplay logic. */
	void UpdateCollisionChannel(uint8 NewTimelineID);
	
	// --- Cosmetic Events ---

	/** Blueprint event called when a timeline switch occurs, for triggering VFX, SFX, and animations. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimelineSwitched(uint8 NewTimelineID);

	/** Cosmetic event called on all clients (Owner and Proxies) to trigger VFX/SFX. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnTimelineChangedCosmetic(uint8 NewTimelineID);
	
public:
	/** Client RPC: Forces a timeline change and flushes prediction to prevent rubber banding. */
	UFUNCTION(Client, Reliable)
	void Client_ForcedTimelineChange(uint8 NewTimelineID);

	/** Called every frame. */
	virtual void Tick(float DeltaTime) override;

	/** Called to bind functionality to input. */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
private:
	/** Timer handle for retrying the PlayerState binding if it's not immediately available. */
	FTimerHandle PlayerStateBindTimer;
	
	// --- Tick Logic Helpers ---
	
	/** Finds and caches the other player character in the world. */
	void CacheOtherPlayerCharacter();
	/** Handles symmetrical player-vs-player collision logic. */
	void UpdatePlayerCollision(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS);
	/** Handles asymmetrical visibility logic for rendering the other player. */
	void UpdatePlayerVisibility(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS);

	// --- Cached Pointers for Tick Optimization ---

	TWeakObjectPtr<class ACharacter> CachedOtherPlayerCharacter;
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedMyPlayerState;
	TWeakObjectPtr<class AChronoSwitchPlayerState> CachedOtherPlayerState;
	
	UPROPERTY(EditAnywhere)
	bool IsGrabbing;
	
	/** Performs a trace from the camera to find interactable objects in the world. */
	bool BoxTraceFront(FHitResult& OutHit, const float DrawDistance = 200, const EDrawDebugTrace::Type Type = EDrawDebugTrace::Type::ForDuration);
};
