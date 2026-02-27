#include "ChronoSwitch/Public/Characters/ChronoSwitchCharacter.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Blueprint/UserWidget.h"
#include "Game/ChronoSwitchGameState.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/Interactable.h"
#include "Net/UnrealNetwork.h"
#include "Gameplay/TimelineActors/TimelineBaseActor.h"
#include "Gameplay/TimelineActors/CausalActor.h"
#include "UI/InteractPromptWidget.h"

#pragma region Lifecycle

AChronoSwitchCharacter::AChronoSwitchCharacter()
{
	// Enable ticking to handle per-frame logic for player-vs-player interaction.
	PrimaryActorTick.bCanEverTick = true;
	// Update before physics to ensure passengers can react to the moving base in the same frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	
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

	// Initialize state trackers
	HeldObjectPos = FVector::ZeroVector;
	HeldObjectVelocity = FVector::ZeroVector;
	CurrentTimelineBlend = 0.0f;
	CurrentVisibilityBlend = 0.0f;
	CachedBodyMID = nullptr;
	CoyoteTimeWindow = 0.15f;
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
	
	// UI is not managed by Server
	if (IsLocallyControlled() && InteractWidgetClass)
	{
		// Create Widget from blueprint class
		InteractWidget = CreateWidget<UInteractPromptWidget>(GetWorld(), InteractWidgetClass);
		
		if (InteractWidget)
		{
			InteractWidget->AddToViewport();
			InteractWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void AChronoSwitchCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

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
		UpdatePlayerVisibility(CachedMyPlayerState.Get(), CachedOtherPlayerState.Get(), DeltaTime);
	}
	
	// Update the held object's position and rotation.
	UpdateHeldObjectTransform(DeltaTime);
	
	// Checks for interactable objects in front of the player
	OnTickSenseInteractable();
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
	
	// Reduce sensitivity when holding an object to simulate weight and prevent network desync.
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
	// Priority 1: Release if already holding an object.
	if (GrabbedComponent)
	{
		Release();
		return;
	}

	// Priority 2: Interact with world objects (Buttons, Levers).
	
	if (SensedActor)
	{
		IInteractable::Execute_Interact(SensedActor, this);
	}
	
	// Priority 3: Attempt to grab a physics object.
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
	
	// Trace against the correct Timeline channel.
	if (BoxTraceFront(HitResult, ReachDistance, EDrawDebugTrace::None))
	{
		// Validate CausalActor specific logic (e.g., prevent grabbing Future if Past is held).
		if (ACausalActor* CausalActor = Cast<ACausalActor>(HitResult.GetActor()))
		{
			if (!CausalActor->CanBeGrabbed(HitResult.GetComponent()))
			{
				return;
			}
		}

		UPrimitiveComponent* ComponentToGrab = HitResult.GetComponent();

		// Validate that the component exists and simulates physics.
		if (ComponentToGrab && ComponentToGrab->IsSimulatingPhysics())
		{
			// Prevent grabbing the object we are standing on to avoid physics loops.
			UPrimitiveComponent* CurrentBase = GetCharacterMovement() ? GetCharacterMovement()->GetMovementBase() : nullptr;
			if (CurrentBase && CurrentBase->GetOwner() == ComponentToGrab->GetOwner())
			{
				return;
			}

			// Prepare object for kinematic attachment.
			ComponentToGrab->WakeAllRigidBodies(); 
			ComponentToGrab->SetSimulatePhysics(false);

			// Ignore collision with self.
			ComponentToGrab->IgnoreActorWhenMoving(this, true);
			
			// Ensure character ignores the object to prevent flying while standing on it.
			if (AActor* OwnerActor = ComponentToGrab->GetOwner())
			{
				MoveIgnoreActorAdd(OwnerActor);
			}

			GrabbedMeshOriginalCollision = ComponentToGrab->GetCollisionObjectType();
			
			// Temporarily change ObjectType to PhysicsBody to allow interaction with Simulated Proxies.
			ComponentToGrab->SetCollisionObjectType(ECC_PhysicsBody);
			
			// Calculate relative rotation (Yaw only) to keep the object upright.
			FVector CamLoc;
			FRotator CamRot;
			GetActorEyesViewPoint(CamLoc, CamRot);
			
			FRotator ObjectRot = ComponentToGrab->GetComponentRotation();
			GrabbedRelativeRotation = FRotator(0.0f, ObjectRot.Yaw - CamRot.Yaw, 0.0f);
			
			// Update the replicated property so clients know an object is being held.
			GrabbedComponent = ComponentToGrab; 
			
			// Initialize local position tracker to prevent network jitter.
			HeldObjectPos = ComponentToGrab->GetComponentLocation();
			HeldObjectVelocity = FVector::ZeroVector;

			// Notify the actor that it has been grabbed.
			if (ATimelineBaseActor* TimelineActor = Cast<ATimelineBaseActor>(ComponentToGrab->GetOwner()))
			{
				TimelineActor->NotifyOnGrabbed(ComponentToGrab, this);
			}
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
		
		// Apply the calculated velocity to preserve momentum (prevents clipping when falling).
		GrabbedMesh->SetPhysicsLinearVelocity(HeldObjectVelocity);

		// Restore original collision channel.
		GrabbedMesh->SetCollisionObjectType(GrabbedMeshOriginalCollision);

		// Notify the actor that it has been released.
		if (ATimelineBaseActor* TimelineActor = Cast<ATimelineBaseActor>(GrabbedMesh->GetOwner()))
		{
			TimelineActor->NotifyOnReleased(GrabbedMesh, this);
		}
	}

	// Clear the replicated property.
	GrabbedComponent = nullptr; 
}

void AChronoSwitchCharacter::OnRep_GrabbedComponent(UPrimitiveComponent* OldComponent)
{
	// Grabbed: Disable physics on client to prevent fighting with server updates.
	if (GrabbedComponent)
	{
		GrabbedComponent->SetSimulatePhysics(false);
		
		GrabbedComponent->IgnoreActorWhenMoving(this, true);

		// Ignore object locally.
		if (AActor* OwnerActor = GrabbedComponent->GetOwner())
		{
			MoveIgnoreActorAdd(OwnerActor);
		}

		// Update collision type for prediction.
		GrabbedComponent->SetCollisionObjectType(ECC_PhysicsBody);
		
		// Initialize local position tracker.
		HeldObjectPos = GrabbedComponent->GetComponentLocation();
		HeldObjectVelocity = FVector::ZeroVector;

		// Notify the actor on the Client.
		if (ATimelineBaseActor* TimelineActor = Cast<ATimelineBaseActor>(GrabbedComponent->GetOwner()))
		{
			TimelineActor->NotifyOnGrabbed(GrabbedComponent, this);
		}
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

		// Notify the actor on the Client.
		if (ATimelineBaseActor* TimelineActor = Cast<ATimelineBaseActor>(OldComponent->GetOwner()))
		{
			TimelineActor->NotifyOnReleased(OldComponent, this);
		}
	}
}

void AChronoSwitchCharacter::UpdateHeldObjectTransform(float DeltaTime)
{
	// Kinematic update for held object. Runs on Simulated Proxies too for visual smoothness.
	if (GrabbedComponent)
	{
		// Capture previous position to calculate velocity.
		const FVector OldPos = HeldObjectPos;
		
		FVector CameraLoc;
		FRotator CameraRot;

		// Explicitly calculate view point for Simulated Proxies to use replicated data.
		if (IsLocallyControlled() || HasAuthority())
		{
			GetActorEyesViewPoint(CameraLoc, CameraRot);
		}
		else
		{
			CameraLoc = GetActorLocation() + FVector(0.f, 0.f, BaseEyeHeight);
			CameraRot = GetBaseAimRotation();
		}
		
		// Predict character position at end of frame to reduce visual lag (PrePhysics tick).
		CameraLoc += GetVelocity() * DeltaTime;
		
		const FVector IdealTargetLocation = CameraLoc + CameraRot.Vector() * HoldDistance;
		
		// Interpolate using local tracker to avoid fighting server replication.
		const FVector CurrentLoc = HeldObjectPos;
		const FVector TargetLocation = FMath::VInterpTo(CurrentLoc, IdealTargetLocation, DeltaTime, 20.0f);
		
		// Apply Yaw offset only to keep the object upright.
		const FRotator TargetRotation = FRotator(0.0f, CameraRot.Yaw + GrabbedRelativeRotation.Yaw, 0.0f);

		// Calculate intended movement to check for lifting.
		const FVector MoveDelta = TargetLocation - CurrentLoc;
		const bool bIsLifting = MoveDelta.Z > 0.1f;

		// Allow lifting the other player by ignoring collision if they are standing on it.
		if (CachedOtherPlayerCharacter.IsValid())
		{
			// Geometric Check: Ensure player is physically ABOVE the mesh.
			const float CharBottomZ = CachedOtherPlayerCharacter->GetActorLocation().Z - CachedOtherPlayerCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FBoxSphereBounds MeshBounds = GrabbedComponent->CalcBounds(GrabbedComponent->GetComponentTransform());
			const float MeshTopZ = MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z;
			
			const bool bIsPhysicallyAbove = CharBottomZ >= (MeshTopZ - 15.0f);
			
			// Only ignore collision if:
			// 1. Engine says they are on it (GetMovementBase)
			// 2. We are moving UP (bIsLifting)
			// 3. They are geometrically on top (bIsPhysicallyAbove)
			const bool bShouldIgnore = (CachedOtherPlayerCharacter->GetMovementBase() == GrabbedComponent) && bIsLifting && bIsPhysicallyAbove;

			GrabbedComponent->IgnoreActorWhenMoving(CachedOtherPlayerCharacter.Get(), bShouldIgnore);
		}

		// Perform kinematic move with sweep to stop at obstacles.
		FHitResult Hit;
		GrabbedComponent->SetWorldLocationAndRotation(TargetLocation, TargetRotation, true, &Hit);

		// Handle sliding along walls/floors.
		if (Hit.bBlockingHit)
		{
			const FVector BlockedLoc = Hit.Location; // Use actual hit location
			const FVector DesiredDelta = TargetLocation - BlockedLoc;

			FVector SlideDelta = FVector::VectorPlaneProject(DesiredDelta, Hit.ImpactNormal);

			// Apply friction if dragging on ground.
			if (Hit.ImpactNormal.Z > 0.7f)
			{
				// Simple friction: Scale down the movement to simulate drag.
				// Lower value = More Friction / Heavier object.
				// 0.2f feels like dragging a heavy box.
				SlideDelta *= 0.2f;
			}

			if (!SlideDelta.IsNearlyZero(0.01f))
			{
				// Nudge slightly off the surface (0.5 units) to prevent catching on floor seams/geometry edges.
				const FVector Nudge = Hit.ImpactNormal * 0.5f;
				
				GrabbedComponent->SetWorldLocationAndRotation(BlockedLoc + Nudge + SlideDelta, TargetRotation, true, &Hit);
				HeldObjectPos = GrabbedComponent->GetComponentLocation();
			}
			else
			{
				HeldObjectPos = BlockedLoc;
			}
		}
		else
		{
			HeldObjectPos = TargetLocation;
		}

		// Calculate velocity for momentum preservation on release.
		if (DeltaTime > KINDA_SMALL_NUMBER)
		{
			HeldObjectVelocity = (HeldObjectPos - OldPos) / DeltaTime;
		}
	}
}

#pragma endregion

#pragma region Interaction Sensing System

void AChronoSwitchCharacter::OnTickSenseInteractable()
{
	if (!IsLocallyControlled())
		return;
	
	AActor* NewSensedActor = nullptr;
	
	// Priority: Checks if player is holding an object
	if (GrabbedComponent)
	{
		NewSensedActor = GrabbedComponent->GetOwner();
	}
	else
	{
		FHitResult HitResult;
		if (BoxTraceFront(HitResult, ReachDistance, EDrawDebugTrace::None))
		{
			NewSensedActor = ValidateInteractable(HitResult.GetActor(), HitResult.GetComponent());
		}
	}
	
	// Checks if the SensedActor changed
	SensedActor = NewSensedActor;
	UpdateInteractWidget();
}

AActor* AChronoSwitchCharacter::ValidateInteractable(AActor* HitActor, UPrimitiveComponent* HitComponent)
{
	if (!HitActor)
		return nullptr;
	if (!HitActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		return nullptr;
	
	// Checks if actor is grabbed
	if (ACausalActor* CausalActor = Cast<ACausalActor>(HitActor))
	{
		return CausalActor->CanBeGrabbed(HitComponent) ? HitActor : nullptr;
	}
	return HitActor;
}

void AChronoSwitchCharacter::UpdateInteractWidget()
{
	if (!SensedActor)
	{
		InteractWidget->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// Only sets Visibility and Text if SensedActor is valid and has changed
	FText Text = IInteractable::Execute_GetInteractPrompt(SensedActor);
	InteractWidget->SetPromptText(Text);
	InteractWidget->SetVisibility(ESlateVisibility::Visible);
}

/** Performs a box trace from the camera to find an interactable object. */
bool AChronoSwitchCharacter::BoxTraceFront(FHitResult& OutHit, const float DrawDistance, const EDrawDebugTrace::Type Type)
{
	// Use GetActorEyesViewPoint for consistency across Client and Server.
	FVector Start;
	FRotator Rot;
	GetActorEyesViewPoint(Start, Rot);

	const FVector End = Start + Rot.Vector() * DrawDistance;
	const FVector HalfSize = FVector(2.f, 2.f, 2.f);
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	
	const AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>();
	if (!PS)
	{
		return false;
	}
	
	// Select the correct Object and Player channels based on the timeline.
	const ECollisionChannel TargetChannel = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel3 : ECC_GameTraceChannel4;
	const ECollisionChannel PlayerChannel = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;

	// Use Object Trace to detect specific timeline objects.
	// Reuse the member array to avoid heap allocation every frame.
	ReusableTraceObjectTypes.Reset(); // Resets count but keeps memory capacity.
	ReusableTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(TargetChannel));
	ReusableTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(PlayerChannel));
	ReusableTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_PhysicsBody));
	ReusableTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
	ReusableTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
	ReusableTraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
	
	return UKismetSystemLibrary::LineTraceSingleForObjects(GetWorld(), Start, End, ReusableTraceObjectTypes, false, ActorsToIgnore, Type, OutHit, true, FLinearColor::Red, FLinearColor::Green, 1.f);
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

	// Anti-Phasing: Prevent switch if destination is blocked.
	if (CheckTimelineOverlap())
	{
		OnAntiPhasingTriggered();
		return;
	}

	// Handle switch logic based on game mode.
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
			// Request switch for the other player.
			Server_RequestOtherPlayerSwitch();
			break;
		}
		case ETimeSwitchMode::GlobalTimer:
		case ETimeSwitchMode::None:
		default:
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
	// Network Correction: Always flush server moves to prevent rubber banding when the server confirms a timeline change.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->FlushServerMoves();
	}

	// Update the PlayerState immediately so that polling logic (like UpdatePlayerVisibility in Tick)
	// sees the new state this frame, instead of waiting for the OnRep packet.
	if (AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>())
	{
		// This will update TimelineID and broadcast OnTimelineIDChanged, triggering HandleTimelineUpdate.
		PS->NotifyTimelineChanged(NewTimelineID);
	}
	else
	{
		// Fallback: If no PlayerState, just update the character locally.
		HandleTimelineUpdate(NewTimelineID);
	}
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
		PS->OnVisorStateChanged.AddUObject(this, &AChronoSwitchCharacter::HandleVisorStateUpdate);
		
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
	
	// Check if we are already in the target timeline state.
	// This prevents double execution of cosmetics when both the Server RPC (Client_ForcedTimelineChange)
	// and the Replication (OnRep_TimelineID) trigger this function in the same frame/network update.
	const ECollisionChannel TargetChannel = (NewTimelineID == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;
	if (GetCapsuleComponent() && GetCapsuleComponent()->GetCollisionObjectType() == TargetChannel) return;
	
	UpdateCollisionChannel(NewTimelineID);

	// Flush server moves to prevent rubber banding.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->FlushServerMoves();
	}

	OnTimelineChangedCosmetic(NewTimelineID);
}

void AChronoSwitchCharacter::HandleVisorStateUpdate(bool bIsVisorActive)
{
	// Only trigger the cosmetic event (which drives the Global MPC) if this is the local player.
	if (IsLocallyControlled())
	{
		OnVisorStateChangedCosmetic(bIsVisorActive);
	}
}

void AChronoSwitchCharacter::UpdateCollisionChannel(uint8 NewTimelineID)
{
	if (!GetCapsuleComponent()) return;
	
	ECollisionChannel NewTimelineChannel = (NewTimelineID == 0) ? ECC_GameTraceChannel1 : ECC_GameTraceChannel2;
	GetCapsuleComponent()->SetCollisionObjectType(NewTimelineChannel);

	// Configure collision responses for everyone (Authority, Autonomous, and Simulated Proxies).
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
	// Iterate over PlayerArray to find the other player efficiently.
	if (AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			if (AChronoSwitchCharacter* FoundChar = Cast<AChronoSwitchCharacter>(PS->GetPawn()))
			{
				if (FoundChar != this)
				{
					CachedOtherPlayerCharacter = FoundChar;
					return;
				}
			}
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
		// Enable collision if in the same timeline.
		MoveIgnoreActorRemove(CachedOtherPlayerCharacter.Get());
	}
	else
	{
		// Ignore collision if in different timelines.
		MoveIgnoreActorAdd(CachedOtherPlayerCharacter.Get());
	}
}

/** Handles asymmetrical visibility of the other player. This logic only affects the local player's view. */
void AChronoSwitchCharacter::UpdatePlayerVisibility(AChronoSwitchPlayerState* MyPS, AChronoSwitchPlayerState* OtherPS, float DeltaTime)
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

		// Visible if in same timeline OR if visor is active (Ghost).
		const bool bShouldOtherBeVisibleAsGhost = !bAreInSameTimeline && bIsVisorActive;
		const bool bShouldOtherBeVisible = bAreInSameTimeline || bShouldOtherBeVisibleAsGhost;
		
		// Update Material Parameter: 1.0 if in same timeline, 0.0 if different.
		// We check slot 0 (usually the body). Ensure your material has a Scalar Parameter named "TimelineBlend".
		if (OtherPlayerMesh->GetNumMaterials() > 0)
		{
			// Cache the Dynamic Material Instance to avoid casting every frame.
			if (!CachedBodyMID)
			{
				CachedBodyMID = Cast<UMaterialInstanceDynamic>(OtherPlayerMesh->GetMaterial(0));
				if (!CachedBodyMID)
				{
					CachedBodyMID = OtherPlayerMesh->CreateAndSetMaterialInstanceDynamic(0);
				}
			}
			
			if (CachedBodyMID)
			{
				const float TargetBlend = bAreInSameTimeline ? 1.0f : 0.0f;
				// Interpolate smoothly towards the target value (Speed 4.0 gives a nice transition of ~1.0s).
				CurrentTimelineBlend = FMath::FInterpTo(CurrentTimelineBlend, TargetBlend, DeltaTime, 4.0f);
				CachedBodyMID->SetScalarParameterValue(FName("MaterialState"), CurrentTimelineBlend);

				// Update Visibility Parameter: 0.0 if Visible, 1.0 if Invisible.
				const float TargetVisibility = bShouldOtherBeVisible ? 0.0f : 1.0f;
				CurrentVisibilityBlend = FMath::FInterpTo(CurrentVisibilityBlend, TargetVisibility, DeltaTime, 4.0f);
				CachedBodyMID->SetScalarParameterValue(FName("FullVanish"), CurrentVisibilityBlend);

				// Completely hide the mesh only when fully dissolved (>= 0.99) to save rendering cost.
				// It will automatically unhide when the value drops below 0.99.
				OtherPlayerMesh->SetHiddenInGame(CurrentVisibilityBlend >= 0.99f);
			}
		}
	}
}



#pragma endregion

#pragma region Player Movement
void AChronoSwitchCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PrevCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PrevCustomMode);
	
	if (PrevMovementMode == MOVE_Walking && GetCharacterMovement()->MovementMode == MOVE_Falling)
	{
		if (GetVelocity().Z <= 0.0f)
		{
			JumpGraceTimeExpiration = GetWorld()->GetTimeSeconds() + CoyoteTimeWindow;
		}
	}
}

bool AChronoSwitchCharacter::CanJumpInternal_Implementation() const
{
	if (Super::CanJumpInternal_Implementation()) return true;
	
	return GetWorld()->GetTimeSeconds() < JumpGraceTimeExpiration;
}

void AChronoSwitchCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	
	JumpGraceTimeExpiration = 0.0f;
}

#pragma endregion
