// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimelineBaseActor.h"
#include "CrossSwitchButton.generated.h"

UCLASS()
class CHRONOSWITCH_API ACrossSwitchButton : public ATimelineBaseActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ACrossSwitchButton();
	
	virtual void Interact_Implementation(ACharacter* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() override;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

};
