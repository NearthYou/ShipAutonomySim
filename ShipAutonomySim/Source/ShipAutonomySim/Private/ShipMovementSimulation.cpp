#include "ShipMovementSimulation.h"

#include "Math/Rotator.h"

namespace
{
bool InRange(double Value, double Minimum, double Maximum)
{
    return FMath::IsFinite(Value) && Value >= Minimum && Value <= Maximum;
}

double EquilibriumSpeed(const FShipMotionParameters& Parameters)
{
    if (Parameters.QuadraticDragCoeff > 0.0)
    {
        return (2.0 * Parameters.MaxThrustAccel) /
            (Parameters.LinearDragCoeff + FMath::Sqrt(
                Parameters.LinearDragCoeff * Parameters.LinearDragCoeff +
                4.0 * Parameters.QuadraticDragCoeff * Parameters.MaxThrustAccel));
    }

    return Parameters.MaxThrustAccel / Parameters.LinearDragCoeff;
}

bool IsFiniteVector(const FVector& Value)
{
    return FMath::IsFinite(Value.X) &&
        FMath::IsFinite(Value.Y) &&
        FMath::IsFinite(Value.Z);
}

FShipSurfaceSample MakeFallback(
    EShipWaterState State,
    const TOptional<FShipSurfaceSample>& LastValidSample,
    double CurrentActorZ)
{
    if (LastValidSample.IsSet())
    {
        return {
            State,
            LastValidSample->SurfaceZ,
            LastValidSample->Normal,
            true};
    }

    return {State, CurrentActorZ, FVector::UpVector, true};
}
}

FShipMotionInput MakeShipMotionInput(double Throttle, double Steer)
{
    const double SafeThrottle = FMath::IsFinite(Throttle) ? Throttle : 0.0;
    const double SafeSteer = FMath::IsFinite(Steer) ? Steer : 0.0;
    return {
        FMath::Clamp(SafeThrottle, -1.0, 1.0),
        FMath::Clamp(SafeSteer, -1.0, 1.0)};
}

FShipMotionStep AdvanceShipMotion(
    const FShipMotionState& State,
    const FShipMotionInput& Input,
    const FShipMotionParameters& Parameters,
    double StepSeconds)
{
    FShipMotionStep Result;
    if (!FMath::IsFinite(State.SignedSpeedCmPerSecond) ||
        !FMath::IsFinite(State.HorizontalYawDegrees) ||
        !FMath::IsFinite(StepSeconds) ||
        StepSeconds <= 0.0 ||
        FMath::Abs(State.SignedSpeedCmPerSecond) > ShipSupportedSpeedCmPerSecond)
    {
        Result.NextState = {0.0, 0.0};
        return Result;
    }

    const double Speed = State.SignedSpeedCmPerSecond;
    Result.AccelerationCmPerSecondSquared =
        Parameters.MaxThrustAccel * Input.Throttle -
        Parameters.LinearDragCoeff * Speed -
        Parameters.QuadraticDragCoeff * Speed * FMath::Abs(Speed);
    Result.YawRateDegreesPerSecond =
        Parameters.MaxYawRate * Input.Steer *
        FMath::Clamp(FMath::Abs(Speed) / Parameters.TurnRefSpeed, 0.0, 1.0);
    Result.TravelCm = Speed * StepSeconds;

    double NextSpeed =
        Speed + Result.AccelerationCmPerSecondSquared * StepSeconds;
    if (FMath::IsNearlyZero(Input.Throttle) &&
        FMath::Abs(NextSpeed) <= Parameters.StopSpeedThreshold)
    {
        NextSpeed = 0.0;
    }

    const double NextYaw = FRotator::NormalizeAxis(
        State.HorizontalYawDegrees +
        Result.YawRateDegreesPerSecond * StepSeconds);
    Result.bValid =
        FMath::IsFinite(NextSpeed) &&
        FMath::IsFinite(NextYaw) &&
        FMath::IsFinite(Result.TravelCm) &&
        FMath::Abs(NextSpeed) <= ShipSupportedSpeedCmPerSecond;
    Result.NextState = Result.bValid
        ? FShipMotionState{NextSpeed, NextYaw}
        : FShipMotionState{0.0, State.HorizontalYawDegrees};
    return Result;
}

FShipSubstepSchedule BuildShipSubstepSchedule(
    double DeltaTimeSeconds,
    double MaxSimulationStepSeconds,
    int32 MaxSubstepsPerTick)
{
    FShipSubstepSchedule Result;
    if (!FMath::IsFinite(DeltaTimeSeconds) ||
        !FMath::IsFinite(MaxSimulationStepSeconds) ||
        DeltaTimeSeconds < 0.0 ||
        MaxSimulationStepSeconds <= 0.0 ||
        MaxSubstepsPerTick <= 0)
    {
        return Result;
    }

    if (DeltaTimeSeconds == 0.0)
    {
        Result.bValid = true;
        return Result;
    }

    const double MaxSimulatedDeltaTime =
        MaxSimulationStepSeconds * MaxSubstepsPerTick;
    Result.SimulatedDeltaTimeSeconds =
        FMath::Min(DeltaTimeSeconds, MaxSimulatedDeltaTime);
    Result.DroppedDeltaTimeSeconds =
        DeltaTimeSeconds - Result.SimulatedDeltaTimeSeconds;
    Result.NumSteps = FMath::Clamp(
        FMath::CeilToInt(
            Result.SimulatedDeltaTimeSeconds / MaxSimulationStepSeconds),
        1,
        MaxSubstepsPerTick);
    Result.StepSeconds =
        Result.SimulatedDeltaTimeSeconds / Result.NumSteps;
    Result.bValid =
        FMath::IsFinite(Result.StepSeconds) &&
        Result.StepSeconds <= MaxSimulationStepSeconds + 1e-12;
    return Result;
}

FShipValidatedMotionParameters ValidateShipMotionParameters(
    const FShipMotionParameters& Candidate)
{
    const bool bRanges =
        InRange(Candidate.LinearDragCoeff, 0.0, 2.0) &&
        InRange(Candidate.QuadraticDragCoeff, 0.0, 0.002) &&
        InRange(Candidate.MaxThrustAccel, 0.001, 500.0) &&
        InRange(Candidate.StopSpeedThreshold, 0.1, 50.0) &&
        InRange(Candidate.MaxYawRate, 0.0, 180.0) &&
        InRange(Candidate.TurnRefSpeed, 1.0, 1000.0) &&
        InRange(Candidate.MaxSimulationStepSeconds, 0.001, 0.016666667) &&
        Candidate.MaxSubstepsPerTick >= 1 &&
        Candidate.MaxSubstepsPerTick <= 32;
    const bool bHasDrag =
        Candidate.LinearDragCoeff > 0.0 ||
        Candidate.QuadraticDragCoeff > 0.0;
    const double CandidateEquilibriumSpeed =
        bRanges && bHasDrag ? EquilibriumSpeed(Candidate) : 0.0;
    const double LambdaMax =
        Candidate.LinearDragCoeff +
        2.0 * Candidate.QuadraticDragCoeff * ShipSupportedSpeedCmPerSecond;
    const double EulerStabilityNumber =
        Candidate.MaxSimulationStepSeconds * LambdaMax;
    const bool bDerived =
        FMath::IsFinite(CandidateEquilibriumSpeed) &&
        CandidateEquilibriumSpeed > Candidate.StopSpeedThreshold &&
        CandidateEquilibriumSpeed <= ShipSupportedSpeedCmPerSecond &&
        FMath::IsFinite(EulerStabilityNumber) &&
        EulerStabilityNumber <= 0.5;

    if (!bRanges || !bHasDrag || !bDerived)
    {
        return {
            FShipMotionParameters::Defaults(),
            EShipMotionParameterState::TuningFallback};
    }

    const FShipMotionParameters Defaults = FShipMotionParameters::Defaults();
    const bool bDefaults =
        FMath::IsNearlyEqual(
            Candidate.LinearDragCoeff, Defaults.LinearDragCoeff, 1e-12) &&
        FMath::IsNearlyEqual(
            Candidate.QuadraticDragCoeff, Defaults.QuadraticDragCoeff, 1e-15) &&
        FMath::IsNearlyEqual(
            Candidate.MaxThrustAccel, Defaults.MaxThrustAccel, 1e-9) &&
        FMath::IsNearlyEqual(
            Candidate.StopSpeedThreshold, Defaults.StopSpeedThreshold, 1e-12) &&
        FMath::IsNearlyEqual(
            Candidate.MaxYawRate, Defaults.MaxYawRate, 1e-9) &&
        FMath::IsNearlyEqual(
            Candidate.TurnRefSpeed, Defaults.TurnRefSpeed, 1e-12) &&
        FMath::IsNearlyEqual(
            Candidate.MaxSimulationStepSeconds,
            Defaults.MaxSimulationStepSeconds,
            1e-12) &&
        Candidate.MaxSubstepsPerTick == Defaults.MaxSubstepsPerTick;

    return {
        Candidate,
        bDefaults
            ? EShipMotionParameterState::Defaults
            : EShipMotionParameterState::Tuned};
}

FShipSurfaceSample ResolveWaterSurfaceSample(
    bool bSubsystemValid,
    bool bComponentValid,
    bool bHasWaves,
    const FWaterBodyQueryResult* QueryResult,
    const TOptional<FShipSurfaceSample>& LastValidSample,
    double CurrentActorZ)
{
    if (!bSubsystemValid || !bComponentValid || QueryResult == nullptr)
    {
        return MakeFallback(
            EShipWaterState::ComponentInvalid,
            LastValidSample,
            CurrentActorZ);
    }

    if (QueryResult->IsInExclusionVolume())
    {
        return MakeFallback(
            EShipWaterState::Excluded,
            LastValidSample,
            CurrentActorZ);
    }

    const EWaterBodyQueryFlags RequiredFlags =
        EWaterBodyQueryFlags::ComputeLocation |
        EWaterBodyQueryFlags::ComputeNormal;
    if (!EnumHasAllFlags(QueryResult->GetQueryFlags(), RequiredFlags))
    {
        return MakeFallback(
            EShipWaterState::QueryInvalid,
            LastValidSample,
            CurrentActorZ);
    }

    const FVector Location = QueryResult->GetWaterSurfaceLocation();
    FVector Normal = QueryResult->GetWaterSurfaceNormal();
    if (!IsFiniteVector(Location) ||
        !IsFiniteVector(Normal) ||
        !Normal.Normalize() ||
        Normal.Z < ShipMinSurfaceNormalZ)
    {
        return MakeFallback(
            EShipWaterState::QueryInvalid,
            LastValidSample,
            CurrentActorZ);
    }

    return {
        bHasWaves
            ? EShipWaterState::ValidWaves
            : EShipWaterState::ValidNoWaves,
        Location.Z,
        Normal,
        false};
}

FShipSurfaceBasis BuildShipSurfaceBasis(
    double YawDegrees,
    const FVector& SurfaceNormal,
    const TOptional<FVector>& LastValidNormal)
{
    const double YawRadians = FMath::DegreesToRadians(YawDegrees);
    const FVector HorizontalHeading(
        FMath::Cos(YawRadians), FMath::Sin(YawRadians), 0.0);
    FVector Normal = SurfaceNormal;
    const auto AcceptNormal = [](FVector& Candidate)
    {
        return IsFiniteVector(Candidate) &&
            Candidate.Normalize() &&
            Candidate.Z >= ShipMinSurfaceNormalZ;
    };

    const bool bUsedFallback = !AcceptNormal(Normal);
    if (bUsedFallback)
    {
        if (LastValidNormal.IsSet())
        {
            Normal = LastValidNormal.GetValue();
            if (!AcceptNormal(Normal))
            {
                Normal = FVector::UpVector;
            }
        }
        else
        {
            Normal = FVector::UpVector;
        }
    }

    FVector Forward = HorizontalHeading - FVector::UpVector *
        (FVector::DotProduct(HorizontalHeading, Normal) / Normal.Z);
    FVector Right = FVector::CrossProduct(Normal, Forward);
    FVector Up = FVector::CrossProduct(Forward, Right);
    const bool bValid =
        IsFiniteVector(Forward) &&
        IsFiniteVector(Right) &&
        IsFiniteVector(Up) &&
        Forward.Normalize() &&
        Right.Normalize() &&
        Up.Normalize();
    if (!bValid)
    {
        return {
            HorizontalHeading,
            FVector::CrossProduct(
                FVector::UpVector, HorizontalHeading).GetSafeNormal(),
            FVector::UpVector,
            true};
    }

    return {Forward, Right, Up, bUsedFallback};
}
