#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ChronoSwitchPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnTimelineIDChanged, uint8);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVisorStateChanged, bool);

/**
 * Custom PlayerState responsible for managing the "Timeline" switching logic.
 * Tracks which reality the player is currently in and synchronizes it across the network.
 */
UCLASS()
class CHRONOSWITCH_API AChronoSwitchPlayerState : public APlayerState
{
	GENERATED_BODY()
	
public:
	AChronoSwitchPlayerState();

	/** Broadcast whenever the Timeline ID is updated */
	FOnTimelineIDChanged OnTimelineIDChanged;
	
	/** Broadcast whenever the Visor state is toggled */
	FOnVisorStateChanged OnVisorStateChanged;

	/** Returns the current Timeline ID */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	FORCEINLINE uint8 GetTimelineID() const { return TimelineID; }

	/** Returns whether the visor is currently active */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	FORCEINLINE bool IsVisorActive() const { return bVisorActive; }

	/** 
	 * Initiates a timeline change request. 
	 * Includes client-side prediction for immediate feedback on the calling client.
	 */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void RequestTimelineChange(uint8 NewID);

	/** 
	 * Forcefully sets the timeline ID. 
	 * Can only be called by the Server (Authority).
	 */
	UFUNCTION(BlueprintAuthorityOnly, Category = "Timeline")
	void SetTimelineID(uint8 NewID);
	
	/** Sets the visor state and synchronizes it across the network */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void SetVisorActive(bool bNewState);

protected:
	/** The current Timeline index, synchronized from Server to Clients */
	UPROPERTY(ReplicatedUsing = OnRep_TimelineID)
	uint8 TimelineID;
	
	/** Whether the special visor is active, allowing the player to see other timelines */
	UPROPERTY(ReplicatedUsing = OnRep_VisorActive, BlueprintReadOnly, Category = "Timeline")
	bool bVisorActive;
	
	/** Replication Support */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_TimelineID();
	
	UFUNCTION()
	void OnRep_VisorActive();

	/** Server RPCs */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetTimelineID(uint8 NewID);
	
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetVisorActive(bool bNewState);

private:
	/** Internal helpers to update local state and notify observers (centralizes logic) */
	void NotifyTimelineChanged(uint8 NewID);
	void NotifyVisorStateChanged(bool bNewState);
};