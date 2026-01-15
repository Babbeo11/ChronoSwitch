// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimelineBaseActor.h"
#include "SwitchDisablerWall.generated.h"

class UBoxComponent;

UCLASS()
class CHRONOSWITCH_API ASwitchDisablerWall : public ATimelineBaseActor
{
	GENERATED_BODY()

public:
	
	// Sets default values for this actor's properties
	ASwitchDisablerWall();
	
	// UFUNCTION()
	// void OnEnterWall(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
	// 	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	//
	// UFUNCTION()
	// void OnExitWall(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, 
	// 	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UStaticMeshComponent* FirstPillar;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	UStaticMeshComponent* SecondPillar;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UBoxComponent* EnterBox = nullptr;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UBoxComponent* ExitBox = nullptr;
	
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline", meta = (DisplayPriority = "0"))
	// ETimelineType TimelineToSwitch;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
