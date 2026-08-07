#include "ChronoSwitch/Public/Characters/ChronoSwitchCharacter.h"

#include "EditorSupportDelegates.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/ChronoSwitchPlayerState.h"
#include "Blueprint/UserWidget.h"
//#include "Game/ChronoSwitchGameState.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Interfaces/Interactable.h"
#include "Net/UnrealNetwork.h"
#include "Gameplay/TimelineActors/TimelineBaseActor.h"
#include "Gameplay/TimelineActors/PhysicsTimelineActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Game/ChronoSwitchPlayerController.h"
#include "UI/PlayerVisorWidget.h"

#pragma region Lifecycle

AChronoSwitchCharacter::AChronoSwitchCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	
	// Update before physics to ensure passengers can react to the moving base in the same frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	
	// Set default speed to walking speed.
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	TargetWalkSpeed = WalkSpeed;
	
	CameraSpringArm = CreateDefaultSubobject<USpringArmComponent>("CameraSpringArm");
	CameraSpringArm->SetupAttachment(GetMesh(), FName("Eyes_Socket"));
	CameraSpringArm->TargetArmLength = 0.0f;
	CameraSpringArm->bUsePawnControlRotation = true;
	
	// Create and configure the first-person camera.
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(CameraSpringArm, USpringArmComponent::SocketName);
	FirstPersonCameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = false;
	
	// Sets default private variables
	GrabbedComponent = nullptr;
	GrabbedRelativeRotation = FRotator::ZeroRotator;

	// Initialize state trackers
	HeldObjectPos = FVector::ZeroVector;
	HeldObjectVelocity = FVector::ZeroVector;
	CurrentTimelineBlend = 0.0f;
	CurrentVisibilityBlend = 0.0f;
	CoyoteTimeWindow = 0.15f;
}

void AChronoSwitchCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Attempt to bind to this character's PlayerState to react to timeline changes.
	// This will retry if the PlayerState is not immediately available.
	BindToPlayerState();
	
	// Assign Player 1 / Player 2 on the server once the character is initialized.
	if (HasAuthority())
	{
		if (AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		{
			const int32 PlayerIndex = GS->PlayerArray.Find(GetPlayerState());
			bIsPlayer1 = (PlayerIndex == 0);
		}
		
		ForceNetUpdate();
	}

	ApplyCharacterAppearance();

	// Start a timer to find and cache the other player.
	GetWorldTimerManager().SetTimer(PlayerCachingTimer, this, &AChronoSwitchCharacter::TryCachePlayers, 0.5f, true);

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
	
	if (PostProcessBaseMaterial && FirstPersonCameraComponent)
	{
		PostProcessDynamicMaterial = UMaterialInstanceDynamic::Create(PostProcessBaseMaterial, this);
		
		if (PostProcessDynamicMaterial)
		{
			FWeightedBlendable Blendable(1.0f, PostProcessDynamicMaterial);
			FirstPersonCameraComponent->PostProcessSettings.WeightedBlendables.Array.Add(Blendable);
		}
	}
	
	// UI is not managed by Server
	if (IsLocallyControlled() && PlayerVisorWidgetClass && bShouldHaveWidget)
	{
		// Create Widget from blueprint class
		PlayerVisorWidget = CreateWidget<UPlayerVisorWidget>(GetWorld(), PlayerVisorWidgetClass);
		
		if (PlayerVisorWidget)
		{
			PlayerVisorWidget->OwningCharacter = this;
			PlayerVisorWidget->AddToViewport();
			PlayerVisorWidget->SetVisibility(ESlateVisibility::Visible);
			
			AChronoSwitchGameState* GS = GetWorld()->GetGameState<AChronoSwitchGameState>();
			AChronoSwitchPlayerState* PS = Cast<AChronoSwitchPlayerState>(CachedMyPlayerState);
			AChronoSwitchPlayerState* OtherPS = Cast<AChronoSwitchPlayerState>(CachedOtherPlayerState);

			if (GS)
			{
				GS->OnTimeSwitchModeChanged.AddDynamic(this, &AChronoSwitchCharacter::HandleModeChanged);
				HandleModeChanged(GS->CurrentTimeSwitchMode);
			}
			
		}
	}
}

void AChronoSwitchCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CachedMyPlayerState.IsValid() && CachedOtherPlayerState.IsValid())
	{
		UpdatePlayerVisibility(CachedMyPlayerState.Get(), CachedOtherPlayerState.Get(), DeltaTime);
	}
	
	// Update the held object's position and rotation.
	UpdateHeldObjectTransform(DeltaTime);
	
	// Smoothly interpolate the MaxWalkSpeed towards the TargetWalkSpeed for gradual sprint transitions.
	if (!FMath::IsNearlyEqual(GetCharacterMovement()->MaxWalkSpeed, TargetWalkSpeed, 0.5f))
	{
		GetCharacterMovement()->MaxWalkSpeed = FMath::FInterpTo(GetCharacterMovement()->MaxWalkSpeed, TargetWalkSpeed, DeltaTime, SprintAccelerationSpeed);
	}

	// Checks for interactable objects in front of the player
	OnTickSenseInteractable();
}

void AChronoSwitchCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedComponent);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedMeshOriginalCollision);
	DOREPLIFETIME(AChronoSwitchCharacter, GrabbedRelativeRotation);
	DOREPLIFETIME(AChronoSwitchCharacter, bIsPlayer1);
	DOREPLIFETIME(AChronoSwitchCharacter, Endgame);
}

void AChronoSwitchCharacter::OnRep_IsPlayer1()
{
	ApplyCharacterAppearance();
}

void AChronoSwitchCharacter::ApplyCharacterAppearance()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	if (bIsPlayer1)
	{
		if (Player1SkeletalMesh)
		{
			MeshComp->SetSkeletalMesh(Player1SkeletalMesh);
		}

		if (Player1AnimClass)
		{
			MeshComp->SetAnimInstanceClass(Player1AnimClass);
		}
	}
	else
	{
		if (Player2SkeletalMesh)
		{
			MeshComp->SetSkeletalMesh(Player2SkeletalMesh);
		}

		if (Player2AnimClass)
		{
			MeshComp->SetAnimInstanceClass(Player2AnimClass);
		}
	}

	ApplyCharacterEmissive();
}

void AChronoSwitchCharacter::ApplyCharacterEmissive()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	const int32 EmissiveSlotIndex = bIsPlayer1 ? 3 : 1;

	// Make sure the slot exists.
	if (MeshComp->GetNumMaterials() <= EmissiveSlotIndex)
	{
		return;
	}

	// We only touch the specific material slot that must stay emissive.
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MeshComp->GetMaterial(EmissiveSlotIndex));
	if (!MID)
	{
		MID = MeshComp->CreateAndSetMaterialInstanceDynamic(EmissiveSlotIndex);
	}

	if (MID)
	{
		MID->SetScalarParameterValue(FName("Emissive Intensity"), 1.0f);
	}
}

#pragma endregion

#pragma region UI

void AChronoSwitchCharacter::HandleModeChanged(ETimeSwitchMode NewMode)
{
	if (!IsLocallyControlled())
		return;
	EWidgetSwitchMode SwitchMode;
	switch (NewMode)
	{
	case ETimeSwitchMode::Personal:
		SwitchMode = EWidgetSwitchMode::Self;
		break;
	case ETimeSwitchMode::CrossPlayer:
		SwitchMode = EWidgetSwitchMode::Friend;
		break;
	default:
		SwitchMode = EWidgetSwitchMode::Disabled;
		break;
	}
	PlayerVisorWidget->UpdateMode(SwitchMode);
}

void AChronoSwitchCharacter::HandleCanSwitchTimelineChanged(bool bCanSwitchTimeline)
{
	if (!IsLocallyControlled())
		return;
	
	PlayerVisorWidget->UpdateCanSwitch(bCanSwitchTimeline);
}

void AChronoSwitchCharacter::HandleSelfTimelineChanged(uint8 NewTimeline)
{
	if (!IsLocallyControlled())
		return;
	PlayerVisorWidget->UpdatePlayerTimeline(NewTimeline);
}

void AChronoSwitchCharacter::HandleOtherTimelineChanged(uint8 NewTimeline)
{
	if (!IsLocallyControlled())
		return;
	PlayerVisorWidget->UpdateOtherPlayerTimeline(NewTimeline); 
}

#pragma endregion

#pragma region Input

void AChronoSwitchCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->ViewPitchMin = LookPitchMin;
			PC->PlayerCameraManager->ViewPitchMax = LookPitchMax;  
		}
	}

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
			EnhancedInput->BindAction(TimeSwitchAction, ETriggerEvent::Started, this, &AChronoSwitchCharacter::OnTimeSwitch);
		}

		if (SprintAction)
		{
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AChronoSwitchCharacter::StartSprinting);
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AChronoSwitchCharacter::StopSprinting);
		}
		
		if (TogglePauseAction)
		{
			EnhancedInput->BindAction(TogglePauseAction, ETriggerEvent::Started, this, &AChronoSwitchCharacter::TogglePause);
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

void AChronoSwitchCharacter::StartSprinting()
{
	TargetWalkSpeed = SprintSpeed;
	Server_SetMaxWalkSpeed(SprintSpeed);
}

void AChronoSwitchCharacter::StopSprinting()
{
	TargetWalkSpeed = WalkSpeed;
	Server_SetMaxWalkSpeed(WalkSpeed);
}

void AChronoSwitchCharacter::TogglePause()
{
	bIsPaused = !bIsPaused;
	
	AChronoSwitchPlayerController* PC = GetController<AChronoSwitchPlayerController>();
	if (PC)
	{
		PC->ChangeInputMode(bIsPaused);
	}
	PlayerVisorWidget->TogglePauseMenu(bIsPaused);
}

void AChronoSwitchCharacter::OnTimeSwitch()
{
	if (IsLocallyControlled())
	{
		Server_OnTimeSwitch();
	}
}

void AChronoSwitchCharacter::Server_OnTimeSwitch_Implementation()
{
	Multicast_OnTimeSwitch();
}

void AChronoSwitchCharacter::Multicast_OnTimeSwitch_Implementation()
{
	OnTimeSwitchPressed();
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
		OnInteracted(true);
		Server_Interact(SensedActor, this);
	}
	else
	{
		OnInteracted(false);
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

void AChronoSwitchCharacter::ForceRelease_Implementation()
{
	Release();
}

void AChronoSwitchCharacter::Server_Grab_Implementation()
{
	// Cannot grab if already holding something (check the replicated pointer).
	if (GrabbedComponent) return;
	
	FHitResult HitResult;
	
	// Trace against the correct Timeline channel.
	if (BoxTraceFront(HitResult, ReachDistance, EDrawDebugTrace::None))
	{
		APhysicsTimelineActor* PhysicsActor = Cast<APhysicsTimelineActor>(HitResult.GetActor());
		
		// Validate PhysicsTimelineActor logic (e.g., check if already held or priority logic).
		if (PhysicsActor)
		{
			if (!PhysicsActor->CanBeGrabbed(HitResult.GetComponent()))
			{
				return;
			}
		}

		UPrimitiveComponent* ComponentToGrab = HitResult.GetComponent();

		// Validate that the component exists.
		// Allow grabbing if it simulates physics OR if it is a PhysicsTimelineActor (which might be temporarily kinematic).
		if (ComponentToGrab && (ComponentToGrab->IsSimulatingPhysics() || PhysicsActor))
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
			
			if (FirstPersonCameraComponent)
			{
				CamLoc = FirstPersonCameraComponent->GetComponentLocation();
				CamRot = FirstPersonCameraComponent->GetComponentRotation();
			}
			else
			{
				GetActorEyesViewPoint(CamLoc, CamRot);
			}
			
			FRotator ObjectRot = ComponentToGrab->GetComponentRotation();
			
			float RawRelativeYaw = FRotator::NormalizeAxis(ObjectRot.Yaw - CamRot.Yaw);
			
			float SnappedRelativeYaw = FMath::RoundHalfToEven(RawRelativeYaw / 90.0f) * 90.0f;
			
			if (FMath::Abs(RawRelativeYaw - SnappedRelativeYaw) <= 30.0f)
			{
				GrabbedRelativeRotation = FRotator(0.0f, SnappedRelativeYaw, 0.0f);
			}
			else
			{
				GrabbedRelativeRotation = FRotator(0.0f, RawRelativeYaw, 0.0f);
			}
			
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

void AChronoSwitchCharacter::Server_Interact_Implementation(UObject* Object, ACharacter* Interactor)
{
	IInteractable::Execute_Interact(Object, Interactor);
}

void AChronoSwitchCharacter::Server_SetMaxWalkSpeed_Implementation(float NewSpeed)
{
	TargetWalkSpeed = NewSpeed;
}

void AChronoSwitchCharacter::OnRep_GrabbedComponent(UPrimitiveComponent* OldComponent)
{
	// Grabbed: Disable physics on client to prevent fighting with server updates.
	if (GrabbedComponent)
	{
		OnObjectGrabbed();
		
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
	if (!IsValid(GrabbedComponent) || !GrabbedComponent->IsRegistered())
	{
		return;
	}

	// Capture a stable pointer up front in case replication changes state later in the tick.
	UPrimitiveComponent* HeldComponent = GrabbedComponent;

	// Capture previous position to calculate velocity.
	const FVector OldPos = HeldObjectPos;
	
	FVector CameraLoc;
	FRotator CameraRot;

	// Explicitly calculate view point for Simulated Proxies to use replicated data.
	if (IsLocallyControlled() || HasAuthority())
	{
		if (FirstPersonCameraComponent)
		{
			CameraLoc = FirstPersonCameraComponent->GetComponentLocation();
			CameraRot = FirstPersonCameraComponent->GetComponentRotation();
		}
		else
		{
			GetActorEyesViewPoint(CameraLoc, CameraRot); 
		}
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
	if (HeldComponent && CachedOtherPlayerCharacter.IsValid())
	{
		ACharacter* OtherChar = CachedOtherPlayerCharacter.Get();
		if (IsValid(OtherChar))
		{
			if (UCapsuleComponent* OtherCapsule = OtherChar->GetCapsuleComponent())
			{
				// Geometric Check: Ensure player is physically ABOVE the mesh.
				const float CharBottomZ = OtherChar->GetActorLocation().Z - OtherCapsule->GetScaledCapsuleHalfHeight();
				const FBoxSphereBounds MeshBounds = HeldComponent->CalcBounds(HeldComponent->GetComponentTransform());
				const float MeshTopZ = MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z;
			
				const bool bIsPhysicallyAbove = CharBottomZ >= (MeshTopZ - 15.0f);
			
				// Only ignore collision if:
				// 1. Engine says they are on it (GetMovementBase)
				// 2. We are moving UP (bIsLifting)
				// 3. They are geometrically on top (bIsPhysicallyAbove)
				const bool bShouldIgnore = (OtherChar->GetMovementBase() == HeldComponent) && bIsLifting && bIsPhysicallyAbove;

				HeldComponent->IgnoreActorWhenMoving(OtherChar, bShouldIgnore);
			}
		}
	}

	// Perform kinematic move with sweep to stop at obstacles.
	FHitResult Hit;
	HeldComponent->SetWorldLocationAndRotation(TargetLocation, TargetRotation, true, &Hit);

	// Handle sliding along walls/floors.
	if (Hit.bBlockingHit)
	{
		const FVector BlockedLoc = Hit.Location;
		const FVector DesiredDelta = TargetLocation - BlockedLoc;

		FVector SlideDelta = FVector::VectorPlaneProject(DesiredDelta, Hit.ImpactNormal);

		// Apply friction if dragging on ground.
		if (Hit.ImpactNormal.Z > 0.7f)
		{
			SlideDelta *= 0.2f;
		}
	
		if (!SlideDelta.IsNearlyZero(0.01f))
		{
			const FVector Nudge = Hit.ImpactNormal * 0.5f;
			HeldComponent->SetWorldLocationAndRotation(BlockedLoc + Nudge + SlideDelta, TargetRotation, true, &Hit);
			HeldObjectPos = HeldComponent->GetComponentLocation();
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

	if (IsLocallyControlled() || HasAuthority())
	{
		if (FVector::Dist(CameraLoc, HeldObjectPos) > MaxHoldDistance)
		{
			Release();
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
	if (PlayerVisorWidget)
	{
		UpdateInteractWidget();
	}
}

AActor* AChronoSwitchCharacter::ValidateInteractable(AActor* HitActor, UPrimitiveComponent* HitComponent)
{
	if (!HitActor)
		return nullptr;
	if (!HitActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
		return nullptr;
	
	// Checks if actor is grabbed
	if (APhysicsTimelineActor* PhysicsActor = Cast<APhysicsTimelineActor>(HitActor))
	{
		return PhysicsActor->CanBeGrabbed(HitComponent) ? HitActor : nullptr;
	}
	return HitActor;
}

void AChronoSwitchCharacter::UpdateInteractWidget()
{
	if (!SensedActor && PlayerVisorWidget)
	{
		PlayerVisorWidget->SetPromptVisibility(ESlateVisibility::Hidden);
		return;
	}
	// Only sets Visibility and Text if SensedActor is valid and has changed
	FText Text = IInteractable::Execute_GetInteractPrompt(SensedActor);
	PlayerVisorWidget->SetPromptText(Text);
	PlayerVisorWidget->SetPromptVisibility(ESlateVisibility::Visible);
}

/** Performs a box trace from the camera to find an interactable object. */
bool AChronoSwitchCharacter::BoxTraceFront(FHitResult& OutHit, const float DrawDistance, const EDrawDebugTrace::Type Type)
{
	FVector Start;
	FRotator Rot;

	if (FirstPersonCameraComponent)
	{
		Start = FirstPersonCameraComponent->GetComponentLocation();
		Rot = FirstPersonCameraComponent->GetComponentRotation();
	}
	else
	{
		GetActorEyesViewPoint(Start, Rot); 
	}

	const FVector End = Start + Rot.Vector() * DrawDistance;
	const FVector HalfSize = FVector(2.f, 2.f, 2.f);
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	
	const AChronoSwitchPlayerState* PS = GetPlayerState<AChronoSwitchPlayerState>();
	if (!PS)
	{
		return false;
	}
	
	// Select the correct trace channel based on the timeline.
	const ECollisionChannel TargetChannel = (PS->GetTimelineID() == 0) ? ECC_GameTraceChannel3 : ECC_GameTraceChannel4;

	// Convert the collision channel to a trace type query for a Channel Trace
	ETraceTypeQuery TraceChannel = UEngineTypes::ConvertToTraceType(TargetChannel);
	
	return UKismetSystemLibrary::LineTraceSingle(GetWorld(), Start, End, TraceChannel, false, ActorsToIgnore, Type, OutHit, true, FLinearColor::Red, FLinearColor::Green, 1.f);
}

#pragma endregion

#pragma region Timeline System

void AChronoSwitchCharacter::ExecuteTimeSwitchLogic()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	AChronoSwitchPlayerState* MyPS = GetPlayerState<AChronoSwitchPlayerState>();
	AChronoSwitchGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AChronoSwitchGameState>() : nullptr;

	if (!MyPS || !GameState || !MyPS->CanSwitchTimeline())
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
	
	if (Endgame) return false;
	
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
	
	if (GrabbedComponent)
	{
		Release();
	}
	
	UpdateCollisionChannel(NewTimelineID);

	// Flush server moves to prevent rubber banding.
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->FlushServerMoves();
	}

	UpdatePlayerCollision();
	
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

void AChronoSwitchCharacter::TryCachePlayers()
{
	if (!CachedMyPlayerState.IsValid())
	{
		CachedMyPlayerState = GetPlayerState<AChronoSwitchPlayerState>();
	}

	if (!CachedOtherPlayerCharacter.IsValid())
	{
		CacheOtherPlayerCharacter();
	}

	if (CachedOtherPlayerCharacter.IsValid() && !CachedOtherPlayerState.IsValid())
	{
		if (AChronoSwitchCharacter* OtherChar = Cast<AChronoSwitchCharacter>(CachedOtherPlayerCharacter.Get()))
		{
			CachedOtherPlayerState = OtherChar->GetPlayerState<AChronoSwitchPlayerState>();
			if (CachedOtherPlayerState.IsValid())
			{
				// Bind to the other player's timeline changes to update collision dynamically.
				CachedOtherPlayerState->OnTimelineIDChanged.AddUObject(this, &AChronoSwitchCharacter::HandleOtherPlayerTimelineUpdate);
				UpdatePlayerCollision();
			}
		}
	}
	
	if (CachedMyPlayerState.IsValid() && CachedOtherPlayerState.IsValid())
	{
		// Adding UI Delegates
		CachedMyPlayerState->OnTimelineIDChanged.AddUObject(this, &AChronoSwitchCharacter::HandleSelfTimelineChanged);
		CachedMyPlayerState->OnCanSwitchTimelineChanged.AddUObject(this, &AChronoSwitchCharacter::HandleCanSwitchTimelineChanged);
		
		CachedOtherPlayerState->OnTimelineIDChanged.AddUObject(this, &AChronoSwitchCharacter::HandleOtherTimelineChanged);
		
		HandleSelfTimelineChanged(CachedMyPlayerState->GetTimelineID());
		HandleCanSwitchTimelineChanged(CachedMyPlayerState->CanSwitchTimeline());
		HandleOtherTimelineChanged(CachedOtherPlayerState->GetTimelineID());
		
		// Clear Timer
		GetWorldTimerManager().ClearTimer(PlayerCachingTimer);
	}
}

void AChronoSwitchCharacter::HandleOtherPlayerTimelineUpdate(uint8 NewTimelineID)
{
	UpdatePlayerCollision();
}

/** Handles symmetrical player-vs-player collision. This logic runs on all machines. */
void AChronoSwitchCharacter::UpdatePlayerCollision()
{
	if (!CachedOtherPlayerCharacter.IsValid() || !CachedMyPlayerState.IsValid() || !CachedOtherPlayerState.IsValid()) return;
	
	const bool bAreInSameTimeline = (CachedMyPlayerState->GetTimelineID() == CachedOtherPlayerState->GetTimelineID());
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
		
		// Manage Overlay Material: apply if in different timelines and not yet fully invisible.
		// The overlay is removed once CurrentVisibilityBlend reaches the 0.5 threshold.
		const bool bShouldShowGhostOverlay = !bAreInSameTimeline && CurrentVisibilityBlend < 0.5f;
		if (bShouldShowGhostOverlay)
		{
			if (OtherPlayerMesh->GetOverlayMaterial() != GhostOverlayMaterial)
				OtherPlayerMesh->SetOverlayMaterial(GhostOverlayMaterial);
		}
		else
		{
			if (OtherPlayerMesh->GetOverlayMaterial() != nullptr)
				OtherPlayerMesh->SetOverlayMaterial(nullptr);
		}

		// Update Material Parameters: 1.0 if in same timeline, 0.0 if different.
		// We update all slots on the mesh. Ensure your materials have Scalar Parameters named "MaterialState" and "FullVanish".
		const int32 NumMaterials = OtherPlayerMesh->GetNumMaterials();
		if (NumMaterials > 0)
		{
			// Cache the Dynamic Material Instances to avoid creating/casting every frame.
			if (CachedBodyMIDs.Num() != NumMaterials)
			{
				CachedBodyMIDs.Empty(NumMaterials);
				for (int32 i = 0; i < NumMaterials; ++i)
				{
					UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(OtherPlayerMesh->GetMaterial(i));
					if (!MID)
					{
						MID = OtherPlayerMesh->CreateAndSetMaterialInstanceDynamic(i);
					}
					CachedBodyMIDs.Add(MID);
				}
			}
			
			if (CachedBodyMIDs.Num() > 0)
			{
				const float TargetBlend = bAreInSameTimeline ? 1.0f : 0.0f;
				
				// Only execute the interpolation and parameter setting if we haven't reached the target yet.
				if (!FMath::IsNearlyEqual(CurrentTimelineBlend, TargetBlend, 0.001f))
				{
					CurrentTimelineBlend = FMath::FInterpTo(CurrentTimelineBlend, TargetBlend, DeltaTime, 4.0f);
					for (int32 i = 0; i < CachedBodyMIDs.Num(); ++i)
					{
						UMaterialInstanceDynamic* MID = CachedBodyMIDs[i];
						if (!MID) continue;

						MID->SetScalarParameterValue(FName("MaterialState"), CurrentTimelineBlend);

						// Base calculation: emissive intensity is the inverse of MaterialState.
						float EmissiveValue = 1.0f - CurrentTimelineBlend;

						// Keep only the correct slot emissive, based on the OTHER character identity.
						AChronoSwitchCharacter* OtherChronoChar = Cast<AChronoSwitchCharacter>(CachedOtherPlayerCharacter.Get());
						const bool bOtherIsPlayer1 = OtherChronoChar ? OtherChronoChar->bIsPlayer1 : false;
						const int32 EmissiveSlotIndex = bOtherIsPlayer1 ? 3 : 1;

						if (i == EmissiveSlotIndex)
						{
							EmissiveValue = 1.0f;
						}

						MID->SetScalarParameterValue(FName("Emissive Intensity"), EmissiveValue);
					}
				}

				// Update Visibility Parameter: 0.0 if Visible, 1.0 if Invisible.
				const float TargetVisibility = bShouldOtherBeVisible ? 0.0f : 1.0f;
				if (!FMath::IsNearlyEqual(CurrentVisibilityBlend, TargetVisibility, 0.001f))
				{
					CurrentVisibilityBlend = FMath::FInterpTo(CurrentVisibilityBlend, TargetVisibility, DeltaTime, 4.0f);
					for (UMaterialInstanceDynamic* MID : CachedBodyMIDs)
					{
						if (MID) MID->SetScalarParameterValue(FName("FullVanish"), CurrentVisibilityBlend);
					}

					// Completely hide the mesh only when fully dissolved (>= 0.99) to save rendering cost.
					const bool bIsFullyInvisible = CurrentVisibilityBlend >= 0.99f;
					OtherPlayerMesh->SetHiddenInGame(bIsFullyInvisible);

					// Remove overlay when the blend reaches the halfway point of the transition.
					if (CurrentVisibilityBlend >= 0.5f && OtherPlayerMesh->GetOverlayMaterial() != nullptr)
					{
						OtherPlayerMesh->SetOverlayMaterial(nullptr);
					}
				}
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
