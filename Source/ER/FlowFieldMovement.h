// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FlowFieldWorld.h"
#include "Components/ActorComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Kismet/GameplayStatics.h"
#include "FlowFieldMovement.generated.h"

class AFlecsZombieBoid;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ER_API UFlowFieldMovement : public UActorComponent
{
	GENERATED_BODY()
	
public:
	// Sets default values for this component's properties
	UFlowFieldMovement();

	UPROPERTY()
	TObjectPtr<UFloatingPawnMovement> FloatingPawnMovement;

	UPROPERTY()
	TObjectPtr<AFlecsZombieBoid> OwnerPawn;

	UPROPERTY()
	TObjectPtr<AFlowFieldWorld> FlowFieldActor;

	UPROPERTY()
	TMap<FVector2D, FVector> DirectionMap;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category="Default")
	FVector GoalPosition;
	
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category="Default")
	float GoalAcceptanceDist = 150.0f;

	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category="Default")
	FVector MoveDirection;

	UPROPERTY(Replicated, BlueprintReadWrite, EditDefaultsOnly, Category="Default")
	bool Move;

	UPROPERTY(Replicated, BlueprintReadWrite, EditDefaultsOnly, Category="Default")
	bool bDestinationReached;
	
	UPROPERTY(EditAnywhere, Category = "Boid Behavior")
	float PerceptionRadius = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Boid Behavior")
	float AlignmentWeight = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Boid Behavior")
	float CohesionWeight = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Boid Behavior")
	float SeparationWeight = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Boid Behavior")
	float FlowFieldWeight = 1.0f;

	// New methods for enemy targeting
	UFUNCTION(BlueprintCallable)
	void SetTargetEnemy(AActor* Enemy);

	UFUNCTION(BlueprintCallable)
	void ClearTargetEnemy();
	void FindAndTargetNearbyEnemy();

	UPROPERTY()
	TArray<APawn*> ExternalNeighbors;

	UPROPERTY()
	bool bUseExternalNeighbors = false;

	UFUNCTION()
	void BeginMovement(TMap<FVector2D, FVector>& InDirectionMap, FVector& InGoalPosition);

	UFUNCTION()
	FVector2D FindCurrentCell(FVector InPawnWorldPos) const;

	UFUNCTION()
	void SetExternalNeighbors(const TArray<APawn*>& InNeighbors);

	static FVector CalculateAlignment(const TArray<APawn*>& Neighbors);
	FVector CalculateCohesion(const TArray<APawn*>& Neighbors) const;
	FVector CalculateSeparation(const TArray<APawn*>& Neighbors) const;
	TArray<APawn*> GetNeighbors();
	
	FVector WanderCenterPoint;
	float WanderRadius = 0;
	FVector WanderTarget;

	FVector LastFacingDirection;
	FVector LastWanderDirection = FVector::ZeroVector;
	
	FVector AdjustDirectionForYawOffset(const FVector& Direction);

	TOptional<FVector> PreviousWanderTarget;
	TOptional<FVector> PreviousAlignment;
	TOptional<FVector> PreviousCohesion;
	TOptional<FVector> PreviousSeparation;
	TOptional<FVector> PreviousDirection;

	// New methods
	void CheckNeighborsDestinationStatus();
	FVector GetGoalSeekingDirection();
	float GetDistanceToNearestObstacle() const;
	FVector GetWanderingDirection();
	void ApplyMovementAndRotation(FVector DesiredDirection, float DeltaTime);
	
	void PrepareForDestruction();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
protected:
	void UpdateWanderRadius();
	// Called when the game starts
	virtual void BeginPlay() override;
	
	UPROPERTY()
	AActor* CurrentTargetEnemy;

	// Method to check and perform attack
	void CheckAndPerformAttack() const;

	bool bIsBeingDestroyed;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
};
