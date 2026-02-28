// Fill out your copyright notice in the Description page of Project Settings.

#include "Gameplay/TimelineActors/FuturePhysicsTimelineActor.h"
#include "Components/StaticMeshComponent.h"

AFuturePhysicsTimelineActor::AFuturePhysicsTimelineActor()
{
	// Override defaults for Future-Only existence.
	ActorTimeline = EActorTimeline::FutureOnly;

	// Set FutureMesh as Root to ensure correct physics replication.
	// This overrides the default behavior of PhysicsTimelineActor which sets PastMesh as Root.
	if (FutureMesh)
	{
		SetRootComponent(FutureMesh);
		
		if (PastMesh)
		{
			PastMesh->SetupAttachment(FutureMesh);
		}
	}
}