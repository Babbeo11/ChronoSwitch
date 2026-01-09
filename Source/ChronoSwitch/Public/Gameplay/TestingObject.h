// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TestingObject.generated.h"

UCLASS()
class CHRONOSWITCH_API ATestingObject : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ATestingObject();
	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(ExposeOnSpawn=true), Category = "Timeline")
	uint8 RequiredTimelineID;
	
	void RefreshLocalVisibility();
	
	UFUNCTION(BlueprintImplementableEvent, Category = "Timeline")
	void OnVisibilityChanged(bool bShouldBeVisible);

protected:
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly)
	UStaticMeshComponent* StaticMesh;
	
	
	void OnTimelineIDChanged(uint8 NewTimelineID);
	
private:
	FDelegateHandle TimelineDelegateHandle;
};
