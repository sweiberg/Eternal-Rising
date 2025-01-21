// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpawnActor.generated.h"

UCLASS()
class ASpawnActor : public AActor
{
	GENERATED_BODY()
    
public:    
	ASpawnActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

public:    
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnPoints(float Radius, int32 NumberOfPoints);

	UFUNCTION(BlueprintCallable, Category = "Spawn")
	bool ShowSpawnMenu(bool bSetToggle);

	static TArray<ASpawnActor*> AllSpawnActors;
	static bool bGlobalCanSpawn;

	FTimerHandle SpawnTimerHandle;

	UPROPERTY(EditDefaultsOnly, Category = "Spawn Settings")
	float SpawnCooldown = 90.0f;


protected:
	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent* OrbMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Materials")
	UMaterialInterface* MatDefault;

	UPROPERTY(EditDefaultsOnly, Category = "Materials")
	UMaterialInterface* MatSelected;

	bool bMenuToggled = false;
	
	void StartGlobalSpawnCooldown();
	void ResetGlobalSpawnCooldown();
};