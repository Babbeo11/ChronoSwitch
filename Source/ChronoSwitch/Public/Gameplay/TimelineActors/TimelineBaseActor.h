#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/ActorComponents/TimelineObserverComponent.h"
#include "Interfaces/Interactable.h"
#include "TimelineBaseActor.generated.h"

UENUM(BlueprintType)
enum class EActorTimeline : uint8
{
	PastOnly    UMETA(DisplayName = "Only in Past"),
	FutureOnly  UMETA(DisplayName = "Only in Future"),
	Both_Static UMETA(DisplayName = "Both Timeline (Static)"),
	Both_Causal UMETA(DisplayName = "Both Timeline (Causal)"),
};

/**
 * The base class for all objects that react to timeline changes.
 * This actor manages two mesh components (Past and Future) and determines their
 * visibility and collision based on the local player's timeline state.
*/
UCLASS(PrioritizeCategories = "Timeline")
class CHRONOSWITCH_API ATimelineBaseActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	/** Sets default values for this actor's properties. */
	ATimelineBaseActor();
	
	/** Implementation of the IInteractable interface, called when the player interacts with this actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact();
	virtual void Interact_Implementation();

protected:
	// --- Components ---

	/** The mesh representing the object in the Past timeline. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* PastMesh;
	
	/** The mesh representing the object in the Future timeline. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* FutureMesh;
	
	/** Listens for changes in the local player's timeline state. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TObjectPtr<UTimelineObserverComponent> TimelineObserver;
	
	// --- Properties ---

	/** Defines how this actor behaves across timelines (e.g., exists only in the past, or in both). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline", meta = (DisplayPriority = "0"))
	EActorTimeline ActorTimeline;
	
	// --- Engine Overrides ---

	/** Called when the game starts or when spawned. */
	virtual void BeginPlay() override;
	/** Called in the editor when a property is changed. Used for visual feedback. */
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	// --- Logic ---

	/** Sets the permanent collision profiles for the meshes at the start of the game. */
	void SetupCollisionProfiles();
	/** Called by the TimelineObserver when the player's timeline or visor state changes. */
	UFUNCTION()
	void HandlePlayerTimelineUpdate(uint8 PlayerTimelineID, bool bIsVisorActive);

	/** Updates the visibility of meshes in the editor for immediate visual feedback. */
	void UpdateEditorVisuals();
};