// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "MultiplayerSessionsSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class CHRONOSWITCH_API UMultiplayerSessionsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	UMultiplayerSessionsSubsystem();
	
	void CreateSession(int32 NumPublicConnections, bool bIsLAN);
	void JoinSession(FOnlineSessionSearchResult& SessionSearchResult);
	void FindSession(int32 MaxSearchResults);
	
protected:
	
private:
};
