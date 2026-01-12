#include "Game/ChronoSwitchPlayerState.h"
#include "Net/UnrealNetwork.h"

AChronoSwitchPlayerState::AChronoSwitchPlayerState()
{
	TimelineID = 0;
	bVisorActive = true;
	
	// Increased priority ensures timeline sync is handled before standard gameplay data
	NetPriority = 3.0f;
}

void AChronoSwitchPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AChronoSwitchPlayerState, TimelineID);
	DOREPLIFETIME(AChronoSwitchPlayerState, bVisorActive);
}

void AChronoSwitchPlayerState::SetTimelineID(uint8 NewID)
{
	if (HasAuthority())
	{
		NotifyTimelineChanged(NewID);
	}
}

void AChronoSwitchPlayerState::RequestTimelineChange(uint8 NewID)
{
	if (TimelineID == NewID) return;

	// Client-side prediction: update locally to remove latency feel
	if (GetNetMode() == NM_Client)
	{
		NotifyTimelineChanged(NewID);
	}
	
	Server_SetTimelineID(NewID);
}

void AChronoSwitchPlayerState::SetVisorActive(bool bNewState)
{
	if (bVisorActive == bNewState) return;

	// Local prediction for the calling client
	if (GetNetMode() == NM_Client)
	{
		NotifyVisorStateChanged(bNewState);
	}

	Server_SetVisorActive(bNewState);
}

void AChronoSwitchPlayerState::NotifyTimelineChanged(uint8 NewID)
{
	if (TimelineID != NewID)
	{
		TimelineID = NewID;
		OnTimelineIDChanged.Broadcast(TimelineID);
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
	// Notify local observers when the value arrives from the server
	OnTimelineIDChanged.Broadcast(TimelineID);
}

void AChronoSwitchPlayerState::OnRep_VisorActive()
{
	OnVisorStateChanged.Broadcast(bVisorActive);
}

void AChronoSwitchPlayerState::Server_SetTimelineID_Implementation(uint8 NewID)
{
	NotifyTimelineChanged(NewID);
}

bool AChronoSwitchPlayerState::Server_SetTimelineID_Validate(uint8 NewID)
{
	return NewID <= 1; // Validation: only allow supported timeline indices
}

void AChronoSwitchPlayerState::Server_SetVisorActive_Implementation(bool bNewState)
{
	NotifyVisorStateChanged(bNewState);
}

bool AChronoSwitchPlayerState::Server_SetVisorActive_Validate(bool bNewState)
{
	return true;
}