#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShipMovement.h"
#include "ShipMovementSimulation.h"
#include "ShipPawn.h"
#include "SimGameMode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipMotionDynamicsTest,
    "ShipAutonomySim.ShipMovement.Motion.Dynamics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipMotionTargetsTest,
    "ShipAutonomySim.ShipMovement.Motion.Targets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipMotionParameterValidationTest,
    "ShipAutonomySim.ShipMovement.Motion.ParameterValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipMotionFrameRatesAndHitchTest,
    "ShipAutonomySim.ShipMovement.Motion.FrameRatesAndHitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
struct FScenarioResult
{
    FShipMotionState State;
    double DistanceCm = 0.0;
    double FirstReached180Seconds = -1.0;
};

FScenarioResult RunScenario(
    int32 FramesPerSecond,
    double DurationSeconds,
    FShipMotionState State,
    FShipMotionInput Input)
{
    FScenarioResult Result{State};
    const double FrameSeconds = 1.0 / FramesPerSecond;
    const int32 FrameCount = FMath::RoundToInt(DurationSeconds * FramesPerSecond);
    double ElapsedSeconds = 0.0;

    for (int32 Frame = 0; Frame < FrameCount; ++Frame)
    {
        const FShipSubstepSchedule Schedule =
            BuildShipSubstepSchedule(FrameSeconds, 1.0 / 120.0, 8);
        check(Schedule.bValid);

        for (int32 StepIndex = 0; StepIndex < Schedule.NumSteps; ++StepIndex)
        {
            const FShipMotionStep Step = AdvanceShipMotion(
                Result.State,
                Input,
                FShipMotionParameters::Defaults(),
                Schedule.StepSeconds);
            check(Step.bValid);

            Result.DistanceCm += FMath::Abs(Step.TravelCm);
            Result.State = Step.NextState;
            ElapsedSeconds += Schedule.StepSeconds;
            if (Result.FirstReached180Seconds < 0.0 &&
                Result.State.SignedSpeedCmPerSecond >= 180.0)
            {
                Result.FirstReached180Seconds = ElapsedSeconds;
            }
        }
    }

    return Result;
}

bool MatchesDefaultParameters(const FShipMotionParameters& Parameters)
{
    const FShipMotionParameters Defaults = FShipMotionParameters::Defaults();
    return Parameters.LinearDragCoeff == Defaults.LinearDragCoeff &&
        Parameters.QuadraticDragCoeff == Defaults.QuadraticDragCoeff &&
        Parameters.MaxThrustAccel == Defaults.MaxThrustAccel &&
        Parameters.StopSpeedThreshold == Defaults.StopSpeedThreshold &&
        Parameters.MaxYawRate == Defaults.MaxYawRate &&
        Parameters.TurnRefSpeed == Defaults.TurnRefSpeed &&
        Parameters.MaxSimulationStepSeconds == Defaults.MaxSimulationStepSeconds &&
        Parameters.MaxSubstepsPerTick == Defaults.MaxSubstepsPerTick;
}
}

bool FShipMotionDynamicsTest::RunTest(const FString&)
{
    const FShipMotionParameters Parameters = FShipMotionParameters::Defaults();
    const double StepSeconds = 1.0 / 120.0;

    TestTrue(
        TEXT("forward equilibrium"),
        FMath::Abs(AdvanceShipMotion(
            {200.0, 0.0}, MakeShipMotionInput(1.0, 0.0), Parameters, StepSeconds)
                .AccelerationCmPerSecondSquared) <= 1e-6);
    TestTrue(
        TEXT("reverse equilibrium"),
        FMath::Abs(AdvanceShipMotion(
            {-200.0, 0.0}, MakeShipMotionInput(-1.0, 0.0), Parameters, StepSeconds)
                .AccelerationCmPerSecondSquared) <= 1e-6);
    TestTrue(
        TEXT("positive drag opposes speed"),
        AdvanceShipMotion(
            {100.0, 0.0}, MakeShipMotionInput(0.0, 0.0), Parameters, StepSeconds)
                .AccelerationCmPerSecondSquared < 0.0);
    TestTrue(
        TEXT("negative drag opposes speed"),
        AdvanceShipMotion(
            {-100.0, 0.0}, MakeShipMotionInput(0.0, 0.0), Parameters, StepSeconds)
                .AccelerationCmPerSecondSquared > 0.0);

    const FShipMotionInput ClampedInput = MakeShipMotionInput(2.0, -3.0);
    TestEqual(TEXT("input high clamp"), ClampedInput.Throttle, 1.0);
    TestEqual(TEXT("input low clamp"), ClampedInput.Steer, -1.0);
    TestEqual(
        TEXT("non-finite input replacement"),
        MakeShipMotionInput(std::numeric_limits<double>::quiet_NaN(), 0.0).Throttle,
        0.0);

    const FShipMotionStep StationarySteer = AdvanceShipMotion(
        {0.0, 10.0}, MakeShipMotionInput(0.0, 1.0), Parameters, StepSeconds);
    TestTrue(TEXT("zero-speed step remains valid"), StationarySteer.bValid);
    TestEqual(
        TEXT("zero-speed steer has no yaw"),
        StationarySteer.NextState.HorizontalYawDegrees,
        10.0);
    TestTrue(
        TEXT("full-speed yaw rate"),
        FMath::Abs(AdvanceShipMotion(
            {200.0, 0.0}, MakeShipMotionInput(1.0, 1.0), Parameters, StepSeconds)
                .YawRateDegreesPerSecond - 45.83662361) <= 1e-8);

    const FShipMotionStep LowSpeedThrust = AdvanceShipMotion(
        {1.0, 0.0}, MakeShipMotionInput(1.0, 0.0), Parameters, StepSeconds);
    TestTrue(TEXT("non-zero throttle preserves low-speed launch"),
        LowSpeedThrust.NextState.SignedSpeedCmPerSecond > 0.0);

    const FShipMotionStep NonFiniteState = AdvanceShipMotion(
        {std::numeric_limits<double>::quiet_NaN(), 0.0},
        MakeShipMotionInput(0.0, 0.0),
        Parameters,
        StepSeconds);
    TestFalse(TEXT("non-finite state rejected"), NonFiniteState.bValid);
    TestEqual(TEXT("non-finite state stops speed"),
        NonFiniteState.NextState.SignedSpeedCmPerSecond, 0.0);

    const FShipMotionStep UnsupportedSpeed = AdvanceShipMotion(
        {501.0, 0.0}, MakeShipMotionInput(0.0, 0.0), Parameters, StepSeconds);
    TestFalse(TEXT("unsupported speed rejected"), UnsupportedSpeed.bValid);

    return !HasAnyErrors();
}

bool FShipMotionTargetsTest::RunTest(const FString&)
{
    const FScenarioResult Acceleration = RunScenario(
        120, 4.0, {0.0, 0.0}, MakeShipMotionInput(1.0, 0.0));
    TestTrue(TEXT("180 first reach"),
        FMath::Abs(Acceleration.FirstReached180Seconds - 3.9917) <= 0.01);
    TestTrue(TEXT("four-second speed"),
        FMath::Abs(Acceleration.State.SignedSpeedCmPerSecond - 180.0) <= 1.0);

    const FScenarioResult Coast = RunScenario(
        120, 8.0, {200.0, 0.0}, MakeShipMotionInput(0.0, 0.0));
    TestTrue(TEXT("coast distance"),
        FMath::Abs(Coast.DistanceCm - 399.9615) <= 0.1);
    TestEqual(TEXT("coast stops"), Coast.State.SignedSpeedCmPerSecond, 0.0);

    FShipMotionState ReversingState{20.0, 0.0};
    bool bCrossedZero = false;
    for (int32 StepIndex = 0; StepIndex < 120; ++StepIndex)
    {
        const FShipMotionStep Step = AdvanceShipMotion(
            ReversingState,
            MakeShipMotionInput(-1.0, 0.0),
            FShipMotionParameters::Defaults(),
            1.0 / 120.0);
        TestTrue(TEXT("reverse transition step valid"), Step.bValid);
        ReversingState = Step.NextState;
        bCrossedZero = bCrossedZero || ReversingState.SignedSpeedCmPerSecond < 0.0;
    }
    TestTrue(TEXT("opposite throttle crosses zero"), bCrossedZero);
    TestTrue(TEXT("opposite throttle establishes reverse speed"),
        ReversingState.SignedSpeedCmPerSecond < 0.0);

    return !HasAnyErrors();
}

bool FShipMotionParameterValidationTest::RunTest(const FString&)
{
    const FShipValidatedMotionParameters Defaults =
        ValidateShipMotionParameters(FShipMotionParameters::Defaults());
    TestEqual(TEXT("default parameter state"),
        Defaults.State, EShipMotionParameterState::Defaults);
    TestTrue(TEXT("default snapshot preserved"),
        MatchesDefaultParameters(Defaults.Parameters));

    FShipMotionParameters TunedCandidate = FShipMotionParameters::Defaults();
    TunedCandidate.MaxYawRate = 40.0;
    const FShipValidatedMotionParameters Tuned =
        ValidateShipMotionParameters(TunedCandidate);
    TestEqual(TEXT("valid tune state"), Tuned.State, EShipMotionParameterState::Tuned);
    TestEqual(TEXT("valid tune adopted"), Tuned.Parameters.MaxYawRate, 40.0);

    const auto TestFallback = [this](
        const TCHAR* Label,
        const FShipMotionParameters& Candidate)
    {
        const FShipValidatedMotionParameters Result =
            ValidateShipMotionParameters(Candidate);
        TestEqual(
            *FString::Printf(TEXT("%s state"), Label),
            Result.State,
            EShipMotionParameterState::TuningFallback);
        TestTrue(
            *FString::Printf(TEXT("%s full default snapshot"), Label),
            MatchesDefaultParameters(Result.Parameters));
    };

    FShipMotionParameters Negative = FShipMotionParameters::Defaults();
    Negative.LinearDragCoeff = -0.1;
    TestFallback(TEXT("negative coefficient"), Negative);

    FShipMotionParameters NonFinite = FShipMotionParameters::Defaults();
    NonFinite.MaxThrustAccel = std::numeric_limits<double>::quiet_NaN();
    TestFallback(TEXT("non-finite coefficient"), NonFinite);

    FShipMotionParameters OutOfRange = FShipMotionParameters::Defaults();
    OutOfRange.TurnRefSpeed = 1001.0;
    TestFallback(TEXT("metadata range"), OutOfRange);

    FShipMotionParameters NoDrag = FShipMotionParameters::Defaults();
    NoDrag.LinearDragCoeff = 0.0;
    NoDrag.QuadraticDragCoeff = 0.0;
    TestFallback(TEXT("zero drag"), NoDrag);

    FShipMotionParameters LowEquilibrium = FShipMotionParameters::Defaults();
    LowEquilibrium.LinearDragCoeff = 2.0;
    LowEquilibrium.QuadraticDragCoeff = 0.0;
    LowEquilibrium.MaxThrustAccel = 0.001;
    LowEquilibrium.StopSpeedThreshold = 0.1;
    TestFallback(TEXT("equilibrium below stop threshold"), LowEquilibrium);

    FShipMotionParameters HighEquilibrium = FShipMotionParameters::Defaults();
    HighEquilibrium.LinearDragCoeff = 0.0;
    HighEquilibrium.QuadraticDragCoeff = 0.000001;
    HighEquilibrium.MaxThrustAccel = 500.0;
    TestFallback(TEXT("equilibrium above supported speed"), HighEquilibrium);

    FShipMotionParameters Unstable = FShipMotionParameters::Defaults();
    Unstable.LinearDragCoeff = 2.0;
    Unstable.QuadraticDragCoeff = 0.002;
    Unstable.MaxSimulationStepSeconds = 0.2;
    TestFallback(TEXT("Euler stability number"), Unstable);

    return !HasAnyErrors();
}

bool FShipMotionFrameRatesAndHitchTest::RunTest(const FString&)
{
    const TArray<int32> FramesPerSecondValues{15, 30, 60, 120};
    const FScenarioResult ReferenceAcceleration = RunScenario(
        120, 4.0, {0.0, 0.0}, MakeShipMotionInput(1.0, 0.0));
    const FScenarioResult ReferenceCoast = RunScenario(
        120, 8.0, {200.0, 0.0}, MakeShipMotionInput(0.0, 0.0));
    const FScenarioResult ReferenceYaw = RunScenario(
        120, 2.0, {200.0, 0.0}, MakeShipMotionInput(1.0, 1.0));

    for (const int32 FramesPerSecond : FramesPerSecondValues)
    {
        const FScenarioResult Acceleration = RunScenario(
            FramesPerSecond,
            4.0,
            {0.0, 0.0},
            MakeShipMotionInput(1.0, 0.0));
        TestTrue(
            *FString::Printf(TEXT("%d FPS acceleration absolute"), FramesPerSecond),
            FMath::Abs(Acceleration.State.SignedSpeedCmPerSecond - 180.0) <= 1.0);
        TestTrue(
            *FString::Printf(TEXT("%d FPS first reach"), FramesPerSecond),
            FMath::Abs(Acceleration.FirstReached180Seconds - 4.0) <= 0.05);
        TestTrue(
            *FString::Printf(TEXT("%d FPS acceleration reference"), FramesPerSecond),
            FMath::Abs(
                Acceleration.State.SignedSpeedCmPerSecond -
                ReferenceAcceleration.State.SignedSpeedCmPerSecond) <= 0.1);

        const FScenarioResult Coast = RunScenario(
            FramesPerSecond,
            8.0,
            {200.0, 0.0},
            MakeShipMotionInput(0.0, 0.0));
        TestEqual(
            *FString::Printf(TEXT("%d FPS coast stops"), FramesPerSecond),
            Coast.State.SignedSpeedCmPerSecond,
            0.0);
        TestTrue(
            *FString::Printf(TEXT("%d FPS coast absolute"), FramesPerSecond),
            FMath::Abs(Coast.DistanceCm - 400.0) <= 5.0);
        TestTrue(
            *FString::Printf(TEXT("%d FPS coast reference"), FramesPerSecond),
            FMath::Abs(Coast.DistanceCm - ReferenceCoast.DistanceCm) <= 0.5);

        const FScenarioResult Yaw = RunScenario(
            FramesPerSecond,
            2.0,
            {200.0, 0.0},
            MakeShipMotionInput(1.0, 1.0));
        TestTrue(
            *FString::Printf(TEXT("%d FPS yaw absolute"), FramesPerSecond),
            FMath::Abs(Yaw.State.HorizontalYawDegrees - 91.67324722) <= 0.1);
        TestTrue(
            *FString::Printf(TEXT("%d FPS yaw reference"), FramesPerSecond),
            FMath::Abs(FRotator::NormalizeAxis(
                Yaw.State.HorizontalYawDegrees -
                ReferenceYaw.State.HorizontalYawDegrees)) <= 0.1);
    }

    const FShipSubstepSchedule Hitch =
        BuildShipSubstepSchedule(0.5, 1.0 / 120.0, 8);
    TestTrue(TEXT("hitch schedule valid"), Hitch.bValid);
    TestEqual(TEXT("hitch bounded to eight steps"), Hitch.NumSteps, 8);
    TestTrue(TEXT("hitch simulated time"),
        FMath::Abs(Hitch.SimulatedDeltaTimeSeconds - 0.066666667) <= 1e-6);
    TestTrue(TEXT("hitch dropped time"),
        FMath::Abs(Hitch.DroppedDeltaTimeSeconds - 0.433333333) <= 1e-6);
    TestTrue(TEXT("hitch step bound"),
        Hitch.StepSeconds <= (1.0 / 120.0) + 1e-12);

    FShipMotionState HitchState{100.0, 10.0};
    for (int32 StepIndex = 0; StepIndex < Hitch.NumSteps; ++StepIndex)
    {
        const FShipMotionStep Step = AdvanceShipMotion(
            HitchState,
            MakeShipMotionInput(1.0, 1.0),
            FShipMotionParameters::Defaults(),
            Hitch.StepSeconds);
        TestTrue(TEXT("hitch motion remains valid"), Step.bValid);
        HitchState = Step.NextState;
    }
    TestTrue(TEXT("hitch speed finite"), FMath::IsFinite(HitchState.SignedSpeedCmPerSecond));
    TestTrue(TEXT("hitch yaw finite"), FMath::IsFinite(HitchState.HorizontalYawDegrees));

    const FShipSubstepSchedule NextFrame =
        BuildShipSubstepSchedule(1.0 / 120.0, 1.0 / 120.0, 8);
    TestEqual(TEXT("next frame does not replay dropped time"), NextFrame.NumSteps, 1);
    TestTrue(TEXT("next frame simulates only current delta"),
        FMath::Abs(NextFrame.SimulatedDeltaTimeSeconds - (1.0 / 120.0)) <= 1e-12);
    TestEqual(TEXT("next frame drops no time"), NextFrame.DroppedDeltaTimeSeconds, 0.0);

    for (const int32 MaxSteps : TArray<int32>{1, 8, 32})
    {
        const FShipSubstepSchedule Bounded =
            BuildShipSubstepSchedule(10.0, 1.0 / 120.0, MaxSteps);
        TestTrue(
            *FString::Printf(TEXT("max step bound %d"), MaxSteps),
            Bounded.NumSteps <= MaxSteps);
    }

    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipWaterClassificationTest,
    "ShipAutonomySim.ShipMovement.Water.Classification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipSurfaceBasisTest,
    "ShipAutonomySim.ShipMovement.Water.SurfaceBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShipWaterClassificationTest::RunTest(const FString&)
{
    const EWaterBodyQueryFlags CompleteFlags =
        EWaterBodyQueryFlags::ComputeLocation |
        EWaterBodyQueryFlags::ComputeNormal |
        EWaterBodyQueryFlags::IncludeWaves;

    FWaterBodyQueryResult CompleteQuery;
    CompleteQuery.SetQueryFlags(CompleteFlags);
    CompleteQuery.SetWaterSurfaceLocation(FVector(10.0, 20.0, 125.0));
    CompleteQuery.SetWaterSurfaceNormal(FVector(0.0, 0.0, 2.0));

    const FShipSurfaceSample Waves = ResolveWaterSurfaceSample(
        true, true, true, &CompleteQuery, TOptional<FShipSurfaceSample>(), 15.0);
    TestEqual(TEXT("waves classified"), Waves.State, EShipWaterState::ValidWaves);
    TestEqual(TEXT("waves surface Z"), Waves.SurfaceZ, 125.0);
    TestTrue(TEXT("waves normal normalized"),
        Waves.Normal.Equals(FVector::UpVector, 1e-6));
    TestFalse(TEXT("waves do not use fallback"), Waves.bUsedFallback);

    const FShipSurfaceSample NoWaves = ResolveWaterSurfaceSample(
        true, true, false, &CompleteQuery, TOptional<FShipSurfaceSample>(), 15.0);
    TestEqual(TEXT("no-waves classified"),
        NoWaves.State, EShipWaterState::ValidNoWaves);
    TestFalse(TEXT("no-waves do not use fallback"), NoWaves.bUsedFallback);

    const FVector LastNormal = FVector(0.0, 1.0, 1.0).GetSafeNormal();
    const TOptional<FShipSurfaceSample> LastValid(FShipSurfaceSample{
        EShipWaterState::ValidWaves, 88.0, LastNormal, false});

    FWaterBodyQueryResult ExcludedQuery = CompleteQuery;
    ExcludedQuery.SetIsInExclusionVolume(true);
    const FShipSurfaceSample Excluded = ResolveWaterSurfaceSample(
        true, true, true, &ExcludedQuery, LastValid, 15.0);
    TestEqual(TEXT("exclusion classified"),
        Excluded.State, EShipWaterState::Excluded);
    TestTrue(TEXT("exclusion uses fallback"), Excluded.bUsedFallback);
    TestEqual(TEXT("exclusion preserves last Z"), Excluded.SurfaceZ, 88.0);
    TestTrue(TEXT("exclusion preserves last normal"),
        Excluded.Normal.Equals(LastNormal, 1e-6));

    FWaterBodyQueryResult MissingFlagsQuery;
    MissingFlagsQuery.SetQueryFlags(EWaterBodyQueryFlags::ComputeLocation);
    MissingFlagsQuery.SetWaterSurfaceLocation(FVector(0.0, 0.0, 50.0));
    const FShipSurfaceSample MissingFlags = ResolveWaterSurfaceSample(
        true, true, true, &MissingFlagsQuery, LastValid, 15.0);
    TestEqual(TEXT("missing flags invalid"),
        MissingFlags.State, EShipWaterState::QueryInvalid);

    FWaterBodyQueryResult NonFiniteLocationQuery = CompleteQuery;
    NonFiniteLocationQuery.SetWaterSurfaceLocation(FVector(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 50.0));
    const FShipSurfaceSample NonFiniteLocation = ResolveWaterSurfaceSample(
        true, true, true, &NonFiniteLocationQuery, LastValid, 15.0);
    TestEqual(TEXT("non-finite location invalid"),
        NonFiniteLocation.State, EShipWaterState::QueryInvalid);

    FWaterBodyQueryResult LowNormalQuery = CompleteQuery;
    LowNormalQuery.SetWaterSurfaceNormal(FVector(1.0, 0.0, 0.05));
    const FShipSurfaceSample LowNormal = ResolveWaterSurfaceSample(
        true, true, true, &LowNormalQuery, LastValid, 15.0);
    TestEqual(TEXT("low normal invalid"),
        LowNormal.State, EShipWaterState::QueryInvalid);

    const FShipSurfaceSample MissingSubsystem = ResolveWaterSurfaceSample(
        false, true, true, &CompleteQuery, LastValid, 15.0);
    TestEqual(TEXT("missing subsystem invalid component"),
        MissingSubsystem.State, EShipWaterState::ComponentInvalid);
    const FShipSurfaceSample MissingComponent = ResolveWaterSurfaceSample(
        true, false, true, &CompleteQuery, LastValid, 15.0);
    TestEqual(TEXT("missing component invalid"),
        MissingComponent.State, EShipWaterState::ComponentInvalid);

    const FShipSurfaceSample FirstFailure = ResolveWaterSurfaceSample(
        false, false, false, nullptr, TOptional<FShipSurfaceSample>(), 42.0);
    TestEqual(TEXT("first failure keeps actor Z"), FirstFailure.SurfaceZ, 42.0);
    TestTrue(TEXT("first failure uses world up"),
        FirstFailure.Normal.Equals(FVector::UpVector, 1e-6));
    TestTrue(TEXT("first failure marks fallback"), FirstFailure.bUsedFallback);

    return !HasAnyErrors();
}

bool FShipSurfaceBasisTest::RunTest(const FString&)
{
    const auto TestBasis = [this](
        const TCHAR* Label,
        double ExpectedYawDegrees,
        const FShipSurfaceBasis& Basis)
    {
        const double ActualYawDegrees = FMath::RadiansToDegrees(
            FMath::Atan2(Basis.Forward.Y, Basis.Forward.X));
        TestTrue(
            *FString::Printf(TEXT("%s preserves yaw"), Label),
            FMath::Abs(FRotator::NormalizeAxis(
                ActualYawDegrees - ExpectedYawDegrees)) <= 0.01);
        TestTrue(
            *FString::Printf(TEXT("%s forward unit"), Label),
            FMath::Abs(Basis.Forward.Size() - 1.0) <= 1e-5);
        TestTrue(
            *FString::Printf(TEXT("%s right unit"), Label),
            FMath::Abs(Basis.Right.Size() - 1.0) <= 1e-5);
        TestTrue(
            *FString::Printf(TEXT("%s up unit"), Label),
            FMath::Abs(Basis.Up.Size() - 1.0) <= 1e-5);
        TestTrue(
            *FString::Printf(TEXT("%s forward right orthogonal"), Label),
            FMath::Abs(FVector::DotProduct(Basis.Forward, Basis.Right)) <= 1e-5);
        TestTrue(
            *FString::Printf(TEXT("%s forward up orthogonal"), Label),
            FMath::Abs(FVector::DotProduct(Basis.Forward, Basis.Up)) <= 1e-5);
        TestTrue(
            *FString::Printf(TEXT("%s right up orthogonal"), Label),
            FMath::Abs(FVector::DotProduct(Basis.Right, Basis.Up)) <= 1e-5);
        TestTrue(
            *FString::Printf(TEXT("%s right handed"), Label),
            FVector::DotProduct(
                FVector::CrossProduct(Basis.Forward, Basis.Right), Basis.Up) >=
                0.99999);
    };

    const FShipSurfaceBasis ZeroYaw = BuildShipSurfaceBasis(
        0.0,
        FVector(1.0, 1.0, 1.0).GetSafeNormal(),
        TOptional<FVector>());
    TestFalse(TEXT("valid zero-yaw normal does not fallback"),
        ZeroYaw.bUsedFallback);
    TestBasis(TEXT("zero yaw slope"), 0.0, ZeroYaw);

    const FShipSurfaceBasis FortyFiveYaw = BuildShipSurfaceBasis(
        45.0,
        FVector(0.0, 1.0, 1.0).GetSafeNormal(),
        TOptional<FVector>());
    TestFalse(TEXT("valid forty-five normal does not fallback"),
        FortyFiveYaw.bUsedFallback);
    TestBasis(TEXT("forty-five yaw slope"), 45.0, FortyFiveYaw);

    const FVector LastValidNormal = FVector(0.0, 1.0, 1.0).GetSafeNormal();
    const FShipSurfaceBasis LowNormalFallback = BuildShipSurfaceBasis(
        30.0, FVector(1.0, 0.0, 0.05), LastValidNormal);
    TestTrue(TEXT("low normal uses fallback"), LowNormalFallback.bUsedFallback);
    TestTrue(TEXT("low normal uses last valid normal"),
        LowNormalFallback.Up.Equals(LastValidNormal, 1e-5));
    TestBasis(TEXT("low normal fallback"), 30.0, LowNormalFallback);

    const FShipSurfaceBasis ZeroFallback = BuildShipSurfaceBasis(
        -20.0, FVector::ZeroVector, TOptional<FVector>());
    TestTrue(TEXT("zero normal uses fallback"), ZeroFallback.bUsedFallback);
    TestTrue(TEXT("zero normal uses world up"),
        ZeroFallback.Up.Equals(FVector::UpVector, 1e-5));
    TestBasis(TEXT("zero normal fallback"), -20.0, ZeroFallback);

    const FShipSurfaceBasis NonFiniteFallback = BuildShipSurfaceBasis(
        75.0,
        FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0),
        LastValidNormal);
    TestTrue(TEXT("non-finite normal uses fallback"),
        NonFiniteFallback.bUsedFallback);
    TestTrue(TEXT("non-finite fallback finite"),
        FMath::IsFinite(NonFiniteFallback.Forward.X) &&
        FMath::IsFinite(NonFiniteFallback.Forward.Y) &&
        FMath::IsFinite(NonFiniteFallback.Forward.Z) &&
        FMath::IsFinite(NonFiniteFallback.Right.X) &&
        FMath::IsFinite(NonFiniteFallback.Right.Y) &&
        FMath::IsFinite(NonFiniteFallback.Right.Z) &&
        FMath::IsFinite(NonFiniteFallback.Up.X) &&
        FMath::IsFinite(NonFiniteFallback.Up.Y) &&
        FMath::IsFinite(NonFiniteFallback.Up.Z));
    TestBasis(TEXT("non-finite normal fallback"), 75.0, NonFiniteFallback);

    return !HasAnyErrors();
}

class FScopedShipTestWorld
{
public:
    FScopedShipTestWorld()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        check(World != nullptr && World->GetPhysicsScene() != nullptr);
    }

    ~FScopedShipTestWorld()
    {
        World->DestroyWorld(false);
    }

    UWorld* World = nullptr;
};

struct FShipMovementTestAccessor
{
    static void BeginPlay(UShipMovement& Movement) { Movement.BeginPlay(); }
    static void SetState(UShipMovement& Movement, double Speed, double Yaw)
    {
        Movement.SignedSpeedCmPerSecond = Speed;
        Movement.HorizontalYawDegrees = Yaw;
    }
    static void Tick(UShipMovement& Movement, float DeltaTime)
    {
        Movement.TickComponent(DeltaTime, LEVELTICK_All, nullptr);
    }
    static double Speed(const UShipMovement& Movement)
    {
        return Movement.SignedSpeedCmPerSecond;
    }
    static double Throttle(const UShipMovement& Movement)
    {
        return Movement.ThrottleInput;
    }
    static double Steer(const UShipMovement& Movement)
    {
        return Movement.SteerInput;
    }
    static double Acceleration(const UShipMovement& Movement)
    {
        return Movement.LastAcceleration;
    }
    static bool BlockingHit(const UShipMovement& Movement)
    {
        return Movement.bLastBlockingHit;
    }
    static int32 DebugDrawCalls(const UShipMovement& Movement)
    {
        return Movement.TestDebugDrawInvocationCount;
    }
    static int32 Substeps(const UShipMovement& Movement)
    {
        return Movement.LastSubsteps;
    }
    static double StepSeconds(const UShipMovement& Movement)
    {
        return Movement.LastStepSeconds;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipMovementRuntimeTest,
    "ShipAutonomySim.ShipMovement.Runtime.FallbackAndBlockingHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipTransformOwnershipTest,
    "ShipAutonomySim.ShipMovement.Runtime.TransformOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShipTransformOwnershipTest::RunTest(const FString&)
{
    const FString RuntimeRoot = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir() / TEXT("Source/ShipAutonomySim"));
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(
        Files, *RuntimeRoot, TEXT("*.h"), true, false);
    IFileManager::Get().FindFilesRecursive(
        Files, *RuntimeRoot, TEXT("*.cpp"), true, false);

    const TArray<FString> DeniedMutators = {
        TEXT("SetActorTransform("), TEXT("SetActorLocation("),
        TEXT("SetActorRotation("), TEXT("SetActorLocationAndRotation("),
        TEXT("SetActorScale3D("), TEXT("TeleportTo("),
        TEXT("AddActorWorldOffset("),
        TEXT("AddActorLocalOffset("), TEXT("AddActorWorldRotation("),
        TEXT("AddActorLocalRotation("), TEXT("AddActorWorldTransform("),
        TEXT("AddActorLocalTransform("), TEXT("SetWorldTransform("),
        TEXT("SetWorldLocation("), TEXT("SetWorldRotation("),
        TEXT("SetWorldLocationAndRotation("), TEXT("SetWorldScale3D("),
        TEXT("AddWorldOffset("),
        TEXT("AddLocalOffset("), TEXT("AddWorldRotation("),
        TEXT("AddLocalRotation("), TEXT("AddWorldTransform("),
        TEXT("AddLocalTransform("), TEXT("MoveComponent("),
        TEXT("MoveComponentImpl("), TEXT("SetRelativeTransform("),
        TEXT("SetRelativeLocation("), TEXT("SetRelativeRotation("),
        TEXT("SetRelativeLocationAndRotation("), TEXT("SetRelativeScale3D("),
        TEXT("AddRelativeLocation("), TEXT("AddRelativeRotation("),
        TEXT("AddRelativeTransform("), TEXT("K2_SetWorldLocation("),
        TEXT("K2_SetWorldRotation("), TEXT("K2_SetWorldTransform("),
        TEXT("K2_AddWorldOffset("), TEXT("K2_AddLocalOffset("),
        TEXT("K2_AddWorldRotation("), TEXT("K2_AddLocalRotation(")
    };
    struct FAllowedLine
    {
        FString FileSuffix;
        FString CompactLine;
        int32 ExpectedCount;
        int32 ActualCount = 0;
    };
    TArray<FAllowedLine> Allowed = {
        {TEXT("/Private/ShipMovement.cpp"),
         TEXT("Owner->SetActorLocationAndRotation("), 1},
        {TEXT("/Private/ShipPawn.cpp"),
         TEXT("VisualMesh->SetRelativeScale3D(FVector(2.0,1.0,1.0));"), 1},
        {TEXT("/Private/ShipPawn.cpp"),
         TEXT("CameraBoom->SetRelativeRotation(FRotator(CameraPitchDegrees,0.0,0.0));"), 1}
    };
    const auto Compact = [](FString Value)
    {
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        Value.ReplaceInline(TEXT("\t"), TEXT(""));
        Value.ReplaceInline(TEXT("\r"), TEXT(""));
        Value.ReplaceInline(TEXT("\n"), TEXT(""));
        return Value;
    };

    FString ShipMovementSource;
    for (const FString& File : Files)
    {
        FString NormalizedFile = File;
        NormalizedFile.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (NormalizedFile.Contains(TEXT("/Private/Tests/")))
        {
            continue;
        }
        FString Source;
        if (!TestTrue(*FString::Printf(TEXT("read %s"), *NormalizedFile),
            FFileHelper::LoadFileToString(Source, *File)))
        {
            continue;
        }
        if (NormalizedFile.EndsWith(TEXT("/Private/ShipMovement.cpp")))
        {
            ShipMovementSource = Source;
        }
        TArray<FString> Lines;
        Source.ParseIntoArrayLines(Lines, false);
        for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
        {
            const FString CompactLine = Compact(Lines[LineIndex]);
            for (const FString& Mutator : DeniedMutators)
            {
                if (!CompactLine.Contains(Mutator))
                {
                    continue;
                }
                bool bAllowed = false;
                for (FAllowedLine& Entry : Allowed)
                {
                    if (NormalizedFile.EndsWith(Entry.FileSuffix) &&
                        CompactLine == Entry.CompactLine)
                    {
                        ++Entry.ActualCount;
                        bAllowed = true;
                        break;
                    }
                }
                if (!bAllowed)
                {
                    AddError(FString::Printf(
                        TEXT("transform mutator denied: %s:%d: %s"),
                        *NormalizedFile, LineIndex + 1, *Lines[LineIndex]));
                }
                break;
            }
        }
    }
    for (const FAllowedLine& Entry : Allowed)
    {
        TestEqual(
            *FString::Printf(TEXT("whitelist count %s"), *Entry.CompactLine),
            Entry.ActualCount,
            Entry.ExpectedCount);
    }
    TestTrue(TEXT("only approved swept actor move statement"),
        Compact(ShipMovementSource).Contains(
            TEXT("Owner->SetActorLocationAndRotation(NewLocation,NewRotation,true,&Hit,ETeleportType::None);")));
    return !HasAnyErrors();
}

bool FShipMovementRuntimeTest::RunTest(const FString&)
{
    FScopedShipTestWorld TestWorld;

    AActor* Ship = TestWorld.World->SpawnActor<AActor>();
    TestNotNull(TEXT("ship spawned"), Ship);
    if (!Ship)
    {
        return false;
    }

    UBoxComponent* ShipRoot = NewObject<UBoxComponent>(Ship);
    Ship->SetRootComponent(ShipRoot);
    Ship->AddInstanceComponent(ShipRoot);
    ShipRoot->SetBoxExtent(FVector(10.0));
    ShipRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ShipRoot->SetCollisionObjectType(ECC_Pawn);
    ShipRoot->SetCollisionResponseToAllChannels(ECR_Ignore);
    ShipRoot->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    ShipRoot->RegisterComponent();

    UShipMovement* Movement = NewObject<UShipMovement>(Ship);
    Ship->AddInstanceComponent(Movement);
    Movement->RegisterComponent();
    FShipMovementTestAccessor::BeginPlay(*Movement);

    AActor* Blocker = TestWorld.World->SpawnActor<AActor>();
    TestNotNull(TEXT("blocker spawned"), Blocker);
    if (!Blocker)
    {
        return false;
    }

    UBoxComponent* BlockerRoot = NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(BlockerRoot);
    Blocker->AddInstanceComponent(BlockerRoot);
    BlockerRoot->SetBoxExtent(FVector(10.0));
    BlockerRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BlockerRoot->SetCollisionObjectType(ECC_WorldStatic);
    BlockerRoot->SetCollisionResponseToAllChannels(ECR_Ignore);
    BlockerRoot->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    BlockerRoot->RegisterComponent();

    FShipMovementTestAccessor::SetState(*Movement, 120.0, 0.0);
    const double StartZ = Ship->GetActorLocation().Z;
    Blocker->SetActorLocation(FVector(1000.0, 0.0, StartZ));
    FShipMovementTestAccessor::Tick(*Movement, 1.0f / 120.0f);
    TestTrue(
        TEXT("fallback moves horizontally"), Ship->GetActorLocation().X > 0.0);
    TestTrue(TEXT("fallback preserves Z"),
        FMath::Abs(Ship->GetActorLocation().Z - StartZ) <= 1e-6);
    TestEqual(TEXT("fallback tick draws debug exactly once"),
        FShipMovementTestAccessor::DebugDrawCalls(*Movement), 1);

    Ship->SetActorLocation(FVector(0.0, 0.0, StartZ));
    Blocker->SetActorLocation(FVector(25.0, 0.0, StartZ));
    FShipMovementTestAccessor::SetState(*Movement, 200.0, 0.0);
    FShipMovementTestAccessor::Tick(*Movement, 1.0f / 15.0f);
    TestTrue(TEXT("blocking hit recorded"),
        FShipMovementTestAccessor::BlockingHit(*Movement));
    TestEqual(TEXT("blocking hit stops speed"),
        FShipMovementTestAccessor::Speed(*Movement), 0.0);
    TestTrue(
        TEXT("sweep prevents penetration"), Ship->GetActorLocation().X <= 5.01);
    const int32 BeforeInvalid =
        FShipMovementTestAccessor::DebugDrawCalls(*Movement);
    FShipMovementTestAccessor::Tick(
        *Movement, std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("invalid tick draws debug exactly once"),
        FShipMovementTestAccessor::DebugDrawCalls(*Movement), BeforeInvalid + 1);

    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPawnConstructionTest,
    "ShipAutonomySim.ShipMovement.Pawn.Construction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPawnInputLifecycleTest,
    "ShipAutonomySim.ShipMovement.Pawn.InputLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPawnFocusLossTest,
    "ShipAutonomySim.ShipMovement.Pawn.FocusLossAndAutopilotGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

class FScopedShipInputWorld
{
public:
    static constexpr float InputFrameSeconds = 1.0f / 60.0f;

    FScopedShipInputWorld()
    {
        GameInstance = NewObject<UGameInstance>(GEngine);
        GameInstance->AddToRoot();
        const FName WorldName = MakeUniqueObjectName(
            GetTransientPackage(),
            UWorld::StaticClass(),
            TEXT("ShipInputAutomationWorld"));
        GameInstance->InitializeStandalone(WorldName, GetTransientPackage());
        World = GameInstance->GetWorld();
        check(World != nullptr && World->SetGameMode(FURL()));

        LocalPlayer = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
        check(GameInstance->AddLocalPlayer(LocalPlayer, 0) == 0);
        World->InitializeActorsForPlay(FURL());
        FString SpawnError;
        check(LocalPlayer->SpawnPlayActor(TEXT(""), SpawnError, World));
        Controller = LocalPlayer->GetPlayerController(World);
        Subsystem =
            LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
        check(Controller != nullptr && Subsystem != nullptr);
        check(Cast<UEnhancedPlayerInput>(Controller->PlayerInput) != nullptr);
        check(Cast<UEnhancedInputComponent>(Controller->InputComponent) != nullptr);
    }

    ~FScopedShipInputWorld()
    {
        if (World != nullptr && World->HasBegunPlay())
        {
            World->EndPlay(EEndPlayReason::Quit);
        }
        if (GameInstance != nullptr)
        {
            GameInstance->Shutdown();
            GameInstance->RemoveFromRoot();
        }
        if (World != nullptr)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    void StartPlay()
    {
        if (!World->HasBegunPlay())
        {
            World->BeginPlay();
        }
    }

    AShipPawn& PossessShip()
    {
        StartPlay();
        AShipPawn* Ship = Cast<AShipPawn>(Controller->GetPawn());
        if (Ship == nullptr)
        {
            Ship = World->SpawnActor<AShipPawn>();
            Controller->Possess(Ship);
        }
        check(Ship != nullptr && Ship->HasActorBegunPlay());
        check(Cast<UEnhancedInputComponent>(Ship->InputComponent) != nullptr);
        FModifyContextOptions RebuildOptions;
        RebuildOptions.bForceImmediately = true;
        Subsystem->RequestRebuildControlMappings(
            RebuildOptions,
            EInputMappingRebuildType::Rebuild);
        TickInput();
        return *Ship;
    }

    void Press(const FKey& Key)
    {
        Controller->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0));
        TickInput();
    }

    void QueueRelease(const FKey& Key)
    {
        Controller->InputKey(FInputKeyParams(Key, IE_Released, 0.0));
    }

    void Release(const FKey& Key)
    {
        QueueRelease(Key);
        TickInput();
    }

    void TickInput()
    {
        Controller->PlayerTick(InputFrameSeconds);
    }

    UGameInstance* GameInstance = nullptr;
    UWorld* World = nullptr;
    ULocalPlayer* LocalPlayer = nullptr;
    APlayerController* Controller = nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
};

struct FShipPawnTestAccessor
{
    static UShipMovement& Movement(AShipPawn& Pawn)
    {
        return *Pawn.ShipMovement;
    }
    static UInputMappingContext& Mapping(AShipPawn& Pawn)
    {
        return *Pawn.ManualControlMapping;
    }
    static int32 MappingRemovalCount(const AShipPawn& Pawn)
    {
        return Pawn.TestManualMappingRemovalCount;
    }
    static int32 ThrottleCompletedCount(const AShipPawn& Pawn)
    {
        return Pawn.TestThrottleCompletedCount;
    }
    static int32 SteerCanceledCount(const AShipPawn& Pawn)
    {
        return Pawn.TestSteerCanceledCount;
    }
    static void ConfigureSteerAsOngoingHold(
        AShipPawn& Pawn,
        UEnhancedInputLocalPlayerSubsystem& Subsystem)
    {
        UInputTriggerHold* Hold =
            NewObject<UInputTriggerHold>(Pawn.ManualControlMapping);
        Hold->HoldTimeThreshold = 10.0f;
        bool bConfigured = false;
        const int32 MappingCount = Pawn.ManualControlMapping->GetMappings().Num();
        for (int32 MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
        {
            FEnhancedActionKeyMapping& Mapping =
                Pawn.ManualControlMapping->GetMapping(MappingIndex);
            if (Mapping.Action == Pawn.SteerAction && Mapping.Key == EKeys::D)
            {
                Mapping.Triggers.Add(Hold);
                bConfigured = true;
                break;
            }
        }
        check(bConfigured);
        FModifyContextOptions Options;
        Options.bForceImmediately = true;
        Subsystem.RequestRebuildControlMappings(Options);
    }
    static void DeactivateForAutopilot(AShipPawn& Pawn)
    {
        Pawn.DeactivateManualInput();
    }
};

bool FShipPawnConstructionTest::RunTest(const FString&)
{
    FScopedShipTestWorld TestWorld;
    AShipPawn* Pawn = TestWorld.World->SpawnActor<AShipPawn>();
    TestNotNull(TEXT("ship pawn spawned"), Pawn);
    if (!Pawn)
    {
        return false;
    }

    UBoxComponent* Root = Cast<UBoxComponent>(Pawn->GetRootComponent());
    TestNotNull(TEXT("box collision root"), Root);
    if (Root)
    {
        TestTrue(TEXT("hull half extents are 100 50 50"),
            Root->GetUnscaledBoxExtent().Equals(FVector(100.0, 50.0, 50.0)));
        TestEqual(TEXT("hull query-only collision"),
            Root->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
        TestFalse(TEXT("hull physics disabled"), Root->IsSimulatingPhysics());
        TestFalse(TEXT("hull gravity disabled"), Root->IsGravityEnabled());
    }

    UStaticMeshComponent* Visual =
        Pawn->FindComponentByClass<UStaticMeshComponent>();
    TestNotNull(TEXT("visual mesh exists"), Visual);
    if (Visual)
    {
        TestTrue(TEXT("visual mesh scale is 2 1 1"),
            Visual->GetRelativeScale3D().Equals(FVector(2.0, 1.0, 1.0)));
        TestEqual(TEXT("visual collision disabled"),
            Visual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
        TestFalse(TEXT("visual physics disabled"), Visual->IsSimulatingPhysics());
    }

    TestNotNull(TEXT("movement component exists"),
        Pawn->FindComponentByClass<UShipMovement>());
    TestNotNull(TEXT("spring arm exists"),
        Pawn->FindComponentByClass<USpringArmComponent>());
    TestNotNull(TEXT("camera exists"),
        Pawn->FindComponentByClass<UCameraComponent>());
    return !HasAnyErrors();
}

bool FShipPawnInputLifecycleTest::RunTest(const FString&)
{
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        TestEqual(TEXT("four WASD mappings"),
            FShipPawnTestAccessor::Mapping(Pawn).GetMappings().Num(), 4);
        TestTrue(TEXT("context registered"),
            Input.Subsystem->HasMappingContext(
                &FShipPawnTestAccessor::Mapping(Pawn)));
        Input.Press(EKeys::W);
        TestEqual(TEXT("Triggered forwards throttle"),
            FShipMovementTestAccessor::Throttle(
                FShipPawnTestAccessor::Movement(Pawn)),
            1.0);
        Input.Release(EKeys::W);
        TestEqual(TEXT("Completed resets throttle"),
            FShipMovementTestAccessor::Throttle(
                FShipPawnTestAccessor::Movement(Pawn)),
            0.0);
        TestEqual(TEXT("Completed handler count"),
            FShipPawnTestAccessor::ThrottleCompletedCount(Pawn), 1);

        Input.Press(EKeys::W);
        Input.Controller->UnPossess();
        TestEqual(TEXT("UnPossessed resets throttle"),
            FShipMovementTestAccessor::Throttle(
                FShipPawnTestAccessor::Movement(Pawn)),
            0.0);
        TestEqual(TEXT("UnPossessed removes context once"),
            FShipPawnTestAccessor::MappingRemovalCount(Pawn), 1);
        TestFalse(TEXT("context removed"),
            Input.Subsystem->HasMappingContext(
                &FShipPawnTestAccessor::Mapping(Pawn)));
    }
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        FShipPawnTestAccessor::ConfigureSteerAsOngoingHold(
            Pawn, *Input.Subsystem);
        Input.Press(EKeys::D);
        Input.Release(EKeys::D);
        TestEqual(TEXT("Canceled binding executed"),
            FShipPawnTestAccessor::SteerCanceledCount(Pawn), 1);
        Input.Controller->UnPossess();
        TestEqual(TEXT("Canceled fixture lifecycle removes context once"),
            FShipPawnTestAccessor::MappingRemovalCount(Pawn), 1);
    }
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        Input.Press(EKeys::W);
        Pawn.Destroy();
        TestEqual(TEXT("EndPlay resets throttle"),
            FShipMovementTestAccessor::Throttle(
                FShipPawnTestAccessor::Movement(Pawn)),
            0.0);
        TestEqual(TEXT("EndPlay removes context once"),
            FShipPawnTestAccessor::MappingRemovalCount(Pawn), 1);
    }
    return true;
}

bool FShipPawnFocusLossTest::RunTest(const FString&)
{
    FString InputConfig;
    TestTrue(TEXT("DefaultInput.ini readable"), FFileHelper::LoadFileToString(
        InputConfig,
        *(FPaths::ProjectConfigDir() / TEXT("DefaultInput.ini"))));
    TestTrue(TEXT("enhanced player input"), InputConfig.Contains(
        TEXT("DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput")));
    TestTrue(TEXT("enhanced component"), InputConfig.Contains(
        TEXT("DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent")));
    TestTrue(TEXT("focus loss requests pressed-key flush"), InputConfig.Contains(
        TEXT("bShouldFlushPressedKeysOnViewportFocusLost=True")));

    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        UShipMovement& Movement = FShipPawnTestAccessor::Movement(Pawn);
        Input.Press(EKeys::W);
        TestTrue(TEXT("W produces non-zero throttle before flush"),
            FShipMovementTestAccessor::Throttle(Movement) > 0.99);
        Input.Press(EKeys::A);
        TestTrue(TEXT("A produces non-zero steer before flush"),
            FShipMovementTestAccessor::Steer(Movement) < -0.99);
        FShipMovementTestAccessor::SetState(Movement, 100.0, 0.0);
        Input.Controller->FlushPressedKeys();
        Input.TickInput();
        const float MovementDeltaTime = 1.0f / 120.0f;
        FShipMovementTestAccessor::Tick(Movement, MovementDeltaTime);
        TestEqual(TEXT("flush clears throttle"),
            FShipMovementTestAccessor::Throttle(Movement), 0.0);
        TestEqual(TEXT("flush clears steer"),
            FShipMovementTestAccessor::Steer(Movement), 0.0);
        const FShipMotionParameters Parameters =
            FShipMotionParameters::Defaults();
        const int32 MovementSubsteps =
            FShipMovementTestAccessor::Substeps(Movement);
        const double MovementStepSeconds =
            FShipMovementTestAccessor::StepSeconds(Movement);
        FShipMotionState ExpectedState{100.0, 0.0};
        double ExpectedDrag = 0.0;
        for (int32 StepIndex = 0; StepIndex < MovementSubsteps; ++StepIndex)
        {
            const FShipMotionStep Step = AdvanceShipMotion(
                ExpectedState,
                MakeShipMotionInput(0.0, 0.0),
                Parameters,
                MovementStepSeconds);
            check(Step.bValid);
            ExpectedDrag = Step.AccelerationCmPerSecondSquared;
            ExpectedState = Step.NextState;
        }
        TestTrue(TEXT("next motion step is drag only"),
            FMath::Abs(
                FShipMovementTestAccessor::Acceleration(Movement) -
                ExpectedDrag) <= 1e-8);
    }
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        UShipMovement& Movement = FShipPawnTestAccessor::Movement(Pawn);
        FShipPawnTestAccessor::ConfigureSteerAsOngoingHold(
            Pawn, *Input.Subsystem);
        Input.Press(EKeys::W);
        Input.Press(EKeys::D);
        const int32 CompletedBefore =
            FShipPawnTestAccessor::ThrottleCompletedCount(Pawn);
        const int32 CanceledBefore =
            FShipPawnTestAccessor::SteerCanceledCount(Pawn);
        Input.QueueRelease(EKeys::W);
        Input.QueueRelease(EKeys::D);
        FShipPawnTestAccessor::DeactivateForAutopilot(Pawn);
        Movement.SetThrottle(0.65f);
        Movement.SetSteer(-0.25f);
        Input.TickInput();
        TestEqual(TEXT("late Completed was delivered"),
            FShipPawnTestAccessor::ThrottleCompletedCount(Pawn),
            CompletedBefore + 1);
        TestEqual(TEXT("late Canceled was delivered"),
            FShipPawnTestAccessor::SteerCanceledCount(Pawn),
            CanceledBefore + 1);
        TestTrue(TEXT("late release preserves autopilot throttle"),
            FMath::Abs(
                FShipMovementTestAccessor::Throttle(Movement) - 0.65) <= 1e-6);
        TestTrue(TEXT("late release preserves autopilot steer"),
            FMath::Abs(
                FShipMovementTestAccessor::Steer(Movement) + 0.25) <= 1e-6);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
