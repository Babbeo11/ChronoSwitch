// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ChronoSwitchGameMode.generated.h"

/**
 * 
 */
UCLASS()
class CHRONOSWITCH_API AChronoSwitchGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
public:
	AChronoSwitchGameMode();
	
	virtual void StartPlay() override;
};
