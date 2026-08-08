#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "CourseBuilder.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "ShipNavigationSimulation.h"
#include "ShipNavigationTypes.h"
#include "ShipMovement.h"
#include "ShipMovementSimulation.h"
#include "ShipNavigator.h"
#include "ShipPawn.h"
#include "SimGameMode.h"

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

    void AddPlayer()
    {
        LocalPlayer = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
        check(GameInstance->AddLocalPlayer(LocalPlayer, 0) == 0);
        FString SpawnError;
        check(LocalPlayer->SpawnPlayActor(TEXT(""), SpawnError, World));
        Controller = LocalPlayer->GetPlayerController(World);
        check(Controller != nullptr);
    }

    UGameInstance* GameInstance = nullptr;
    UWorld* World = nullptr;
    ULocalPlayer* LocalPlayer = nullptr;
    APlayerController* Controller = nullptr;
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

struct FShipNavigatorTestAccessor
{
    static void Tick(UShipNavigator& Navigator, float DeltaTime)
    {
        Navigator.TickComponent(DeltaTime, LEVELTICK_All, nullptr);
    }

    static void SetShipLocationOverride(
        UShipNavigator& Navigator,
        const FVector& Location)
    {
        Navigator.TestShipLocationOverride = Location;
    }

    static void SetForwardSpeedOverride(
        UShipNavigator& Navigator,
        double SpeedCmPerSecond)
    {
        Navigator.TestForwardSpeedOverride = SpeedCmPerSecond;
    }

    static void CorruptPath(UShipNavigator& Navigator)
    {
        Navigator.WorldPath.Reset();
    }

    static const FShipPathProgress& Progress(const UShipNavigator& Navigator)
    {
        return Navigator.Progress;
    }

    static bool CoastLatched(const UShipNavigator& Navigator)
    {
        return Navigator.bCoastLatched;
    }

    static float LastThrottleCommand(const UShipNavigator& Navigator)
    {
        return Navigator.TestLastThrottleCommand;
    }

    static float LastSteerCommand(const UShipNavigator& Navigator)
    {
        return Navigator.TestLastSteerCommand;
    }
};

struct FShipNavigationGameModeTestAccessor
{
    static const FShipRuntimeErrorState& RuntimeErrorState(
        const ASimGameMode& GameMode)
    {
        return GameMode.RuntimeErrorState;
    }

    static void SetWaterSurfaceOverride(
        ASimGameMode& GameMode,
        double WaterSurfaceZCm)
    {
        GameMode.TestWaterSurfaceOverrideCm = WaterSurfaceZCm;
    }

    static bool UsesRandomSlide(const ASimGameMode& GameMode)
    {
        return GameMode.bUseRandomSlide;
    }

    static TOptional<double> ForcedSlide(const ASimGameMode& GameMode)
    {
        return GameMode.ForcedSlideCm;
    }

    static int32 SetupFailureLogCount(const ASimGameMode& GameMode)
    {
        return GameMode.TestSetupFailureLogCount;
    }

    static int32 TerminalLogCount(const ASimGameMode& GameMode)
    {
        return GameMode.TestTerminalLogCount;
    }

    static int32 RuntimeErrorLogCount(const ASimGameMode& GameMode)
    {
        return GameMode.TestRuntimeErrorLogCount;
    }

    static int32 EnterAutonomyCallCount(const ASimGameMode& GameMode)
    {
        return GameMode.TestEnterAutonomyCallCount;
    }

    static void ResetTerminalState(ASimGameMode& GameMode)
    {
        GameMode.RunResult = EShipRunResult::Running;
        GameMode.RuntimeErrorState = FShipRuntimeErrorState{};
        GameMode.bTerminalLogged = false;
        GameMode.TestTerminalLogCount = 0;
    }

    static void ApplyTerminalInputs(
        ASimGameMode& GameMode,
        bool bCollision,
        bool bSuccess,
        bool bTimeout)
    {
        const EShipRunResult Candidate = SelectTerminalResult(
            FShipTerminalInputs{
                bCollision,
                bSuccess,
                bTimeout,
                GameMode.RuntimeErrorState.bLatched});
        GameMode.LatchTerminalResult(Candidate);
    }
};

class FNavigatorTestFixture
{
public:
    FNavigatorTestFixture()
    {
        Owner = World.World->SpawnActor<AActor>();
        check(Owner != nullptr);
        USceneComponent* Root = NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Root);
        Owner->AddInstanceComponent(Root);
        Root->RegisterComponent();

        Movement = NewObject<UShipMovement>(Owner, TEXT("TestShipMovement"));
        Navigator = NewObject<UShipNavigator>(Owner, TEXT("TestShipNavigator"));
        Owner->AddInstanceComponent(Movement);
        Owner->AddInstanceComponent(Navigator);
        Movement->RegisterComponent();
        Navigator->RegisterComponent();

        Wall = World.World->SpawnActor<AStaticMeshActor>();
        RunOwner = Cast<ASimGameMode>(World.World->GetAuthGameMode());
        check(Movement != nullptr && Navigator != nullptr);
        check(Wall != nullptr && RunOwner != nullptr);
    }

    FScopedCourseTestWorld World;
    AActor* Owner = nullptr;
    UShipMovement* Movement = nullptr;
    UShipNavigator* Navigator = nullptr;
    AStaticMeshActor* Wall = nullptr;
    ASimGameMode* RunOwner = nullptr;
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
    FShipNavigatorControlCoastAndErrorTest,
    "ShipAutonomySim.ShipNavigation.Unit.Navigator.ControlCoastAndError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShipNavigatorControlCoastAndErrorTest::RunTest(const FString&)
{
    const TArray<FVector> Path{
        FVector(0.0, 0.0, 0.0),
        FVector(1000.0, 0.0, 0.0),
        FVector(2000.0, 0.0, 0.0)};

    {
        FNavigatorTestFixture Fixture;
        Fixture.Navigator->SetNavigationEnabled(true);
        TestFalse(TEXT("enable before configure is rejected"),
            Fixture.Navigator->IsNavigationEnabled());
        TestFalse(TEXT("invalid path configure rejected"),
            Fixture.Navigator->Configure(
                TArray<FVector>{Path[0], Path[1]},
                Fixture.Movement,
                Fixture.Wall,
                Fixture.RunOwner));
        TestTrue(TEXT("valid three point configure succeeds"),
            Fixture.Navigator->Configure(
                Path,
                Fixture.Movement,
                Fixture.Wall,
                Fixture.RunOwner));
        TestFalse(TEXT("configure alone does not enable navigation"),
            Fixture.Navigator->IsNavigationEnabled());

        bool bNavigatorPrerequisiteFound = false;
        for (const FTickPrerequisite& Prerequisite :
            Fixture.Movement->PrimaryComponentTick.GetPrerequisites())
        {
            bNavigatorPrerequisiteFound = bNavigatorPrerequisiteFound
                || Prerequisite.PrerequisiteObject.Get() == Fixture.Navigator;
        }
        TestTrue(TEXT("Movement waits for Navigator tick"),
            bNavigatorPrerequisiteFound);

        Fixture.Navigator->SetNavigationEnabled(true);
        TestTrue(TEXT("configured Navigator can enable"),
            Fixture.Navigator->IsNavigationEnabled());
        const FTransform TransformBefore = Fixture.Owner->GetActorTransform();
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        TestTrue(TEXT("straight path commands full throttle"),
            FMath::IsNearlyEqual(
                FShipNavigatorTestAccessor::LastThrottleCommand(
                    *Fixture.Navigator),
                1.0f,
                1e-6f));
        TestTrue(TEXT("straight path commands zero steer"),
            FMath::IsNearlyEqual(
                FShipNavigatorTestAccessor::LastSteerCommand(
                    *Fixture.Navigator),
                0.0f,
                1e-6f));
        TestTrue(TEXT("Navigator never mutates owner transform"),
            Fixture.Owner->GetActorTransform().Equals(TransformBefore));

        FShipNavigatorTestAccessor::SetForwardSpeedOverride(
            *Fixture.Navigator, 100.0);
        FShipNavigatorTestAccessor::SetShipLocationOverride(
            *Fixture.Navigator, FVector(1000.0, 0.0, 0.0));
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        TestEqual(TEXT("progress enters final segment"),
            FShipNavigatorTestAccessor::Progress(*Fixture.Navigator)
                .ActiveSegmentIndex,
            1);
        FShipNavigatorTestAccessor::SetShipLocationOverride(
            *Fixture.Navigator, FVector(1990.0, 0.0, 0.0));
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        TestTrue(TEXT("coast latches near endpoint"),
            FShipNavigatorTestAccessor::CoastLatched(*Fixture.Navigator));
        TestTrue(TEXT("coast commands zero throttle"),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastThrottleCommand(
                    *Fixture.Navigator),
                1e-6f));
        FShipNavigatorTestAccessor::SetShipLocationOverride(
            *Fixture.Navigator, FVector(1200.0, 0.0, 0.0));
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        TestTrue(TEXT("coast does not resume throttle"),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastThrottleCommand(
                    *Fixture.Navigator),
                1e-6f));
        TestTrue(TEXT("coast throttle stays non-negative"),
            FShipNavigatorTestAccessor::LastThrottleCommand(
                *Fixture.Navigator) >= 0.0f);
    }

    {
        FNavigatorTestFixture Fixture;
        TestTrue(TEXT("lateral endpoint fixture configures"),
            Fixture.Navigator->Configure(
                Path,
                Fixture.Movement,
                Fixture.Wall,
                Fixture.RunOwner));
        Fixture.Navigator->SetNavigationEnabled(true);
        FShipNavigatorTestAccessor::SetForwardSpeedOverride(
            *Fixture.Navigator, 100.0);
        FShipNavigatorTestAccessor::SetShipLocationOverride(
            *Fixture.Navigator, FVector(1000.0, 0.0, 0.0));
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        FShipNavigatorTestAccessor::SetShipLocationOverride(
            *Fixture.Navigator, FVector(2050.0, 500.0, 0.0));
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        TestTrue(TEXT("lateral endpoint reaches total progress"),
            FMath::IsNearlyEqual(
                FShipNavigatorTestAccessor::Progress(*Fixture.Navigator)
                    .MonotonicDistanceCm,
                2000.0,
                1e-6));
        TestTrue(TEXT("lateral endpoint latches coast on first tick"),
            FShipNavigatorTestAccessor::CoastLatched(*Fixture.Navigator));
        TestTrue(TEXT("lateral endpoint commands zero throttle"),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastThrottleCommand(
                    *Fixture.Navigator),
                1e-6f));
    }

    const auto VerifyRuntimeError = [this, &Path](
        const TCHAR* Label,
        int32 CaseIndex,
        EShipRuntimeCalculationError ExpectedError)
    {
        FNavigatorTestFixture Fixture;
        TestTrue(*FString::Printf(TEXT("%s configure"), Label),
            Fixture.Navigator->Configure(
                Path,
                Fixture.Movement,
                Fixture.Wall,
                Fixture.RunOwner));
        Fixture.Navigator->SetNavigationEnabled(true);
        Fixture.Movement->SetThrottle(0.8f);
        Fixture.Movement->SetSteer(-0.4f);
        if (CaseIndex == 0)
        {
            FShipNavigatorTestAccessor::CorruptPath(*Fixture.Navigator);
        }
        else if (CaseIndex == 1)
        {
            FShipNavigatorTestAccessor::SetShipLocationOverride(
                *Fixture.Navigator,
                FVector(
                    std::numeric_limits<double>::quiet_NaN(),
                    0.0,
                    0.0));
        }
        else
        {
            FShipNavigatorTestAccessor::SetShipLocationOverride(
                *Fixture.Navigator,
                FVector(1000.0, 0.0, 0.0));
            FShipNavigatorTestAccessor::SetForwardSpeedOverride(
                *Fixture.Navigator,
                std::numeric_limits<double>::quiet_NaN());
        }

        AddExpectedError(
            TEXT("Stage4RuntimeCalculationError"),
            EAutomationExpectedErrorFlags::Contains,
            1);
        FShipNavigatorTestAccessor::Tick(*Fixture.Navigator, 1.0f / 60.0f);
        const FShipRuntimeErrorState& RuntimeState =
            FShipNavigationGameModeTestAccessor::RuntimeErrorState(
                *Fixture.RunOwner);
        TestTrue(*FString::Printf(TEXT("%s error latched"), Label),
            RuntimeState.bLatched);
        TestEqual(*FString::Printf(TEXT("%s first error"), Label),
            RuntimeState.FirstError,
            ExpectedError);
        TestEqual(*FString::Printf(TEXT("%s report count"), Label),
            RuntimeState.ReportCount,
            1);
        TestFalse(*FString::Printf(TEXT("%s disables Navigator"), Label),
            Fixture.Navigator->IsNavigationEnabled());
        TestTrue(*FString::Printf(TEXT("%s zeros throttle"), Label),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastThrottleCommand(
                    *Fixture.Navigator),
                1e-6f));
        TestTrue(*FString::Printf(TEXT("%s zeros steer"), Label),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastSteerCommand(
                    *Fixture.Navigator),
                1e-6f));
    };

    VerifyRuntimeError(
        TEXT("invalid path"),
        0,
        EShipRuntimeCalculationError::InvalidNavigationPath);
    VerifyRuntimeError(
        TEXT("non-finite location"),
        1,
        EShipRuntimeCalculationError::InvalidProgressProjection);
    VerifyRuntimeError(
        TEXT("stopping distance"),
        2,
        EShipRuntimeCalculationError::InvalidStoppingDistance);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipGameModeOptionBootstrapTest,
    "ShipAutonomySim.ShipNavigation.Unit.GameMode.OptionBootstrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipGameModeTerminalRuntimeErrorTest,
    "ShipAutonomySim.ShipNavigation.Unit.GameMode.TerminalRuntimeError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShipGameModeOptionBootstrapTest::RunTest(const FString&)
{
    struct FOptionCase
    {
        const TCHAR* Label;
        const TCHAR* Options;
        bool bExpectedRandom;
        EShipSetupFailure ExpectedFailure;
        bool bExpectedForced;
        double ExpectedSlideCm;
    };
    const TArray<FOptionCase> Cases{
        {TEXT("absent"), TEXT(""), true, EShipSetupFailure::None, false, 0.0},
        {TEXT("empty"), TEXT("?Stage4Slide="), false,
            EShipSetupFailure::SlideOptionEmpty, false, 0.0},
        {TEXT("junk"), TEXT("?Stage4Slide=west"), false,
            EShipSetupFailure::SlideOptionMalformed, false, 0.0},
        {TEXT("conversion"), TEXT("?Stage4Slide=1e"), false,
            EShipSetupFailure::SlideOptionMalformed, false, 0.0},
        {TEXT("nan"), TEXT("?Stage4Slide=NaN"), false,
            EShipSetupFailure::SlideOptionNonFinite, false, 0.0},
        {TEXT("inf"), TEXT("?Stage4Slide=Inf"), false,
            EShipSetupFailure::SlideOptionNonFinite, false, 0.0},
        {TEXT("below"), TEXT("?Stage4Slide=-501"), false,
            EShipSetupFailure::SlideOptionOutOfRange, false, -501.0},
        {TEXT("above"), TEXT("?Stage4Slide=501"), false,
            EShipSetupFailure::SlideOptionOutOfRange, false, 501.0},
        {TEXT("lower"), TEXT("?Stage4Slide=-500"), false,
            EShipSetupFailure::None, true, -500.0},
        {TEXT("zero"), TEXT("?Stage4Slide=0"), false,
            EShipSetupFailure::None, true, 0.0},
        {TEXT("upper"), TEXT("?Stage4Slide=500"), false,
            EShipSetupFailure::None, true, 500.0}
    };

    for (const FOptionCase& Case : Cases)
    {
        FScopedCourseTestWorld Fixture;
        ASimGameMode* GameMode =
            Cast<ASimGameMode>(Fixture.World->GetAuthGameMode());
        TestNotNull(*FString::Printf(TEXT("%s GameMode"), Case.Label), GameMode);
        if (GameMode == nullptr)
        {
            continue;
        }
        FString ErrorMessage;
        GameMode->InitGame(TEXT("MainLevel"), Case.Options, ErrorMessage);
        TestEqual(*FString::Printf(TEXT("%s random mode"), Case.Label),
            FShipNavigationGameModeTestAccessor::UsesRandomSlide(*GameMode),
            Case.bExpectedRandom);
        TestEqual(*FString::Printf(TEXT("%s setup failure"), Case.Label),
            GameMode->GetSetupFailure(),
            Case.ExpectedFailure);
        const TOptional<double> ForcedSlide =
            FShipNavigationGameModeTestAccessor::ForcedSlide(*GameMode);
        TestEqual(*FString::Printf(TEXT("%s forced state"), Case.Label),
            ForcedSlide.IsSet(),
            Case.bExpectedForced);
        if (ForcedSlide.IsSet())
        {
            TestTrue(*FString::Printf(TEXT("%s forced value"), Case.Label),
                FMath::IsNearlyEqual(
                    ForcedSlide.GetValue(), Case.ExpectedSlideCm, 1e-9));
        }
        TestNull(*FString::Printf(TEXT("%s builder not spawned in InitGame"), Case.Label),
            GameMode->GetCourseBuilder());
        TestEqual(*FString::Printf(TEXT("%s classification has no error log"), Case.Label),
            FShipNavigationGameModeTestAccessor::SetupFailureLogCount(*GameMode),
            0);
    }

    {
        FScopedCourseTestWorld Fixture;
        ASimGameMode* GameMode =
            Cast<ASimGameMode>(Fixture.World->GetAuthGameMode());
        check(GameMode != nullptr);
        FString ErrorMessage;
        GameMode->InitGame(
            TEXT("MainLevel"), TEXT("?Stage4Slide=west"), ErrorMessage);
        AddExpectedError(
            TEXT("Stage4SetupFailure"),
            EAutomationExpectedErrorFlags::Contains,
            1);
        Fixture.World->BeginPlay();
        TestEqual(TEXT("invalid BeginPlay logs setup failure once"),
            FShipNavigationGameModeTestAccessor::SetupFailureLogCount(
                *GameMode),
            1);
        TestNull(TEXT("invalid BeginPlay spawns no builder"),
            GameMode->GetCourseBuilder());
        TestNull(TEXT("invalid BeginPlay spawns no ship"),
            GameMode->GetRunShip());
    }

    {
        FScopedCourseTestWorld Fixture;
        Fixture.AddPlayer();
        ASimGameMode* GameMode =
            Cast<ASimGameMode>(Fixture.World->GetAuthGameMode());
        check(GameMode != nullptr);
        FString ErrorMessage;
        GameMode->InitGame(
            TEXT("MainLevel"), TEXT("?Stage4Slide=0"), ErrorMessage);
        FShipNavigationGameModeTestAccessor::SetWaterSurfaceOverride(
            *GameMode, 75.0);
        Fixture.World->BeginPlay();
        TestEqual(TEXT("valid BeginPlay has no setup failure"),
            GameMode->GetSetupFailure(), EShipSetupFailure::None);
        TestNotNull(TEXT("valid BeginPlay creates builder"),
            GameMode->GetCourseBuilder());
        TestNotNull(TEXT("valid BeginPlay creates ship"),
            GameMode->GetRunShip());
        TestTrue(TEXT("valid BeginPlay possesses run ship"),
            Fixture.Controller->GetPawn() == GameMode->GetRunShip());
        TestEqual(TEXT("EnterAutonomy called once before first GameMode tick"),
            FShipNavigationGameModeTestAccessor::EnterAutonomyCallCount(
                *GameMode),
            1);
        TestTrue(TEXT("Navigator enabled before BeginPlay returns"),
            GameMode->GetRunShip() != nullptr
            && GameMode->GetRunShip()->GetNavigator() != nullptr
            && GameMode->GetRunShip()->GetNavigator()->IsNavigationEnabled());
        TestEqual(TEXT("forced slide resolved"),
            GameMode->GetResolvedSlideCm(), 0.0);
        TestEqual(TEXT("elapsed starts at zero"),
            GameMode->GetElapsedRunSeconds(), 0.0);
    }
    return !HasAnyErrors();
}

bool FShipGameModeTerminalRuntimeErrorTest::RunTest(const FString&)
{
    {
        FScopedCourseTestWorld Fixture;
        ASimGameMode* GameMode =
            Cast<ASimGameMode>(Fixture.World->GetAuthGameMode());
        check(GameMode != nullptr);

        FShipNavigationGameModeTestAccessor::ResetTerminalState(*GameMode);
        FShipNavigationGameModeTestAccessor::ApplyTerminalInputs(
            *GameMode, true, true, true);
        TestEqual(TEXT("collision wins same-tick priority"),
            GameMode->GetRunResult(), EShipRunResult::Collision);
        FShipNavigationGameModeTestAccessor::ApplyTerminalInputs(
            *GameMode, false, true, false);
        TestEqual(TEXT("terminal result cannot be overwritten"),
            GameMode->GetRunResult(), EShipRunResult::Collision);
        TestEqual(TEXT("terminal result logs once"),
            FShipNavigationGameModeTestAccessor::TerminalLogCount(*GameMode),
            1);

        FShipNavigationGameModeTestAccessor::ResetTerminalState(*GameMode);
        FShipNavigationGameModeTestAccessor::ApplyTerminalInputs(
            *GameMode, false, true, true);
        TestEqual(TEXT("success wins timeout"),
            GameMode->GetRunResult(), EShipRunResult::Success);

        FShipNavigationGameModeTestAccessor::ResetTerminalState(*GameMode);
        FShipNavigationGameModeTestAccessor::ApplyTerminalInputs(
            *GameMode, false, false, true);
        TestEqual(TEXT("timeout latches without higher priority"),
            GameMode->GetRunResult(), EShipRunResult::Timeout);
    }

    {
        FScopedCourseTestWorld Fixture;
        Fixture.AddPlayer();
        ASimGameMode* GameMode =
            Cast<ASimGameMode>(Fixture.World->GetAuthGameMode());
        check(GameMode != nullptr);
        FString ErrorMessage;
        GameMode->InitGame(
            TEXT("MainLevel"), TEXT("?Stage4Slide=0"), ErrorMessage);
        FShipNavigationGameModeTestAccessor::SetWaterSurfaceOverride(
            *GameMode, 75.0);
        Fixture.World->BeginPlay();
        AShipPawn* RunShip = GameMode->GetRunShip();
        TestNotNull(TEXT("runtime error fixture has ship"), RunShip);
        UShipNavigator* Navigator =
            RunShip != nullptr ? RunShip->GetNavigator() : nullptr;
        TestNotNull(TEXT("runtime error fixture has Navigator"), Navigator);
        if (Navigator == nullptr)
        {
            return false;
        }
        FShipNavigatorTestAccessor::Tick(*Navigator, 1.0f / 60.0f);
        AddExpectedError(
            TEXT("Stage4RuntimeCalculationError"),
            EAutomationExpectedErrorFlags::Contains,
            1);
        GameMode->ReportRuntimeCalculationError(
            EShipRuntimeCalculationError::InvalidHeading);
        GameMode->ReportRuntimeCalculationError(
            EShipRuntimeCalculationError::InvalidThrottle);
        TestTrue(TEXT("runtime error remains latched"),
            GameMode->HasRuntimeCalculationError());
        TestEqual(TEXT("first runtime error is preserved"),
            GameMode->GetRuntimeCalculationError(),
            EShipRuntimeCalculationError::InvalidHeading);
        TestEqual(TEXT("every runtime report is counted"),
            GameMode->GetRuntimeCalculationErrorCount(), 2);
        TestEqual(TEXT("runtime error marker logs once"),
            FShipNavigationGameModeTestAccessor::RuntimeErrorLogCount(
                *GameMode),
            1);
        TestFalse(TEXT("runtime error disables Navigator"),
            Navigator->IsNavigationEnabled());
        TestTrue(TEXT("runtime error zeros throttle command"),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastThrottleCommand(*Navigator),
                1e-6f));
        TestTrue(TEXT("runtime error zeros steer command"),
            FMath::IsNearlyZero(
                FShipNavigatorTestAccessor::LastSteerCommand(*Navigator),
                1e-6f));
        FShipNavigationGameModeTestAccessor::ApplyTerminalInputs(
            *GameMode, false, true, false);
        TestEqual(TEXT("runtime error permanently blocks success"),
            GameMode->GetRunResult(), EShipRunResult::Running);
        FShipNavigationGameModeTestAccessor::ApplyTerminalInputs(
            *GameMode, false, true, true);
        TestEqual(TEXT("runtime error still permits timeout"),
            GameMode->GetRunResult(), EShipRunResult::Timeout);
    }
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
