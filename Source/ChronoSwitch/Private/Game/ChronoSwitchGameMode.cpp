// Fill out your copyright notice in the Description page of Project Settings.


#include "ChronoSwitch/Public/Game/ChronoSwitchGameMode.h"

AChronoSwitchGameMode::AChronoSwitchGameMode()
{
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/Blueprints/BP_ChronoSwitchCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}

void AChronoSwitchGameMode::StartPlay()
{
	Super::StartPlay();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, TEXT("GameMode avviato correttamente"));
	}
	
	UE_LOG(LogTemp, Log, TEXT("AChronoSwitchGameMode::StartPlay chiamato con successo"));
}


