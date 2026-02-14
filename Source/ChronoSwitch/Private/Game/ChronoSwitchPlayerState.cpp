#include "Game/ChronoSwitchPlayerState.h"

#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "ChronoSwitch/Public/Characters/ChronoSwitchCharacter.h"

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

	// Client-Side Prediction:
	// Update the state locally immediately so the player feels zero latency.
	NotifyTimelineChanged(NewID);
	
	// Send the request to the server to validate and replicate the change.
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

	// Client-Side Prediction:
	// Update the state locally immediately so the player feels zero latency.
	NotifyVisorStateChanged(bNewState);

	// Send the request to the server to validate and replicate the change.
	Server_SetVisorActive(bNewState);
}

void AChronoSwitchPlayerState::SetTimelineID(uint8 NewID)
{
	// This function is the authoritative source for changing the timeline.
	// It can only be called on the server.
	if (HasAuthority())
	{
		NotifyTimelineChanged(NewID);

		// Explicitly tell the owning client to update immediately via a Client RPC.
		// This flushes the movement prediction buffer, preventing the client from "snapping back" to the old timeline.
		if (AChronoSwitchCharacter* ChronoChar = Cast<AChronoSwitchCharacter>(GetPawn()))
		{
			ChronoChar->Client_ForcedTimelineChange(NewID);
		}

		// Broadcast to all clients immediately to ensure observers see the material change instantly.
		Multicast_TimelineChanged(NewID);
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

void AChronoSwitchPlayerState::Multicast_TimelineChanged_Implementation(uint8 NewID)
{
	// Update local state on all clients immediately.
	// If this client is the owner (who predicted), NotifyTimelineChanged handles the redundancy safely.
	NotifyTimelineChanged(NewID);
}


void AChronoSwitchPlayerState::NotifyVisorStateChanged(bool bNewState)
{
	if (bVisorActive != bNewState)
	{
		bVisorActive = bNewState;
		OnVisorStateChanged.Broadcast(bVisorActive);
	}
}

void AChronoSwitchPlayerState::OnRep_TimelineID(uint8 OldTimelineID)
{
	// Handle updates from the server.
	// If this client predicted the change (via RequestTimelineChange), TimelineID will already match,
	// and we do nothing.
	// If this is a remote update (e.g., Global Timer), we broadcast the change to update visuals/collision.
	if (TimelineID != OldTimelineID)
	{
		OnTimelineIDChanged.Broadcast(TimelineID);
	}
}

void AChronoSwitchPlayerState::OnRep_VisorActive(bool bOldVisorActive)
{
	if (bVisorActive != bOldVisorActive)
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