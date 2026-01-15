// Fill out your copyright notice in the Description page of Project Settings.


#include "ChronoSwitch/Public/Characters/ChronoSwitchCharacter.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Interfaces/Interactable.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "EngineUtils.h"
#include "Kismet/KismetSystemLibrary.h"


AChronoSwitchCharacter::AChronoSwitchCharacter()
{
	// Enable ticking to handle per-frame logic for player-vs-player interaction.
	PrimaryActorTick.bCanEverTick = true;
	
	// Create and configure the first-person camera.
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	
	// Create the first-person mesh (arms), attached to the camera.
	// This mesh is only visible to the owning player.
	FirstPersonMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMeshComponent->SetupAttachment(FirstPersonCameraComponent);
	FirstPersonMeshComponent->SetOnlyOwnerSee(true);
	FirstPersonMeshComponent->bCastDynamicShadow = false;
	FirstPersonMeshComponent->CastShadow = false;
	
	// The third-person body mesh should not be visible to the owning player.
	GetMesh()->SetOwnerNoSee(true);
}

void AChronoSwitchCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Attempt to bind to this character's PlayerState to react to timeline changes.
	// This will retry if the PlayerState is not immediately available.
	BindToPlayerState();	
	
	// Add the input mapping context for the local player.
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void AChronoSwitchCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// --- Per-Frame Player Interaction Logic ---
	// Tick orchestrates the logic for handling collision and visibility with the other player.

	// Ensure the other player character is cached for efficiency.
	if (!CachedOtherPlayerCharacter.IsValid())
	{
		CacheOtherPlayerCharacter();
	}

	// Cache both player states to avoid fetching them multiple times per frame.
	if (!CachedMyPlayerState.IsValid())
	{
		CachedMyPlayerState = GetPlayerState<AChronoSwitchPlayerState>();
	}
	if (!CachedOtherPlayerState.IsValid() && CachedOtherPlayerCharacter.IsValid())
	{
		if(const auto OtherChar = Cast<AChronoSwitchCharacter>(CachedOtherPlayerCharacter.Get()))
		{
			CachedOtherPlayerState = OtherChar->GetPlayerState<AChronoSwitchPlayerState>();
		}
	}

	// If all required data is available, execute the interaction logic.
	if (CachedMyPlayerState.IsValid() && CachedOtherPlayerState.IsValid())
	{
		UpdatePlayerCollision(CachedMyPlayerState.Get(), CachedOtherPlayerState.Get());
		UpdatePlayerVisibility(CachedMyPlayerState.Get(), CachedOtherPlayerState.Get());
	}
}

void AChronoSwitchCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AChronoSwitchCharacter::Move);
		}

		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AChronoSwitchCharacter::Look);
		}

		if (JumpAction)
		{
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AChronoSwitchCharacter::JumpStart);
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &AChronoSwitchCharacter::JumpStop);
		}
		
		if (InteractAction)
		{
			EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AChronoSwitchCharacter::Interact);
		}
	}
}

/** Handles forward/backward and right/left movement input. */
void AChronoSwitchCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (Controller)
	{
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

/** Handles camera look input (pitch and yaw). */
void AChronoSwitchCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisValue = Value.Get<FVector2D>();
	if (Controller)
	{
		AddControllerYawInput(LookAxisValue.X);
		AddControllerPitchInput(LookAxisValue.Y);
	}
}

/** Handles the start of a jump action. */
void AChronoSwitchCharacter::JumpStart()
{
	Jump();
}

/** Handles the end of a jump action. */
void AChronoSwitchCharacter::JumpStop()
{
	StopJumping();
}

/** Handles the interact action, performing a trace to find an interactable object. */
void AChronoSwitchCharacter::Interact()
{
	FHitResult OutHit = FHitResult();
	if (BoxTraceFront(OutHit))
	{
		AActor* HitActor = OutHit.GetActor();
		// Cast the hit actor to the IInteractable interface and call Interact if successful.
		if (IInteractable* HitActorWithInterface = Cast<IInteractable>(HitActor))
		{
			HitActorWithInterface->Interact();
		}
	}
}

/** Finds the other player character in the world and caches a weak pointer to it for optimization. */
void AChronoSwitchCharacter::CacheOtherPlayerCharacter()
{
	for (TActorIterator<AChronoSwitchCharacter> It(GetWorld()); It; ++It)
	{
		AChronoSwitchCharacter* FoundChar = *It;
		if (FoundChar && FoundChar != this)
		{
			CachedOtherPlayerCharacter = FoundChar;
			return; // Found it, no need to continue looping
		}
	}
}

/** Handles symmetrical player-vs-player collision. This logic runs on all machines. */
void AChronoSwitchCharacter::UpdatePlayerCollision(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS)
{
	// Each character is responsible for deciding whether to ignore the other character for movement.
	// This ensures server and client simulations agree, preventing rubber-banding.
	if (!CachedOtherPlayerCharacter.IsValid()) return;
	
	const bool bAreInSameTimeline = (MyPS->GetTimelineID() == OtherPS->GetTimelineID());
	if (bAreInSameTimeline)
	{
		// If in the same timeline, stop ignoring the other player to enable collision.
		MoveIgnoreActorRemove(CachedOtherPlayerCharacter.Get());
	}
	else
	{
		// If in different timelines, ignore the other player to allow passing through.
		MoveIgnoreActorAdd(CachedOtherPlayerCharacter.Get());
	}
}

/** Handles asymmetrical visibility of the other player. This logic only affects the local player's view. */
void AChronoSwitchCharacter::UpdatePlayerVisibility(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS)
{
	// This logic only needs to run on the locally controlled character.
	if (!IsLocallyControlled() || !CachedOtherPlayerCharacter.IsValid())
	{
		return;
	}

	// As the local player, determine how the other player's character mesh should be rendered.
	USkeletalMeshComponent* OtherPlayerMesh = CachedOtherPlayerCharacter->GetMesh();
	if (OtherPlayerMesh)
	{
		const bool bAreInSameTimeline = (MyPS->GetTimelineID() == OtherPS->GetTimelineID());
		const bool bIsVisorActive = MyPS->IsVisorActive();

		// The other player is visible if they are in the same timeline, or if they are in a different
		// timeline but the local player's visor is active (as a ghost).
		const bool bShouldOtherBeVisibleAsGhost = !bAreInSameTimeline && bIsVisorActive;
		const bool bShouldOtherBeVisible = bAreInSameTimeline || bShouldOtherBeVisibleAsGhost;
		OtherPlayerMesh->SetHiddenInGame(!bShouldOtherBeVisible);
		// A call to update the ghost material effect on the OtherPlayerMesh would go here.
	}
}

/** Binds the UpdateCollisionChannel function to the PlayerState's OnTimelineIDChanged delegate. Retries if the PlayerState is not yet valid. */
void AChronoSwitchCharacter::BindToPlayerState()
{
	if (AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>())
	{
		PS->OnTimelineIDChanged.AddUObject(this, &AChronoSwitchCharacter::UpdateCollisionChannel);
		
		UpdateCollisionChannel(PS->GetTimelineID());
	}
	else
	{
		GetWorldTimerManager().SetTimer(PlayerStateBindTimer, this, &AChronoSwitchCharacter::BindToPlayerState, 0.1f, false);
	}
}

/** Updates this character's collision ObjectType to match its current timeline. */
void AChronoSwitchCharacter::UpdateCollisionChannel(uint8 NewTimelineID)
{
	if (!GetCapsuleComponent()) return;
	
	// This logic simply changes our character's physical identity.
	// The ATimelineBaseActor objects will then use this identity to determine collision.
	ECollisionChannel NewTimelineChannel = (NewTimelineID == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;
	
	GetCapsuleComponent()->SetCollisionObjectType(NewTimelineChannel);
}

/** Performs a box trace from the camera to find an interactable object. */
bool AChronoSwitchCharacter::BoxTraceFront(FHitResult& OutHit, const float DrawDistance, const EDrawDebugTrace::Type Type)
{
	const FVector Start = FirstPersonCameraComponent->GetComponentLocation();
	const FVector End = Start + FirstPersonCameraComponent->GetForwardVector() * DrawDistance;
	const FVector HalfSize = FVector(10.f, 10.f, 10.f);
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	
	// Get the PlayerState directly from the character to determine the correct trace channel.
	const AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>();
	if (!PS)
	{
		return false;
	}
	
	// Select the correct trace channel based on the player's current timeline.
	// ECC_GameTraceChannel3 is for Past, ECC_GameTraceChannel4 is for Future.
	const ECollisionChannel CollisionChannel = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel3 : ECC_GameTraceChannel4;
	
	return UKismetSystemLibrary::BoxTraceSingle(GetWorld(), Start, End, HalfSize, FirstPersonCameraComponent->GetComponentRotation(), UEngineTypes::ConvertToTraceType(CollisionChannel), false, ActorsToIgnore , Type, OutHit , true);
}