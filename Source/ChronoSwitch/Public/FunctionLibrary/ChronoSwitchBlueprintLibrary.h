#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Game/ChronoSwitchGameState.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "ChronoSwitchBlueprintLibrary.generated.h"

UCLASS()
class CHRONOSWITCH_API UChronoSwitchBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 
	 * Returns the ChronoSwitchGameState directly, avoiding manual casting in Blueprints.
	 * Pure node: doesn't require execution pins.
	 */
	UFUNCTION(BlueprintPure, Category = "ChronoSwitch|Helpers", meta = (WorldContext = "WorldContextObject"))
	static AChronoSwitchGameState* GetChronoGameState(const UObject* WorldContextObject);

	/** 
	 * Returns the ChronoSwitchPlayerState for the provided Controller or Pawn.
	 */
	UFUNCTION(BlueprintPure, Category = "ChronoSwitch|Helpers")
	static AChronoSwitchPlayerState* GetChronoPlayerState(const AActor* TargetActor);
};