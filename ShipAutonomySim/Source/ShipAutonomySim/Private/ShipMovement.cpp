#include "ShipMovement.h"
#include "ShipMovementSimulation.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Math/RotationMatrix.h"
#include "Misc/ScopeExit.h"
#include "WaterBodyComponent.h"
#include "WaterSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipMovement, Log, All);

namespace
{
const TCHAR* WaterStateName(uint8 Value)
{
    switch (static_cast<EShipWaterState>(Value))
    {
    case EShipWaterState::ValidWaves:
        return TEXT("ValidWaves");
    case EShipWaterState::ValidNoWaves:
        return TEXT("ValidNoWaves");
    case EShipWaterState::Excluded:
        return TEXT("Excluded");
    case EShipWaterState::QueryInvalid:
        return TEXT("QueryInvalid");
    case EShipWaterState::ComponentInvalid:
        return TEXT("ComponentInvalid");
    }
    return TEXT("Uninitialized");
}

const TCHAR* ParameterStateName(uint8 Value)
{
    switch (static_cast<EShipMotionParameterState>(Value))
    {
    case EShipMotionParameterState::Defaults:
        return TEXT("Defaults");
    case EShipMotionParameterState::Tuned:
        return TEXT("Tuned");
    case EShipMotionParameterState::TuningFallback:
        return TEXT("TuningFallback");
    }
    return TEXT("Uninitialized");
}
}

UShipMovement::UShipMovement()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UShipMovement::SetThrottle(float Value)
{
    if (!FMath::IsFinite(Value))
    {
        ThrottleInput = 0.0;
        if (!bLoggedBadThrottle)
        {
            UE_LOG(
                LogShipMovement,
                Warning,
                TEXT("Non-finite throttle replaced with zero"));
            bLoggedBadThrottle = true;
        }
        return;
    }

    ThrottleInput = FMath::Clamp(static_cast<double>(Value), -1.0, 1.0);
}

void UShipMovement::SetSteer(float Value)
{
    if (!FMath::IsFinite(Value))
    {
        SteerInput = 0.0;
        if (!bLoggedBadSteer)
        {
            UE_LOG(
                LogShipMovement,
                Warning,
                TEXT("Non-finite steer replaced with zero"));
            bLoggedBadSteer = true;
        }
        return;
    }

    SteerInput = FMath::Clamp(static_cast<double>(Value), -1.0, 1.0);
}

void UShipMovement::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!IsValid(Owner) || !IsValid(Owner->GetRootComponent()))
    {
        UE_LOG(
            LogShipMovement,
            Error,
            TEXT("ShipMovement requires an owner with a root component"));
        SetComponentTickEnabled(false);
        return;
    }

    HorizontalYawDegrees = Owner->GetActorRotation().Yaw;
    AppliedUpVector = Owner->GetActorUpVector();
    if (!AppliedUpVector.Normalize())
    {
        AppliedUpVector = FVector::UpVector;
    }
}

FShipMotionParameters UShipMovement::BuildCandidateParameters() const
{
    FShipMotionParameters Parameters;
    Parameters.LinearDragCoeff = LinearDragCoeff;
    Parameters.QuadraticDragCoeff = QuadraticDragCoeff;
    Parameters.MaxThrustAccel = MaxThrustAccel;
    Parameters.StopSpeedThreshold = StopSpeedThreshold;
    Parameters.MaxYawRate = MaxYawRate;
    Parameters.TurnRefSpeed = TurnRefSpeed;
    Parameters.MaxSimulationStepSeconds = MaxSimulationStepSeconds;
    Parameters.MaxSubstepsPerTick = MaxSubstepsPerTick;
    return Parameters;
}

void UShipMovement::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ON_SCOPE_EXIT
    {
        DrawMovementDebug();
    };

    if (!IsValid(GetOwner()) || !IsValid(GetOwner()->GetRootComponent()) ||
        !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
    {
        SignedSpeedCmPerSecond = 0.0;
        LastSubsteps = 0;
        LastSimulatedDeltaTime = 0.0;
        LastDroppedDeltaTime = 0.0;
        return;
    }

    const FShipValidatedMotionParameters Validated =
        ValidateShipMotionParameters(BuildCandidateParameters());
    const FShipSubstepSchedule Schedule = BuildShipSubstepSchedule(
        DeltaTime,
        Validated.Parameters.MaxSimulationStepSeconds,
        Validated.Parameters.MaxSubstepsPerTick);
    if (!Schedule.bValid)
    {
        SignedSpeedCmPerSecond = 0.0;
        return;
    }

    const uint8 ParameterState = static_cast<uint8>(Validated.State);
    if (LastParameterState != ParameterState)
    {
        UE_LOG(
            LogShipMovement,
            Display,
            TEXT("Motion parameter state changed to %s"),
            ParameterStateName(ParameterState));
        LastParameterState = ParameterState;
    }

    LastSubsteps = Schedule.NumSteps;
    LastStepSeconds = Schedule.StepSeconds;
    LastSimulatedDeltaTime = Schedule.SimulatedDeltaTimeSeconds;
    LastDroppedDeltaTime = Schedule.DroppedDeltaTimeSeconds;
    const bool bDroppingTime = Schedule.DroppedDeltaTimeSeconds > 0.0;
    if (bDroppingTime && !bWasDroppingTime)
    {
        UE_LOG(
            LogShipMovement,
            Warning,
            TEXT("Dropping %.6f seconds beyond the bounded substep budget"),
            Schedule.DroppedDeltaTimeSeconds);
    }
    bWasDroppingTime = bDroppingTime;
    bLastBlockingHit = false;
    LastImpactNormal = FVector::ZeroVector;
    LastHitName.Reset();

    const EWaterBodyQueryFlags QueryFlags =
        EWaterBodyQueryFlags::ComputeLocation |
        EWaterBodyQueryFlags::ComputeNormal |
        EWaterBodyQueryFlags::IncludeWaves;

    for (int32 StepIndex = 0; StepIndex < Schedule.NumSteps; ++StepIndex)
    {
        const FShipMotionState Before{
            SignedSpeedCmPerSecond,
            HorizontalYawDegrees};
        const FShipMotionStep Motion = AdvanceShipMotion(
            Before,
            MakeShipMotionInput(ThrottleInput, SteerInput),
            Validated.Parameters,
            Schedule.StepSeconds);
        if (!Motion.bValid)
        {
            SignedSpeedCmPerSecond = 0.0;
            break;
        }

        const double Heading =
            FMath::DegreesToRadians(Before.HorizontalYawDegrees);
        const FVector HorizontalForward(
            FMath::Cos(Heading),
            FMath::Sin(Heading),
            0.0);
        AActor* Owner = GetOwner();
        const FVector CurrentLocation = Owner->GetActorLocation();
        FVector NewLocation =
            CurrentLocation + HorizontalForward * Motion.TravelCm;

        UWaterSubsystem* Water =
            UWaterSubsystem::GetWaterSubsystem(GetWorld());
        if (IsValid(Water) && !OceanBodyComponent.IsValid())
        {
            OceanBodyComponent = Water->GetOceanBodyComponent();
        }
        UWaterBodyComponent* Ocean = OceanBodyComponent.Get();
        TOptional<FWaterBodyQueryResult> Query;
        if (IsValid(Water) && IsValid(Ocean))
        {
            Query.Emplace(Ocean->QueryWaterInfoClosestToWorldLocation(
                FVector(NewLocation.X, NewLocation.Y, CurrentLocation.Z),
                QueryFlags));
        }
        TOptional<FShipSurfaceSample> Last;
        if (bHasLastValidSurface)
        {
            Last.Emplace(FShipSurfaceSample{
                EShipWaterState::ValidNoWaves,
                LastValidSurfaceZ,
                LastValidSurfaceNormal,
                false});
        }
        const FShipSurfaceSample Surface = ResolveWaterSurfaceSample(
            IsValid(Water),
            IsValid(Ocean),
            IsValid(Ocean) && Ocean->HasWaves(),
            Query.IsSet() ? &Query.GetValue() : nullptr,
            Last,
            CurrentLocation.Z);
        const uint8 WaterState = static_cast<uint8>(Surface.State);
        if (LastWaterState != WaterState)
        {
            UE_LOG(
                LogShipMovement,
                Display,
                TEXT("Water state changed to %s"),
                WaterStateName(WaterState));
            LastWaterState = WaterState;
        }
        bLastWaterFallback = Surface.bUsedFallback;
        LastTargetSurfaceZ = Surface.SurfaceZ + WaterlineOffsetCm;
        if (!Surface.bUsedFallback)
        {
            bHasLastValidSurface = true;
            LastValidSurfaceZ = Surface.SurfaceZ;
            LastValidSurfaceNormal = Surface.Normal;
        }

        const double HeightSpeed =
            FMath::IsFinite(WaterHeightInterpSpeed) &&
                WaterHeightInterpSpeed > 0.0
            ? WaterHeightInterpSpeed
            : 5.0;
        const double NormalSpeed =
            FMath::IsFinite(WaterNormalInterpSpeed) &&
                WaterNormalInterpSpeed > 0.0
            ? WaterNormalInterpSpeed
            : 5.0;
        const double HeightAlpha =
            1.0 - FMath::Exp(-HeightSpeed * Schedule.StepSeconds);
        const double NormalAlpha =
            1.0 - FMath::Exp(-NormalSpeed * Schedule.StepSeconds);
        NewLocation.Z = FMath::Lerp(
            CurrentLocation.Z,
            LastTargetSurfaceZ,
            HeightAlpha);
        FVector BlendedUp =
            FMath::Lerp(AppliedUpVector, Surface.Normal, NormalAlpha);
        AppliedUpVector =
            BlendedUp.Normalize() ? BlendedUp : FVector::UpVector;

        const FShipSurfaceBasis Basis = BuildShipSurfaceBasis(
            Motion.NextState.HorizontalYawDegrees,
            AppliedUpVector,
            bHasLastValidSurface
                ? TOptional<FVector>(LastValidSurfaceNormal)
                : TOptional<FVector>());
        const FQuat NewRotation =
            FRotationMatrix::MakeFromXZ(Basis.Forward, Basis.Up).ToQuat();
        FHitResult Hit;
        Owner->SetActorLocationAndRotation(
            NewLocation, NewRotation, true, &Hit, ETeleportType::None);

        SignedSpeedCmPerSecond = Motion.NextState.SignedSpeedCmPerSecond;
        HorizontalYawDegrees = Motion.NextState.HorizontalYawDegrees;
        LastAcceleration = Motion.AccelerationCmPerSecondSquared;
        LastYawRate = Motion.YawRateDegreesPerSecond;
        bLastBlockingHit = Hit.bBlockingHit;
        if (Hit.bBlockingHit)
        {
            SignedSpeedCmPerSecond = 0.0;
            LastImpactNormal = Hit.ImpactNormal;
            LastHitName =
                IsValid(Hit.GetActor()) ? Hit.GetActor()->GetName() : TEXT("Unknown");
            break;
        }
    }
}

void UShipMovement::DrawMovementDebug() const
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestDebugDrawInvocationCount;
#endif
#if !UE_BUILD_SHIPPING
    if (!bShowMovementDebug || !GEngine)
    {
        return;
    }

    const FString Message = FString::Printf(
        TEXT("Ship Manual  T %.2f  S %.2f\n")
        TEXT("speed %.3f cm/s  accel %.3f cm/s^2  yawRate %.3f deg/s\n")
        TEXT("substeps %d  h %.6f  simulated %.6f  dropped %.6f\n")
        TEXT("water %s  fallback %s  targetZ %.3f  appliedZ %.3f  offset %.3f\n")
        TEXT("normal %s  up %s\n")
        TEXT("hit %s  impact %s  parameter %s"),
        ThrottleInput,
        SteerInput,
        SignedSpeedCmPerSecond,
        LastAcceleration,
        LastYawRate,
        LastSubsteps,
        LastStepSeconds,
        LastSimulatedDeltaTime,
        LastDroppedDeltaTime,
        WaterStateName(LastWaterState),
        bLastWaterFallback ? TEXT("yes") : TEXT("no"),
        LastTargetSurfaceZ,
        GetOwner() ? GetOwner()->GetActorLocation().Z : 0.0,
        WaterlineOffsetCm,
        *LastValidSurfaceNormal.ToCompactString(),
        *AppliedUpVector.ToCompactString(),
        bLastBlockingHit ? *LastHitName : TEXT("none"),
        *LastImpactNormal.ToCompactString(),
        ParameterStateName(LastParameterState));
    GEngine->AddOnScreenDebugMessage(
        0x53484950,
        0.0f,
        FColor::Cyan,
        Message);
#endif
}
