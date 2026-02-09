// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ChronoSwitchGameState.generated.h"

/**
 * 
 */

/**
 * Defines the different modes for switching timelines in the game.
 */
UENUM(BlueprintType)
enum class ETimeSwitchMode : uint8
{
	None,           // No time switches occur.
	Personal,       // Players switch their own timeline manually.
	CrossPlayer,     // Players switch the OTHER player's timeline.
	GlobalTimer    // The server switches everyone's timeline periodically.
};

/**
 * Manages the global state of the game, specifically the active Time Switch Mode.
 * Handles the Global Timer logic when that mode is active.
 */
UCLASS()
class CHRONOSWITCH_API AChronoSwitchGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AChronoSwitchGameState();
	
	// --- Replication ---
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// --- Game Rules ---

	/** Sets the current time switch mode. Can only be called on the server. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Game Rules")
	void SetTimeSwitchMode(ETimeSwitchMode NewMode);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game Rules", meta = (DisplayPriority = "0"))
	float GlobalSwitchTime = 5.0f;
	
	/** The current mode governing how timeline switches occur. Replicated to all clients. */
	UPROPERTY(ReplicatedUsing = OnRep_TimeSwitchMode, EditAnywhere, BlueprintReadWrite, Category = "Game Rules", meta = (DisplayPriority = "0"))
	ETimeSwitchMode CurrentTimeSwitchMode;
	
	/** Timer function called periodically when in GlobalTimer mode. Switches all players. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly ,Category = "Game Rules")
	void PerformGlobalSwitch();
	
	/** Forces all players to switch to a specific timeline ID (0 or 1). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Game Rules")
	void SetGlobalTimeline(uint8 TargetID);
	
	/** Forces all players to set their visor state to a specific value. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Game Rules")
	void SetGlobalVisorState(bool bNewState);
	
	UFUNCTION(BlueprintCallable, Category = "Game Rules")
	bool AreBothPlayersInTimeline(uint8 TimelineID) const;

protected:
	virtual void BeginPlay() override;

private:
	/** RepNotify for CurrentTimeSwitchMode. Called on clients when the mode changes. */
	UFUNCTION()
	void OnRep_TimeSwitchMode();

	/** Handle for the global switch timer. */
	FTimerHandle GlobalSwitchTimerHandle;
};
