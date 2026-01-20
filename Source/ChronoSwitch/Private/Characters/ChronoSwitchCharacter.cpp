// Fill out your copyright notice in the Description page of Project Settings.


#include "ChronoSwitch/Public/Characters/ChronoSwitchCharacter.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "EngineUtils.h"
#include "Game/ChronoSwitchGameState.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/Interactable.h"
#include "Net/UnrealNetwork.h"


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
	ObjectPhysicsHandle->LinearStiffness = 750.0f;
	ObjectPhysicsHandle->LinearDamping = 200.0f;
	ObjectPhysicsHandle->AngularDamping = 500.0f;
	ObjectPhysicsHandle->AngularStiffness = 1500.0f;
	ObjectPhysicsHandle->InterpolationSpeed = 50.0f;
	
	// Sets default private variables
	GrabbedComponent = nullptr;
	
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

void AChronoSwitchCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedComponent);
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
	
	// --- Physics Handle Update ---
	// If we are holding an object, update its target location to follow the camera.
	if (HasAuthority())
	{
		FVector CameraLoc;
		FRotator CameraRot;
		GetActorEyesViewPoint(CameraLoc, CameraRot);
		
		const FVector TargetLocation = CameraLoc + CameraRot.Vector() * HoldDistance;
		const FRotator TargetRotation = FRotator(0, CameraRot.Yaw, 0); // Keep object upright (Yaw only)
		
		ObjectPhysicsHandle->SetTargetLocationAndRotation(TargetLocation, TargetRotation);
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
			// Bind only the main Interact function. It will handle the logic flow (Release vs Grab vs Interact).
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
	// 1. Priority: Release if we are already holding an object.
	// We check the replicated GrabbedComponent because GetGrabbedComponent() is only valid on the Server.
	if (GrabbedComponent)
	{
		Release();
		return;
	}

	// 2. Interaction: Check for interactable objects (Buttons, Levers) via BoxTrace.
	FHitResult HitResult;
	if (BoxTraceFront(HitResult))
	{
		AActor* HitActor = HitResult.GetActor();
		if (HitActor && HitActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		{
			IInteractable::Execute_Interact(HitActor, this);
		}
	}

	// 3. Grabbing: Attempt to grab a physics object.
	// This calls the Server RPC which performs its own validation trace.
	AttemptGrab();
}

void AChronoSwitchCharacter::AttemptGrab()
{
	// Forward the request to the server.
	Server_Grab();
}

void AChronoSwitchCharacter::Release()
{
	// Forward the request to the server.
	Server_Release();
}

void AChronoSwitchCharacter::Server_Grab_Implementation()
{
	// Cannot grab if already holding something.
	if (ObjectPhysicsHandle->GetGrabbedComponent()) return;
	
	FHitResult HitResult;
	
	// Use BoxTraceFront to ensure we trace against the correct Timeline channel (Past/Future).
	if (BoxTraceFront(HitResult, ReachDistance))
	{
		UPrimitiveComponent* ComponentToGrab = HitResult.GetComponent();

		// Validate that the component exists and simulates physics.
		if (ComponentToGrab && ComponentToGrab->IsSimulatingPhysics())
		{
			// Wake up the physics body to ensure it reacts immediately to the handle.
			ComponentToGrab->WakeAllRigidBodies(); 
			
			// Configure the physics handle for a tight grip.
			// High stiffness ensures the object follows the camera closely without lagging.
			ObjectPhysicsHandle->LinearStiffness = 5000.0f;
			ObjectPhysicsHandle->AngularStiffness = 4000.0f;
			
			// Grab the component at the impact point.
			ObjectPhysicsHandle->GrabComponentAtLocation(ComponentToGrab, NAME_None, HitResult.ImpactPoint);
			
			// Update the replicated property so clients know an object is being held.
			GrabbedComponent = ComponentToGrab; 
			
			// Lock rotation to keep the object upright (preventing it from spinning wildly while held).
			if (FBodyInstance* BodyInstance = ComponentToGrab->GetBodyInstance())
			{
				BodyInstance->bLockXRotation = true;
				BodyInstance->bLockYRotation = true;
				BodyInstance->bLockZRotation = true;
				
				// Switch to Default DOF mode to apply the locks.
				BodyInstance->SetDOFLock(EDOFMode::Default);
			}
		}
	}
}

void AChronoSwitchCharacter::Server_Release_Implementation()
{
	// Clear the replicated property.
	GrabbedComponent = nullptr; 

	if (UPrimitiveComponent* GrabbedMesh = ObjectPhysicsHandle->GetGrabbedComponent())
	{
		// Re-enable collision between the object and the character.
		GrabbedMesh->IgnoreActorWhenMoving(this, false);
		
		// Detach the object from the physics handle.
		ObjectPhysicsHandle->ReleaseComponent();

		// Restore full degrees of freedom (allow rotation) for the physics object.
		if (FBodyInstance* BodyInstance = GrabbedMesh->GetBodyInstance())
		{
			BodyInstance->SetDOFLock(EDOFMode::SixDOF);
		}
	}
}

void AChronoSwitchCharacter::OnRep_GrabbedComponent(UPrimitiveComponent* OldComponent)
{
	// --- Client-Side Physics Optimization ---
	
	// Case 1: We just grabbed an object (GrabbedComponent is valid).
	// We disable physics simulation on the client side.
	// Why? Because the Server is updating the object's position via the Physics Handle.
	// If the client also simulates physics (gravity), it fights the server updates, causing visual jitter.
	if (GrabbedComponent)
	{
		GrabbedComponent->SetSimulatePhysics(false);
	}

	// Case 2: We just released an object (OldComponent was valid).
	// We re-enable physics simulation so the object falls/tumbles naturally on the client.
	if (OldComponent && IsValid(OldComponent))
	{
		OldComponent->SetSimulatePhysics(true);
		OldComponent->WakeAllRigidBodies();
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

	// --- Simulated Proxy Physics Management ---
	// We dynamically adjust the remote player's physics to solve both Jitter and Dragging issues.
	if (ACharacter* OtherChar = CachedOtherPlayerCharacter.Get())
	{
		if (OtherChar->GetLocalRole() == ROLE_SimulatedProxy)
		{
			UCharacterMovementComponent* OtherCMC = OtherChar->GetCharacterMovement();
			UCapsuleComponent* OtherCapsule = OtherChar->GetCapsuleComponent();

			if (OtherCMC && OtherCapsule)
			{
				// HYBRID PHYSICS LOGIC:
				// Check if the proxy is standing on a physics object (e.g., a cube being dragged).
				UPrimitiveComponent* BaseComponent = OtherChar->GetMovementBase();
				const bool bIsOnPhysicsObject = BaseComponent && BaseComponent->IsSimulatingPhysics();

				if (bIsOnPhysicsObject)
				{
					// DRAGGING MODE: Enable Physics.
					// The player is on a moving physics object. We enable gravity so they "stick" 
					// to the object and move with it locally via friction.
					OtherCMC->GravityScale = 1.0f;

					// Configure collision to block ONLY objects in the Proxy's current timeline.
					const uint8 OtherTimelineID = OtherPS->GetTimelineID();
					if (OtherTimelineID == 0) // Past
					{
						OtherCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
						OtherCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
					}
					else // Future
					{
						OtherCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
						OtherCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
					}
				}
				else
				{
					// STABILITY MODE (Default): Disable Physics.
					// The player is on a static floor (TimelineActor) or switching timelines. 
					// We disable gravity to prevent them from falling through "Ghost" floors 
					// or jittering when the server correction fights local gravity.
					OtherCMC->GravityScale = 0.0f;
					
					OtherCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
					OtherCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
				}
			}
		}
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
	// Use GetActorEyesViewPoint for consistent start location and rotation on both Client and Server.
	// Accessing FirstPersonCameraComponent directly on the Server can be unreliable if the component isn't updated.
	FVector Start;
	FRotator Rot;
	GetActorEyesViewPoint(Start, Rot);

	const FVector End = Start + Rot.Vector() * DrawDistance;
	const FVector HalfSize = FVector(10.f, 10.f, 10.f);
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	
	// Get the PlayerState directly from the character to determine the correct trace channel.
	const AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>();
	if (!PS)
	{
		return false;
	}
	
	// Select the correct Object Channel based on the player's current timeline.
	// ECC_GameTraceChannel3 is for Past Objects, ECC_GameTraceChannel4 is for Future Objects.
	const ECollisionChannel TargetChannel = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel3 : ECC_GameTraceChannel4;
	// Also include the Player Channel for the current timeline, in case actors are using that channel.
	const ECollisionChannel PlayerChannel = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;

	// We must use an Object Trace because the Timeline Actors are set to specific Object Types (3/4)
	// and might ignore standard visibility traces.
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(TargetChannel));
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(PlayerChannel));
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_PhysicsBody)); // Also include standard physics objects
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic)); // Include dynamic objects (props, movers)
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic)); // Include static objects (buttons, walls for occlusion)
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn)); // Include pawns (other players/NPCs)
	
	return UKismetSystemLibrary::BoxTraceSingleForObjects(GetWorld(), Start, End, HalfSize, Rot, ObjectTypes, false, ActorsToIgnore , Type, OutHit , true);
}