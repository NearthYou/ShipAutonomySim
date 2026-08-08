#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Misc/AutomationTest.h"
#include "ShipNavigationSimulation.h"
#include "ShipNavigationTypes.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
