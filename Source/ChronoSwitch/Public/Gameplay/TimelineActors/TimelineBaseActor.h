#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/ActorComponents/TimelineObserverComponent.h"
#include "Interfaces/Interactable.h"
#include "TimelineBaseActor.generated.h"


/**
 * Defines the temporal existence of an actor.
 */
UENUM(BlueprintType)
enum class EActorTimeline : uint8
{
	PastOnly    UMETA(DisplayName = "Only in Past"),
	FutureOnly  UMETA(DisplayName = "Only in Future"),
	Both_Static UMETA(DisplayName = "Both Timelines (Static)"),
	Both_Causal UMETA(DisplayName = "Both Timelines (Causal)"),
};

/**
 * Base class for objects that exist within the dual-timeline mechanic.
 * Manages visibility and collision for Past and Future meshes based on the local player's state.
 */
UCLASS(PrioritizeCategories = "Timeline")
class CHRONOSWITCH_API ATimelineBaseActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ATimelineBaseActor();


#pragma region IInteractable Interface
	virtual void Interact_Implementation(ACharacter* Interactor) override;
	virtual FText GetInteractPrompt_Implementation() override;
#pragma endregion

#pragma region Interaction Hooks
	/** Called when the actor is grabbed by a character. */
	virtual void NotifyOnGrabbed(UPrimitiveComponent* Mesh, ACharacter* Grabber);

	/** Called when the actor is released by a character. */
	virtual void NotifyOnReleased(UPrimitiveComponent* Mesh, ACharacter* Grabber);
#pragma endregion

protected:
#pragma region Components
	/** Mesh visible in the Past timeline. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PastMesh;
	
	/** Mesh visible in the Future timeline. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FutureMesh;
	
	/** Component that listens for local player timeline changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TObjectPtr<UTimelineObserverComponent> TimelineObserver;
#pragma endregion

#pragma region Configuration
	/** Specifies which timeline(s) this actor belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline", meta = (DisplayPriority = "0"))
	EActorTimeline ActorTimeline;

	/** If true, allows seeing the mesh from the other timeline as a ghost when in Both_Static mode (requires Visor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	bool bShowStaticGhost;

	/** Time in seconds to wait before hiding the mesh of the previous timeline. Useful for dissolve effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	float TransitionDuration;
	
#pragma endregion

#pragma region Engine Overrides
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
#pragma endregion

private:
#pragma region Timeline Logic
	/** Configures collision profiles based on the actor's timeline settings. */
	void SetupCollisionProfiles();

	/** Helper to apply specific collision settings to a mesh based on its timeline ID. */
	void ConfigureMeshCollision(UStaticMeshComponent* Mesh, uint8 MeshTimelineID);
	
	/** Update ShadowCacheInvalidationBehavior based on the mesh state. */
	void UpdateShadowCache(UStaticMeshComponent* Mesh, bool bAlwaysCache);
	
	/** Handles updates from the TimelineObserver when the player switches timelines. */
	UFUNCTION()
	void HandlePlayerTimelineUpdate(uint8 PlayerTimelineID, bool bIsVisorActive);

	/** Updates mesh visibility in the editor for WYSIWYG feedback. */
	void UpdateEditorVisuals();
	
	/** Called by the timer to finalize the visibility state (hide the old mesh). */
	void FinalizeTimelineTransition(bool bShowPast, bool bShowFuture);

	/** Handle for the transition timer. */
	FTimerHandle TransitionTimerHandle;

#pragma endregion
};