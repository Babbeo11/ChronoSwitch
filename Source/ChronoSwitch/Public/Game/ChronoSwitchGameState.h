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
	GlobalTimer,    // The server switches everyone's timeline periodically.
	CrossPlayer     // Players switch the OTHER player's timeline.
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
	
protected:
	virtual void BeginPlay() override;

private:
	/** RepNotify for CurrentTimeSwitchMode. Called on clients when the mode changes. */
	UFUNCTION()
	void OnRep_TimeSwitchMode();
	
	/** Timer function called periodically when in GlobalTimer mode. Switches all players. */
	void PerformGlobalSwitch();

	/** Handle for the global switch timer. */
	FTimerHandle GlobalSwitchTimerHandle;
};
