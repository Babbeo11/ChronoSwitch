#include "Gameplay/ActorComponents/TimelineObserverComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

UTimelineObserverComponent::UTimelineObserverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	TargetTimeline = ETimelineType::Past;
}

void UTimelineObserverComponent::SetTargetTimeline(ETimelineType NewTimeline)
{
	if (TargetTimeline == NewTimeline) return;

	TargetTimeline = NewTimeline;
    
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		SetupActorCollision();
		// Re-initialize or refresh state if the target changed at runtime
		InitializeBinding();
	}
}

void UTimelineObserverComponent::BeginPlay()
{
	Super::BeginPlay();
	SetupActorCollision();
	InitializeBinding();
}

void UTimelineObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up delegate subscription to prevent memory leaks or crashes on stale objects
	if (TimelineDelegateHandle.IsValid())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(PC->PlayerState))
			{
				PS->OnTimelineIDChanged.Remove(TimelineDelegateHandle);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UTimelineObserverComponent::InitializeBinding()
{
	APlayerController* LocalPC = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	// Ensure we have a valid local controller and player state (might not be ready on first frame)
	if (LocalPC && LocalPC->IsLocalController() && LocalPC->PlayerState)
	{
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(LocalPC->PlayerState))
		{
			if (TimelineDelegateHandle.IsValid())
			{
				PS->OnTimelineIDChanged.Remove(TimelineDelegateHandle);
			}

			TimelineDelegateHandle = PS->OnTimelineIDChanged.AddUObject(this, &UTimelineObserverComponent::HandleTimelineChanged);
			UpdateTimelineState(PS->GetTimelineID());
			return;
		}
	}

	// Retry binding if PlayerState or Controller isn't ready yet
	if (GetNetMode() != NM_DedicatedServer)
	{
		FTimerHandle RetryTimer;
		GetWorld()->GetTimerManager().SetTimer(RetryTimer, this, &UTimelineObserverComponent::InitializeBinding, 0.2f, false);
	}
}

void UTimelineObserverComponent::HandleTimelineChanged(uint8 NewTimelineID)
{
	UpdateTimelineState(NewTimelineID);
}

void UTimelineObserverComponent::UpdateTimelineState(uint8 CurrentTimelineID)
{
	const bool bActiveInTimeline = (CurrentTimelineID == static_cast<uint8>(TargetTimeline));
	
	if (OnTimelineStateChanged.IsBound())
	{
		OnTimelineStateChanged.Broadcast(bActiveInTimeline);
	}
}

void UTimelineObserverComponent::SetupActorCollision()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	TArray<UPrimitiveComponent*> Primitives;
	Owner->GetComponents<UPrimitiveComponent>(Primitives);

	const ECollisionChannel MyChannel = (TargetTimeline == ETimelineType::Past) ? CHANNEL_PAST : CHANNEL_FUTURE;
	const ECollisionChannel OtherChannel = (TargetTimeline == ETimelineType::Past) ? CHANNEL_FUTURE : CHANNEL_PAST;

	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Primitive) continue;

		// Set custom collision profile to allow manual channel overrides
		Primitive->SetCollisionProfileName(TEXT("Custom"));
		Primitive->SetCollisionObjectType(MyChannel);
		
		// Configure responses: block current timeline, ignore the other
		Primitive->SetCollisionResponseToAllChannels(ECR_Block);
		Primitive->SetCollisionResponseToChannel(MyChannel, ECR_Block);
		Primitive->SetCollisionResponseToChannel(OtherChannel, ECR_Ignore);
		
		// Ensure interaction with pawns remains consistent
		Primitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}