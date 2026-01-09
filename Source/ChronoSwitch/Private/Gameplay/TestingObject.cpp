// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TestingObject.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
ATestingObject::ATestingObject()
{
	
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	
	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	RootComponent = StaticMesh;
	
	RequiredTimelineID = 0;
}

void ATestingObject::BeginPlay()
{
	Super::BeginPlay();
	
	RefreshLocalVisibility();
}

void ATestingObject::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TimelineDelegateHandle.IsValid())
	{
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(PC->PlayerState))
			{
				PS->OnTimelineIDChanged.Remove(TimelineDelegateHandle);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATestingObject::RefreshLocalVisibility()
{
	APlayerController* LocalPC = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	if (LocalPC && LocalPC->IsLocalController() && LocalPC->PlayerState)
	{
		AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(LocalPC->PlayerState);
		if (PS)
		{
			if (TimelineDelegateHandle.IsValid())
			{
				PS->OnTimelineIDChanged.Remove(TimelineDelegateHandle);
			}

			TimelineDelegateHandle = PS->OnTimelineIDChanged.AddUObject(this, &ATestingObject::OnTimelineIDChanged);
			
			OnTimelineIDChanged(PS->GetTimelineID());
			return;
		}
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		FTimerHandle RetryTimer;
		GetWorldTimerManager().SetTimer(RetryTimer, this, &ATestingObject::RefreshLocalVisibility, 0.2f, false);
	}
}

void ATestingObject::OnTimelineIDChanged(uint8 NewTimelineID)
{
	bool bShouldBeVisible = (NewTimelineID == RequiredTimelineID);
	
	OnVisibilityChanged(bShouldBeVisible);
    
	// if (StaticMesh)
	// {
	// 	StaticMesh->SetHiddenInGame(!bShouldBeVisible);
	// }
}


// Called every frame
void ATestingObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

