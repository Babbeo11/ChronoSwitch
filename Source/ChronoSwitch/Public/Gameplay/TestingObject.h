// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "GameFramework/Actor.h"
#include "TestingObject.generated.h"

UCLASS()
class CHRONOSWITCH_API ATestingObject : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATestingObject();
	
	void UpdateVisibilityFromPlayerState(AChronoSwitchPlayerState* PS, int32 TargetTimelineID);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ExposeOnSpawn=true), Category = "Timeline")
	int32 TimelineID = 0;
	
	UPROPERTY(ReplicatedUsing=OnRep_VisibilityChanged)
	bool bIsVisible = true;
	
	UFUNCTION()
	void OnRep_VisibilityChanged();
	
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UStaticMeshComponent* StaticMesh;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
