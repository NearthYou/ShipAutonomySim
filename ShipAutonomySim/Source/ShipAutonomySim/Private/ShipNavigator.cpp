#include "ShipNavigator.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "ShipMovement.h"
#include "ShipMovementSimulation.h"
#include "ShipNavigationSimulation.h"
#include "SimGameMode.h"

UShipNavigator::UShipNavigator()
{
	PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UShipNavigator::Configure(
    const TArray<FVector>& InWorldPath,
    UShipMovement* InMovement,
    AStaticMeshActor* InActualWall,
    ASimGameMode* InRunOwner)
{
    SetNavigationEnabled(false);
    bConfigured = false;
    WorldPath.Reset();
    if (InWorldPath.Num() != 3
        || InMovement == nullptr
        || !IsValid(InActualWall)
        || !IsValid(InRunOwner))
    {
        return false;
    }
    for (const FVector& Point : InWorldPath)
    {
        if (!FMath::IsFinite(Point.X)
            || !FMath::IsFinite(Point.Y)
            || !FMath::IsFinite(Point.Z))
        {
            return false;
        }
    }
    for (int32 Index = 0; Index < InWorldPath.Num() - 1; ++Index)
    {
        const double SegmentLengthSquared =
            FVector::DistSquaredXY(InWorldPath[Index], InWorldPath[Index + 1]);
        if (!FMath::IsFinite(SegmentLengthSquared)
            || SegmentLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
        {
            return false;
        }
    }

    WorldPath = InWorldPath;
    Movement = InMovement;
    ActualWall = InActualWall;
    RunOwner = InRunOwner;
    Progress = FShipPathProgress{};
    LiveTarget = WorldPath[0];
    bCoastLatched = false;
#if WITH_DEV_AUTOMATION_TESTS
    TestShipLocationOverride.Reset();
    TestForwardSpeedOverride.Reset();
    TestLastThrottleCommand = 0.0f;
    TestLastSteerCommand = 0.0f;
#endif
    Movement->AddTickPrerequisiteComponent(this);
    bConfigured = true;
    return true;
}

void UShipNavigator::SetNavigationEnabled(bool bEnabled)
{
    if (!bEnabled)
    {
        if (Movement != nullptr)
        {
            Movement->SetThrottle(0.0f);
            Movement->SetSteer(0.0f);
        }
#if WITH_DEV_AUTOMATION_TESTS
        TestLastThrottleCommand = 0.0f;
        TestLastSteerCommand = 0.0f;
#endif
        bNavigationEnabled = false;
        SetComponentTickEnabled(false);
        return;
    }

    if (!bConfigured
        || Movement == nullptr
        || !ActualWall.IsValid()
        || !RunOwner.IsValid())
    {
        return;
    }
    bNavigationEnabled = true;
    SetComponentTickEnabled(true);
}

bool UShipNavigator::IsNavigationEnabled() const
{
    return bNavigationEnabled;
}

void UShipNavigator::FailRuntimeCalculation(
    EShipRuntimeCalculationError Error)
{
    if (RunOwner.IsValid())
    {
        RunOwner->ReportRuntimeCalculationError(Error);
    }
    SetNavigationEnabled(false);
}

void UShipNavigator::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bNavigationEnabled)
    {
        return;
    }
    if (!bConfigured
        || WorldPath.Num() != 3
        || Movement == nullptr
        || !ActualWall.IsValid()
        || !RunOwner.IsValid()
        || GetOwner() == nullptr)
    {
        FailRuntimeCalculation(
            EShipRuntimeCalculationError::InvalidNavigationPath);
        return;
    }

    FVector ShipLocation = GetOwner()->GetActorLocation();
#if WITH_DEV_AUTOMATION_TESTS
    if (TestShipLocationOverride.IsSet())
    {
        ShipLocation = TestShipLocationOverride.GetValue();
    }
#endif
    FShipPathProgress NextProgress;
    if (!AdvancePathProgress(
            WorldPath,
            ShipLocation,
            Progress,
            NextProgress))
    {
        FailRuntimeCalculation(
            EShipRuntimeCalculationError::InvalidProgressProjection);
        return;
    }
    Progress = NextProgress;

    if (!FindLookaheadTarget(
            WorldPath,
            Progress,
            LookaheadDistanceCm,
            LiveTarget))
    {
        FailRuntimeCalculation(
            EShipRuntimeCalculationError::InvalidLookaheadTarget);
        return;
    }

    const FVector ShipForward = GetOwner()->GetActorForwardVector();
    if (!FMath::IsFinite(ShipForward.X)
        || !FMath::IsFinite(ShipForward.Y)
        || !FMath::IsFinite(ShipForward.Z)
        || !FMath::IsFinite(HeadingFullSteerDegrees)
        || HeadingFullSteerDegrees <= 0.0)
    {
        FailRuntimeCalculation(EShipRuntimeCalculationError::InvalidHeading);
        return;
    }
    if (!FMath::IsFinite(FullThrottleHeadingDegrees)
        || FullThrottleHeadingDegrees < 0.0
        || !FMath::IsFinite(MinimumThrottleHeadingDegrees)
        || MinimumThrottleHeadingDegrees <= FullThrottleHeadingDegrees
        || !FMath::IsFinite(MinimumThrottle)
        || MinimumThrottle < 0.0
        || MinimumThrottle > 1.0)
    {
        FailRuntimeCalculation(EShipRuntimeCalculationError::InvalidThrottle);
        return;
    }

    double HeadingErrorDegrees = 0.0;
    float Steer = 0.0f;
    float Throttle = 0.0f;
    if (!ComputeGuidanceCommands(
            ShipForward,
            ShipLocation,
            LiveTarget,
            HeadingFullSteerDegrees,
            FullThrottleHeadingDegrees,
            MinimumThrottleHeadingDegrees,
            MinimumThrottle,
            HeadingErrorDegrees,
            Steer,
            Throttle))
    {
        FailRuntimeCalculation(EShipRuntimeCalculationError::InvalidHeading);
        return;
    }
    if (!FMath::IsFinite(HeadingErrorDegrees)
        || !FMath::IsFinite(Steer)
        || Steer < -1.0f
        || Steer > 1.0f)
    {
        FailRuntimeCalculation(EShipRuntimeCalculationError::InvalidHeading);
        return;
    }
    if (!FMath::IsFinite(Throttle)
        || Throttle < 0.0f
        || Throttle > 1.0f)
    {
        FailRuntimeCalculation(EShipRuntimeCalculationError::InvalidThrottle);
        return;
    }

    if (Progress.ActiveSegmentIndex == WorldPath.Num() - 2)
    {
        const double RemainingDistanceCm =
            FVector::DistXY(ShipLocation, WorldPath.Last());
        double SignedSpeedCmPerSecond =
            Movement->GetSignedSpeedCmPerSecond();
#if WITH_DEV_AUTOMATION_TESTS
        if (TestForwardSpeedOverride.IsSet())
        {
            SignedSpeedCmPerSecond = TestForwardSpeedOverride.GetValue();
        }
#endif
        if (!FMath::IsFinite(RemainingDistanceCm)
            || !FMath::IsFinite(SignedSpeedCmPerSecond))
        {
            FailRuntimeCalculation(
                EShipRuntimeCalculationError::InvalidStoppingDistance);
            return;
        }
        const double ForwardSpeed =
            FMath::Max(0.0, SignedSpeedCmPerSecond);
        const FShipMotionParameters MotionParameters =
            FShipMotionParameters::Defaults();
        double StoppingDistanceCm = 0.0;
        if (!ComputeDynamicStoppingDistance(
                MotionParameters,
                ForwardSpeed,
                StoppingDistanceCm))
        {
            FailRuntimeCalculation(
                EShipRuntimeCalculationError::InvalidStoppingDistance);
            return;
        }
        if (bCoastLatched
            || RemainingDistanceCm <= StoppingDistanceCm + CoastMarginCm)
        {
            bCoastLatched = true;
            Throttle = 0.0f;
        }
    }

    Movement->SetSteer(Steer);
    Movement->SetThrottle(Throttle);
#if WITH_DEV_AUTOMATION_TESTS
    TestLastSteerCommand = Steer;
    TestLastThrottleCommand = Throttle;
#endif
    DrawNavigationDebug();
}

void UShipNavigator::DrawNavigationDebug() const
{
#if !UE_BUILD_SHIPPING
    UWorld* World = GetWorld();
    if (World == nullptr || WorldPath.Num() != 3)
    {
        return;
    }
    for (int32 Index = 0; Index < WorldPath.Num() - 1; ++Index)
    {
        DrawDebugLine(
            World,
            WorldPath[Index],
            WorldPath[Index + 1],
            FColor::Green,
            false,
            0.0f,
            0,
            3.0f);
    }
    DrawDebugPoint(World, WorldPath[1], 16.0f, FColor::Yellow, false, 0.0f);
    DrawDebugPoint(World, LiveTarget, 12.0f, FColor::Cyan, false, 0.0f);
    if (ActualWall.IsValid())
    {
        UStaticMeshComponent* WallMesh =
            ActualWall->GetStaticMeshComponent();
        if (WallMesh != nullptr)
        {
            DrawDebugBox(
                World,
                WallMesh->Bounds.Origin,
                WallMesh->Bounds.BoxExtent,
                WallMesh->GetComponentQuat(),
                FColor::Red,
                false,
                0.0f,
                0,
                3.0f);
        }
    }
#endif
}
