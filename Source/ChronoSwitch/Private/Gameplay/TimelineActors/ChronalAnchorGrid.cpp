// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TimelineActors/ChronalAnchorGrid.h"
#include "Characters/ChronoSwitchCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/ChronoSwitchPlayerState.h"


// Sets default values
AChronalAnchorGrid::AChronalAnchorGrid()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BarrierMesh = CreateDefaultSubobject<UStaticMeshComponent>("BarrierMesh");
	GridBorder1 = CreateDefaultSubobject<UStaticMeshComponent>("GridBorder1");
	GridBorder2 = CreateDefaultSubobject<UStaticMeshComponent>("GridBorder2");
	GridBorder1->SetupAttachment(SceneRoot);
	GridBorder2->SetupAttachment(SceneRoot);
	BarrierMesh->SetupAttachment(SceneRoot);
	BoxCollider = CreateDefaultSubobject<UBoxComponent>("BoxCollider");
	BoxCollider->SetupAttachment(SceneRoot);
	
	// Collision Setups
	BarrierMesh->SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	
	BoxCollider->SetGenerateOverlapEvents(true);
	BoxCollider->SetCollisionEnabled(ECollisionEnabled::Type::QueryOnly);
	BoxCollider->SetCollisionResponseToAllChannels(ECR_Overlap);
	
	bShouldDisableVisor = true;
	bShouldDisableSwitch = true;
}

// Called when the game starts or when spawned
void AChronalAnchorGrid::BeginPlay()
{
	Super::BeginPlay();
	
	if (BoxCollider)
	{
		BoxCollider->OnComponentBeginOverlap.AddDynamic(this, &AChronalAnchorGrid::OnBeginOverlap);
		BoxCollider->OnComponentEndOverlap.AddDynamic(this, &AChronalAnchorGrid::OnEndOverlap);
	}
}

void AChronalAnchorGrid::OnBeginOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit)
{
	if (!HasAuthority()) return;

	AChronoSwitchCharacter* Player = Cast<AChronoSwitchCharacter>(Other);
	if (!Player) return;
	
	if (OtherComp != Player->GetCapsuleComponent()) return;

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("OnBeginOverlap"));

	float Sign = GetDirectionSign(Player);
	StoredDirectionSigns.Add(Player, Sign);
}

void AChronalAnchorGrid::OnEndOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex)
{
	if (!HasAuthority()) return;

	AChronoSwitchCharacter* Player = Cast<AChronoSwitchCharacter>(Other);
	if (!Player) return;
	
	if (OtherComp != Player->GetCapsuleComponent()) return;

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("OnEndOverlap"));

	// The map returns a pointer to the value
	float* OldSignPtr = StoredDirectionSigns.Find(Player);
	if (!OldSignPtr)
		return;

	float OldSign = *OldSignPtr;
	float NewSign = GetDirectionSign(Player);
	
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, FString::Printf(TEXT("Crossing: %.2f -> %.2f"), OldSign, NewSign));
	
	// Player is entering the C.A.G Zone 
	if (OldSign < 0 && NewSign > 0)
	{
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(Player->GetPlayerState()))
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("Entered"));
			PS->RequestTimelineChange(static_cast<uint8>(TargetForcedTimeline));
			
			if (bShouldDisableVisor)
			{
				PS->RequestVisorStateChange(false);
			}
			if (bShouldDisableSwitch)
			{
				PS->SetCanSwitchTimeline(false);
			}
		}
	}
	// Player is exiting the C.A.G Zone
	else if (OldSign > 0 && NewSign < 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Orange, TEXT("Exited"));
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(Player->GetPlayerState()))
		{
			if (bShouldDisableVisor)
			{
				PS->RequestVisorStateChange(true);
			}
			if (bShouldDisableSwitch)
			{
				PS->SetCanSwitchTimeline(true);
			}
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
