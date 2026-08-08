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
