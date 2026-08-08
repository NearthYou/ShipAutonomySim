#include "CourseBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "ShipNavigationSimulation.h"
#include "UObject/UObjectGlobals.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "WaterSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogCourseBuilder, Log, All);

ACourseBuilder::ACourseBuilder()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACourseBuilder::SetForcedSlideCm(double InSlideCm)
{
    checkf(
        FMath::IsFinite(InSlideCm)
            && InSlideCm >= -500.0
            && InSlideCm <= 500.0,
        TEXT("Forced Stage 4 slide must be finite and within [-500, 500]"));
    ForcedSlideCm = InSlideCm;
}

void ACourseBuilder::ClearForcedSlide()
{
    ForcedSlideCm.Reset();
}

ATargetPoint* ACourseBuilder::GetStartTarget() const
{
    return StartTarget.Get();
}

ATargetPoint* ACourseBuilder::GetEndTarget() const
{
    return EndTarget.Get();
}

AStaticMeshActor* ACourseBuilder::GetWallActor() const
{
    return WallActor.Get();
}

const TArray<FVector>& ACourseBuilder::GetWorldPath() const
{
    return WorldPath;
}

int32 ACourseBuilder::GetResolvedRandomSeed() const
{
    return ResolvedRandomSeed;
}

double ACourseBuilder::GetResolvedSlideCm() const
{
    return ResolvedSlideCm;
}

void ACourseBuilder::ClearSpawnedCourseActors()
{
    if (IsValid(StartTarget))
    {
        StartTarget->Destroy();
    }
    if (IsValid(EndTarget))
    {
        EndTarget->Destroy();
    }
    if (IsValid(WallActor))
    {
        WallActor->Destroy();
    }
    StartTarget = nullptr;
    EndTarget = nullptr;
    WallActor = nullptr;
    WorldPath.Reset();
}

bool ACourseBuilder::BuildRuntimeCourse(
    FShipCourseBuildResult& OutResult,
    EShipSetupFailure& OutFailure)
{
    OutResult = FShipCourseBuildResult{};
    OutFailure = EShipSetupFailure::None;

    if (IsValid(StartTarget) || IsValid(EndTarget) || IsValid(WallActor)
        || !WorldPath.IsEmpty())
    {
        OutFailure = EShipSetupFailure::CourseSpawnFailed;
        return false;
    }

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        OutFailure = EShipSetupFailure::CourseSpawnFailed;
        return false;
    }

    if (ForcedSlideCm.IsSet())
    {
        ResolvedRandomSeed = 0;
        ResolvedSlideCm = ForcedSlideCm.GetValue();
    }
    else
    {
        // UE 5.5.4 seeds NAME_None streams from FPlatformTime::Cycles().
        FRandomStream RandomStream(NAME_None);
        ResolvedRandomSeed = RandomStream.GetInitialSeed();
        ResolvedSlideCm = RandomStream.FRandRange(-500.0, 500.0);
        if (!bRandomCourseLogged)
        {
            UE_LOG(
                LogCourseBuilder,
                Display,
                TEXT("Stage4RandomCourse seed=%d slide=%.3f"),
                ResolvedRandomSeed,
                ResolvedSlideCm);
            bRandomCourseLogged = true;
#if WITH_DEV_AUTOMATION_TESTS
            ++TestRandomCourseLogCount;
#endif
        }
    }
    OutResult.RandomSeed = ResolvedRandomSeed;
    OutResult.SlideCm = ResolvedSlideCm;

    const FShipCourseDefinition PlanarDefinition = BuildCourseDefinition(
        GetActorTransform(),
        0.0,
        ResolvedSlideCm);
    const FVector WaterQueryLocation(
        PlanarDefinition.WallWorld.X,
        PlanarDefinition.WallWorld.Y,
        GetActorLocation().Z);
#if WITH_DEV_AUTOMATION_TESTS
    TestLastWaterQueryLocation = WaterQueryLocation;
#endif

    double WaterSurfaceZCm = 0.0;
#if WITH_DEV_AUTOMATION_TESTS
    if (TestWaterSurfaceOverrideCm.IsSet())
    {
        if (!ValidateWaterReference(
                false,
                TestWaterSurfaceOverrideCm.GetValue(),
                WaterSurfaceZCm,
                OutFailure))
        {
            return false;
        }
    }
    else
#endif
    {
        UWaterSubsystem* WaterSubsystem =
            UWaterSubsystem::GetWaterSubsystem(World);
        UWaterBodyComponent* OceanBody = WaterSubsystem != nullptr
            ? WaterSubsystem->GetOceanBodyComponent().Get()
            : nullptr;
        if (OceanBody == nullptr)
        {
            OutFailure = EShipSetupFailure::WaterSurfaceUnavailable;
            return false;
        }

        const FWaterBodyQueryResult WaterInfo =
            OceanBody->QueryWaterInfoClosestToWorldLocation(
                WaterQueryLocation,
                EWaterBodyQueryFlags::ComputeLocation);
        const bool bInExclusionVolume = WaterInfo.IsInExclusionVolume();
        const double QueriedSurfaceZCm = bInExclusionVolume
            ? 0.0
            : WaterInfo.GetWaterSurfaceLocation().Z;
        if (!ValidateWaterReference(
                bInExclusionVolume,
                QueriedSurfaceZCm,
                WaterSurfaceZCm,
                OutFailure))
        {
            return false;
        }
    }
    OutResult.WaterSurfaceZCm = WaterSurfaceZCm;

    const FShipCourseDefinition Definition = BuildCourseDefinition(
        GetActorTransform(),
        WaterSurfaceZCm,
        ResolvedSlideCm);
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMesh == nullptr)
    {
        OutFailure = EShipSetupFailure::MeshLoadFailed;
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Owner = this;
    SpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FRotator FlatRotation(0.0, GetActorRotation().Yaw, 0.0);
    const FTransform StartTransform(
        FlatRotation,
        Definition.StartWorld,
        FVector::OneVector);
    StartTarget = World->SpawnActor<ATargetPoint>(
        ATargetPoint::StaticClass(),
        StartTransform,
        SpawnParameters);
    if (!IsValid(StartTarget))
    {
        ClearSpawnedCourseActors();
        OutFailure = EShipSetupFailure::CourseSpawnFailed;
        return false;
    }

    const FTransform EndTransform(
        FlatRotation,
        Definition.EndWorld,
        FVector::OneVector);
    EndTarget = World->SpawnActor<ATargetPoint>(
        ATargetPoint::StaticClass(),
        EndTransform,
        SpawnParameters);
    if (!IsValid(EndTarget))
    {
        ClearSpawnedCourseActors();
        OutFailure = EShipSetupFailure::CourseSpawnFailed;
        return false;
    }

    const FTransform WallTransform(
        FlatRotation,
        Definition.WallWorld,
        Definition.WallScale);
    WallActor = World->SpawnActorDeferred<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(),
        WallTransform,
        this,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!IsValid(WallActor))
    {
        ClearSpawnedCourseActors();
        OutFailure = EShipSetupFailure::CourseSpawnFailed;
        return false;
    }

    UStaticMeshComponent* WallMesh = WallActor->GetStaticMeshComponent();
    if (WallMesh == nullptr || !WallMesh->SetStaticMesh(CubeMesh))
    {
        ClearSpawnedCourseActors();
        OutFailure = EShipSetupFailure::MeshLoadFailed;
        return false;
    }
    WallMesh->SetMobility(EComponentMobility::Static);
    WallMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    WallMesh->SetCollisionObjectType(ECC_WorldStatic);
    WallMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    WallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    WallActor->FinishSpawning(WallTransform);

    WorldPath = Definition.WorldPath;
    OutResult.WorldPath = WorldPath;
    OutResult.StartTarget = StartTarget.Get();
    OutResult.EndTarget = EndTarget.Get();
    OutResult.WallActor = WallActor.Get();
    OutFailure = EShipSetupFailure::None;
    return true;
}
