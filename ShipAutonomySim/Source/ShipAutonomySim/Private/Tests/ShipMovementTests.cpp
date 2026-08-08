#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Misc/AutomationTest.h"
#include "ShipMovementSimulation.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
