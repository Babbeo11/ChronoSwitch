// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PhysicsTimelineActor.h"
#include "FuturePhysicsTimelineActor.generated.h"

/**
 * Specialized version of PhysicsTimelineActor for objects that exist ONLY in the Future.
 * Sets the FutureMesh as the RootComponent to ensure correct physics replication.
 */
UCLASS()
class CHRONOSWITCH_API AFuturePhysicsTimelineActor : public APhysicsTimelineActor
{
	GENERATED_BODY()
public:
	AFuturePhysicsTimelineActor();
};