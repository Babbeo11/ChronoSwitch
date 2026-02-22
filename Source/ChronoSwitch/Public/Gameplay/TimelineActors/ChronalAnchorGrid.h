// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ChronalAnchorGrid.generated.h"

class AChronoSwitchCharacter;
class UBoxComponent;

/**
 * Enum to easly specify in editor the timeline to force onto the players
 */
UENUM(BlueprintType)
enum class EForcedTimeline : uint8
{
	Past UMETA(DisplayName="Past"),
	Future UMETA(DisplayName="Future")
};
/**
 * Class for Chronal Anchor Grid portals that lock the player in a specific
 * timeline, and can disable visor
 */
UCLASS()
class CHRONOSWITCH_API AChronalAnchorGrid : public AActor
{
	GENERATED_BODY()
	//To Do: Use case of objects that passes the barrier?
	
public:
#pragma region Lifecycle
	// Sets default values for this actor's properties
	AChronalAnchorGrid();
	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

#pragma endregion
	
#pragma region Settings
	// Target Timeline of the C.A.G. Zone
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAG Settings")
	EForcedTimeline TargetForcedTimeline;
	
	// Dictates whether it should disable visor or not
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAG Settings")
	bool bShouldDisableVisor;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CAG Settings")
	bool bShouldDisableSwitch;
	
	// The map stores the sign of the entry direction for each player
	UPROPERTY()
	TMap<AChronoSwitchCharacter*, float> StoredDirectionSigns;
	
#pragma endregion
	
protected:
#pragma region Lifecycle
	
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
#pragma endregion
	
#pragma region Components
	/** Mesh of the Chronal Anchor Grid barrier */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* BarrierMesh;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* GridBorder1;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* GridBorder2;
	
	/** Collider box that triggers the effect of the barrier */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> BoxCollider;
	
#pragma endregion
	
#pragma region Collision Delegates
	UFUNCTION(BlueprintCallable, Category = "Collision")
	void OnBeginOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
	
	UFUNCTION(BlueprintCallable, Category = "Collision")
	void OnEndOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);
	
#pragma endregion
	
private:
	
#pragma region Utility
	/** Calculates from which side of the barrier the player is triggering the event */
	UFUNCTION()
	float GetDirectionSign(const AActor* Actor) const;
	
#pragma endregion
};
