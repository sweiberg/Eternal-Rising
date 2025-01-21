#include "FlowFieldMovement.h"
#include "FlecsZombieBoid.h"
#include "SurvivorPawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"

// Sets default values for this component's properties
UFlowFieldMovement::UFlowFieldMovement(): Move(false), WanderRadius(900.0f), bDestinationReached(false) , bIsBeingDestroyed(false)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	SetIsReplicated(true);
}

// Called when the game starts
void UFlowFieldMovement::BeginPlay()
{
	Super::BeginPlay();

	FlowFieldActor = Cast<AFlowFieldWorld>(UGameplayStatics::GetActorOfClass(GetWorld(), AFlowFieldWorld::StaticClass()));
	OwnerPawn = Cast<AFlecsZombieBoid>(GetOwner());
	FloatingPawnMovement = OwnerPawn ? OwnerPawn->FindComponentByClass<UFloatingPawnMovement>() : nullptr;

	// Setup initial wandering parameters
	WanderCenterPoint = FlowFieldActor ? FlowFieldActor->GetActorLocation() : FVector::ZeroVector;
	UpdateWanderRadius();
}

void UFlowFieldMovement::UpdateWanderRadius()
{
	TArray<APawn*> CurrentNeighbors = GetNeighbors();
	int32 NeighborCount = CurrentNeighbors.Num();
	
	// Base radius for 24 zombies or fewer
	if (NeighborCount <= 24)
	{
		WanderRadius = 900.0f;
	}
	// 48 zombies
	else if (NeighborCount <= 48)
	{
		WanderRadius = 1200.0f;
	}
	// 72 zombies
	else if (NeighborCount <= 72)
	{
		WanderRadius = 1500.0f;
	}
	// 96 zombies
	else if (NeighborCount <= 96)
	{
		WanderRadius = 1800.0f;
	}
	// 120 zombies or more
	else
	{
		WanderRadius = 2000.0f;
	}
}

void UFlowFieldMovement::BeginMovement(TMap<FVector2D, FVector>& InDirectionMap, FVector& InGoalPosition)
{
	ClearTargetEnemy();
	DirectionMap = InDirectionMap;
	GoalPosition = InGoalPosition;
	Move = true;
	bDestinationReached = false;
    
	if (OwnerPawn)
	{
		if (FloatingPawnMovement)
		{
			FloatingPawnMovement->SetUpdatedComponent(OwnerPawn->GetRootComponent());
			FloatingPawnMovement->Activate(true);
			OwnerPawn->SetAnimation(0);
		}
	}
}

void UFlowFieldMovement::CheckNeighborsDestinationStatus()
{
    if (!OwnerPawn)
        return;

    // First check if we're within the wander bounds
    FVector CurrentLocation = OwnerPawn->GetActorLocation();
    float MinX = GoalPosition.X - WanderRadius;
    float MaxX = GoalPosition.X + WanderRadius;
    float MinY = GoalPosition.Y - WanderRadius;
    float MaxY = GoalPosition.Y + WanderRadius;

    bool bWithinWanderBounds = 
        CurrentLocation.X >= MinX && CurrentLocation.X <= MaxX &&
        CurrentLocation.Y >= MinY && CurrentLocation.Y <= MaxY;

    // Add debug visualization of bounds check
    // DrawDebugBox(
    //     GetWorld(),
    //     FVector(GoalPosition.X, GoalPosition.Y, CurrentLocation.Z),
    //     FVector(WanderRadius, WanderRadius, 10.0f),
    //     FColor::Yellow,
    //     false,
    //     -1.0f,
    //     0,
    //     2.0f
    // );

    // If we're not within wander bounds, keep moving
    if (!bWithinWanderBounds)
    {
        return;
    }

    // Check if any neighbor has reached destination
    TArray<APawn*> Neighbors = GetNeighbors();

    for (APawn* Neighbor : Neighbors)
    {
        UFlowFieldMovement* NeighborMovement = Neighbor ? Neighbor->FindComponentByClass<UFlowFieldMovement>() : nullptr;
        if (NeighborMovement && NeighborMovement->bDestinationReached)
        {
            // If we found at least one neighbor that reached destination, enter wandering
            Move = false;
            bDestinationReached = true;
            return;
        }
    }
}

FVector UFlowFieldMovement::GetGoalSeekingDirection()
{
    if (!OwnerPawn || !FlowFieldActor)
    {
        return FVector::ZeroVector;
    }

    FVector2D CurrentCell = FindCurrentCell(OwnerPawn->GetActorLocation());
    FVector FlowFieldDirection;
    
    // Check if we're in a valid cell
    if (FVector* FoundDirection = DirectionMap.Find(CurrentCell))
    {
        FlowFieldDirection = *FoundDirection;
        
        // If we're in a blocked cell or dead end, try to find the best neighboring cell
        if (FlowFieldDirection.IsZero())
        {
            float BestCost = MAX_FLT;
            FVector BestDirection = FVector::ZeroVector;
            bool bFoundValidCell = false;
            
            // Check all neighboring cells
            for (int32 xOffset = -1; xOffset <= 1; xOffset++)
            {
                for (int32 yOffset = -1; yOffset <= 1; yOffset++)
                {
                    if (xOffset == 0 && yOffset == 0) continue;
                    
                    FVector2D NeighborCell(CurrentCell.X + xOffset, CurrentCell.Y + yOffset);
                    
                    // Skip if outside grid bounds
                    if (NeighborCell.X < 0 || NeighborCell.X >= FlowFieldActor->xAmount ||
                        NeighborCell.Y < 0 || NeighborCell.Y >= FlowFieldActor->yAmount)
                    {
                        continue;
                    }
                    
                    if (FVector* NeighborDirection = DirectionMap.Find(NeighborCell))
                    {
                        if (!NeighborDirection->IsZero())
                        {
                            // Get world position of neighbor cell center
                            FVector NeighborWorldPos = FlowFieldActor->GetActorLocation() + 
                                FVector(NeighborCell.X * FlowFieldActor->cellSize + FlowFieldActor->cellRadius,
                                       NeighborCell.Y * FlowFieldActor->cellSize + FlowFieldActor->cellRadius,
                                       OwnerPawn->GetActorLocation().Z);
                            
                            // Calculate cost based on distance to goal and current position
                            float DistanceToGoal = FVector::Distance(NeighborWorldPos, GoalPosition);
                            float DistanceFromCurrent = FVector::Distance(NeighborWorldPos, OwnerPawn->GetActorLocation());
                            float TotalCost = DistanceToGoal + DistanceFromCurrent * 0.5f; // Weight current position less
                            
                            // Update best direction if this is the best cost so far
                            if (TotalCost < BestCost)
                            {
                                BestCost = TotalCost;
                                BestDirection = *NeighborDirection;
                                bFoundValidCell = true;
                            }
                        }
                    }
                }
            }
            
            if (bFoundValidCell)
            {
                FlowFieldDirection = BestDirection;
            }
            else
            {
                // If no valid neighbors found, try to move directly towards goal
                // but with increased weight on obstacle avoidance
                FlowFieldDirection = (GoalPosition - OwnerPawn->GetActorLocation()).GetSafeNormal();
            }
        }
    }
    else
    {
        // Fallback if we're somehow outside the flow field
        FlowFieldDirection = (GoalPosition - OwnerPawn->GetActorLocation()).GetSafeNormal();
    }

    // Calculate standard boid forces
    TArray<APawn*> Neighbors = GetNeighbors();
    FVector AlignmentForce = CalculateAlignment(Neighbors);
    FVector CohesionForce = CalculateCohesion(Neighbors);
    FVector SeparationForce = CalculateSeparation(Neighbors);
    
    // Combine forces with adjusted weights
    FVector FinalDirection = (
        FlowFieldDirection * FlowFieldWeight +
        AlignmentForce * AlignmentWeight +
        CohesionForce * CohesionWeight +
        SeparationForce * SeparationWeight
    ).GetSafeNormal();

    // If we're very close to an obstacle, prioritize flow field direction even more
    float DistanceToObstacle = GetDistanceToNearestObstacle();
    if (DistanceToObstacle < 200.0f)
    {
        float ObstacleAvoidanceBlend = FMath::Clamp(1.0f - (DistanceToObstacle / 200.0f), 0.0f, 1.0f);
        FinalDirection = FMath::Lerp(FinalDirection, FlowFieldDirection, ObstacleAvoidanceBlend);
    }

    return FinalDirection;
}

float UFlowFieldMovement::GetDistanceToNearestObstacle() const
{
	if (!OwnerPawn)
		return MAX_FLT;

	FVector Start = OwnerPawn->GetActorLocation();
	Start.Z = 50.0f;
    
	const float CheckDistance = 200.0f;
	float NearestDistance = CheckDistance;

	// Check in multiple directions
	for (int32 i = 0; i < 8; ++i)
	{
		float Angle = (PI * 2.0f * i) / 8.0f;
		FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
        
		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);
        
		FCollisionObjectQueryParams ObjectQueryParams;
		ObjectQueryParams.RemoveObjectTypesToQuery(ECC_GameTraceChannel1);

		if (GetWorld()->LineTraceSingleByObjectType(
			HitResult,
			Start,
			Start + Direction * CheckDistance,
			ObjectQueryParams,
			QueryParams
		))
		{
			NearestDistance = FMath::Min(NearestDistance, HitResult.Distance);
		}
	}

	return NearestDistance;
}

FVector UFlowFieldMovement::GetWanderingDirection()
{
    if (!FlowFieldActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("FlowFieldActor is null in GetWanderingDirection"));
        return FVector::ZeroVector;
    }
    
    // Calculate grid boundaries
    float MinX = GoalPosition.X - WanderRadius;
    float MaxX = GoalPosition.X + WanderRadius;
    float MinY = GoalPosition.Y - WanderRadius;
    float MaxY = GoalPosition.Y + WanderRadius;

    FVector BoxExtent = FVector(
        FMath::Abs(MaxX - MinX) / 2.0f, 
        FMath::Abs(MaxY - MinY) / 2.0f, 
        200.0f 
    );
    
    // Get current pawn location and velocity
    FVector CurrentLocation = OwnerPawn->GetActorLocation();
    FVector CurrentVelocity = OwnerPawn->GetVelocity();
    
    // Store previous wander target if not already set
    if (!PreviousWanderTarget.IsSet())
    {
        PreviousWanderTarget = WanderTarget;
    }

    // Improved Boundary Handling with smooth transition
    bool bAtXBoundary = CurrentLocation.X <= MinX || CurrentLocation.X >= MaxX;
    bool bAtYBoundary = CurrentLocation.Y <= MinY || CurrentLocation.Y >= MaxY;

    if (bAtXBoundary || bAtYBoundary)
    {
        float RandomX, RandomY;
        float BoundaryBuffer = WanderRadius * 0.2f; // Add buffer zone for smoother transition
        
        if (CurrentLocation.X <= MinX)
        {
            RandomX = FMath::RandRange(MinX + BoundaryBuffer, MaxX);
        }
        else if (CurrentLocation.X >= MaxX)
        {
            RandomX = FMath::RandRange(MinX, MaxX - BoundaryBuffer);
        }
        else
        {
            RandomX = FMath::RandRange(MinX, MaxX);
        }

        if (CurrentLocation.Y <= MinY)
        {
            RandomY = FMath::RandRange(MinY + BoundaryBuffer, MaxY);
        }
        else if (CurrentLocation.Y >= MaxY)
        {
            RandomY = FMath::RandRange(MinY, MaxY - BoundaryBuffer);
        }
        else
        {
            RandomY = FMath::RandRange(MinY, MaxY);
        }

        // Smoothly interpolate to new wander target
        FVector NewTarget = FVector(RandomX, RandomY, GoalPosition.Z);
        WanderTarget = FMath::VInterpTo(PreviousWanderTarget.GetValue(), NewTarget, GetWorld()->GetDeltaSeconds(), 2.0f);
    }
    
    // Update wander target with momentum
    if (WanderTarget == FVector::ZeroVector || 
        FVector::Distance(CurrentLocation, WanderTarget) < GoalAcceptanceDist)
    {
        // Generate new target with consideration for current velocity
        FVector DirectionalBias = CurrentVelocity.GetSafeNormal() * WanderRadius * 0.3f;
        float RandomX = FMath::RandRange(MinX, MaxX) + DirectionalBias.X;
        float RandomY = FMath::RandRange(MinY, MaxY) + DirectionalBias.Y;
        
        // Clamp to boundaries
        RandomX = FMath::Clamp(RandomX, MinX, MaxX);
        RandomY = FMath::Clamp(RandomY, MinY, MaxY);
        
        FVector NewTarget = FVector(RandomX, RandomY, GoalPosition.Z);
        
        // Smooth transition to new target
        WanderTarget = FMath::VInterpTo(PreviousWanderTarget.GetValue(), NewTarget, GetWorld()->GetDeltaSeconds(), 2.0f);
    }

    // Store current target for next frame
    PreviousWanderTarget = WanderTarget;

    // Calculate smooth wander direction with momentum
    FVector DesiredDirection = (WanderTarget - CurrentLocation).GetSafeNormal();
    FVector SmoothedDirection = FMath::VInterpTo(
        CurrentVelocity.GetSafeNormal(),
        DesiredDirection,
        GetWorld()->GetDeltaSeconds(),
        3.0f
    );

    // Calculate boid forces with temporal smoothing
    TArray<APawn*> Neighbors = GetNeighbors();
    FVector AlignmentForce = CalculateAlignment(Neighbors);
    FVector CohesionForce = CalculateCohesion(Neighbors);
    FVector SeparationForce = CalculateSeparation(Neighbors);

    // Smooth force transitions
    if (!PreviousAlignment.IsSet()) PreviousAlignment = AlignmentForce;
    if (!PreviousCohesion.IsSet()) PreviousCohesion = CohesionForce;
    if (!PreviousSeparation.IsSet()) PreviousSeparation = SeparationForce;

    AlignmentForce = FMath::VInterpTo(PreviousAlignment.GetValue(), AlignmentForce, GetWorld()->GetDeltaSeconds(), 4.0f);
    CohesionForce = FMath::VInterpTo(PreviousCohesion.GetValue(), CohesionForce, GetWorld()->GetDeltaSeconds(), 4.0f);
    SeparationForce = FMath::VInterpTo(PreviousSeparation.GetValue(), SeparationForce, GetWorld()->GetDeltaSeconds(), 4.0f);

    // Store forces for next frame
    PreviousAlignment = AlignmentForce;
    PreviousCohesion = CohesionForce;
    PreviousSeparation = SeparationForce;

    // Combine forces with smooth transitions
    FVector CombinedDirection = (
        SmoothedDirection * FlowFieldWeight +
        AlignmentForce * AlignmentWeight +
        CohesionForce * CohesionWeight +
        SeparationForce * SeparationWeight
    ).GetSafeNormal();

    // Smooth boundary correction
    FVector BoundaryCorrection = FVector::ZeroVector;
    float BoundaryFalloff = 100.0f; // Distance over which boundary force fades in
    
    // Gradual boundary force
    if (CurrentLocation.X < MinX + BoundaryFalloff)
    {
        float Strength = 1.0f - ((CurrentLocation.X - MinX) / BoundaryFalloff);
        BoundaryCorrection.X = Strength * BoundaryFalloff;
    }
    else if (CurrentLocation.X > MaxX - BoundaryFalloff)
    {
        float Strength = 1.0f - ((MaxX - CurrentLocation.X) / BoundaryFalloff);
        BoundaryCorrection.X = -Strength * BoundaryFalloff;
    }

    if (CurrentLocation.Y < MinY + BoundaryFalloff)
    {
        float Strength = 1.0f - ((CurrentLocation.Y - MinY) / BoundaryFalloff);
        BoundaryCorrection.Y = Strength * BoundaryFalloff;
    }
    else if (CurrentLocation.Y > MaxY - BoundaryFalloff)
    {
        float Strength = 1.0f - ((MaxY - CurrentLocation.Y) / BoundaryFalloff);
        BoundaryCorrection.Y = -Strength * BoundaryFalloff;
    }

    // Smooth final direction
    FVector FinalDirection = (CombinedDirection + BoundaryCorrection.GetSafeNormal()).GetSafeNormal();
    
    if (!PreviousDirection.IsSet()) PreviousDirection = FinalDirection;
    
    FinalDirection = FMath::VInterpTo(
        PreviousDirection.GetValue(),
        FinalDirection,
        GetWorld()->GetDeltaSeconds(),
        4.0f
    );
    
    PreviousDirection = FinalDirection;

    FindAndTargetNearbyEnemy();
    
    return FinalDirection;
}

void UFlowFieldMovement::ApplyMovementAndRotation(FVector DesiredDirection, float DeltaTime)
{
    if (!OwnerPawn || !FloatingPawnMovement)
        return;

    FRotator CurrentRotation = OwnerPawn->GetActorRotation();
    FRotator TargetRotation = DesiredDirection.Rotation();
    FVector ForwardVector = TargetRotation.Vector();
    TargetRotation.Yaw -= 90.0f;
    //TargetRotation.Normalize();

    // Obstacle Detection with Multiple Directions
    FVector Start = OwnerPawn->GetActorLocation();
    Start.Z = 50.0f;
    
    float LineTraceDistance = 200.0f;
    TArray<FVector> CheckDirections = {
        ForwardVector,
        FRotationMatrix(FRotator(0, 45, 0)).TransformVector(ForwardVector),
        FRotationMatrix(FRotator(0, -45, 0)).TransformVector(ForwardVector)
    };

    FVector ObstacleAvoidanceForce = FVector::ZeroVector;
    bool bHitObstacle = false;

    for (const FVector& CheckDirection : CheckDirections)
    {
        FVector End = Start + CheckDirection * LineTraceDistance;

        FHitResult HitResult;
        FCollisionQueryParams QueryParams;
        QueryParams.AddIgnoredActor(OwnerPawn);
    	
        FCollisionObjectQueryParams ObjectQueryParams;
        ObjectQueryParams.RemoveObjectTypesToQuery(ECC_GameTraceChannel1);

        if (GetWorld()->LineTraceSingleByChannel(
            HitResult, 
            End, 
            Start, 
            ECC_Visibility, 
            QueryParams
        ))
        {
            bHitObstacle = true;

            // Determine alternative navigation direction
            FVector AvoidanceDirection = FVector::CrossProduct(HitResult.ImpactNormal, FVector::UpVector).GetSafeNormal();
            
            // Ensure avoidance is away from the obstacle
            float DotProduct = FVector::DotProduct(AvoidanceDirection, ForwardVector);
            if (DotProduct < 0)
            {
                AvoidanceDirection *= -1;
            }

            // Calculate avoidance intensity based on proximity
            float DistanceToObstacle = HitResult.Distance;
            float AvoidanceIntensity = FMath::Clamp(1.0f - (DistanceToObstacle / LineTraceDistance), 0.0f, 1.0f);
            
            // Strong avoidance force when close to obstacle
            ObstacleAvoidanceForce = AvoidanceDirection * AvoidanceIntensity * 75.0f;
            DesiredDirection = (DesiredDirection + ObstacleAvoidanceForce).GetSafeNormal();
        }
    }

    // Ground snapping
    FVector CurrentLocation = OwnerPawn->GetActorLocation();
    FVector GroundTraceStart = CurrentLocation;
    FVector GroundTraceEnd = GroundTraceStart - FVector(0, 0, 200.0f); // Adjust trace distance as needed

    FHitResult GroundHit;
    FCollisionQueryParams GroundQueryParams;
    GroundQueryParams.AddIgnoredActor(OwnerPawn);
    
    FCollisionObjectQueryParams GroundObjectQueryParams;
    GroundObjectQueryParams.RemoveObjectTypesToQuery(ECC_GameTraceChannel1);

    bool bHitGround = GetWorld()->LineTraceSingleByObjectType(
        GroundHit,
        GroundTraceStart,
        GroundTraceEnd,
        GroundObjectQueryParams,
        GroundQueryParams
    );

    // Calculate target Z position with offset
    float TargetZ = CurrentLocation.Z;
    const float GroundOffset = 1.0f; // Adjust this value to control how high above ground the pawn hovers

    if (bHitGround)
    {
        TargetZ = GroundHit.ImpactPoint.Z + GroundOffset;
        
        // Smooth Z adjustment
        float CurrentZ = CurrentLocation.Z;
        float NewZ = FMath::FInterpTo(CurrentZ, TargetZ, DeltaTime, 10.0f);
        
        // Update movement velocity with new Z position
        FVector CurrentVelocity = FloatingPawnMovement->Velocity;
        CurrentVelocity.Z = (NewZ - CurrentZ) / DeltaTime;
        FloatingPawnMovement->Velocity = CurrentVelocity;
    }

    // Calculate dynamic interpolation speed for rotation
    TargetRotation = DesiredDirection.Rotation();
    TargetRotation.Yaw -= 90.0f;
    //TargetRotation.Normalize();

    float AngleDifference = FMath::Abs(CurrentRotation.Yaw - TargetRotation.Yaw);
    float InterpSpeed = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 180.0f), FVector2D(1.0f, 2.0f), AngleDifference);

    // Smooth rotation interpolation
    FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, InterpSpeed);
    NewRotation.Pitch = CurrentRotation.Pitch;
    NewRotation.Roll = CurrentRotation.Roll;

    OwnerPawn->SetActorRotation(NewRotation);

    // Apply movement
    OwnerPawn->AddMovementInput(DesiredDirection, 1.0f, false);
}

FVector2D UFlowFieldMovement::FindCurrentCell(FVector InPawnWorldPos) const
{
	if (!FlowFieldActor)
	{
		return FVector2D::ZeroVector;
	}

	// Get relative position to flow field grid
	FVector RelativePos = InPawnWorldPos - FlowFieldActor->GetActorLocation();
	float CellSize = FlowFieldActor->cellSize;

	// Calculate grid coordinates
	int32 GridX = FMath::FloorToInt(RelativePos.X / CellSize);
	int32 GridY = FMath::FloorToInt(RelativePos.Y / CellSize);

	// Ensure we stay within grid bounds
	GridX = FMath::Clamp(GridX, 0, FlowFieldActor->xAmount - 1);
	GridY = FMath::Clamp(GridY, 0, FlowFieldActor->yAmount - 1);
	
	return FVector2D(GridX, GridY);
}

void UFlowFieldMovement::SetExternalNeighbors(const TArray<APawn*>& InNeighbors)
{
	ExternalNeighbors = InNeighbors;
	bUseExternalNeighbors = true;
}

TArray<APawn*> UFlowFieldMovement::GetNeighbors()
{
	if (bIsBeingDestroyed)
	{
		return TArray<APawn*>();
	}

	TArray<APawn*> Neighbors;
	if (!OwnerPawn)
		return Neighbors;

	if (bUseExternalNeighbors && !ExternalNeighbors.IsEmpty())
	{
		// Filter out any destroyed pawns from external neighbors
		TArray<APawn*> ValidNeighbors;
		for (APawn* Neighbor : ExternalNeighbors)
		{
			if (IsValid(Neighbor) && !Neighbor->IsPendingKillPending())
			{
				ValidNeighbors.Add(Neighbor);
			}
		}
		return ValidNeighbors;
	}
	
	// If external neighbors are set, use them
	if (bUseExternalNeighbors && !ExternalNeighbors.IsEmpty())
	{
		return ExternalNeighbors;
	}
	
	if (!OwnerPawn)
		return Neighbors;

	// Use sphere overlap method as you already have
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerPawn);

	TArray<FOverlapResult> OverlapResults;
	bool bHasOverlaps = GetWorld()->OverlapMultiByChannel(
		OverlapResults,
		OwnerPawn->GetActorLocation(),
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(PerceptionRadius),
		QueryParams
	);

	if (bHasOverlaps)
	{
		for (const FOverlapResult& Overlap : OverlapResults)
		{
			if (APawn* OtherPawn = Cast<APawn>(Overlap.GetActor()))
			{
				if (OtherPawn != OwnerPawn)
				{
					Neighbors.Add(OtherPawn);
				}
			}
		}
	}

	return Neighbors;
}

FVector UFlowFieldMovement::CalculateAlignment(const TArray<APawn*>& Neighbors)
{
	if (Neighbors.Num() == 0)
		return FVector::ZeroVector;

	FVector AverageVelocity = FVector::ZeroVector;
	for (APawn* Neighbor : Neighbors)
	{
		if (UFloatingPawnMovement* Movement = Neighbor->FindComponentByClass<UFloatingPawnMovement>())
		{
			AverageVelocity += Movement->Velocity;
		}
	}

	return (AverageVelocity / Neighbors.Num()).GetSafeNormal();
}

FVector UFlowFieldMovement::CalculateCohesion(const TArray<APawn*>& Neighbors) const
{
	if (Neighbors.Num() == 0)
		return FVector::ZeroVector;

	FVector CenterOfMass = FVector::ZeroVector;
	for (APawn* Neighbor : Neighbors)
	{
		CenterOfMass += Neighbor->GetActorLocation();
	}

	CenterOfMass /= Neighbors.Num();
	return (CenterOfMass - OwnerPawn->GetActorLocation()).GetSafeNormal();
}

FVector UFlowFieldMovement::CalculateSeparation(const TArray<APawn*>& Neighbors) const
{
    if (Neighbors.Num() == 0)
        return FVector::ZeroVector;

    FVector SeparationForce = FVector::ZeroVector;
    const float PersonalSpace = 250.0f; // Minimum desired distance between pawns
    const float CriticalDistance = 140.0f; // Distance at which separation force becomes very strong
    const float MaxSeparationForce = 3.0f; // Maximum magnitude of separation force
    
    for (APawn* Neighbor : Neighbors)
    {
        FVector ToOwner = OwnerPawn->GetActorLocation() - Neighbor->GetActorLocation();
        float Distance = ToOwner.Size();
        
        if (Distance <= PersonalSpace)
        {
            // Calculate base force that increases as distance decreases
            float NormalizedDist = FMath::Clamp(Distance / PersonalSpace, 0.0f, 1.0f);
            float ForceStrength = 1.0f - NormalizedDist;
            
            // Exponentially increase force when within critical distance
            if (Distance < CriticalDistance)
            {
                float CriticalFactor = 1.0f - (Distance / CriticalDistance);
                ForceStrength *= (1.0f + CriticalFactor * 2.0f);
            }
            
            // Add velocity-based avoidance
            if (UFloatingPawnMovement* NeighborMovement = Neighbor->FindComponentByClass<UFloatingPawnMovement>())
            {
                FVector RelativeVelocity = FloatingPawnMovement->Velocity - NeighborMovement->Velocity;
                float ClosingSpeed = FVector::DotProduct(RelativeVelocity, ToOwner.GetSafeNormal());
                
                // If pawns are moving towards each other, increase separation force
                if (ClosingSpeed < 0.0f)
                {
                    ForceStrength *= (1.0f - ClosingSpeed * 0.1f); // Adjust multiplier as needed
                }
            }
            
            // Weight force by direction of neighbor's movement
            if (UFloatingPawnMovement* NeighborMovement = Neighbor->FindComponentByClass<UFloatingPawnMovement>())
            {
                FVector NeighborDirection = NeighborMovement->Velocity.GetSafeNormal();
                float DirectionAlignment = FVector::DotProduct(NeighborDirection, ToOwner.GetSafeNormal());
                
                // Increase force if neighbor is moving towards us
                if (DirectionAlignment < 0.0f)
                {
                    ForceStrength *= (1.0f - DirectionAlignment);
                }
            }
            
            // Add weighted force
            SeparationForce += ToOwner.GetSafeNormal() * ForceStrength;
        }
    }
    
    // Clamp the maximum force and normalize
    if (!SeparationForce.IsZero())
    {
        float CurrentMagnitude = SeparationForce.Size();
        if (CurrentMagnitude > MaxSeparationForce)
        {
            SeparationForce = (SeparationForce / CurrentMagnitude) * MaxSeparationForce;
        }
    }

    return SeparationForce.GetSafeNormal();
}

void UFlowFieldMovement::CheckAndPerformAttack() const
{
    if (!CurrentTargetEnemy || !OwnerPawn)
        return;

    float DistanceToTarget = FVector::Distance(OwnerPawn->GetActorLocation(), CurrentTargetEnemy->GetActorLocation());
    
    // Attack range (adjust as needed)
    const float AttackRange = 150.0f;

    if (DistanceToTarget <= AttackRange)
    {
        FVector Start = OwnerPawn->GetActorLocation();
        Start.Z = 50.0f;
        
        float LineTraceDistance = 700.0f;
        FVector ForwardVector = (CurrentTargetEnemy->GetActorLocation() - Start).GetSafeNormal();
        
        TArray<FVector> CheckDirections = {
            ForwardVector,
            FRotationMatrix(FRotator(0, 45, 0)).TransformVector(ForwardVector),
            FRotationMatrix(FRotator(0, -45, 0)).TransformVector(ForwardVector)
        };

        for (const FVector& CheckDirection : CheckDirections)
        {
            FVector End = Start + CheckDirection * LineTraceDistance;

            FHitResult HitResult;
            FCollisionQueryParams QueryParams;
            QueryParams.AddIgnoredActor(OwnerPawn);

            if (GetWorld()->LineTraceSingleByChannel(
                HitResult, 
                Start, 
                End, 
                ECC_Visibility, 
                QueryParams
            ))
            {
                // If the hit actor is the target enemy, perform attack
                if (HitResult.GetActor() == CurrentTargetEnemy)
                {
                	OwnerPawn->PerformAttack(CurrentTargetEnemy);
                }
            }
        }
    }
}

void UFlowFieldMovement::SetTargetEnemy(AActor* Enemy)
{
    CurrentTargetEnemy = Enemy;
}

void UFlowFieldMovement::ClearTargetEnemy()
{
	CurrentTargetEnemy = nullptr;
}

void UFlowFieldMovement::FindAndTargetNearbyEnemy()
{
	FVector Start = OwnerPawn->GetActorLocation();
	Start.Z = 50.0f;
    
	float LineTraceDistance = 700.0f; // Adjust detection range as needed
	FVector ForwardVector = OwnerPawn->GetActorRotation().Vector();
    
	TArray<FVector> CheckDirections = {
		ForwardVector,
		FRotationMatrix(FRotator(0, 45, 0)).TransformVector(ForwardVector),
		FRotationMatrix(FRotator(0, -45, 0)).TransformVector(ForwardVector)
	};

	for (const FVector& CheckDirection : CheckDirections)
	{
		FVector End = Start + CheckDirection * LineTraceDistance;

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(OwnerPawn);

		if (GetWorld()->LineTraceSingleByChannel(
			HitResult, 
			Start, 
			End, 
			ECC_Pawn, 
			QueryParams
		))
		{
			// If the hit actor is a survivor, set as target
			if (ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(HitResult.GetActor()))
			{
				SetTargetEnemy(SurvivorPawn);
			}
		}
	}
}

void UFlowFieldMovement::PrepareForDestruction()
{
	bIsBeingDestroyed = true;
    
	// Remove this pawn from external neighbors of other pawns
	TArray<AActor*> AllPawns;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APawn::StaticClass(), AllPawns);
    
	for (AActor* Actor : AllPawns)
	{
		if (APawn* OtherPawn = Cast<APawn>(Actor))
		{
			if (UFlowFieldMovement* OtherMovement = OtherPawn->FindComponentByClass<UFlowFieldMovement>())
			{
				// Remove this pawn from other pawns' external neighbors
				TArray<APawn*> UpdatedNeighbors = OtherMovement->ExternalNeighbors;
				UpdatedNeighbors.Remove(Cast<APawn>(GetOwner()));
				OtherMovement->SetExternalNeighbors(UpdatedNeighbors);
			}
		}
	}
}

void UFlowFieldMovement::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFlowFieldMovement, Move);
	DOREPLIFETIME(UFlowFieldMovement, bDestinationReached);
}

void UFlowFieldMovement::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerPawn || !FloatingPawnMovement)
		return;
    
	AFlecsZombieBoid* ZombieBoid = Cast<AFlecsZombieBoid>(OwnerPawn);
	if (!ZombieBoid)
		return;

	// Update wander radius based on current neighbor count
	UpdateWanderRadius();
    
	// Rest of the existing TickComponent code remains the same...
	if (CurrentTargetEnemy)
	{
		float DistanceToEnemy = FVector::Distance(OwnerPawn->GetActorLocation(), CurrentTargetEnemy->GetActorLocation());
        
		if (DistanceToEnemy <= GoalAcceptanceDist)
		{
			CheckAndPerformAttack();
			Move = false;
			bDestinationReached = true;
			ZombieBoid->PerformAttack(CurrentTargetEnemy);
			return;
		}

		GoalPosition = CurrentTargetEnemy->GetActorLocation();
		Move = true;
		bDestinationReached = false;
        
		FVector DesiredDirection = (GoalPosition - OwnerPawn->GetActorLocation()).GetSafeNormal();
		ApplyMovementAndRotation(DesiredDirection, DeltaTime);
		OwnerPawn->SetAnimation(0);
		return;
	}

	if (Move || bDestinationReached)
	{
		ZombieBoid->SetAnimation(0);
	}

	float DistanceToGoal = FVector::Distance(OwnerPawn->GetActorLocation(), GoalPosition);
	if (DistanceToGoal <= GoalAcceptanceDist)
	{
		Move = false;
		bDestinationReached = true;
	}
	else if (Move)
	{
		CheckNeighborsDestinationStatus();
	}

	FVector DesiredDirection;
	if (Move)
	{
		DesiredDirection = GetGoalSeekingDirection();
		ApplyMovementAndRotation(DesiredDirection, DeltaTime);
	}
	else if (bDestinationReached)
	{
		DesiredDirection = GetWanderingDirection();
		ApplyMovementAndRotation(DesiredDirection, DeltaTime);
	}
}
