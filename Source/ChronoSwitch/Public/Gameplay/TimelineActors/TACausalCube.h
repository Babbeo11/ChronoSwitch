// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimelineBaseActor.h"
#include "GameFramework/Actor.h"
#include "TACausalCube.generated.h"

UCLASS()
class CHRONOSWITCH_API ATACausalCube : public ATimelineBaseActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATACausalCube();
	
	virtual void Interact_Implementation() override;
	virtual bool IsGrabbable_Implementation() override;
	virtual void Release_Implementation() override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	UPROPERTY(EditAnywhere)
	bool IsAlreadyGrabbed;
	
};
