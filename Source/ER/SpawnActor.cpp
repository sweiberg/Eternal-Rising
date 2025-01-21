#include "SpawnActor.h"
#include "ClientPlayerController.h"
#include "FlecsSubsystem.h"

// Static member initialization
bool ASpawnActor::bGlobalCanSpawn = true;
TArray<ASpawnActor*> ASpawnActor::AllSpawnActors;

ASpawnActor::ASpawnActor()
{
    PrimaryActorTick.bCanEverTick = false;

    OrbMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereMesh"));
    RootComponent = OrbMesh;
    OrbMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    OrbMesh->SetCollisionObjectType(ECC_WorldDynamic);
    OrbMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    OrbMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMeshAsset.Succeeded())
    {
        OrbMesh->SetStaticMesh(SphereMeshAsset.Object);
    }

    SpawnCooldown = 60.0f;
}

void ASpawnActor::BeginPlay()
{
    Super::BeginPlay();

    // Add this instance to the global list
    AllSpawnActors.Add(this);
}

void ASpawnActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    // Remove this instance from the global list
    AllSpawnActors.Remove(this);
}

bool ASpawnActor::ShowSpawnMenu(bool bSetToggle)
{
    if (!OrbMesh || !MatDefault || !MatSelected) return false;

    if (bSetToggle && !bMenuToggled)
    {
        if (!bGlobalCanSpawn) return false; // Check global spawn state

        OrbMesh->SetMaterial(0, MatSelected);
        bMenuToggled = true;

        SpawnPoints(800.0f, 24);

        // Start cooldown timer for all spawn actors
        StartGlobalSpawnCooldown();

        return true;
    }

    if (!bSetToggle && bMenuToggled && bGlobalCanSpawn)
    {
        OrbMesh->SetMaterial(0, MatDefault); 
        bMenuToggled = false;
        return true; 
    }

    return false;
}

void ASpawnActor::StartGlobalSpawnCooldown()
{
    bGlobalCanSpawn = false;

    // Start timer on one instance, but affect all
    GetWorld()->GetTimerManager().SetTimer(
        SpawnTimerHandle,
        this,
        &ASpawnActor::ResetGlobalSpawnCooldown,
        SpawnCooldown,
        false
    );

    // Set all spawn actors to selected material
    for (ASpawnActor* SpawnActor : AllSpawnActors)
    {
        if (IsValid(SpawnActor) && SpawnActor->OrbMesh && SpawnActor->MatSelected)
        {
            SpawnActor->OrbMesh->SetMaterial(0, SpawnActor->MatSelected);
            SpawnActor->bMenuToggled = true;
        }
    }
}

void ASpawnActor::ResetGlobalSpawnCooldown()
{
    bGlobalCanSpawn = true;

    // Reset all spawn actors
    for (ASpawnActor* SpawnActor : AllSpawnActors)
    {
        if (IsValid(SpawnActor) && SpawnActor->OrbMesh && SpawnActor->MatDefault)
        {
            SpawnActor->OrbMesh->SetMaterial(0, SpawnActor->MatDefault);
            SpawnActor->bMenuToggled = false;
        }
    }
}

void ASpawnActor::SpawnPoints(float Radius, int32 NumberOfPoints)
{
    if (NumberOfPoints <= 0 || !bGlobalCanSpawn) return;

    FVector ActorLocation = GetActorLocation();
    ActorLocation.Z = 0;

    if (AClientPlayerController* PlayerController = Cast<AClientPlayerController>(GetWorld()->GetFirstPlayerController()))
    {
        PlayerController->ServerRequestSpawnHorde(ActorLocation, Radius, NumberOfPoints);
    }
}

void ASpawnActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}