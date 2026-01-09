
#include "Game/ChronoSwitchPlayerState.h"
#include "Net/UnrealNetwork.h"

AChronoSwitchPlayerState::AChronoSwitchPlayerState()
{
	TimelineID = 0;
	// Increased priority to ensure timeline synchronization is prioritized over standard gameplay data
	NetPriority = 3.0f; 
}

void AChronoSwitchPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AChronoSwitchPlayerState, TimelineID);
}

void AChronoSwitchPlayerState::SetTimelineID(uint8 NewID)
{
	if (HasAuthority())
	{
		UpdateTimeline(NewID);
	}
}

void AChronoSwitchPlayerState::RequestTimelineChange(uint8 NewID)
{
	if (TimelineID == NewID) return;

	// Client-side prediction: update locally before server confirmation to remove latency feel
	if (GetNetMode() == NM_Client)
	{
		UpdateTimeline(NewID);
	}
	
	Server_SetTimelineID(NewID);
}

bool AChronoSwitchPlayerState::Server_SetTimelineID_Validate(uint8 NewID)
{
	// Logic validation: only allow supported timelines (e.g., 0 and 1)
	return NewID <= 1;
}

void AChronoSwitchPlayerState::Server_SetTimelineID_Implementation(uint8 NewID)
{
	UpdateTimeline(NewID);
}

void AChronoSwitchPlayerState::UpdateTimeline(uint8 NewID)
{
	if (TimelineID != NewID)
	{
		TimelineID = NewID;
		
		// Broadcast immediately for the server and for the predicting client
		OnTimelineIDChanged.Broadcast(TimelineID);
	}
}

void AChronoSwitchPlayerState::OnRep_TimelineID()
{
	// Broadcast to all other clients when the replicated variable arrives
	OnTimelineIDChanged.Broadcast(TimelineID);
}