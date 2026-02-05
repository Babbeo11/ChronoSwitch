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
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/Interactable.h"
#include "Net/UnrealNetwork.h"

#pragma region Lifecycle

AChronoSwitchCharacter::AChronoSwitchCharacter()
{
	// Enable ticking to handle per-frame logic for player-vs-player interaction.
	PrimaryActorTick.bCanEverTick = true;
	// Update after physics and camera to prevent visual lag for held objects.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	
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
	
	// Sets default private variables
	GrabbedComponent = nullptr;
	GrabbedRelativeRotation = FRotator::ZeroRotator;
	
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

	// Execute interaction and visibility logic.
	if (CachedMyPlayerState.IsValid() && CachedOtherPlayerState.IsValid())
	{
		UpdatePlayerCollision(CachedMyPlayerState.Get(), CachedOtherPlayerState.Get());
		UpdatePlayerVisibility(CachedMyPlayerState.Get(), CachedOtherPlayerState.Get());
	}
	
	// Update held object transform (Kinematic).
	UpdateHeldObjectTransform(DeltaTime);
}

void AChronoSwitchCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedComponent);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedMeshOriginalCollision);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedRelativeRotation);
}

#pragma endregion

#pragma region Input

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
	
	// Reduce sensitivity when holding an object.
	// This simulates weight and prevents network desync during fast turns.
	if (GrabbedComponent)
	{
		LookAxisValue *= 0.25f; // Reduce sensitivity to 25%
	}

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

#pragma endregion

#pragma region Interaction System

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
	// Cannot grab if already holding something (check the replicated pointer).
	if (GrabbedComponent) return;
	
	FHitResult HitResult;
	
	// Use BoxTraceFront to ensure we trace against the correct Timeline channel (Past/Future).
	if (BoxTraceFront(HitResult, ReachDistance))
	{
		UPrimitiveComponent* ComponentToGrab = HitResult.GetComponent();

		// Validate that the component exists and simulates physics.
		if (ComponentToGrab && ComponentToGrab->IsSimulatingPhysics())
		{
			// Prevent grabbing the current movement base to avoid physics loops.
			if (GetCharacterMovement() && GetCharacterMovement()->GetMovementBase() == ComponentToGrab)
			{
				return;
			}

			// Prepare object for kinematic attachment.
			ComponentToGrab->WakeAllRigidBodies(); 
			ComponentToGrab->SetSimulatePhysics(false);

			// Ignore collision with self.
			ComponentToGrab->IgnoreActorWhenMoving(this, true);
			
			// Make character ignore the object to prevent "bootstrapping" (flying while standing on held object).
			if (AActor* OwnerActor = ComponentToGrab->GetOwner())
			{
				MoveIgnoreActorAdd(OwnerActor);
			}

			GrabbedMeshOriginalCollision = ComponentToGrab->GetCollisionObjectType();
			
			// Temporarily change ObjectType to PhysicsBody to allow collision with Simulated Proxies.
			ComponentToGrab->SetCollisionObjectType(ECC_PhysicsBody);
			
			// Calculate relative rotation (Yaw only) to keep the object upright.
			FVector CamLoc;
			FRotator CamRot;
			GetActorEyesViewPoint(CamLoc, CamRot);
			
			FRotator ObjectRot = ComponentToGrab->GetComponentRotation();
			GrabbedRelativeRotation = FRotator(0.0f, ObjectRot.Yaw - CamRot.Yaw, 0.0f);
			
			// Update the replicated property so clients know an object is being held.
			GrabbedComponent = ComponentToGrab; 
		}
	}
}

void AChronoSwitchCharacter::Server_Release_Implementation()
{
	if (GrabbedComponent)
	{
		UPrimitiveComponent* GrabbedMesh = GrabbedComponent;

		// Restore collision settings.
		GrabbedMesh->IgnoreActorWhenMoving(this, false);
		
		if (AActor* OwnerActor = GrabbedMesh->GetOwner())
		{
			MoveIgnoreActorRemove(OwnerActor);
		}
		
		if (CachedOtherPlayerCharacter.IsValid())
		{
			GrabbedMesh->IgnoreActorWhenMoving(CachedOtherPlayerCharacter.Get(), false);
		}
		
		// Restore physics simulation.
		GrabbedMesh->SetSimulatePhysics(true);
		GrabbedMesh->WakeAllRigidBodies();

		// Restore original collision channel.
		GrabbedMesh->SetCollisionObjectType(GrabbedMeshOriginalCollision);
	}

	// Clear the replicated property.
	GrabbedComponent = nullptr; 
}

void AChronoSwitchCharacter::OnRep_GrabbedComponent(UPrimitiveComponent* OldComponent)
{
	// Handle Client-side physics state.
	
	// Grabbed: Disable physics to prevent fighting with server updates.
	if (GrabbedComponent)
	{
		GrabbedComponent->SetSimulatePhysics(false);
		
		GrabbedComponent->IgnoreActorWhenMoving(this, true);

		// Ignore object locally.
		if (AActor* OwnerActor = GrabbedComponent->GetOwner())
		{
			MoveIgnoreActorAdd(OwnerActor);
		}

		// Change ObjectType to PhysicsBody while holding.
		GrabbedComponent->SetCollisionObjectType(ECC_PhysicsBody);
	}

	// Released: Re-enable physics.
	if (OldComponent && IsValid(OldComponent))
	{
		OldComponent->SetSimulatePhysics(true);
		OldComponent->WakeAllRigidBodies();
		
		OldComponent->IgnoreActorWhenMoving(this, false);

		if (AActor* OwnerActor = OldComponent->GetOwner())
		{
			MoveIgnoreActorRemove(OwnerActor);
		}

		if (CachedOtherPlayerCharacter.IsValid())
		{
			OldComponent->IgnoreActorWhenMoving(CachedOtherPlayerCharacter.Get(), false);
		}

		OldComponent->SetCollisionObjectType(GrabbedMeshOriginalCollision);
	}
}

void AChronoSwitchCharacter::UpdateHeldObjectTransform(float DeltaTime)
{
	// Kinematic update for held object to ensure smoothness on Server and Client.
	if (GrabbedComponent && (HasAuthority() || IsLocallyControlled()))
	{
		FVector CameraLoc;
		FRotator CameraRot;
		GetActorEyesViewPoint(CameraLoc, CameraRot);
		
		const FVector IdealTargetLocation = CameraLoc + CameraRot.Vector() * HoldDistance;
		
		// Interpolate for smooth movement and weight.
		const FVector CurrentLoc = GrabbedComponent->GetComponentLocation();
		const FVector TargetLocation = FMath::VInterpTo(CurrentLoc, IdealTargetLocation, DeltaTime, 20.0f);
		
		// Apply Yaw offset to Camera Yaw. Keep Pitch and Roll zero to keep the object upright.
		const FRotator TargetRotation = FRotator(0.0f, CameraRot.Yaw + GrabbedRelativeRotation.Yaw, 0.0f);

		// Allow lifting the other player by ignoring collision if they are standing on the object.
		if (CachedOtherPlayerCharacter.IsValid())
		{
			bool bShouldIgnore = (CachedOtherPlayerCharacter->GetMovementBase() == GrabbedComponent);

			// Enforce timeline isolation manually since PhysicsBody collides with everything.
			if (CachedOtherPlayerState.IsValid() && CachedMyPlayerState.IsValid())
			{
				if (CachedOtherPlayerState->GetTimelineID() != CachedMyPlayerState->GetTimelineID())
				{
					bShouldIgnore = true;
				}
			}
			
			GrabbedComponent->IgnoreActorWhenMoving(CachedOtherPlayerCharacter.Get(), bShouldIgnore);
		}

		// Perform kinematic move with sweep.
		FHitResult Hit;
		GrabbedComponent->SetWorldLocationAndRotation(TargetLocation, TargetRotation, true, &Hit);

		// Handle sliding along walls/floors.
		if (Hit.bBlockingHit)
		{
			const FVector BlockedLoc = GrabbedComponent->GetComponentLocation();
			const FVector DesiredDelta = TargetLocation - BlockedLoc;

			FVector SlideDelta = FVector::VectorPlaneProject(DesiredDelta, Hit.ImpactNormal);

			// Apply friction if dragging on ground.
			if (Hit.ImpactNormal.Z > 0.7f)
			{
				const FVector TargetSlidePos = BlockedLoc + SlideDelta;
				const FVector NewPos = FMath::VInterpTo(BlockedLoc, TargetSlidePos, DeltaTime, 15.0f);
				SlideDelta = NewPos - BlockedLoc;
			}

			if (!SlideDelta.IsNearlyZero(0.1f))
			{
				GrabbedComponent->SetWorldLocationAndRotation(BlockedLoc + SlideDelta, TargetRotation, true, &Hit);
			}
		}
	}
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

#pragma endregion

#pragma region Timeline System

void AChronoSwitchCharacter::ExecuteTimeSwitchLogic()
{
	AChronoSwitchPlayerState* MyPS = GetPlayerState<AChronoSwitchPlayerState>();
	AChronoSwitchGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AChronoSwitchGameState>() : nullptr;

	if (!MyPS || !GameState)
	{
		return;
	}

	// Anti-Phasing: Prevent switch if the destination is blocked.
	if (CheckTimelineOverlap())
	{
		OnAntiPhasingTriggered();
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
			if (GrabbedComponent)
			{
				Release();
			}
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
		case ETimeSwitchMode::None:
		default:
			// In GlobalTimer mode and None, personal input does nothing.
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
				OtherChar->Release();

				// Force immediate replication to minimize desync.
				OtherPS->ForceNetUpdate();
			}
		}
	}
}

void AChronoSwitchCharacter::Client_ForcedTimelineChange_Implementation(uint8 NewTimelineID)
{
	// Force immediate state update and flush movement buffer.
	HandleTimelineUpdate(NewTimelineID);
}

bool AChronoSwitchCharacter::CheckTimelineOverlap()
{
	if (!GetCapsuleComponent()) return false;
	
	const AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>();
	if (!PS) return false;
	
	FCollisionShape CapsuleShape = GetCapsuleComponent()->GetCollisionShape();
	
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	
	// Check the OPPOSITE timeline channel.
	const ECollisionChannel ChannelToTest = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel2 : ECC_GameTraceChannel1;
	
	bool bIsBlocked = GetWorld()->OverlapBlockingTestByChannel(
		GetActorLocation(), 
		GetActorQuat(), 
		ChannelToTest, 
		CapsuleShape, 
		QueryParams);	
	
	return bIsBlocked;
	
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
	
	UpdateCollisionChannel(NewTimelineID);

	// Flush server moves to prevent rubber banding.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->FlushServerMoves();
	}

	OnTimelineChangedCosmetic(NewTimelineID);
	OnTimelineSwitched(NewTimelineID);
}

void AChronoSwitchCharacter::UpdateCollisionChannel(uint8 NewTimelineID)
{
	if (!GetCapsuleComponent()) return;
	
	ECollisionChannel NewTimelineChannel = (NewTimelineID == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;
	GetCapsuleComponent()->SetCollisionObjectType(NewTimelineChannel);

	// Simulated Proxies manage their own collision in ConfigureSimulatedProxyPhysics.
	if (GetLocalRole() == ROLE_SimulatedProxy) return;

	// Configure Authority collision responses.
	if (NewTimelineID == 0) // Past
	{
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block);  // Block Past Objects
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore); // Ignore Future Objects
	}
	else // Future
	{
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore); // Ignore Past Objects
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Block);  // Block Future Objects
	}
}

#pragma endregion

#pragma region Player Management

/** Finds the other player character in the world and caches a weak pointer to it for optimization. */
void AChronoSwitchCharacter::CacheOtherPlayerCharacter()
{
	for (TActorIterator<AChronoSwitchCharacter> It(GetWorld()); It; ++It)
	{
		AChronoSwitchCharacter* FoundChar = *It;
		if (FoundChar && FoundChar != this)
		{
			CachedOtherPlayerCharacter = FoundChar;
			
			// Ensure other player ticks after this one to reduce lag when carrying them.
			CachedOtherPlayerCharacter->AddTickPrerequisiteActor(this);
			
			return; // Found it, no need to continue looping
		}
	}
}

/** Handles symmetrical player-vs-player collision. This logic runs on all machines. */
void AChronoSwitchCharacter::UpdatePlayerCollision(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS)
{
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

	// Handle physics for the remote player (Simulated Proxy).
	if (AChronoSwitchCharacter* OtherChar = Cast<AChronoSwitchCharacter>(CachedOtherPlayerCharacter.Get()))
	{
		if (OtherChar->GetLocalRole() == ROLE_SimulatedProxy)
		{
			// Check if the proxy is standing on a physics object (e.g., a cube being dragged).
			UPrimitiveComponent* BaseComponent = OtherChar->GetMovementBase();
			
			// Consider it a physics object if it simulates physics OR if it is the object we are currently holding.
			// Held objects are kinematic (SimulatePhysics=false) but act as moving platforms.
			const bool bIsHeldObject = (BaseComponent && BaseComponent == GrabbedComponent);
			const bool bIsOnPhysicsObject = (BaseComponent && BaseComponent->IsSimulatingPhysics()) || bIsHeldObject;

			ConfigureSimulatedProxyPhysics(OtherChar, OtherPS, bIsOnPhysicsObject);
		}
	}
}

void AChronoSwitchCharacter::ConfigureSimulatedProxyPhysics(AChronoSwitchCharacter* ProxyChar, AChronoSwitchPlayerState* ProxyPS, bool bIsOnPhysicsObject)
{
	UCharacterMovementComponent* ProxyCMC = ProxyChar->GetCharacterMovement();
	UCapsuleComponent* ProxyCapsule = ProxyChar->GetCapsuleComponent();

	if (!ProxyCMC || !ProxyCapsule) return;

	// Switch between "Dragging Mode" (Physics Enabled) and "Stability Mode" (Physics Disabled)
	// to handle moving platforms and static floors.

	if (bIsOnPhysicsObject)
	{
		// --- DRAGGING MODE ---
		// The player is on a moving physics object. We enable gravity so they "stick" 
		// to the object and move with it locally via friction.
		ProxyCMC->GravityScale = 1.0f;
		
		// Restore collision with the world so they don't fall through walls while being dragged.
		ProxyCapsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		ProxyCapsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);

		// Configure collision to block ONLY objects in the Proxy's current timeline.
		const uint8 ProxyTimelineID = ProxyPS->GetTimelineID();
		if (ProxyTimelineID == 0) // Past
		{
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
			// Block Past Objects (Channel 3) so they can stand on the held cube
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Block);
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
		}
		else // Future
		{
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
			// Block Future Objects (Channel 4) so they can stand on the held cube
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
			ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Block);
		}
	}
	else
	{
		// --- STABILITY MODE (Default) ---
		// The player is on a static floor (TimelineActor) or switching timelines. 
		// We disable gravity to prevent them from falling through "Ghost" floors 
		// or jittering when the server correction fights local gravity.
		ProxyCMC->GravityScale = 0.0f;
		
		// Ignore standard world geometry to prevent jitter on static floors.
		// The proxy should rely purely on server interpolation when not being dragged.
		ProxyCapsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
		ProxyCapsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
		
		// Ignore Timeline Collision Channels (1 & 2) to prevent jitter.
		ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
		ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);

		// Block PhysicsBody so we can be pushed by held objects.
		ProxyCapsule->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		
		// Ignore Timeline Interaction/Object Channels (3 & 4) to prevent jitter on static floors.
		// NOTE: This affects the CAPSULE collision, not the Interaction Trace. Interactions will still work.
		ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel3, ECR_Ignore);
		ProxyCapsule->SetCollisionResponseToChannel(ECC_GameTraceChannel4, ECR_Ignore);
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

#pragma endregion
