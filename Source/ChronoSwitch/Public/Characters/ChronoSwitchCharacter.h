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

	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMeshComponent;
	
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
	
	// --- Timeline Logic ---

	/** Binds this character to its associated PlayerState to listen for timeline changes. */
	void BindToPlayerState();
	
	/** Updates the character's collision ObjectType when its timeline changes. */
	void UpdateCollisionChannel(uint8 NewTimelineID);
	
	
public:
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
	
	/** Performs a trace from the camera to find interactable objects in the world. */
	bool BoxTraceFront(FHitResult& OutHit, const float DrawDistance = 200, const EDrawDebugTrace::Type Type = EDrawDebugTrace::Type::ForDuration);
};
