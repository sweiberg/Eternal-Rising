﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "DirectorPawn.h"

#include "FlecsZombieBoid.h"
#include "FlowFieldMovement.h"
#include "SpawnActor.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "SurvivorPawn.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/DecalComponent.h"
#include "GameFramework/HUD.h"
#include "Engine/AssetManager.h"
#include "Engine/OverlapResult.h"


// Sets default values
ADirectorPawn::ADirectorPawn(): SelectionWidget(nullptr), DefaultMappingContext(nullptr), InputData(nullptr)
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void ADirectorPawn::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameAndUI());
		PC->bShowMouseCursor = true;
	}

	if (MarqueeWidgetClass)
	{
		SelectionWidget = CreateWidget<UMarqueeSelectionWidget>(GetWorld(), MarqueeWidgetClass);
		if (SelectionWidget)
		{
			SelectionWidget->AddToViewport();
		}
	}
    
	EnsureFlowFieldActor();
}

void ADirectorPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
	DOREPLIFETIME(ADirectorPawn, SelectedPawns);
	DOREPLIFETIME(ADirectorPawn, SelectedPawnMovements);
	DOREPLIFETIME(ADirectorPawn, FlowFieldActor);
}

// Called to bind functionality to input
void ADirectorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	// Initialize Enhanced Input
	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (InputData)
		{
			EnhancedInputComponent->BindAction(InputData->StartDragEvent, ETriggerEvent::Started, this, &ADirectorPawn::StartMarqueeSelection);
			EnhancedInputComponent->BindAction(InputData->EndDragEvent, ETriggerEvent::Completed, this, &ADirectorPawn::EndMarqueeSelection);
			EnhancedInputComponent->BindAction(InputData->LeftMouseClick, ETriggerEvent::Completed, this, &ADirectorPawn::SpawnActors);
			EnhancedInputComponent->BindAction(InputData->RightMouseClick, ETriggerEvent::Completed, this, &ADirectorPawn::MoveToLocation);
			UE_LOG(LogTemp, Log, TEXT("Input actions bound in SetupPlayerInputComponent"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("InputData or LeftMouseClick action is null in SetupPlayerInputComponent"));
		}
        
		// Add the mapping context
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 1);
				UE_LOG(LogTemp, Log, TEXT("Mapping Context added to Input Subsystem"));
			}
		}
	}
}

// Called every frame
void ADirectorPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (bIsSelecting && SelectionWidget)
	{
		FVector2D CurrentMousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetWorld());
		SelectionWidget->UpdateMarquee(InitialMousePosition, CurrentMousePosition);
	}

	GetTotalZombies();
	GetSpawnCooldownRemaining();
}

void ADirectorPawn::StartMarqueeSelection()
{
	bIsSelecting = true;

	if (SelectionWidget)
	{
		SelectionWidget->SetVisibility(ESlateVisibility::Visible); // Ensure widget is visible
	}
	
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		InitialMousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetWorld());
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeGameAndUI().SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock).SetHideCursorDuringCapture(false));
	}

	//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Drag started!"));
}

void ADirectorPawn::EndMarqueeSelection()
{
	bIsSelecting = false;

	if (SelectionWidget)
	{
		SelectionWidget->SetVisibility(ESlateVisibility::Hidden);
	}
	
	FVector2D CurrentMousePosition;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		//PC->GetMousePosition(CurrentMousePosition.X, CurrentMousePosition.Y);
		CurrentMousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetWorld());

		PC->SetInputMode(FInputModeGameAndUI());
		PC->bShowMouseCursor = true;
	}
	
	FVector2D TopLeft(
		FMath::Min(InitialMousePosition.X, CurrentMousePosition.X),
		FMath::Min(InitialMousePosition.Y, CurrentMousePosition.Y)
	);
    
	FVector2D BottomRight(
		FMath::Max(InitialMousePosition.X, CurrentMousePosition.X),
		FMath::Max(InitialMousePosition.Y, CurrentMousePosition.Y)
	);
	
	PerformSelection(TopLeft * UWidgetLayoutLibrary::GetViewportScale(GetWorld()), BottomRight * UWidgetLayoutLibrary::GetViewportScale(GetWorld()));
	//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, TEXT("Drag ended!"));
}


void ADirectorPawn::PerformSelection(const FVector2D& TopLeft, const FVector2D& BottomRight)
{
    UWorld* World = GetWorld();
    if (!World) return;

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    // Clear previous selection
    for (APawn* Pawn : SelectedPawns)
    {
    	if (IsValid(Pawn) && !Pawn->IsPendingKillPending())
    	{
    		Pawn->GetComponentByClass<UInstancedStaticMeshComponent>()->SetCustomDepthStencilValue(0);
    	}
    }
    
    SelectedPawns.Empty();
    SelectedPawnMovements.Empty();

    // Get all pawns in the world first
    TArray<AActor*> AllPawns;
    UGameplayStatics::GetAllActorsOfClass(World, AFlecsZombieBoid::StaticClass(), AllPawns);

    // Add a buffer to make selection more forgiving
    const float SelectionBuffer = 0.0f;

    // Screen bounds for our selection box with buffer
    float MinX = FMath::Min(TopLeft.X, BottomRight.X) - SelectionBuffer;
    float MaxX = FMath::Max(TopLeft.X, BottomRight.X) + SelectionBuffer;
    float MinY = FMath::Min(TopLeft.Y, BottomRight.Y) - SelectionBuffer;
    float MaxY = FMath::Max(TopLeft.Y, BottomRight.Y) + SelectionBuffer;

    // Check each pawn to see if it's in our selection box
    for (AActor* Actor : AllPawns)
    {
    	if (IsValid(Actor) && !Actor->IsPendingKillPending())
    	{
    		if (AFlecsZombieBoid* Pawn = Cast<AFlecsZombieBoid>(Actor))
    		{
    			FVector2D ScreenPos;
    			if (PC->ProjectWorldLocationToScreen(Pawn->GetActorLocation(), ScreenPos))
    			{
    				// Check if the pawn's screen position is within our marquee bounds
    				if (ScreenPos.X >= MinX && ScreenPos.X <= MaxX &&
						ScreenPos.Y >= MinY && ScreenPos.Y <= MaxY)
    				{
    					SelectedPawns.Add(Pawn);
    					Pawn->GetComponentByClass<UInstancedStaticMeshComponent>()->SetCustomDepthStencilValue(1);

    					// Directly get the FlowFieldMovement component from the Boid
    					if (UFlowFieldMovement* MovementComponent = Pawn->FlowFieldMovement)
    					{
    						SelectedPawnMovements.Add(MovementComponent);
    						//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, 
							//	FString::Printf(TEXT("Added Movement Component from Pawn: %s"), *Pawn->GetName()));
    					}
    					else
    					{
    						//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Red, 
							//	FString::Printf(TEXT("No FlowFieldMovement Component found in Pawn: %s"), *Pawn->GetName()));
    					}
    				}
    			}
    		}
    	}
    }

    // Debug visualization remains the same
    // for (APawn* SelectedPawn : SelectedPawns)
    // {
    //     DrawDebugBox(
    //         World,
    //         SelectedPawn->GetActorLocation(),
    //         FVector(50.0f),
    //         FQuat::Identity,
    //         FColor::Green,
    //         false,
    //         1.0f
    //     );
    // }
}

bool ADirectorPawn::ServerMoveToLocation_Validate(const FVector& TargetLocation, bool bIsEnemyTarget, const TArray<APawn*>& Pawns, const TArray<UFlowFieldMovement*>& PawnMovements)
{
	return true;
}

void ADirectorPawn::ServerMoveToLocation_Implementation(const FVector& TargetLocation, bool bIsEnemyTarget, const TArray<APawn*>& Pawns, const TArray<UFlowFieldMovement*>& PawnMovements)
{
	// Add debug message to verify server execution
	//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, FString::Printf(TEXT("Server: Move to location called")));
    
	MulticastPrepareMovement(TargetLocation, bIsEnemyTarget, Pawns, PawnMovements);
}

bool ADirectorPawn::EnsureFlowFieldActor()
{
	if (!FlowFieldActor)
	{
		// Try to find the FlowFieldActor in the world
		FlowFieldActor = Cast<AFlowFieldWorld>(UGameplayStatics::GetActorOfClass(GetWorld(), AFlowFieldWorld::StaticClass()));
        
		// Debug message about the attempt
		//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, 
		//	FString::Printf(TEXT("EnsureFlowFieldActor result: %s"), 
		//	FlowFieldActor ? TEXT("Found") : TEXT("Not Found")));
	}
    
	return FlowFieldActor != nullptr;
}

void ADirectorPawn::MulticastPrepareMovement_Implementation(const FVector& TargetLocation, bool bIsEnemyTarget, const TArray<APawn*>& Pawns, const TArray<UFlowFieldMovement*>& PawnMovements)
{
    // Add debug message to verify client execution
    if (!HasAuthority())
    {
        //GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Blue, 
        //    FString::Printf(TEXT("Client: Multicast received. Pawns: %d"), Pawns.Num()));
    }

	TArray<APawn*> ValidPawns;
	TArray<UFlowFieldMovement*> ValidMovements;

	// Filter out invalid pawns
	for (APawn* Pawn : Pawns)
	{
		if (IsValid(Pawn) && !Pawn->IsPendingKillPending())
		{
			ValidPawns.Add(Pawn);
		}
	}

	// Filter out invalid movement components
	for (UFlowFieldMovement* Movement : PawnMovements)
	{
		if (IsValid(Movement) && IsValid(Movement->GetOwner()) && !Movement->GetOwner()->IsPendingKillPending())
		{
			ValidMovements.Add(Movement);
		}
	}

	// Update stored pawns with only valid ones
	SelectedPawns = ValidPawns;
	StartPosition = TargetLocation;
    
	if (!ValidMovements.IsEmpty() && EnsureFlowFieldActor())
    {
        TMap<FVector2D, FVector> DirectionMap;
        FVector GoalPosition;
    	
        FlowFieldActor->GenerateFlowField(FlowFieldActor->GridCells, StartPosition, DirectionMap, GoalPosition);

        // Debug message for flow field generation
        //GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, 
        //    FString::Printf(TEXT("Generated flow field. Goal: %s"), *GoalPosition.ToString()));

        for (UFlowFieldMovement* Movement : ValidMovements)
        {
            if (Movement)
            {
                Movement->SetExternalNeighbors(SelectedPawns);
                Movement->BeginMovement(DirectionMap, GoalPosition);
                
                if (bIsEnemyTarget)
                {
                    // Find closest survivor to the target location
                    ASurvivorPawn* ClosestSurvivor = nullptr;
                    float ClosestDistance = MAX_FLT;
                    
                    TArray<AActor*> FoundSurvivors;
                    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASurvivorPawn::StaticClass(), FoundSurvivors);
                    
                    for (AActor* Actor : FoundSurvivors)
                    {
                        float Distance = FVector::Distance(Actor->GetActorLocation(), TargetLocation);
                        if (Distance < ClosestDistance)
                        {
                            ClosestDistance = Distance;
                            ClosestSurvivor = Cast<ASurvivorPawn>(Actor);
                        }
                    }
                    
                    if (ClosestSurvivor)
                    {
                        Movement->SetTargetEnemy(ClosestSurvivor);
                    }
                }
                else
                {
                    Movement->ClearTargetEnemy();
                }
            }
        }
    }
}

void ADirectorPawn::MoveToLocation()
{
    FVector WorldLocation, WorldDirection;

    if (AClientPlayerController* PC = Cast<AClientPlayerController>(GetController()))
    {
        if (PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
        {
            // Clean up existing decal if it exists
            if (ActiveDecal)
            {
                ActiveDecal->DestroyComponent();
                ActiveDecal = nullptr;
            }

            FVector Start = WorldLocation;
            FVector End = Start + (WorldDirection * 40000.0f);

            FHitResult HitResult;
            FCollisionQueryParams QueryParams;
            QueryParams.AddIgnoredActor(this);

            ASurvivorPawn* TargetSurvivor = nullptr;
            TArray<FHitResult> HitResults;
            FCollisionObjectQueryParams ObjectQueryParams;
            ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

            if (GetWorld()->LineTraceMultiByObjectType(HitResults, Start, End, ObjectQueryParams, QueryParams))
            {
                for (const FHitResult& Hit : HitResults)
                {
                    if (ASurvivorPawn* Survivor = Cast<ASurvivorPawn>(Hit.GetActor()))
                    {
                        TargetSurvivor = Survivor;
                        break;
                    }
                }
            }

            if (TargetSurvivor)
            {
                // Spawn decal at survivor location
                if (TargetDecalMaterial && !SelectedPawns.IsEmpty())
                {
                    FRotator DecalRotation = FRotator(-90.0f, 0.0f, 0.0f);
                    ActiveDecal = UGameplayStatics::SpawnDecalAtLocation(
                        GetWorld(),
                        TargetDecalMaterial,
                        DecalSize,
                        TargetSurvivor->GetActorLocation(),
                        DecalRotation,
                        DecalLifespan
                    );
                }
                ServerMoveToLocation(TargetSurvivor->GetActorLocation(), true, SelectedPawns, SelectedPawnMovements);
            }
            else if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility))
            {
                // Spawn decal at hit location
                if (TargetDecalMaterial && !SelectedPawns.IsEmpty())
                {
                    FRotator DecalRotation = FRotator(-90.0f, 0.0f, 0.0f);
                    ActiveDecal = UGameplayStatics::SpawnDecalAtLocation(
                        GetWorld(),
                        TargetDecalMaterial,
                        DecalSize,
                        HitResult.ImpactPoint,
                        DecalRotation,
                        DecalLifespan
                    );
                }
                ServerMoveToLocation(HitResult.ImpactPoint, false, SelectedPawns, SelectedPawnMovements);
            }
        }
    }
}

void ADirectorPawn::GetSpawnCooldownRemaining()
{
	// Find any spawn actor to check the timer
	ASpawnActor* FoundSpawner = Cast<ASpawnActor>(UGameplayStatics::GetActorOfClass(GetWorld(), ASpawnActor::StaticClass()));
    
	if (!ASpawnActor::bGlobalCanSpawn && FoundSpawner)
	{
		float ElapsedTime = GetWorld()->GetTimerManager().GetTimerElapsed(FoundSpawner->SpawnTimerHandle);
		float TotalSpawnTime = FoundSpawner->SpawnCooldown;
        
		// Normalize the elapsed time to be between 0 and 1
		SpawnTimeRemaining = ElapsedTime / TotalSpawnTime;
        
		// Clamp the value between 0 and 1 to handle edge cases
		SpawnTimeRemaining = FMath::Clamp(SpawnTimeRemaining, 0.0f, 1.0f);
        
		//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, 
		// FString::Printf(TEXT("Normalized Time Elapsed: %f"), SpawnTimeRemaining));
	}
}

void ADirectorPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
    
	if (ActiveDecal)
	{
		ActiveDecal->DestroyComponent();
		ActiveDecal = nullptr;
	}
}

void ADirectorPawn::GetTotalZombies()
{
	TArray<AActor*> FoundZombies;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFlecsZombieBoid::StaticClass(), FoundZombies);
    
	TotalZombies = FoundZombies.Num();
}

void ADirectorPawn::SpawnActors()
{
	// Check if total zombies is at or near the limit
	if (TotalZombies >= MaxZombies || TotalZombies > (MaxZombies - ZombieSpawnBuffer))
	{
		//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, 
		//    FString::Printf(TEXT("Cannot spawn: Too many zombies (%d/120)"), TotalZombies));
		return;
	}

	// If we're under the limit, proceed with spawning
	if (AClientPlayerController* PC = Cast<AClientPlayerController>(GetController()))
	{
		PC->SpawnActors();
	}
}