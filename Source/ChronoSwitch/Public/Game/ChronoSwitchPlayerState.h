#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ChronoSwitchPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnTimelineIDChanged, uint8);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVisorStateChanged, bool);

/** Manages player-specific state that needs to be synchronized across the network,
 *  such as the current timeline and visor status.
*/
UCLASS()
class CHRONOSWITCH_API AChronoSwitchPlayerState : public APlayerState
{
	GENERATED_BODY()
	
public:
	AChronoSwitchPlayerState();

	// --- Delegates ---

	/** Broadcasts locally whenever the Timeline ID is updated. */
	FOnTimelineIDChanged OnTimelineIDChanged;
	
	/** Broadcasts locally whenever the Visor state is toggled. */
	FOnVisorStateChanged OnVisorStateChanged;

	// --- Getters ---

	/** Returns the current Timeline ID. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	FORCEINLINE uint8 GetTimelineID() const { return TimelineID; }

	/** Returns true if the timeline-viewing visor is currently active. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	FORCEINLINE bool IsVisorActive() const { return bVisorActive; }

	// --- State Change Requests (Client Side Prediction) ---

	/** Initiates a timeline change request. Includes client-side prediction for immediate feedback. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void RequestTimelineChange(uint8 NewID);

	/** Initiates a visor state change request. Includes client-side prediction for immediate feedback. */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void RequestVisorStateChange(bool bNewState);

	// --- Authority Setters (Server Side) ---

	/** Forcefully sets the timeline ID. Can only be called on the server. */
	UFUNCTION(BlueprintAuthorityOnly, Category = "Timeline")
	void SetTimelineID(uint8 NewID);
	
	/** Forcefully sets the visor state. Can only be called on the server. */
	UFUNCTION(BlueprintAuthorityOnly, Category = "Timeline")
	void SetVisorActive(bool bNewState);
	
	UFUNCTION(BlueprintAuthorityOnly, Category = "Timeline")
	void SetCanSwitchTimeline(bool bNewState);

protected:
	// --- Replicated Properties ---

	/** The current timeline index (0 for Past, 1 for Future). Replicated to all clients. */
	UPROPERTY(ReplicatedUsing = OnRep_TimelineID)
	uint8 TimelineID;
	
	/** True if the special visor is active, allowing the player to see elements from the other timeline. */
	UPROPERTY(ReplicatedUsing = OnRep_VisorActive, BlueprintReadOnly, Category = "Timeline")
	bool bVisorActive;
	
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Timeline")
	bool bCanSwitchTimeline;
	
	/** Standard Unreal function for defining replicated properties. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** RepNotify function called on clients when TimelineID is replicated. */
	UFUNCTION()
	void OnRep_TimelineID(uint8 OldTimelineID);
	
	/** RepNotify function called on clients when bVisorActive is replicated. */
	UFUNCTION()
	void OnRep_VisorActive(bool bOldVisorActive);

	// --- Server RPCs (Internal) ---

	/** Server-side implementation for a timeline change request. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetTimelineID(uint8 NewID);
	
	/** Server-side implementation for a visor state change request. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetVisorActive(bool bNewState);

	/** Multicast RPC to broadcast timeline changes immediately to all clients, bypassing replication delay. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_TimelineChanged(uint8 NewID);

public:
	// --- Helpers (Exposed for RPCs) ---

	/** Updates the local state and broadcasts the change. Used by Client RPCs to sync immediately. */
	void NotifyTimelineChanged(uint8 NewID);
	
private:
	/** Internal helper to update the local state and broadcast the change. */
	void NotifyVisorStateChanged(bool bNewState);
};