// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TimeInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UTimeInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class CHRONOSWITCH_API ITimeInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	virtual uint8 GetCurrentTimelineID() { return 0; };
};
