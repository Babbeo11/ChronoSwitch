// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TimelineActors/CrossSwitchButton.h"

#include "Characters/ChronoSwitchCharacter.h"
#include "EngineUtils.h"
#include "Game/ChronoSwitchPlayerState.h"


// Sets default values
ACrossSwitchButton::ACrossSwitchButton()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void ACrossSwitchButton::BeginPlay()
{
	Super::BeginPlay();
	
}

void ACrossSwitchButton::Interact_Implementation(ACharacter* Interactor)
{
	// The logic for a world object should always run on the server to be authoritative.
	if (!HasAuthority())
	{
		return;
	}

	// Find the player character who is NOT the one who interacted with the button.
	for (TActorIterator<AChronoSwitchCharacter> It(GetWorld()); It; ++It)
	{
		AChronoSwitchCharacter* FoundChar = *It;
		if (FoundChar && FoundChar != Interactor)
		{
			if (AChronoSwitchPlayerState* OtherPS = FoundChar->GetPlayerState<AChronoSwitchPlayerState>())
			{
				// Authoritatively switch the other player's timeline.
				const uint8 CurrentID = OtherPS->GetTimelineID();
				const uint8 NewID = (CurrentID == 0) ? 1 : 0;
				OtherPS->SetTimelineID(NewID);
			}
			break; // Found and switched, no need to continue looping.
		}
	}
}
