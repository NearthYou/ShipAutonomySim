#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Misc/AutomationTest.h"
#include "ShipNavigationSimulation.h"
#include "ShipNavigationTypes.h"
#include "ShipMovementSimulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FStage4SlideOptionClassificationTest,
    "ShipAutonomySim.ShipNavigation.Unit.Options.Classification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCourseGeometryTest,
    "ShipAutonomySim.ShipNavigation.Unit.Course.Geometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCourseFrameTransformTest,
    "ShipAutonomySim.ShipNavigation.Unit.Course.FrameTransform",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStage4SlideOptionClassificationTest::RunTest(const FString&)
{
    struct FCase
    {
        const TCHAR* Label;
        bool bHasOption;
        const TCHAR* RawValue;
        EStage4SlideOptionState ExpectedState;
        EShipSetupFailure ExpectedFailure;
        double ExpectedSlideCm;
    };

    const TArray<FCase> Cases{
        {TEXT("absent"), false, TEXT(""), EStage4SlideOptionState::Absent,
            EShipSetupFailure::None, 0.0},
        {TEXT("empty"), true, TEXT(""), EStage4SlideOptionState::Empty,
            EShipSetupFailure::SlideOptionEmpty, 0.0},
        {TEXT("whitespace"), true, TEXT("  \t"), EStage4SlideOptionState::Empty,
            EShipSetupFailure::SlideOptionEmpty, 0.0},
        {TEXT("junk"), true, TEXT("west"), EStage4SlideOptionState::Malformed,
            EShipSetupFailure::SlideOptionMalformed, 0.0},
        {TEXT("incomplete exponent"), true, TEXT("1e"),
            EStage4SlideOptionState::Malformed,
            EShipSetupFailure::SlideOptionMalformed, 0.0},
        {TEXT("named NaN"), true, TEXT("NaN"), EStage4SlideOptionState::NonFinite,
            EShipSetupFailure::SlideOptionNonFinite, 0.0},
        {TEXT("positive infinity"), true, TEXT("+Inf"),
            EStage4SlideOptionState::NonFinite,
            EShipSetupFailure::SlideOptionNonFinite, 0.0},
        {TEXT("negative infinity"), true, TEXT("-infinity"),
            EStage4SlideOptionState::NonFinite,
            EShipSetupFailure::SlideOptionNonFinite, 0.0},
        {TEXT("below range"), true, TEXT("-501"),
            EStage4SlideOptionState::OutOfRange,
            EShipSetupFailure::SlideOptionOutOfRange, -501.0},
        {TEXT("above range"), true, TEXT("501"),
            EStage4SlideOptionState::OutOfRange,
            EShipSetupFailure::SlideOptionOutOfRange, 501.0},
        {TEXT("lower boundary"), true, TEXT("-500"),
            EStage4SlideOptionState::Valid, EShipSetupFailure::None, -500.0},
        {TEXT("zero"), true, TEXT("0"), EStage4SlideOptionState::Valid,
            EShipSetupFailure::None, 0.0},
        {TEXT("upper boundary"), true, TEXT("500"),
            EStage4SlideOptionState::Valid, EShipSetupFailure::None, 500.0},
        {TEXT("trimmed decimal"), true, TEXT(" 125.5 "),
            EStage4SlideOptionState::Valid, EShipSetupFailure::None, 125.5},
        {TEXT("decimal exponent"), true, TEXT("-1.25e2"),
            EStage4SlideOptionState::Valid, EShipSetupFailure::None, -125.0},
    };

    for (const FCase& Case : Cases)
    {
        const FStage4SlideOptionResult Result =
            ClassifySlideOption(Case.bHasOption, Case.RawValue);
        TestEqual(
            *FString::Printf(TEXT("%s state"), Case.Label),
            Result.State,
            Case.ExpectedState);
        TestEqual(
            *FString::Printf(TEXT("%s failure"), Case.Label),
            Result.Failure,
            Case.ExpectedFailure);
        TestTrue(
            *FString::Printf(TEXT("%s slide"), Case.Label),
            FMath::IsNearlyEqual(Result.SlideCm, Case.ExpectedSlideCm, 1e-9));
        TestEqual(
            *FString::Printf(TEXT("%s random eligibility"), Case.Label),
            Result.State == EStage4SlideOptionState::Absent,
            !Case.bHasOption);
        if (Case.bHasOption && Case.ExpectedState != EStage4SlideOptionState::Valid)
        {
            TestFalse(
                *FString::Printf(TEXT("%s invalid option cannot fallback"), Case.Label),
                Result.State == EStage4SlideOptionState::Absent);
        }
    }

    return !HasAnyErrors();
}

bool FShipCourseGeometryTest::RunTest(const FString&)
{
    struct FCase
    {
        double SlideCm;
        double ExpectedWaypointY;
    };
    const TArray<FCase> Cases{
        {-500.0, 250.0},
        {0.0, -750.0},
        {500.0, -250.0},
    };

    for (const FCase& Case : Cases)
    {
        const FShipCourseDefinition Course = BuildCourseDefinition(
            FTransform::Identity,
            50.0,
            Case.SlideCm);
        TestTrue(TEXT("start uses reference surface"),
            Course.StartWorld.Equals(FVector(0.0, 0.0, 50.0), 1e-6));
        TestTrue(TEXT("end uses reference surface"),
            Course.EndWorld.Equals(FVector(2000.0, 0.0, 50.0), 1e-6));
        TestTrue(TEXT("wall center uses slide and immersion"),
            Course.WallWorld.Equals(
                FVector(1000.0, Case.SlideCm, 200.0), 1e-6));
        TestTrue(TEXT("waypoint uses shorter-side sign"),
            Course.WaypointWorld.Equals(
                FVector(1000.0, Case.ExpectedWaypointY, 50.0), 1e-6));
        TestTrue(TEXT("cube scale is one ten five"),
            Course.WallScale.Equals(FVector(1.0, 10.0, 5.0), 1e-6));
        TestTrue(TEXT("cube dimensions are one hundred one thousand five hundred"),
            (Course.WallScale * 100.0).Equals(
                FVector(100.0, 1000.0, 500.0), 1e-6));
        TestTrue(TEXT("wall bottom is one hundred below surface"),
            FMath::IsNearlyEqual(
                Course.WallWorld.Z - Course.WallScale.Z * 50.0,
                -50.0,
                1e-6));
        TestEqual(TEXT("course has exactly three path points"),
            Course.WorldPath.Num(), 3);
        if (Course.WorldPath.Num() == 3)
        {
            TestTrue(TEXT("path starts at start"),
                Course.WorldPath[0].Equals(Course.StartWorld, 1e-6));
            TestTrue(TEXT("path passes waypoint"),
                Course.WorldPath[1].Equals(Course.WaypointWorld, 1e-6));
            TestTrue(TEXT("path ends at end"),
                Course.WorldPath[2].Equals(Course.EndWorld, 1e-6));
        }
    }

    return !HasAnyErrors();
}

bool FShipCourseFrameTransformTest::RunTest(const FString&)
{
    const FTransform CourseActorTransform(
        FRotator(25.0, 90.0, 30.0),
        FVector(100.0, 200.0, 700.0),
        FVector(2.0, 3.0, 4.0));
    const FShipCourseDefinition Course = BuildCourseDefinition(
        CourseActorTransform,
        25.0,
        100.0);

    TestTrue(TEXT("translation applies to start XY only"),
        Course.StartWorld.Equals(FVector(100.0, 200.0, 25.0), 1e-6));
    TestTrue(TEXT("yaw rotates end without actor scale"),
        Course.EndWorld.Equals(FVector(100.0, 2200.0, 25.0), 1e-6));
    TestTrue(TEXT("yaw rotates waypoint without pitch or roll"),
        Course.WaypointWorld.Equals(FVector(750.0, 1200.0, 25.0), 1e-6));
    TestTrue(TEXT("wall uses flat frame and reference height"),
        Course.WallWorld.Equals(FVector(0.0, 1200.0, 175.0), 1e-6));

    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPathProgressMonotonicityTest,
    "ShipAutonomySim.ShipNavigation.Unit.Guidance.ProgressMonotonicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPathSegmentTransitionTest,
    "ShipAutonomySim.ShipNavigation.Unit.Guidance.SegmentTransition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipLookaheadTest,
    "ShipAutonomySim.ShipNavigation.Unit.Guidance.Lookahead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipSteeringAndThrottleTest,
    "ShipAutonomySim.ShipNavigation.Unit.Guidance.SteeringAndThrottle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDynamicStoppingDistanceTest,
    "ShipAutonomySim.ShipNavigation.Unit.Guidance.DynamicStoppingDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
TArray<FVector> MakeRightAnglePath()
{
    return {
        FVector(0.0, 0.0, 15.0),
        FVector(1000.0, 0.0, 15.0),
        FVector(1000.0, 1000.0, 15.0)};
}
}

bool FShipPathProgressMonotonicityTest::RunTest(const FString&)
{
    const TArray<FVector> Path = MakeRightAnglePath();
    FShipPathProgress Progress;
    TestTrue(TEXT("active segment projection succeeds"),
        AdvancePathProgress(
            Path,
            FVector(400.0, 0.0, 900.0),
            FShipPathProgress{0, 600.0},
            Progress));
    TestEqual(TEXT("progress stays on active segment"),
        Progress.ActiveSegmentIndex, 0);
    TestTrue(TEXT("progress never decreases"),
        FMath::IsNearlyEqual(Progress.MonotonicDistanceCm, 600.0, 1e-6));

    TestTrue(TEXT("future-near position projects current segment"),
        AdvancePathProgress(
            Path,
            FVector(900.0, 800.0, -300.0),
            FShipPathProgress{0, 600.0},
            Progress));
    TestEqual(TEXT("future-near position cannot jump segment"),
        Progress.ActiveSegmentIndex, 0);
    TestTrue(TEXT("future-near progress uses current segment only"),
        FMath::IsNearlyEqual(Progress.MonotonicDistanceCm, 900.0, 1e-6));

    return !HasAnyErrors();
}

bool FShipPathSegmentTransitionTest::RunTest(const FString&)
{
    const TArray<FVector> Path = MakeRightAnglePath();
    FShipPathProgress FirstTick;
    TestTrue(TEXT("endpoint plane transition succeeds"),
        AdvancePathProgress(
            Path,
            FVector(1100.0, 500.0, 15.0),
            FShipPathProgress{0, 900.0},
            FirstTick));
    TestEqual(TEXT("one tick advances exactly one segment"),
        FirstTick.ActiveSegmentIndex, 1);
    TestTrue(TEXT("transition commits first segment length"),
        FMath::IsNearlyEqual(FirstTick.MonotonicDistanceCm, 1000.0, 1e-6));

    FShipPathProgress SecondTick;
    TestTrue(TEXT("next tick projects new segment"),
        AdvancePathProgress(
            Path,
            FVector(1100.0, 500.0, 15.0),
            FirstTick,
            SecondTick));
    TestEqual(TEXT("second tick remains on final segment"),
        SecondTick.ActiveSegmentIndex, 1);
    TestTrue(TEXT("second tick adds final segment projection"),
        FMath::IsNearlyEqual(SecondTick.MonotonicDistanceCm, 1500.0, 1e-6));

    return !HasAnyErrors();
}

bool FShipLookaheadTest::RunTest(const FString&)
{
    const TArray<FVector> Path = MakeRightAnglePath();
    FVector Target = FVector::ZeroVector;
    TestTrue(TEXT("lookahead crosses segment boundary"),
        FindLookaheadTarget(Path, FShipPathProgress{0, 800.0}, 300.0, Target));
    TestTrue(TEXT("lookahead follows polyline arc"),
        Target.Equals(FVector(1000.0, 100.0, 15.0), 1e-6));

    TestTrue(TEXT("lookahead near end succeeds"),
        FindLookaheadTarget(Path, FShipPathProgress{1, 1950.0}, 300.0, Target));
    TestTrue(TEXT("lookahead clamps to final endpoint"),
        Target.Equals(FVector(1000.0, 1000.0, 15.0), 1e-6));
    TestFalse(TEXT("negative lookahead is rejected"),
        FindLookaheadTarget(Path, FShipPathProgress{0, 0.0}, -1.0, Target));

    return !HasAnyErrors();
}

bool FShipSteeringAndThrottleTest::RunTest(const FString&)
{
    struct FCase
    {
        const TCHAR* Label;
        FVector Target;
        double ExpectedHeadingDegrees;
        float ExpectedSteer;
        float ExpectedThrottle;
    };
    const TArray<FCase> Cases{
        {TEXT("positive full steer"), FVector(86.6025403784, 50.0, 0.0),
            30.0, 1.0f, 0.8375f},
        {TEXT("negative full steer"), FVector(86.6025403784, -50.0, 0.0),
            -30.0, -1.0f, 0.8375f},
        {TEXT("full throttle boundary"), FVector(93.9692620786, 34.2020143326, 0.0),
            20.0, 2.0f / 3.0f, 1.0f},
        {TEXT("interpolated throttle"), FVector(76.6044443119, 64.2787609687, 0.0),
            40.0, 1.0f, 0.675f},
        {TEXT("minimum throttle boundary"), FVector(50.0, 86.6025403784, 0.0),
            60.0, 1.0f, 0.35f},
    };

    for (const FCase& Case : Cases)
    {
        double HeadingDegrees = 0.0;
        float Steer = 0.0f;
        float Throttle = 0.0f;
        TestTrue(
            *FString::Printf(TEXT("%s command calculation"), Case.Label),
            ComputeGuidanceCommands(
                FVector::ForwardVector,
                FVector::ZeroVector,
                Case.Target,
                30.0,
                20.0,
                60.0,
                0.35,
                HeadingDegrees,
                Steer,
                Throttle));
        TestTrue(
            *FString::Printf(TEXT("%s heading"), Case.Label),
            FMath::IsNearlyEqual(
                HeadingDegrees, Case.ExpectedHeadingDegrees, 1e-6));
        TestTrue(
            *FString::Printf(TEXT("%s steer"), Case.Label),
            FMath::IsNearlyEqual(Steer, Case.ExpectedSteer, 1e-6f));
        TestTrue(
            *FString::Printf(TEXT("%s throttle"), Case.Label),
            FMath::IsNearlyEqual(Throttle, Case.ExpectedThrottle, 1e-6f));
    }

    double HeadingDegrees = 0.0;
    float Steer = 0.0f;
    float Throttle = 0.0f;
    TestFalse(TEXT("non-finite tuning is rejected"),
        ComputeGuidanceCommands(
            FVector::ForwardVector,
            FVector::ZeroVector,
            FVector::RightVector,
            std::numeric_limits<double>::quiet_NaN(),
            20.0,
            60.0,
            0.35,
            HeadingDegrees,
            Steer,
            Throttle));
    TestFalse(TEXT("descending throttle thresholds are rejected"),
        ComputeGuidanceCommands(
            FVector::ForwardVector,
            FVector::ZeroVector,
            FVector::RightVector,
            30.0,
            60.0,
            20.0,
            0.35,
            HeadingDegrees,
            Steer,
            Throttle));

    return !HasAnyErrors();
}

bool FShipDynamicStoppingDistanceTest::RunTest(const FString&)
{
    double DistanceCm = -1.0;
    TestTrue(TEXT("default drag stopping simulation succeeds"),
        ComputeDynamicStoppingDistance(
            FShipMotionParameters::Defaults(), 200.0, DistanceCm));
    TestTrue(TEXT("default drag stopping distance matches Stage 3"),
        FMath::Abs(DistanceCm - 399.9615) <= 0.1);

    TestTrue(TEXT("speed at stop threshold succeeds"),
        ComputeDynamicStoppingDistance(
            FShipMotionParameters::Defaults(), 5.0, DistanceCm));
    TestEqual(TEXT("speed at stop threshold needs no distance"),
        DistanceCm, 0.0);
    TestFalse(TEXT("negative initial speed is rejected"),
        ComputeDynamicStoppingDistance(
            FShipMotionParameters::Defaults(), -1.0, DistanceCm));
    TestFalse(TEXT("non-finite initial speed is rejected"),
        ComputeDynamicStoppingDistance(
            FShipMotionParameters::Defaults(),
            std::numeric_limits<double>::infinity(),
            DistanceCm));

    FShipMotionParameters NoDeceleration = FShipMotionParameters::Defaults();
    NoDeceleration.LinearDragCoeff = 0.0;
    NoDeceleration.QuadraticDragCoeff = 0.0;
    TestFalse(TEXT("non-decelerating step is rejected"),
        ComputeDynamicStoppingDistance(NoDeceleration, 100.0, DistanceCm));

    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
