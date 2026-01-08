// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TestingObject.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Net/UnrealNetwork.h"


// Sets default values
ATestingObject::ATestingObject()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(RootComponent);
	SetNetDormancy(DORM_Awake);
}

void ATestingObject::UpdateVisibilityFromPlayerState(AChronoSwitchPlayerState* PS, int32 TargetTimelineID)
{
	if (!HasAuthority() || !PS)
		return;
	bool bShouldBeVisible = (PS->TimelineID == TargetTimelineID);
	
	if (bIsVisible != bShouldBeVisible)
	{
		bIsVisible = bShouldBeVisible;
		ForceNetUpdate();
	}
}

bool ATestingObject::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget,
	const FVector& SrcLocation) const
{
	//UE_LOG(LogTemp, Warning, TEXT("IsNetRelevantFor called for actor %s, RealViewer %s"), *GetName(), RealViewer ? *RealViewer->GetName() : TEXT("nullptr"));
	const APlayerController* PC = Cast<APlayerController>(RealViewer);
	if (PC && PC->PlayerState)
	{
		
		
		if (const AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(PC->PlayerState))
		{
			UE_LOG(LogTemp, Warning, TEXT("Override"));
			return PS->TimelineID == TimelineID;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("non Override"));
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

void ATestingObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATestingObject, bIsVisible);
}

void ATestingObject::OnRep_VisibilityChanged()
{
	SetActorHiddenInGame(!bIsVisible);
	
	SetActorEnableCollision(bIsVisible);
}

void ATestingObject::BeginPlay()
{
	Super::BeginPlay();

}



// Called every frame
void ATestingObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

