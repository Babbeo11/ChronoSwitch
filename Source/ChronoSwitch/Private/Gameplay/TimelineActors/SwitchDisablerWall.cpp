// Fill out your copyright notice in the Description page of Project Settings.


#include "Gameplay/TimelineActors/SwitchDisablerWall.h"

#include "Characters/ChronoSwitchCharacter.h"
#include "Components/BoxComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
ASwitchDisablerWall::ASwitchDisablerWall()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	FirstPillar = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPillar"));
	SecondPillar = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SecondPillar"));
	
	EnterBox = CreateDefaultSubobject<UBoxComponent>(TEXT("EnterBox"));
	ExitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ExitBox"));
	
	FirstPillar->SetupAttachment(SceneRoot);
	SecondPillar->SetupAttachment(SceneRoot);
	EnterBox->SetupAttachment(SceneRoot);
	ExitBox->SetupAttachment(SceneRoot);
	
	// EnterBox->OnComponentBeginOverlap.AddDynamic(this, &ASwitchDisablerWall::OnEnterWall);
	// ExitBox->OnComponentBeginOverlap.AddDynamic(this, &ASwitchDisablerWall::OnExitWall);
	
	//TimelineToSwitch = ETimelineType::Past;
}

/*void ASwitchDisablerWall::OnEnterWall(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AChronoSwitchCharacter* Character = Cast<AChronoSwitchCharacter>(OtherActor);
	 if (Character)
	 {
	 	APlayerController* LocalPC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	 	AChronoSwitchPlayerState* PS = (LocalPC) ? Cast<AChronoSwitchPlayerState>(LocalPC->PlayerState) : nullptr;
	 	UE_LOG(LogTemp, Warning, TEXT("Is Character"));
	    if (PS->GetTimelineID() != static_cast<uint8>(TimelineToSwitch))
	    {
		    PS->RequestTimelineChange(static_cast<uint8>(TimelineToSwitch));
	    	UE_LOG(LogTemp, Warning, TEXT("Timeline switched"));
	    }
	 }
}*/

// void ASwitchDisablerWall::OnExitWall(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
// 	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
// {
// 	UE_LOG(LogTemp, Warning, TEXT("Exit Wall"));
// }

// Called when the game starts or when spawned
void ASwitchDisablerWall::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASwitchDisablerWall::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

