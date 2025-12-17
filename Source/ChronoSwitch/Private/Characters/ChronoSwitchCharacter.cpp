// Fill out your copyright notice in the Description page of Project Settings.


#include "ChronoSwitch/Public/Characters/ChronoSwitchCharacter.h"


// Sets default values
AChronoSwitchCharacter::AChronoSwitchCharacter()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AChronoSwitchCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AChronoSwitchCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void AChronoSwitchCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

