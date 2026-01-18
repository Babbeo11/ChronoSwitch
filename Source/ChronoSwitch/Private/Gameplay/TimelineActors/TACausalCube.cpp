// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TimelineActors/TACausalCube.h"



// Sets default values
ATACausalCube::ATACausalCube()
{
	IsAlreadyGrabbed = false;
}

void ATACausalCube::Interact_Implementation()
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("TACausalCube::Interact_Implementation() implementation"));
}

bool ATACausalCube::IsGrabbable_Implementation()
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("TACausalCube::IsGrabbable_Implementation() implementation"));
	return !IsAlreadyGrabbed;
}

void ATACausalCube::Release_Implementation()
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("TACausalCube::Release_Implementation() implementation"));
	IsAlreadyGrabbed = false;
}

// Called when the game starts or when spawned
void ATACausalCube::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ATACausalCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

