// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimelineBaseActor.h"
#include "GameFramework/Actor.h"
#include "Interfaces/Interactable.h"
#include "TAInteractableObject.generated.h"

UCLASS()
class CHRONOSWITCH_API ATAInteractableObject : public ATimelineBaseActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATAInteractableObject();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
};
