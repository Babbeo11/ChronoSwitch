// Fill out your copyright notice in the Description page of Project Settings.


#include "ChronoSwitch/Public/Game/ChronoSwitchGameState.h"
#include "ChronoSwitch/Public/Game/ChronoSwitchPlayerState.h"
#include "Net/UnrealNetwork.h"

AChronoSwitchGameState::AChronoSwitchGameState()
	: CurrentTimeSwitchMode(ETimeSwitchMode::Personal)
{
}

void AChronoSwitchGameState::BeginPlay()
{
	Super::BeginPlay();

	// On the server, check the initial mode and start the timer if necessary.
	// This handles cases where the default mode is set to GlobalTimer in the Blueprint editor,
	// ensuring the timer starts correctly on map load.
	if (HasAuthority())
	{
		if (CurrentTimeSwitchMode == ETimeSwitchMode::GlobalTimer)
		{
			GetWorld()->GetTimerManager().SetTimer(GlobalSwitchTimerHandle, this, &AChronoSwitchGameState::PerformGlobalSwitch, GlobalSwitchTime, true);
		}
	}
}

void AChronoSwitchGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChronoSwitchGameState, CurrentTimeSwitchMode);
}

void AChronoSwitchGameState::SetTimeSwitchMode(ETimeSwitchMode NewMode)
{
	// Only the server can change the game mode.
	if (HasAuthority() && CurrentTimeSwitchMode != NewMode)
	{
		CurrentTimeSwitchMode = NewMode; // Updates the value and triggers replication.

		// Manage the Global Timer based on the new mode.
		// First, ensure any existing timer is cleared to prevent overlapping logic.
		GetWorld()->GetTimerManager().ClearTimer(GlobalSwitchTimerHandle);

		// If the new mode is GlobalTimer, start the periodic switch.
		if (CurrentTimeSwitchMode == ETimeSwitchMode::GlobalTimer)
		{
			GetWorld()->GetTimerManager().SetTimer(GlobalSwitchTimerHandle, this, &AChronoSwitchGameState::PerformGlobalSwitch, GlobalSwitchTime, true);
		}
	}
}

void AChronoSwitchGameState::OnRep_TimeSwitchMode()
{
	// This function runs on clients when the server updates CurrentTimeSwitchMode.
	// Use this for client-side updates (e.g., updating a UI icon or playing a sound).
}

void AChronoSwitchGameState::PerformGlobalSwitch()
{
	// Executed by the timer on the Server.
	// Iterates through all connected players and forces a timeline switch.
	for (APlayerState* PS : PlayerArray)
	{
		if (AChronoSwitchPlayerState* ChronoPS = Cast<AChronoSwitchPlayerState>(PS))
		{
			const uint8 CurrentID = ChronoPS->GetTimelineID();
			const uint8 NewID = (CurrentID == 0) ? 1 : 0;
			
			// Calling SetTimelineID on the PlayerState will now automatically trigger 
			// the Client_ForcedTimelineChange RPC on the owning client, 
			// ensuring immediate prediction flushing and preventing rubber banding.
			ChronoPS->SetTimelineID(NewID);
		}
	}
}
