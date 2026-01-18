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
#include "Game/ChronoSwitchGameState.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"


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
	
	// Create the component that listens for the local player's timeline state changes.
	ObjectPhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("ObjectPhysicsHandle"));
	
	// Sets default private variables
	IsGrabbing = false;
	
	// The third-person body mesh should not be visible to the owning player.
	GetMesh()->SetOwnerNoSee(true);
}

void AChronoSwitchCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Attempt to bind to this character's PlayerState to react to timeline changes.
	// This will retry if the PlayerState is not immediately available.
	BindToPlayerState();	

	// --- Network Optimization: Simulated Proxies ---
	// Remote players (Simulated Proxies) should not calculate local physics against timeline objects.
	// We disable specific collision channels and gravity to force them to strictly interpolate the Server's position.
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
		}

		// Disable gravity for proxies. This prevents them from "falling" locally when the server 
		// stops sending updates (e.g., when standing still), eliminating visual jitter.
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->GravityScale = 0.f;
		}
	}
	
	
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
	
	// Manage movement of grabbed object
	if (IsGrabbing)
	{
		FVector location = FirstPersonCameraComponent->GetComponentLocation() + FirstPersonCameraComponent->GetForwardVector() * 250;
		ObjectPhysicsHandle->SetTargetLocation(location);
		//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, location.ToString());
		
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
		
		if (TimeSwitchAction)
		{
			EnhancedInput->BindAction(TimeSwitchAction, ETriggerEvent::Started, this, &AChronoSwitchCharacter::OnTimeSwitchPressed);
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
	if (!IsGrabbing)
	{
		FHitResult OutHit = FHitResult();
		if (BoxTraceFront(OutHit))
		{
			AActor* HitActor = OutHit.GetActor();
			// Use ImplementsInterface to support Blueprint-only implementations.
			// Cast<IInteractable> fails if the interface is added via Blueprint Class Settings.
			if (HitActor && HitActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
			{
				if (IInteractable::Execute_IsGrabbable(HitActor))
				{
					UPrimitiveComponent* HitComponent = OutHit.GetComponent();
					
					// CHECK: The object must simulate physics to be grabbed by the PhysicsHandle.
					if (HitComponent && HitComponent->IsSimulatingPhysics())
					{
						HitComponent->WakeAllRigidBodies(); // Wake up the object if it was sleeping to save resources.
						ObjectPhysicsHandle->GrabComponentAtLocationWithRotation(HitComponent, FName(), HitComponent->GetComponentLocation(), FRotator(0, 0, 0));
						IsGrabbing = true;
					}
				}
				else
				{
					IInteractable::Execute_Interact(HitActor, this);
				}
			}
		}
	}
	else
	{
		// Safety check to prevent crashes if the grabbed object was destroyed.
		if (UPrimitiveComponent* GrabbedComponent = ObjectPhysicsHandle->GetGrabbedComponent())
		{
			if (AActor* GrabbedActor = GrabbedComponent->GetOwner())
			{
				if (GrabbedActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
				{
					IInteractable::Execute_Release(GrabbedActor);
				}
			}
		}
		ObjectPhysicsHandle->ReleaseComponent();
		IsGrabbing = false;
	}
}

void AChronoSwitchCharacter::ExecuteTimeSwitchLogic()
{
	AChronoSwitchPlayerState* MyPS = GetPlayerState<AChronoSwitchPlayerState>();
	AChronoSwitchGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AChronoSwitchGameState>() : nullptr;

	if (!MyPS || !GameState)
	{
		return;
	}

	// The behavior of the time switch depends on the current game mode.
	switch (GameState->CurrentTimeSwitchMode)
	{
		case ETimeSwitchMode::Personal:
		{
			// In Personal mode, we switch our own timeline.
			const uint8 CurrentID = MyPS->GetTimelineID();
			const uint8 NewID = (CurrentID == 0) ? 1 : 0;
			MyPS->RequestTimelineChange(NewID);
			break;
		}
		case ETimeSwitchMode::CrossPlayer:
		{
			// In CrossPlayer mode, our input requests a switch for the OTHER player.
			// This must be done via a server RPC for authority.
			Server_RequestOtherPlayerSwitch();
			break;
		}
		case ETimeSwitchMode::GlobalTimer:
		default:
			// In GlobalTimer mode, personal input does nothing.
			break;
	}
}

void AChronoSwitchCharacter::Server_RequestOtherPlayerSwitch_Implementation()
{
	// This code runs on the server. Find the other player and switch their timeline.
	if (CachedOtherPlayerCharacter.IsValid())
	{
		if (AChronoSwitchCharacter* OtherChar = Cast<AChronoSwitchCharacter>(CachedOtherPlayerCharacter.Get()))
		{
			if (AChronoSwitchPlayerState* OtherPS = OtherChar->GetPlayerState<AChronoSwitchPlayerState>())
			{
				const uint8 CurrentID = OtherPS->GetTimelineID();
				const uint8 NewID = (CurrentID == 0) ? 1 : 0;
				OtherPS->SetTimelineID(NewID); // Authoritative call

				// Server Replication Priority:
				// Force immediate replication of the PlayerState to minimize desync time.
				OtherPS->ForceNetUpdate();
			}
		}
	}
}

void AChronoSwitchCharacter::Client_ForcedTimelineChange_Implementation(uint8 NewTimelineID)
{
	// This RPC is triggered by the Server to force an immediate state update on the Client.
	// It calls HandleTimelineUpdate, which updates collision, visuals, and flushes the movement buffer.
	HandleTimelineUpdate(NewTimelineID);
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
		// Bind our handler to the PlayerState's delegate.
		PS->OnTimelineIDChanged.AddUObject(this, &AChronoSwitchCharacter::HandleTimelineUpdate);
		
		// Set the initial collision state WITHOUT triggering cosmetic effects.
		UpdateCollisionChannel(PS->GetTimelineID());
	}
	else
	{
		GetWorldTimerManager().SetTimer(PlayerStateBindTimer, this, &AChronoSwitchCharacter::BindToPlayerState, 0.1f, false);
	}
}

void AChronoSwitchCharacter::HandleTimelineUpdate(uint8 NewTimelineID)
{
	// This function is called by the delegate when a change occurs.
	
	// 1. Core Logic: Update physical collision channels.
	UpdateCollisionChannel(NewTimelineID);

	// 2. Network Correction: Flush Server Moves.
	// We discard any saved moves based on the old timeline state. This forces the client 
	// to accept the new server position immediately, preventing rubber banding (snap-back).
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->FlushServerMoves();
	}

	// 3. Cosmetics: Trigger visual and sound effects.
	OnTimelineChangedCosmetic(NewTimelineID);
	OnTimelineSwitched(NewTimelineID);
}

void AChronoSwitchCharacter::UpdateCollisionChannel(uint8 NewTimelineID)
{
	if (!GetCapsuleComponent()) return;
	
	// This logic simply changes our character's physical identity.
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