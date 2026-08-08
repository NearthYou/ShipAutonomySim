#pragma once

#include "CoreMinimal.h"
#include "WaterBodyTypes.h"

inline constexpr double ShipSupportedSpeedCmPerSecond = 500.0;
inline constexpr double ShipMinSurfaceNormalZ = 0.1;

enum class EShipMotionParameterState : uint8
{
    Defaults,
    Tuned,
    TuningFallback
};

enum class EShipWaterState : uint8
{
    ValidWaves,
    ValidNoWaves,
    Excluded,
    QueryInvalid,
    ComponentInvalid
};

struct FShipMotionParameters
{
    double LinearDragCoeff = 0.447501534;
    double QuadraticDragCoeff = 0.000400390770;
    double MaxThrustAccel = 105.5159376;
    double StopSpeedThreshold = 5.0;
    double MaxYawRate = 45.83662361;
    double TurnRefSpeed = 200.0;
    double MaxSimulationStepSeconds = 1.0 / 120.0;
    int32 MaxSubstepsPerTick = 8;

    static FShipMotionParameters Defaults()
    {
        return FShipMotionParameters{};
    }
};

struct FShipMotionState
{
    double SignedSpeedCmPerSecond = 0.0;
    double HorizontalYawDegrees = 0.0;
};

struct FShipMotionInput
{
    double Throttle = 0.0;
    double Steer = 0.0;
};

struct FShipMotionStep
{
    FShipMotionState NextState;
    double TravelCm = 0.0;
    double YawRateDegreesPerSecond = 0.0;
    double AccelerationCmPerSecondSquared = 0.0;
    bool bValid = false;
};

struct FShipSubstepSchedule
{
    double SimulatedDeltaTimeSeconds = 0.0;
    double DroppedDeltaTimeSeconds = 0.0;
    double StepSeconds = 0.0;
    int32 NumSteps = 0;
    bool bValid = false;
};

struct FShipValidatedMotionParameters
{
    FShipMotionParameters Parameters;
    EShipMotionParameterState State = EShipMotionParameterState::TuningFallback;
};

struct FShipSurfaceSample
{
    EShipWaterState State = EShipWaterState::ComponentInvalid;
    double SurfaceZ = 0.0;
    FVector Normal = FVector::UpVector;
    bool bUsedFallback = true;
};

struct FShipSurfaceBasis
{
    FVector Forward = FVector::ForwardVector;
    FVector Right = FVector::RightVector;
    FVector Up = FVector::UpVector;
    bool bUsedFallback = false;
};

FShipMotionInput MakeShipMotionInput(double Throttle, double Steer);
FShipMotionStep AdvanceShipMotion(
    const FShipMotionState& State,
    const FShipMotionInput& Input,
    const FShipMotionParameters& Parameters,
    double StepSeconds);
FShipSubstepSchedule BuildShipSubstepSchedule(
    double DeltaTimeSeconds,
    double MaxSimulationStepSeconds,
    int32 MaxSubstepsPerTick);
FShipValidatedMotionParameters ValidateShipMotionParameters(
    const FShipMotionParameters& Candidate);
FShipSurfaceSample ResolveWaterSurfaceSample(
    bool bSubsystemValid,
    bool bComponentValid,
    bool bHasWaves,
    const FWaterBodyQueryResult* QueryResult,
    const TOptional<FShipSurfaceSample>& LastValidSample,
    double CurrentActorZ);
FShipSurfaceBasis BuildShipSurfaceBasis(
    double YawDegrees,
    const FVector& SurfaceNormal,
    const TOptional<FVector>& LastValidNormal);
