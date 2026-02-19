#include "FunctionLibrary/ChronoSwitchBlueprintLibrary.h"
#include "Game/ChronoSwitchGameState.h"
#include "Game/ChronoSwitchPlayerState.h"

AChronoSwitchGameState* UChronoSwitchBlueprintLibrary::GetChronoGameState(const UObject* WorldContextObject)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return World->GetGameState<AChronoSwitchGameState>();
	}
	return nullptr;
}

AChronoSwitchPlayerState* UChronoSwitchBlueprintLibrary::GetChronoPlayerState(const AActor* TargetActor)
{
	if (!TargetActor) return nullptr;

	// Case 1: Target is a Pawn/Character
	if (const APawn* Pawn = Cast<APawn>(TargetActor))
	{
		return Pawn->GetPlayerState<AChronoSwitchPlayerState>();
	}

	// Case 2: Target is a Controller
	if (const AController* Controller = Cast<AController>(TargetActor))
	{
		return Controller->GetPlayerState<AChronoSwitchPlayerState>();
	}

	// Case 3: Target is the PlayerState itself (just casting)
	if (const APlayerState* PS = Cast<APlayerState>(TargetActor))
	{
		return Cast<AChronoSwitchPlayerState>(const_cast<APlayerState*>(PS));
	}

	return nullptr;
}