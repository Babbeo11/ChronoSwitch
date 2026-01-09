// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ChronoSwitchPlayerState.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnTimelineIDChanged, uint8);

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

	/** Returns the current Timeline ID */
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	FORCEINLINE uint8 GetTimelineID() const { return TimelineID; }

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

	/** Delegate broadcast whenever the Timeline ID is updated */
	FOnTimelineIDChanged OnTimelineIDChanged;
	
protected:
	/** The current Timeline index, synchronized from Server to Clients */
	UPROPERTY(ReplicatedUsing = OnRep_TimelineID)
	uint8 TimelineID;
	
	/** RPC to handle the timeline change on the server */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SetTimelineID(uint8 NewID);
	
	/** Replication callback for TimelineID */
	UFUNCTION()
	void OnRep_TimelineID();
	
	/** Internal helper to update the local value and notify listeners */
	void UpdateTimeline(uint8 NewID);
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};