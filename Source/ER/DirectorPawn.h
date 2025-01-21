// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EnhancedInputSubsystems.h"
#include "InputData.h"
#include "UI/MarqueeSelectionWidget.h"
#include "FlecsZombieBoid.h"
#include "ClientPlayerController.h"
#include "FlowFieldMovement.h"
#include "FlowFieldWorld.h"
#include "DirectorPawn.generated.h"

UCLASS()
class ER_API ADirectorPawn : public APawn
{
	GENERATED_BODY()

public:
	static bool bGlobalCanSpawn;
	// Sets default values for this pawn's properties
	ADirectorPawn();

	UPROPERTY(Replicated)
	AFlowFieldWorld* FlowFieldActor;

	UPROPERTY()
	UMarqueeSelectionWidget* SelectionWidget;

	UPROPERTY(Replicated)
	TArray<APawn*> SelectedPawns;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> MarqueeWidgetClass;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerMoveToLocation(const FVector& TargetLocation, bool bIsEnemyTarget, const TArray<APawn*>& Pawns, const TArray<UFlowFieldMovement*>& PawnMovements);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPrepareMovement(const FVector& TargetLocation, bool bIsEnemyTarge, const TArray<APawn*>& Pawns, const TArray<UFlowFieldMovement*>& PawnMovements);

	UPROPERTY(EditDefaultsOnly, Category = "Visual Feedback")
	UMaterialInterface* TargetDecalMaterial;

	// Optional: Add these properties to customize the decal
	UPROPERTY(EditDefaultsOnly, Category = "Visual Feedback")
	FVector DecalSize = FVector(100.0f, 100.0f, 100.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Visual Feedback")
	float DecalLifespan = 2.0f;
	
	UFUNCTION()
	bool EnsureFlowFieldActor();

	UPROPERTY(Replicated)
	TArray<UFlowFieldMovement*> SelectedPawnMovements;

	UPROPERTY()
	UDecalComponent* ActiveDecal;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void GetTotalZombies();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UInputMappingContext* DefaultMappingContext;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	UInputData* InputData;

private:
	UPROPERTY()
	FTimerHandle SpawnCooldownTimer;
	
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void StartMarqueeSelection();
	void EndMarqueeSelection();
	void MoveToLocation();

	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void GetSpawnCooldownRemaining();

	UPROPERTY(BlueprintReadOnly, Category = "Spawn Settings")
	float SpawnTimeRemaining;

	UPROPERTY(BlueprintReadOnly, Category = "Spawn Settings")
	int32 TotalZombies;

	UPROPERTY(BlueprintReadOnly, Category = "Spawning")
	int32 MaxZombies = 120;

	UPROPERTY(EditAnywhere, Category = "Spawning")
	int32 ZombieSpawnBuffer = 24;

	bool bIsSelecting = false;
	bool FoundStartPosition = false;
	FVector2D InitialMousePosition;
	FVector StartPosition;
	
	// Spawn System Functions
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnActors();
	
	void PerformSelection(const FVector2D& TopLeft, const FVector2D& BottomRight);
};
