#include "Game/ChronoSwitchPlayerState.h"

#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"

AChronoSwitchPlayerState::AChronoSwitchPlayerState()
{
	TimelineID = 0;
	bVisorActive = true;
	
	// A higher network priority ensures timeline state changes are sent promptly.
	NetPriority = 3.0f;
}

void AChronoSwitchPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AChronoSwitchPlayerState, TimelineID);
	DOREPLIFETIME(AChronoSwitchPlayerState, bVisorActive);
}

void AChronoSwitchPlayerState::RequestTimelineChange(uint8 NewID)
{
	if (TimelineID == NewID) return;

	// Always update locally for immediate feedback (Prediction).
	NotifyTimelineChanged(NewID);
	
	// Send the request to the server for authoritative execution.
	Server_SetTimelineID(NewID);
}

void AChronoSwitchPlayerState::NotifyTimelineChanged(uint8 NewID)
{
	if (TimelineID != NewID)
	{
		TimelineID = NewID;
		OnTimelineIDChanged.Broadcast(TimelineID);
	}
}

void AChronoSwitchPlayerState::RequestVisorStateChange(bool bNewState)
{
	if (bVisorActive == bNewState) return;

	// Always update locally for immediate feedback (Prediction).
	NotifyVisorStateChanged(bNewState);

	// Send the request to the server for authoritative execution.
	Server_SetVisorActive(bNewState);
}

void AChronoSwitchPlayerState::SetTimelineID(uint8 NewID)
{
	// This function is the authoritative source for changing the timeline.
	// It can only be called on the server.
	if (HasAuthority())
	{
		NotifyTimelineChanged(NewID);
	}
}

void AChronoSwitchPlayerState::SetVisorActive(bool bNewState)
{
	// This function is the authoritative source for changing the visor state.
	// It can only be called on the server.
	if (HasAuthority())
	{
		NotifyVisorStateChanged(bNewState);
	}
}



void AChronoSwitchPlayerState::NotifyVisorStateChanged(bool bNewState)
{
	if (bVisorActive != bNewState)
	{
		bVisorActive = bNewState;
		OnVisorStateChanged.Broadcast(bVisorActive);
	}
}

void AChronoSwitchPlayerState::OnRep_TimelineID()
{
	// When a replicated value changes, this function is called on clients.
	// We skip the broadcast on the client that initiated the change (the Autonomous Proxy)
	// because they have already performed client-side prediction.
	// We check if this PlayerState is owned by the local controller to determine this.
	const APlayerController* PC = GetPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		OnTimelineIDChanged.Broadcast(TimelineID);
	}
}

void AChronoSwitchPlayerState::OnRep_VisorActive()
{
	// Apply the same logic as OnRep_TimelineID.
	const APlayerController* PC = GetPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		OnVisorStateChanged.Broadcast(bVisorActive);
	}
}

void AChronoSwitchPlayerState::Server_SetTimelineID_Implementation(uint8 NewID)
{
	// The server calls its own authoritative function to change the state.
	SetTimelineID(NewID);
}

bool AChronoSwitchPlayerState::Server_SetTimelineID_Validate(uint8 NewID)
{
	// Basic validation: only allow supported timeline indices.
	return NewID <= 1;
}

void AChronoSwitchPlayerState::Server_SetVisorActive_Implementation(bool bNewState)
{
	// The server calls its own authoritative function to change the state.
	SetVisorActive(bNewState);
}

bool AChronoSwitchPlayerState::Server_SetVisorActive_Validate(bool bNewState)
{
	return true;
}