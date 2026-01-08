// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ChronoSwitchPlayerState.generated.h"

/**
 * 
 */
UCLASS()
class CHRONOSWITCH_API AChronoSwitchPlayerState : public APlayerState
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated)
	int32 TimelineID = 0;
};
