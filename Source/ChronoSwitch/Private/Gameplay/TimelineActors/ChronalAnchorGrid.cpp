// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TimelineActors/ChronalAnchorGrid.h"
#include "Characters/ChronoSwitchCharacter.h"
#include "Components/BoxComponent.h"
#include "Game/ChronoSwitchPlayerState.h"


// Sets default values
AChronalAnchorGrid::AChronalAnchorGrid()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	
	BarrierMesh = CreateDefaultSubobject<UStaticMeshComponent>("BarrierMesh");
	BarrierMesh->SetupAttachment(GetRootComponent());
	BoxCollider = CreateDefaultSubobject<UBoxComponent>("BoxCollider");
	BoxCollider->SetupAttachment(GetRootComponent());
	
	// Collision Setups
	BarrierMesh->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	
	BoxCollider->SetGenerateOverlapEvents(true);
	BoxCollider->SetCollisionEnabled(ECollisionEnabled::Type::QueryOnly);
	BoxCollider->SetCollisionResponseToAllChannels(ECR_Overlap);
	BoxCollider->OnComponentBeginOverlap.AddDynamic(this, &AChronalAnchorGrid::OnBeginOverlap);
	BoxCollider->OnComponentEndOverlap.AddDynamic(this, &AChronalAnchorGrid::OnEndOverlap);
}

// Called when the game starts or when spawned
void AChronalAnchorGrid::BeginPlay()
{
	Super::BeginPlay();
	
}

void AChronalAnchorGrid::OnBeginOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("OnBeginOverlap"));
	AChronoSwitchCharacter* Player = Cast<AChronoSwitchCharacter>(Other);
	if (!Player)
	{
		return;
	}

	float Sign = GetDirectionSign(Player);
	StoredDirectionSigns.Add(Player, Sign);
}

void AChronalAnchorGrid::OnEndOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex)
{
	// To Do: Disable possibility of player to change visor?
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("OnEndOverlap"));
	AChronoSwitchCharacter* Player = Cast<AChronoSwitchCharacter>(Other);
	if (!Player)
		return;

	// The map returns a pointer to the value
	float* OldSignPtr = StoredDirectionSigns.Find(Player);
	if (!OldSignPtr)
		return;

	float OldSign = *OldSignPtr;
	float NewSign = GetDirectionSign(Player);
	
	// Player is entering the C.A.G Zone 
	if (OldSign < 0 && NewSign > 0)
	{
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(Player->GetPlayerState()))
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("Entered"));
			PS->RequestTimelineChange(static_cast<uint8>(TargetForcedTimeline));
			if (shouldDisableVisor)
				PS->RequestVisorStateChange(false);
		}
	}
	// Player is exiting the C.A.G Zone
	else if (OldSign > 0 && NewSign < 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("Exited"));
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(Player->GetPlayerState()))
		{
			if (shouldDisableVisor)
				PS->RequestVisorStateChange(true);
		}
	}

	StoredDirectionSigns.Remove(Player);
}

float AChronalAnchorGrid::GetDirectionSign(const AActor* Actor) const
{
	FVector Distance = Actor->GetActorLocation() - GetActorLocation();
	return FVector::DotProduct(GetActorForwardVector(), Distance);
}

// Called every frame
void AChronalAnchorGrid::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

