#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Components/StaticMeshComponent.h"
#include "CourseBuilder.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCourseRuntimeSetupFailureTest,
    "ShipAutonomySim.ShipNavigation.Unit.Course.RuntimeSetupFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

class FScopedCourseTestWorld
{
public:
    FScopedCourseTestWorld()
    {
        GameInstance = NewObject<UGameInstance>(GEngine);
        GameInstance->AddToRoot();
        const FName WorldName = MakeUniqueObjectName(
            GetTransientPackage(),
            UWorld::StaticClass(),
            TEXT("ShipCourseAutomationWorld"));
        GameInstance->InitializeStandalone(WorldName, GetTransientPackage());
        World = GameInstance->GetWorld();
        check(World != nullptr && World->SetGameMode(FURL()));
        World->InitializeActorsForPlay(FURL());
    }

    ~FScopedCourseTestWorld()
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

    UGameInstance* GameInstance = nullptr;
    UWorld* World = nullptr;
};

struct FCourseBuilderTestAccessor
{
    static FVector LastWaterQueryLocation(const ACourseBuilder& Builder)
    {
        return Builder.TestLastWaterQueryLocation;
    }

    static int32 RandomCourseLogCount(const ACourseBuilder& Builder)
    {
        return Builder.TestRandomCourseLogCount;
    }

    static void SetWaterSurfaceOverride(
        ACourseBuilder& Builder,
        double WaterSurfaceZCm)
    {
        Builder.TestWaterSurfaceOverrideCm = WaterSurfaceZCm;
    }
};

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

bool FShipCourseRuntimeSetupFailureTest::RunTest(const FString&)
{
    double SurfaceZCm = 123.0;
    EShipSetupFailure Failure = EShipSetupFailure::None;
    TestFalse(TEXT("exclusion rejects finite water surface"),
        ValidateWaterReference(true, 50.0, SurfaceZCm, Failure));
    TestEqual(TEXT("exclusion has priority"),
        Failure, EShipSetupFailure::WaterLocationExcluded);
    TestEqual(TEXT("exclusion clears output surface"), SurfaceZCm, 0.0);

    SurfaceZCm = 123.0;
    Failure = EShipSetupFailure::None;
    TestFalse(TEXT("non-finite water surface rejected"),
        ValidateWaterReference(
            false,
            std::numeric_limits<double>::quiet_NaN(),
            SurfaceZCm,
            Failure));
    TestEqual(TEXT("non-finite water failure"),
        Failure, EShipSetupFailure::WaterSurfaceUnavailable);
    TestEqual(TEXT("non-finite clears output surface"), SurfaceZCm, 0.0);

    SurfaceZCm = 0.0;
    Failure = EShipSetupFailure::WaterSurfaceUnavailable;
    TestTrue(TEXT("finite non-excluded water succeeds"),
        ValidateWaterReference(false, 75.0, SurfaceZCm, Failure));
    TestEqual(TEXT("finite water clears failure"),
        Failure, EShipSetupFailure::None);
    TestEqual(TEXT("finite water preserves surface"), SurfaceZCm, 75.0);

    FScopedCourseTestWorld TestWorld;
    const FTransform RandomBuilderTransform(
        FRotator(20.0, 90.0, 15.0),
        FVector(100.0, 200.0, 900.0),
        FVector(2.0, 3.0, 4.0));
    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACourseBuilder* RandomBuilder = TestWorld.World->SpawnActor<ACourseBuilder>(
        ACourseBuilder::StaticClass(),
        RandomBuilderTransform,
        SpawnParameters);
    TestNotNull(TEXT("random course builder spawned"), RandomBuilder);
    if (RandomBuilder == nullptr)
    {
        return false;
    }

    FShipCourseBuildResult RandomResult;
    Failure = EShipSetupFailure::None;
    TestFalse(TEXT("world without Ocean cannot build course"),
        RandomBuilder->BuildRuntimeCourse(RandomResult, Failure));
    TestEqual(TEXT("missing Ocean classification"),
        Failure, EShipSetupFailure::WaterSurfaceUnavailable);
    TestNull(TEXT("failed build has no start target"),
        RandomBuilder->GetStartTarget());
    TestNull(TEXT("failed build has no end target"),
        RandomBuilder->GetEndTarget());
    TestNull(TEXT("failed build has no wall"),
        RandomBuilder->GetWallActor());
    TestEqual(TEXT("failed build has no path"),
        RandomBuilder->GetWorldPath().Num(), 0);
    TestNull(TEXT("failed result has no start target"), RandomResult.StartTarget);
    TestNull(TEXT("failed result has no end target"), RandomResult.EndTarget);
    TestNull(TEXT("failed result has no wall"), RandomResult.WallActor);
    TestEqual(TEXT("failed result has no path"), RandomResult.WorldPath.Num(), 0);
    TestEqual(TEXT("result and getter expose same random seed"),
        RandomResult.RandomSeed,
        RandomBuilder->GetResolvedRandomSeed());
    TestNotEqual(TEXT("absent slide uses time-derived seed"),
        RandomResult.RandomSeed, 0);
    TestTrue(TEXT("random slide stays in configured range"),
        RandomResult.SlideCm >= -500.0 && RandomResult.SlideCm <= 500.0);
    TestTrue(TEXT("result and getter expose same random slide"),
        FMath::IsNearlyEqual(
            RandomResult.SlideCm,
            RandomBuilder->GetResolvedSlideCm(),
            1e-9));
    TestEqual(TEXT("random course marker logged once"),
        FCourseBuilderTestAccessor::RandomCourseLogCount(*RandomBuilder), 1);

    const FShipCourseDefinition RandomPlanarDefinition = BuildCourseDefinition(
        RandomBuilder->GetActorTransform(),
        0.0,
        RandomResult.SlideCm);
    const FVector QueryLocation =
        FCourseBuilderTestAccessor::LastWaterQueryLocation(*RandomBuilder);
    TestTrue(TEXT("water query uses transformed wall X"),
        FMath::IsNearlyEqual(
            QueryLocation.X,
            RandomPlanarDefinition.WallWorld.X,
            1e-6));
    TestTrue(TEXT("water query uses transformed wall Y"),
        FMath::IsNearlyEqual(
            QueryLocation.Y,
            RandomPlanarDefinition.WallWorld.Y,
            1e-6));

    ACourseBuilder* ForcedBuilder = TestWorld.World->SpawnActor<ACourseBuilder>();
    TestNotNull(TEXT("forced course builder spawned"), ForcedBuilder);
    if (ForcedBuilder == nullptr)
    {
        return false;
    }
    ForcedBuilder->SetForcedSlideCm(250.0);
    FCourseBuilderTestAccessor::SetWaterSurfaceOverride(*ForcedBuilder, 75.0);
    FShipCourseBuildResult ForcedResult;
    Failure = EShipSetupFailure::WaterSurfaceUnavailable;
    TestTrue(TEXT("forced course builds with validated test water"),
        ForcedBuilder->BuildRuntimeCourse(ForcedResult, Failure));
    TestEqual(TEXT("forced course clears failure"),
        Failure, EShipSetupFailure::None);
    TestEqual(TEXT("forced course uses seed zero"), ForcedResult.RandomSeed, 0);
    TestEqual(TEXT("forced course does not log random marker"),
        FCourseBuilderTestAccessor::RandomCourseLogCount(*ForcedBuilder), 0);
    TestEqual(TEXT("forced slide preserved"), ForcedResult.SlideCm, 250.0);
    TestEqual(TEXT("forced water surface preserved"),
        ForcedResult.WaterSurfaceZCm, 75.0);
    TestEqual(TEXT("forced path has exactly three points"),
        ForcedResult.WorldPath.Num(), 3);
    TestTrue(TEXT("start getter matches result"),
        ForcedBuilder->GetStartTarget() == ForcedResult.StartTarget);
    TestTrue(TEXT("end getter matches result"),
        ForcedBuilder->GetEndTarget() == ForcedResult.EndTarget);
    TestTrue(TEXT("wall getter matches result"),
        ForcedBuilder->GetWallActor() == ForcedResult.WallActor);
    TestEqual(TEXT("path getter matches result size"),
        ForcedBuilder->GetWorldPath().Num(), ForcedResult.WorldPath.Num());
    if (ForcedResult.WorldPath.Num() == 3)
    {
        TestTrue(TEXT("start target uses path start"),
            ForcedResult.StartTarget != nullptr
            && ForcedResult.StartTarget->GetActorLocation().Equals(
                ForcedResult.WorldPath[0], 1e-6));
        TestTrue(TEXT("end target uses path end"),
            ForcedResult.EndTarget != nullptr
            && ForcedResult.EndTarget->GetActorLocation().Equals(
                ForcedResult.WorldPath[2], 1e-6));
    }
    if (ForcedResult.WallActor != nullptr)
    {
        UStaticMeshComponent* WallMesh =
            ForcedResult.WallActor->GetStaticMeshComponent();
        TestNotNull(TEXT("wall mesh component exists"), WallMesh);
        if (WallMesh != nullptr)
        {
            TestNotNull(
                TEXT("wall cube mesh assigned"),
                WallMesh->GetStaticMesh().Get());
            TestEqual(TEXT("wall is static"),
                WallMesh->Mobility, EComponentMobility::Static);
            TestEqual(TEXT("wall collision is query only"),
                WallMesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
            TestEqual(TEXT("wall object type is world static"),
                WallMesh->GetCollisionObjectType(), ECC_WorldStatic);
            TestEqual(TEXT("wall blocks pawn"),
                WallMesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
            TestEqual(TEXT("wall ignores world dynamic"),
                WallMesh->GetCollisionResponseToChannel(ECC_WorldDynamic),
                ECR_Ignore);
        }
    }

    TArray<AActor*> TargetsBefore;
    TArray<AActor*> WallsBefore;
    UGameplayStatics::GetAllActorsOfClass(
        TestWorld.World, ATargetPoint::StaticClass(), TargetsBefore);
    UGameplayStatics::GetAllActorsOfClass(
        TestWorld.World, AStaticMeshActor::StaticClass(), WallsBefore);
    FShipCourseBuildResult DuplicateResult;
    Failure = EShipSetupFailure::None;
    TestFalse(TEXT("repeated build is rejected"),
        ForcedBuilder->BuildRuntimeCourse(DuplicateResult, Failure));
    TestEqual(TEXT("repeated build classification"),
        Failure, EShipSetupFailure::CourseSpawnFailed);
    TArray<AActor*> TargetsAfter;
    TArray<AActor*> WallsAfter;
    UGameplayStatics::GetAllActorsOfClass(
        TestWorld.World, ATargetPoint::StaticClass(), TargetsAfter);
    UGameplayStatics::GetAllActorsOfClass(
        TestWorld.World, AStaticMeshActor::StaticClass(), WallsAfter);
    TestEqual(TEXT("repeated build creates no targets"),
        TargetsAfter.Num(), TargetsBefore.Num());
    TestEqual(TEXT("repeated build creates no walls"),
        WallsAfter.Num(), WallsBefore.Num());
    TestTrue(TEXT("repeated build preserves start reference"),
        ForcedBuilder->GetStartTarget() == ForcedResult.StartTarget);
    TestTrue(TEXT("repeated build preserves end reference"),
        ForcedBuilder->GetEndTarget() == ForcedResult.EndTarget);
    TestTrue(TEXT("repeated build preserves wall reference"),
        ForcedBuilder->GetWallActor() == ForcedResult.WallActor);

    ForcedBuilder->ClearForcedSlide();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipConvexHullGapTest,
    "ShipAutonomySim.ShipNavigation.Unit.Geometry.ConvexHullGap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipTerminalPriorityTest,
    "ShipAutonomySim.ShipNavigation.Unit.Terminal.Priority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipRuntimeCalculationErrorLatchTest,
    "ShipAutonomySim.ShipNavigation.Unit.Terminal.RuntimeCalculationErrorLatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShipConvexHullGapTest::RunTest(const FString&)
{
    const FBox UnitBox(FVector(-1.0), FVector(1.0));
    struct FCase
    {
        const TCHAR* Label;
        FBox FirstBox;
        FTransform FirstTransform;
        FBox SecondBox;
        FTransform SecondTransform;
        double ExpectedGapCm;
    };
    const TArray<FCase> Cases{
        {
            TEXT("axis aligned separation"),
            UnitBox,
            FTransform::Identity,
            UnitBox,
            FTransform(FVector(5.0, 0.0, 0.0)),
            3.0,
        },
        {
            TEXT("yaw rotated ship"),
            FBox(FVector(-2.0, -1.0, -1.0), FVector(2.0, 1.0, 1.0)),
            FTransform(FRotator(0.0, 45.0, 0.0)),
            UnitBox,
            FTransform(FVector(6.0, 0.0, 0.0)),
            2.8786796564,
        },
        {
            TEXT("scaled wall"),
            UnitBox,
            FTransform::Identity,
            UnitBox,
            FTransform(
                FRotator::ZeroRotator,
                FVector(6.0, 0.0, 0.0),
                FVector(2.0, 3.0, 4.0)),
            3.0,
        },
        {
            TEXT("full pitch roll yaw and nonuniform scale"),
            UnitBox,
            FTransform(
                FRotator(90.0, 90.0, 90.0),
                FVector::ZeroVector,
                FVector(2.0, 3.0, 4.0)),
            UnitBox,
            FTransform(FVector(10.0, 0.0, 0.0)),
            5.0,
        },
        {
            TEXT("touching edge"),
            UnitBox,
            FTransform::Identity,
            UnitBox,
            FTransform(FVector(2.0, 0.0, 0.0)),
            0.0,
        },
        {
            TEXT("overlapping hulls"),
            UnitBox,
            FTransform::Identity,
            UnitBox,
            FTransform(FVector(1.0, 0.0, 0.0)),
            0.0,
        },
    };

    for (const FCase& Case : Cases)
    {
        double GapCm = -1.0;
        TestTrue(
            *FString::Printf(TEXT("%s gap calculation"), Case.Label),
            ComputeConvexHullGapCm(
                Case.FirstBox,
                Case.FirstTransform,
                Case.SecondBox,
                Case.SecondTransform,
                GapCm));
        TestTrue(
            *FString::Printf(TEXT("%s gap"), Case.Label),
            FMath::IsNearlyEqual(GapCm, Case.ExpectedGapCm, 1e-6));
        TestTrue(
            *FString::Printf(TEXT("%s gap non-negative"), Case.Label),
            GapCm >= 0.0);
    }

    TArray<FVector2D> Corners;
    TransformBoxCornersToXY(
        UnitBox,
        FTransform(
            FRotator(90.0, 90.0, 90.0),
            FVector::ZeroVector,
            FVector(2.0, 3.0, 4.0)),
        Corners);
    TestEqual(TEXT("all local corners are transformed"), Corners.Num(), 8);
    TArray<FVector2D> Hull;
    TestTrue(TEXT("transformed corners build a hull"),
        BuildConvexHullXY(Corners, Hull));
    TestEqual(TEXT("axis-permuted cube hull has four corners"), Hull.Num(), 4);
    double MinimumX = TNumericLimits<double>::Max();
    double MaximumX = TNumericLimits<double>::Lowest();
    double MinimumY = TNumericLimits<double>::Max();
    double MaximumY = TNumericLimits<double>::Lowest();
    for (const FVector2D& Point : Hull)
    {
        MinimumX = FMath::Min(MinimumX, Point.X);
        MaximumX = FMath::Max(MaximumX, Point.X);
        MinimumY = FMath::Min(MinimumY, Point.Y);
        MaximumY = FMath::Max(MaximumY, Point.Y);
    }
    TestTrue(TEXT("pitch roll yaw maps scaled Z to world X"),
        FMath::IsNearlyEqual(MinimumX, -4.0, 1e-6)
        && FMath::IsNearlyEqual(MaximumX, 4.0, 1e-6));
    TestTrue(TEXT("pitch roll yaw maps scaled Y to world Y"),
        FMath::IsNearlyEqual(MinimumY, -3.0, 1e-6)
        && FMath::IsNearlyEqual(MaximumY, 3.0, 1e-6));

    return !HasAnyErrors();
}

bool FShipTerminalPriorityTest::RunTest(const FString&)
{
    for (int32 Mask = 0; Mask < 8; ++Mask)
    {
        const bool bCollision = (Mask & 1) != 0;
        const bool bSuccess = (Mask & 2) != 0;
        const bool bTimeout = (Mask & 4) != 0;
        const EShipRunResult Expected = bCollision
            ? EShipRunResult::Collision
            : bSuccess
                ? EShipRunResult::Success
                : bTimeout
                    ? EShipRunResult::Timeout
                    : EShipRunResult::Running;
        TestEqual(
            *FString::Printf(TEXT("terminal priority mask %d"), Mask),
            SelectTerminalResult(FShipTerminalInputs{
                bCollision,
                bSuccess,
                bTimeout,
                false}),
            Expected);
    }

    TestEqual(TEXT("runtime error blocks success"),
        SelectTerminalResult(FShipTerminalInputs{false, true, false, true}),
        EShipRunResult::Running);
    TestEqual(TEXT("runtime error preserves timeout"),
        SelectTerminalResult(FShipTerminalInputs{false, true, true, true}),
        EShipRunResult::Timeout);
    TestEqual(TEXT("runtime error preserves collision"),
        SelectTerminalResult(FShipTerminalInputs{true, true, true, true}),
        EShipRunResult::Collision);

    return !HasAnyErrors();
}

bool FShipRuntimeCalculationErrorLatchTest::RunTest(const FString&)
{
    FShipRuntimeErrorState State;
    TestFalse(TEXT("None does not change latch"),
        LatchRuntimeCalculationError(
            EShipRuntimeCalculationError::None, State));
    TestFalse(TEXT("None leaves latch clear"), State.bLatched);
    TestEqual(TEXT("None leaves count zero"), State.ReportCount, 0);

    TestTrue(TEXT("first error changes latch"),
        LatchRuntimeCalculationError(
            EShipRuntimeCalculationError::InvalidHeading, State));
    TestTrue(TEXT("first error latches"), State.bLatched);
    TestEqual(TEXT("first error is preserved"),
        State.FirstError,
        EShipRuntimeCalculationError::InvalidHeading);
    TestEqual(TEXT("first error count"), State.ReportCount, 1);

    TestFalse(TEXT("same error does not relatch"),
        LatchRuntimeCalculationError(
            EShipRuntimeCalculationError::InvalidHeading, State));
    TestEqual(TEXT("same error increments count"), State.ReportCount, 2);
    TestEqual(TEXT("same error preserves first"),
        State.FirstError,
        EShipRuntimeCalculationError::InvalidHeading);

    TestFalse(TEXT("different error does not relatch"),
        LatchRuntimeCalculationError(
            EShipRuntimeCalculationError::InvalidThrottle, State));
    TestEqual(TEXT("different error increments count"), State.ReportCount, 3);
    TestEqual(TEXT("different error preserves first"),
        State.FirstError,
        EShipRuntimeCalculationError::InvalidHeading);
    TestEqual(TEXT("latched error keeps success blocked"),
        SelectTerminalResult(FShipTerminalInputs{
            false,
            true,
            false,
            State.bLatched}),
        EShipRunResult::Running);

    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
