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
		// Refresh state to match the new target timeline
		InitializeBinding();
	}
}

ECollisionChannel UTimelineObserverComponent::GetCollisionChannelForTimeline(ETimelineType Timeline)
{
	return (Timeline == ETimelineType::Past) ? CHANNEL_PAST : CHANNEL_FUTURE;
}

ECollisionChannel UTimelineObserverComponent::GetCollisionTraceChannelForTimeline(ETimelineType Timeline)
{
	return (Timeline == ETimelineType::Past) ? CHANNEL_TRACE_PAST : CHANNEL_TRACE_FUTURE;
}

void UTimelineObserverComponent::BeginPlay()
{
	Super::BeginPlay();
	SetupActorCollision();
	InitializeBinding();
}

void UTimelineObserverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up delegate subscriptions using the cached PlayerState
	if (CachedPlayerState.IsValid())
	{
		CachedPlayerState->OnTimelineIDChanged.Remove(OnTimelineIDChangedHandle);
		CachedPlayerState->OnVisorStateChanged.Remove(OnVisorStateChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UTimelineObserverComponent::InitializeBinding()
{
	// Clear any pending retry timers
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
	}

	APlayerController* LocalPC = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	// Ensure we only bind on the local client/player observing the timeline
	if (LocalPC && LocalPC->IsLocalController() && LocalPC->PlayerState)
	{
		if (AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(LocalPC->PlayerState))
		{
			CachedPlayerState = PS;

			OnTimelineIDChangedHandle = PS->OnTimelineIDChanged.AddUObject(this, &UTimelineObserverComponent::HandleTimelineChanged);
			OnVisorStateChangedHandle = PS->OnVisorStateChanged.AddUObject(this, &UTimelineObserverComponent::HandleVisorStateChanged);
			
			UpdateTimelineState(CachedPlayerState->GetTimelineID());
			return;
		}
	}
	
	// If PlayerState isn't ready yet (common during initial spawn), retry shortly
	if (GetWorld() && !GetWorld()->IsNetMode(NM_DedicatedServer))
	{
		FTimerHandle RetryTimer;
		GetWorld()->GetTimerManager().SetTimer(RetryTimer, this, &UTimelineObserverComponent::InitializeBinding, BINDING_RETRY_DELAY, false);
	}
}

void UTimelineObserverComponent::HandleTimelineChanged(uint8 NewTimelineID)
{
	UpdateTimelineState(NewTimelineID);
}

void UTimelineObserverComponent::HandleVisorStateChanged(bool bNewState)
{
	// The new state is implicitly handled by UpdateTimelineState which reads it from the PlayerState.
	if (CachedPlayerState.IsValid())
	{
		UpdateTimelineState(CachedPlayerState->GetTimelineID());
	}
}

void UTimelineObserverComponent::UpdateTimelineState(uint8 CurrentTimelineID)
{
	if (!CachedPlayerState.IsValid()) return;

	const bool bActiveInTimeline = (CurrentTimelineID == static_cast<uint8>(TargetTimeline));
	const bool bVisorActive = CachedPlayerState->IsVisorActive();

	// Visibility is granted if we are in the correct timeline OR if the visor is active
	const bool bShouldBeVisible = bActiveInTimeline || bVisorActive;

	TArray<UPrimitiveComponent*> Primitives;
	GetOwner()->GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive)
		{
			Primitive->SetHiddenInGame(!bShouldBeVisible);
		}
	}

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
	
	const ETimelineType OtherTimeline = (TargetTimeline == ETimelineType::Past) ? ETimelineType::Future : ETimelineType::Past;

	const ECollisionChannel MyChannel = GetCollisionChannelForTimeline(TargetTimeline);
	const ECollisionChannel OtherChannel = GetCollisionChannelForTimeline(OtherTimeline);
	
	const ECollisionChannel MyTraceChannel = GetCollisionTraceChannelForTimeline(TargetTimeline);
	const ECollisionChannel OtherTraceChannel = GetCollisionTraceChannelForTimeline(OtherTimeline);
	
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Primitive || Primitive->ComponentHasTag(FName("NoTimelineCollision"))) continue;

		
		// Use 'Custom' profile to allow fine-grained control over timeline channels
		
		Primitive->SetCollisionProfileName(TEXT("Custom"));
		Primitive->SetCollisionObjectType(MyChannel);
		
		Primitive->SetCollisionResponseToAllChannels(ECR_Block);
		
		// Ensure we block objects in our own timeline but ignore those in the 'other' timeline
		Primitive->SetCollisionResponseToChannel(MyChannel, ECR_Block);
		Primitive->SetCollisionResponseToChannel(OtherChannel, ECR_Ignore);
		
		Primitive->SetCollisionResponseToChannel(MyTraceChannel, ECR_Block);
		Primitive->SetCollisionResponseToChannel(OtherTraceChannel, ECR_Ignore);
		
		// Standard gameplay interaction
		Primitive->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}
}