#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/ActorComponents/TimelineObserverComponent.h" 
#include "Interfaces/TimeInterface.h"
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
 * Base actor class that synchronizes its state with a TimelineObserverComponent.
 */
UCLASS(PrioritizeCategories = "00 | Timeline")
class CHRONOSWITCH_API ATimelineBaseActor : public AActor, public ITimeInterface
{
	GENERATED_BODY()

public:
	ATimelineBaseActor();
	
	UFUNCTION(BlueprintCallable, Category = "Timeline")
	void SetActorTimeline(EActorTimeline NewTimeline);
	
	virtual uint8 GetCurrentTimelineID() override;

protected:
	
	UPROPERTY(BlueprintReadOnly, Category = "Timeline")
	USceneComponent* SceneRoot;
	
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "Timeline")
	USceneComponent* PastRoot;
	
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = "Timeline")
	USceneComponent* FutureRoot;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite,Category = "Timeline")
	UStaticMeshComponent* PastMesh;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite,Category = "Timeline")
	UStaticMeshComponent* FutureMesh;
	
	/** Component responsible for handling timeline-specific logic and collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Timeline")
	TObjectPtr<UTimelineObserverComponent> TimelineObserver;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline", Setter = SetActorTimeline, meta = (DisplayPriority = "0"))
	EActorTimeline ActorTimeline;
	
	/** Used to ensure the observer is correctly initialized with the chosen timeline in the editor. */
	virtual void OnConstruction(const FTransform& Transform) override;
};