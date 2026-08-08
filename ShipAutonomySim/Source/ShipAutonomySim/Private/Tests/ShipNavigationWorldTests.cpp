#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CourseBuilder.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "ShipNavigationSimulation.h"
#include "ShipPawn.h"
#include "SimGameMode.h"

namespace
{
const TArray<double> ExpectedSlides{
    -500.0,
    -400.0,
    -300.0,
    -200.0,
    -100.0,
    0.0,
    100.0,
    200.0,
    300.0,
    400.0,
    500.0};

struct FStage4CaseResult
{
    double SlideCm = 0.0;
    bool bSuccess = false;
    double ElapsedSeconds = 0.0;
    double MinimumWallDistanceCm = TNumericLimits<double>::Max();
};

UWorld* FindFreshMainLevelWorld(
    int32 PreviousWorldId,
    const TWeakObjectPtr<UWorld>& PreviousWorldIdentity)
{
    if (GEngine == nullptr)
    {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        const TWeakObjectPtr<UWorld> CandidateIdentity(World);
        const bool bSameWorldIdentity =
            World != nullptr
            && World->GetUniqueID() == PreviousWorldId
            && PreviousWorldIdentity.HasSameIndexAndSerialNumber(
                CandidateIdentity);
        if (Context.WorldType == EWorldType::Game
            && World != nullptr
            && !bSameWorldIdentity
            && World->HasBegunPlay()
            && World->GetMapName().Contains(TEXT("MainLevel"))
            && IsValid(World->GetAuthGameMode<ASimGameMode>()))
        {
            return World;
        }
    }
    return nullptr;
}
}

class FRunShipNavigationSweepCommand final : public IAutomationLatentCommand
{
public:
    explicit FRunShipNavigationSweepCommand(FAutomationTestBase* InTest)
        : Test(InTest)
        , OverallDeadlineSeconds(FPlatformTime::Seconds() + 720.0)
        , WorldLoadDeadlineSeconds(FPlatformTime::Seconds() + 15.0)
    {
    }

    virtual bool Update() override
    {
        if (State == EState::Finished)
        {
            return true;
        }

        const double NowSeconds = FPlatformTime::Seconds();
        if (NowSeconds >= OverallDeadlineSeconds)
        {
            RecordFailure(TEXT("Stage4OverallWatchdogTimeout"));
            Finalize();
            return true;
        }

        if (State == EState::WaitingForWorld)
        {
            return UpdateWaitingForWorld(NowSeconds);
        }
        return UpdateObservingCase(NowSeconds);
    }

private:
    enum class EState : uint8
    {
        WaitingForWorld,
        ObservingCase,
        Finished
    };

    bool UpdateWaitingForWorld(double NowSeconds)
    {
        UWorld* World = FindFreshMainLevelWorld(
            PreviousWorldId,
            PreviousWorldIdentity);
        if (World == nullptr)
        {
            if (NowSeconds >= WorldLoadDeadlineSeconds)
            {
                RecordFailure(TEXT("Stage4FreshWorldLoadTimeout"));
                Finalize();
                return true;
            }
            return false;
        }

        ASimGameMode* GameMode = World->GetAuthGameMode<ASimGameMode>();
        if (!IsValid(GameMode))
        {
            return false;
        }
        if (GameMode->GetSetupFailure() != EShipSetupFailure::None)
        {
            ++SetupFailureCount;
            BeginCurrentResult(GameMode);
            RecordFailure(FString::Printf(
                TEXT("Stage4SetupFailure case=%d failure=%d"),
                CurrentCaseIndex,
                static_cast<int32>(GameMode->GetSetupFailure())));
            Results.Add(CurrentResult);
            return StartNextCase();
        }
        if (!IsValid(GameMode->GetCourseBuilder())
            || !IsValid(GameMode->GetRunShip()))
        {
            if (NowSeconds >= WorldLoadDeadlineSeconds)
            {
                ++SetupFailureCount;
                BeginCurrentResult(GameMode);
                RecordFailure(TEXT("Stage4SetupIncomplete"));
                Results.Add(CurrentResult);
                return StartNextCase();
            }
            return false;
        }

        BeginCurrentResult(GameMode);
        const double ExpectedSlide = ExpectedSlides[CurrentCaseIndex];
        if (!FMath::IsNearlyEqual(
                GameMode->GetResolvedSlideCm(),
                ExpectedSlide,
                1e-6))
        {
            RecordFailure(FString::Printf(
                TEXT("Stage4FreshWorldSlideMismatch case=%d expected=%.0f actual=%.3f"),
                CurrentCaseIndex,
                ExpectedSlide,
                GameMode->GetResolvedSlideCm()));
            Results.Add(CurrentResult);
            return StartNextCase();
        }

        CurrentWorld = World;
        CaseDeadlineSeconds = NowSeconds + 60.0;
        State = EState::ObservingCase;
        return false;
    }

    bool UpdateObservingCase(double NowSeconds)
    {
        UWorld* World = CurrentWorld.Get();
        ASimGameMode* GameMode = IsValid(World)
            ? World->GetAuthGameMode<ASimGameMode>()
            : nullptr;
        if (!IsValid(World) || !IsValid(GameMode))
        {
            RecordFailure(TEXT("Stage4ObservedWorldLost"));
            Results.Add(CurrentResult);
            return StartNextCase();
        }
        if (GameMode->GetSetupFailure() != EShipSetupFailure::None)
        {
            ++SetupFailureCount;
            CurrentResult.ElapsedSeconds =
                GameMode->GetElapsedRunSeconds();
            RecordFailure(FString::Printf(
                TEXT("Stage4SetupFailure case=%d failure=%d"),
                CurrentCaseIndex,
                static_cast<int32>(GameMode->GetSetupFailure())));
            Results.Add(CurrentResult);
            return StartNextCase();
        }
        if (GameMode->HasRuntimeCalculationError())
        {
            ++RuntimeCalculationErrorCount;
            CurrentResult.ElapsedSeconds =
                GameMode->GetElapsedRunSeconds();
            RecordFailure(FString::Printf(
                TEXT("Stage4RuntimeCalculationError case=%d error=%d reports=%d"),
                CurrentCaseIndex,
                static_cast<int32>(GameMode->GetRuntimeCalculationError()),
                GameMode->GetRuntimeCalculationErrorCount()));
            Results.Add(CurrentResult);
            return StartNextCase();
        }

        AShipPawn* RunShip = GameMode->GetRunShip();
        ACourseBuilder* Builder = GameMode->GetCourseBuilder();
        AStaticMeshActor* WallActor = IsValid(Builder)
            ? Builder->GetWallActor()
            : nullptr;
        UBoxComponent* ShipCollision = IsValid(RunShip)
            ? RunShip->FindComponentByClass<UBoxComponent>()
            : nullptr;
        UStaticMeshComponent* WallMesh = IsValid(WallActor)
            ? WallActor->GetStaticMeshComponent()
            : nullptr;
        UStaticMesh* WallStaticMesh = WallMesh != nullptr
            ? WallMesh->GetStaticMesh().Get()
            : nullptr;
        if (ShipCollision == nullptr
            || WallMesh == nullptr
            || WallStaticMesh == nullptr)
        {
            RecordFailure(TEXT("Stage4CollisionBoundsUnavailable"));
            Results.Add(CurrentResult);
            return StartNextCase();
        }

        const FVector ShipExtent = ShipCollision->GetUnscaledBoxExtent();
        const FBox ShipLocalBox(-ShipExtent, ShipExtent);
        const FBox WallLocalBox = WallStaticMesh->GetBoundingBox();
        double GapCm = 0.0;
        if (!ComputeConvexHullGapCm(
                ShipLocalBox,
                ShipCollision->GetComponentTransform(),
                WallLocalBox,
                WallMesh->GetComponentTransform(),
                GapCm))
        {
            RecordFailure(TEXT("Stage4HullGapCalculationError"));
            Results.Add(CurrentResult);
            return StartNextCase();
        }
        CurrentResult.MinimumWallDistanceCm = FMath::Min(
            CurrentResult.MinimumWallDistanceCm,
            GapCm);

        const bool bActualWallHit =
            GameMode->GetCollisionActor() == WallActor
            || GameMode->GetCollisionComponent() == WallMesh;
        if (bActualWallHit)
        {
            CurrentResult.MinimumWallDistanceCm = 0.0;
        }

        const EShipRunResult RunResult = GameMode->GetRunResult();
        if (RunResult == EShipRunResult::Running)
        {
            if (NowSeconds >= CaseDeadlineSeconds)
            {
                CurrentResult.ElapsedSeconds =
                    GameMode->GetElapsedRunSeconds();
                RecordFailure(FString::Printf(
                    TEXT("Stage4CaseWatchdogTimeout case=%d"),
                    CurrentCaseIndex));
                Results.Add(CurrentResult);
                return StartNextCase();
            }
            return false;
        }

        CurrentResult.ElapsedSeconds = GameMode->GetElapsedRunSeconds();
        CurrentResult.bSuccess = RunResult == EShipRunResult::Success;
        if (RunResult == EShipRunResult::Collision)
        {
            ++CollisionCount;
            if (!bActualWallHit)
            {
                ++OtherBlockingCollisionCount;
            }
        }
        else if (RunResult == EShipRunResult::Timeout)
        {
            ++TimeoutCount;
        }
        Results.Add(CurrentResult);
        return StartNextCase();
    }

    void BeginCurrentResult(const ASimGameMode* GameMode)
    {
        CurrentResult = FStage4CaseResult{};
        CurrentResult.SlideCm = ExpectedSlides[CurrentCaseIndex];
        if (GameMode != nullptr)
        {
            CurrentResult.ElapsedSeconds =
                GameMode->GetElapsedRunSeconds();
        }
    }

    bool StartNextCase()
    {
        UWorld* World = CurrentWorld.Get();
        if (World == nullptr)
        {
            World = FindFreshMainLevelWorld(
                PreviousWorldId,
                PreviousWorldIdentity);
        }
        if (IsValid(World))
        {
            PreviousWorldId = World->GetUniqueID();
            PreviousWorldIdentity = World;
        }

        ++CurrentCaseIndex;
        if (CurrentCaseIndex >= ExpectedSlides.Num())
        {
            Finalize();
            return true;
        }
        if (!IsValid(World))
        {
            RecordFailure(TEXT("Stage4TravelSourceWorldUnavailable"));
            Finalize();
            return true;
        }

        const FString Options = FString::Printf(
            TEXT("Stage4Slide=%.0f"),
            ExpectedSlides[CurrentCaseIndex]);
        State = EState::WaitingForWorld;
        WorldLoadDeadlineSeconds = FPlatformTime::Seconds() + 15.0;
        UGameplayStatics::OpenLevel(
            World,
            FName(TEXT("/Game/Maps/MainLevel")),
            true,
            Options);
        CurrentWorld.Reset();
        return false;
    }

    void RecordFailure(const FString& Reason)
    {
        FailureCounts.FindOrAdd(Reason) += 1;
        Test->AddError(Reason);
    }

    void Finalize()
    {
        if (State == EState::Finished)
        {
            return;
        }
        State = EState::Finished;

        int32 SuccessCount = 0;
        for (const FStage4CaseResult& Result : Results)
        {
            SuccessCount += Result.bSuccess ? 1 : 0;
        }
        Test->TestEqual(TEXT("fresh world case count"),
            Results.Num(), ExpectedSlides.Num());
        Test->TestEqual(TEXT("success count"), SuccessCount, 11);
        Test->TestEqual(TEXT("collision count"), CollisionCount, 0);
        Test->TestEqual(TEXT("timeout count"), TimeoutCount, 0);
        Test->TestEqual(TEXT("setup failure count"), SetupFailureCount, 0);
        Test->TestEqual(
            TEXT("runtime calculation error count"),
            RuntimeCalculationErrorCount,
            0);
        Test->TestEqual(
            TEXT("other blocking collision count"),
            OtherBlockingCollisionCount,
            0);

        const int32 ComparableCount =
            FMath::Min(Results.Num(), ExpectedSlides.Num());
        for (int32 Index = 0; Index < ComparableCount; ++Index)
        {
            Test->TestTrue(
                *FString::Printf(TEXT("slide order %d"), Index),
                FMath::IsNearlyEqual(
                    Results[Index].SlideCm,
                    ExpectedSlides[Index],
                    1e-6));
            Test->TestTrue(
                *FString::Printf(TEXT("minimum wall distance case %d"), Index),
                FMath::IsFinite(Results[Index].MinimumWallDistanceCm)
                    && Results[Index].MinimumWallDistanceCm > 0.0
                    && Results[Index].MinimumWallDistanceCm
                        < TNumericLimits<double>::Max());
        }

        UE_LOG(
            LogTemp,
            Display,
            TEXT("Stage4SweepCounts success=%d collision=%d timeout=%d setup=%d runtime=%d cases=%d"),
            SuccessCount,
            CollisionCount,
            TimeoutCount,
            SetupFailureCount,
            RuntimeCalculationErrorCount,
            Results.Num());
        UE_LOG(
            LogTemp,
            Display,
            TEXT("slide | success | elapsed | min wall distance"));
        for (const FStage4CaseResult& Result : Results)
        {
            UE_LOG(
                LogTemp,
                Display,
                TEXT("%.0f | %s | %.3f | %.3f"),
                Result.SlideCm,
                Result.bSuccess ? TEXT("true") : TEXT("false"),
                Result.ElapsedSeconds,
                Result.MinimumWallDistanceCm);
        }
        for (const TPair<FString, int32>& Failure : FailureCounts)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("Stage4SweepFailure reason=%s count=%d"),
                *Failure.Key,
                Failure.Value);
        }
    }

    FAutomationTestBase* Test = nullptr;
    EState State = EState::WaitingForWorld;
    int32 CurrentCaseIndex = 0;
    int32 PreviousWorldId = INDEX_NONE;
    TWeakObjectPtr<UWorld> PreviousWorldIdentity;
    TWeakObjectPtr<UWorld> CurrentWorld;
    double OverallDeadlineSeconds = 0.0;
    double WorldLoadDeadlineSeconds = 0.0;
    double CaseDeadlineSeconds = 0.0;
    FStage4CaseResult CurrentResult;
    TArray<FStage4CaseResult> Results;
    TMap<FString, int32> FailureCounts;
    int32 CollisionCount = 0;
    int32 TimeoutCount = 0;
    int32 SetupFailureCount = 0;
    int32 RuntimeCalculationErrorCount = 0;
    int32 OtherBlockingCollisionCount = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipNavigationActualWorldSweepTest,
    "ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FShipNavigationActualWorldSweepTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FRunShipNavigationSweepCommand(this));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
