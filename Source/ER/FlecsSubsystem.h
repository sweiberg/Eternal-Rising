﻿#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "FlecsZombieHorde.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FlecsAIController.h"
#include "SurvivorPawn.h"
#include "flecs.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "FlecsSubsystem.generated.h"

struct FlecsTransform
{
	FTransform Value;
};

struct FlecsPosition
{
	FVector Value;
};

struct Agent {
	FVector position;
	FVector velocity;
	std::vector<Agent*> neighbors;
};

struct FlecsVelocity
{
	FVector Value;
};

struct FlecsLastTraceTime {
	float LastTraceTime = 0.0f;
};

struct FlecsZombie
{
	float Health;
};

struct Horde {};

struct FlecsISMIndex
{
	int Value;
};

struct FlecsTargetLocation
{
	FVector Value;
};

struct FlecsIsmRef
{
	UInstancedStaticMeshComponent* Value;
};

struct FlecsHordeRef
{
	AFlecsZombieHorde* Value;
};

USTRUCT(BlueprintType)
struct FFlecsEntityHandle
{
	GENERATED_USTRUCT_BODY()
	FFlecsEntityHandle()  {}
	FFlecsEntityHandle(int inId)
	{
		FlecsEntityId = inId;
	}
	UPROPERTY(BlueprintReadWrite)
	int FlecsEntityId;
};

UCLASS()
class ER_API UFlecsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	flecs::world* GetEcsWorld() const;

	//UPROPERTY(EditAnywhere)
	//UInstancedStaticMeshComponent* ZombieRenderer = nullptr;

	UFUNCTION(BlueprintCallable, Category="FLECS")
	void InitFlecs(UStaticMesh* InMesh, TSubclassOf<AFlecsZombieBoid> InBoid);

	UFUNCTION(NetMulticast, Unreliable, BlueprintCallable, Category = "FLECS")
	void SpawnZombieHorde(FVector SpawnLocation, float Radius, int32 NumEntities);

	UPROPERTY(Replicated);
	TWeakObjectPtr<UStaticMesh> DefaultMesh;

	UFUNCTION(BlueprintImplementableEvent, Category = "AI")
	void OnHordeSpawned(AFlecsZombieHorde* Horde);
	
	UFUNCTION(BlueprintPure, Category = "AI")
	TArray<AFlecsZombieHorde*>& Hordes() { return AgentInstances; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY()
	AFlowFieldWorld* FlowFieldActor;
	
	void InitializeFlowFieldActor(UWorld* World);

	UFUNCTION(Server, Reliable)
	void ServerInitiateMovement(const FVector& TargetLocation, bool bIsEnemyTarget, const TArray<AFlecsZombieBoid*>& Boids);
	
	UFUNCTION(NetMulticast, Reliable)
	void MulticastExecuteMovement(const FVector& TargetLocation, bool bIsEnemyTarget, const TArray<AFlecsZombieBoid*>& Boids);

	UPROPERTY(Replicated)
	TArray<AFlecsZombieBoid*> SelectedPawns;

	UPROPERTY(EditDefaultsOnly, Category = "Spawning")
	TSubclassOf<AFlecsZombieBoid> ZombieBoidBP;

	UPROPERTY(Replicated)
	TArray<AFlecsZombieBoid*> SelectedPawnMovements;

	bool EnsureFlowFieldActor();
	
protected:
	FTickerDelegate OnTickDelegate;
	FTSTicker::FDelegateHandle OnTickHandle;
	flecs::world* ECSWorld = nullptr;
	int32 CurrentAgentIndex = -1;

	UPROPERTY(Replicated, Transient)
	TArray<AFlecsZombieHorde*> AgentInstances;

	UPROPERTY(Replicated, Category = Spawn, EditDefaultsOnly)
	TSubclassOf<AFlecsZombieHorde> HordeBP;
	
	virtual void SpawnZombieHorde_Implementation(FVector SpawnLocation, float Radius, int32 NumEntities);

private:
	bool Tick(float DeltaTime);
	
	struct FTransformUpdate
	{
		int32 Index;
		FTransform Transform;
	};
};
