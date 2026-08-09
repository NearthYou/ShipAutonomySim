#include "SimGameMode.h"

#include "CourseBuilder.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "ShipCapture.h"
#include "ShipCaptureSimulation.h"
#include "ShipMovement.h"
#include "ShipNavigationSimulation.h"
#include "ShipNavigator.h"
#include "ShipPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimGameMode, Log, All);

#if WITH_DEV_AUTOMATION_TESTS
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
#endif

ASimGameMode::ASimGameMode()
{
	DefaultPawnClass = nullptr;
	ShipPawnClass = AShipPawn::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void ASimGameMode::InitGame(
    const FString& MapName,
    const FString& Options,
    FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    RunResult = EShipRunResult::Running;
    SetupFailure = EShipSetupFailure::None;
    RuntimeErrorState = FShipRuntimeErrorState{};
    ForcedSlideCm.Reset();
    ElapsedRunSeconds = 0.0;
    bUseRandomSlide = true;
    bRunActive = false;
    bSetupFailureLogged = false;
    bTerminalLogged = false;
    bCaptureFinalizeRequested = false;
    RunShip = nullptr;
    CourseBuilder = nullptr;
    CollisionActor.Reset();
    CollisionComponent.Reset();
#if WITH_DEV_AUTOMATION_TESTS
    TestEnterAutonomyCallCount = 0;
    TestSetupFailureLogCount = 0;
    TestTerminalLogCount = 0;
    TestRuntimeErrorLogCount = 0;
    TestCaptureStartCallCount = 0;
    TestCaptureFinalizeCallCount = 0;
    TestLastCaptureStartSlideCm = 0.0;
    bTestLastCaptureFinalizeSuccess = false;
    bStage5CaptureEnabled = true;
    const bool bHasStage5Capture =
        UGameplayStatics::HasOption(Options, TEXT("Stage5Capture"));
    const FString RawStage5Capture =
        UGameplayStatics::ParseOption(Options, TEXT("Stage5Capture"));
    if (bHasStage5Capture && RawStage5Capture == TEXT("0"))
    {
        bStage5CaptureEnabled = false;
    }
#endif

    const bool bHasSlide =
        UGameplayStatics::HasOption(Options, TEXT("Stage4Slide"));
    const FString RawSlide =
        UGameplayStatics::ParseOption(Options, TEXT("Stage4Slide"));
    const FStage4SlideOptionResult Parsed =
        ClassifySlideOption(bHasSlide, RawSlide);
    bUseRandomSlide = Parsed.State == EStage4SlideOptionState::Absent;
    if (Parsed.State == EStage4SlideOptionState::Valid)
    {
        ForcedSlideCm = Parsed.SlideCm;
    }
    else if (Parsed.State != EStage4SlideOptionState::Absent)
    {
        SetupFailure = Parsed.Failure;
    }
}

void ASimGameMode::BeginPlay()
{
	Super::BeginPlay();

#if WITH_DEV_AUTOMATION_TESTS
	++TestBeginPlayInvocationCount;
    if (bTestSkipStage4Orchestration)
    {
        return;
    }
#endif

    if (SetupFailure != EShipSetupFailure::None)
    {
        RecordSetupFailure(SetupFailure);
        return;
    }
    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        RecordSetupFailure(EShipSetupFailure::CourseSpawnFailed);
        return;
    }

    const FTransform BuilderTransform = FTransform::Identity;
    CourseBuilder = World->SpawnActorDeferred<ACourseBuilder>(
        ACourseBuilder::StaticClass(),
        BuilderTransform,
        this,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!IsValid(CourseBuilder))
    {
        RecordSetupFailure(EShipSetupFailure::CourseSpawnFailed);
        return;
    }
    if (bUseRandomSlide)
    {
        CourseBuilder->ClearForcedSlide();
    }
    else if (ForcedSlideCm.IsSet())
    {
        CourseBuilder->SetForcedSlideCm(ForcedSlideCm.GetValue());
    }
#if WITH_DEV_AUTOMATION_TESTS
    if (TestWaterSurfaceOverrideCm.IsSet())
    {
        FCourseBuilderTestAccessor::SetWaterSurfaceOverride(
            *CourseBuilder,
            TestWaterSurfaceOverrideCm.GetValue());
    }
#endif
    CourseBuilder->FinishSpawning(BuilderTransform);

    FShipCourseBuildResult BuildResult;
    EShipSetupFailure BuildFailure = EShipSetupFailure::None;
    if (!CourseBuilder->BuildRuntimeCourse(BuildResult, BuildFailure))
    {
        RecordSetupFailure(BuildFailure);
        return;
    }
    const double ResolvedSlideCm = CourseBuilder->GetResolvedSlideCm();
    if (!IsValidCaptureWallSlide(
            BuildResult.SlideCm,
            ResolvedSlideCm))
    {
        RecordSetupFailure(
            EShipSetupFailure::CaptureInitializationFailed);
        return;
    }
    if (BuildResult.WorldPath.Num() != 3
        || !IsValid(BuildResult.StartTarget)
        || !IsValid(BuildResult.EndTarget)
        || !IsValid(BuildResult.WallActor))
    {
        RecordSetupFailure(EShipSetupFailure::CourseSpawnFailed);
        return;
    }

    APlayerController* PlayerController = World->GetFirstPlayerController();
    if (!IsValid(PlayerController))
    {
        RecordSetupFailure(EShipSetupFailure::PlayerControllerUnavailable);
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    RunShip = World->SpawnActor<AShipPawn>(
        ShipPawnClass,
        BuildResult.StartTarget->GetActorTransform(),
        SpawnParameters);
    if (!IsValid(RunShip))
    {
        RecordSetupFailure(EShipSetupFailure::ShipSpawnFailed);
        return;
    }
    PlayerController->Possess(RunShip);
#if WITH_DEV_AUTOMATION_TESTS
    ++TestEnterAutonomyCallCount;
#endif
    if (!RunShip->EnterAutonomy(
            BuildResult.WorldPath,
            BuildResult.WallActor,
            this))
    {
        RecordSetupFailure(EShipSetupFailure::AutonomyActivationFailed);
        return;
    }

    if (!StartRunCapture(BuildResult.SlideCm, ResolvedSlideCm))
    {
        return;
    }
    ElapsedRunSeconds = 0.0;
    RunResult = EShipRunResult::Running;
}

bool ASimGameMode::StartRunCapture(
    double BuildSlideCm,
    double ResolvedSlideCm)
{
    if (!IsValidCaptureWallSlide(BuildSlideCm, ResolvedSlideCm))
    {
        RecordSetupFailure(
            EShipSetupFailure::CaptureInitializationFailed);
        return false;
    }
    if (!IsValid(RunShip))
    {
        RecordSetupFailure(
            EShipSetupFailure::CaptureInitializationFailed);
        return false;
    }

    UShipMovement* Movement =
        RunShip->FindComponentByClass<UShipMovement>();
    if (Movement == nullptr)
    {
        RecordSetupFailure(EShipSetupFailure::AutonomyActivationFailed);
        return false;
    }
    AddTickPrerequisiteComponent(Movement);

    bool bCaptureEnabled = true;
#if WITH_DEV_AUTOMATION_TESTS
    bCaptureEnabled = bStage5CaptureEnabled;
#endif
    if (!bCaptureEnabled)
    {
        bRunActive = true;
        return true;
    }

    UShipCapture* Capture = RunShip->GetCapture();
    if (!IsValid(Capture))
    {
        RecordSetupFailure(
            EShipSetupFailure::CaptureInitializationFailed);
        return false;
    }
    Capture->AddTickPrerequisiteComponent(Movement);
    AddTickPrerequisiteComponent(Capture);
#if WITH_DEV_AUTOMATION_TESTS
    ++TestCaptureStartCallCount;
    TestLastCaptureStartSlideCm = BuildSlideCm;
#endif
    if (!Capture->StartCapture(BuildSlideCm))
    {
        RecordSetupFailure(
            EShipSetupFailure::CaptureInitializationFailed);
        return false;
    }
    bRunActive = true;
    return true;
}

void ASimGameMode::FinalizeRunCapture(bool bSimulationSucceeded)
{
    if (bCaptureFinalizeRequested)
    {
        return;
    }
    bCaptureFinalizeRequested = true;
#if WITH_DEV_AUTOMATION_TESTS
    ++TestCaptureFinalizeCallCount;
    bTestLastCaptureFinalizeSuccess = bSimulationSucceeded;
#endif
    if (IsValid(RunShip))
    {
        if (UShipCapture* Capture = RunShip->GetCapture())
        {
            Capture->StopAndFinalize(bSimulationSucceeded);
        }
    }
}

void ASimGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (SetupFailure != EShipSetupFailure::None
        || RunResult != EShipRunResult::Running)
    {
        DisableNavigatorAndZeroInputs();
        return;
    }
    if (!bRunActive || !IsValid(RunShip) || !IsValid(CourseBuilder))
    {
        return;
    }

    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f)
    {
        ElapsedRunSeconds += static_cast<double>(DeltaSeconds);
    }
    UShipMovement* Movement = RunShip->FindComponentByClass<UShipMovement>();
    if (Movement == nullptr)
    {
        RecordSetupFailure(EShipSetupFailure::AutonomyActivationFailed);
        return;
    }
    if (RuntimeErrorState.bLatched)
    {
        DisableNavigatorAndZeroInputs();
    }

    FHitResult BlockingHit;
    const bool bBlockingHit = Movement->ConsumeBlockingHit(BlockingHit);
    if (bBlockingHit)
    {
        CollisionActor = BlockingHit.GetActor();
        CollisionComponent = BlockingHit.GetComponent();
    }

    bool bSuccessConditions = false;
    const TArray<FVector>& WorldPath = CourseBuilder->GetWorldPath();
    if (WorldPath.Num() != 3)
    {
        ReportRuntimeCalculationError(
            EShipRuntimeCalculationError::InvalidNavigationPath);
    }
    else
    {
        const FVector ShipLocation = RunShip->GetActorLocation();
        const FVector EndLocation = WorldPath.Last();
        const double GoalDistanceCm = FVector::DistXY(ShipLocation, EndLocation);
        const double SignedSpeedCmPerSecond =
            Movement->GetSignedSpeedCmPerSecond();
        if (!FMath::IsFinite(GoalDistanceCm)
            || !FMath::IsFinite(SignedSpeedCmPerSecond))
        {
            ReportRuntimeCalculationError(
                EShipRuntimeCalculationError::InvalidProgressProjection);
        }
        else
        {
            bSuccessConditions = !RuntimeErrorState.bLatched
                && GoalDistanceCm <= GoalRadiusCm
                && FMath::Abs(SignedSpeedCmPerSecond)
                    <= SuccessSpeedThresholdCmPerSecond;
        }
    }

    const FShipTerminalInputs Inputs{
        bBlockingHit,
        bSuccessConditions,
        ElapsedRunSeconds >= TimeoutSeconds,
        RuntimeErrorState.bLatched};
    LatchTerminalResult(SelectTerminalResult(Inputs));
    if (RuntimeErrorState.bLatched
        && RunResult == EShipRunResult::Running)
    {
        DisableNavigatorAndZeroInputs();
    }
}

void ASimGameMode::RecordSetupFailure(EShipSetupFailure Failure)
{
    if (SetupFailure == EShipSetupFailure::None)
    {
        SetupFailure = Failure;
    }
    bRunActive = false;
    DisableNavigatorAndZeroInputs();
    FinalizeRunCapture(false);
    if (!bSetupFailureLogged)
    {
        UE_LOG(
            LogSimGameMode,
            Error,
            TEXT("Stage4SetupFailure failure=%d"),
            static_cast<int32>(SetupFailure));
        bSetupFailureLogged = true;
#if WITH_DEV_AUTOMATION_TESTS
        ++TestSetupFailureLogCount;
#endif
    }
}

void ASimGameMode::DisableNavigatorAndZeroInputs()
{
    if (!IsValid(RunShip))
    {
        return;
    }
    if (UShipNavigator* Navigator = RunShip->GetNavigator())
    {
        Navigator->SetNavigationEnabled(false);
    }
    if (UShipMovement* Movement =
            RunShip->FindComponentByClass<UShipMovement>())
    {
        Movement->SetThrottle(0.0f);
        Movement->SetSteer(0.0f);
    }
}

void ASimGameMode::LatchTerminalResult(EShipRunResult Candidate)
{
    if (RunResult != EShipRunResult::Running
        || Candidate == EShipRunResult::Running)
    {
        return;
    }
    RunResult = Candidate;
    bRunActive = false;
    DisableNavigatorAndZeroInputs();
    LogTerminalOnce(Candidate);
    FinalizeRunCapture(Candidate == EShipRunResult::Success);
}

void ASimGameMode::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    FinalizeRunCapture(false);
    Super::EndPlay(EndPlayReason);
}

void ASimGameMode::LogTerminalOnce(EShipRunResult Result)
{
    if (bTerminalLogged)
    {
        return;
    }
    const TCHAR* ResultName = TEXT("Running");
    switch (Result)
    {
    case EShipRunResult::Success:
        ResultName = TEXT("Success");
        break;
    case EShipRunResult::Timeout:
        ResultName = TEXT("Timeout");
        break;
    case EShipRunResult::Collision:
        ResultName = TEXT("Collision");
        break;
    case EShipRunResult::Running:
        break;
    }
    UE_LOG(
        LogSimGameMode,
        Display,
        TEXT("Stage4Terminal %s elapsed=%.3f slide=%.3f actor=%s component=%s"),
        ResultName,
        ElapsedRunSeconds,
        GetResolvedSlideCm(),
        CollisionActor.IsValid() ? *CollisionActor->GetName() : TEXT("None"),
        CollisionComponent.IsValid()
            ? *CollisionComponent->GetName()
            : TEXT("None"));
    bTerminalLogged = true;
#if WITH_DEV_AUTOMATION_TESTS
    ++TestTerminalLogCount;
#endif
}

void ASimGameMode::ReportRuntimeCalculationError(
    EShipRuntimeCalculationError Error)
{
    const bool bFirstReport =
        LatchRuntimeCalculationError(Error, RuntimeErrorState);
    if (bFirstReport)
    {
        UE_LOG(
            LogSimGameMode,
            Error,
            TEXT("Stage4RuntimeCalculationError error=%d"),
            static_cast<int32>(Error));
#if WITH_DEV_AUTOMATION_TESTS
        ++TestRuntimeErrorLogCount;
#endif
    }
    if (RuntimeErrorState.bLatched)
    {
        DisableNavigatorAndZeroInputs();
    }
}

EShipRunResult ASimGameMode::GetRunResult() const
{
    return RunResult;
}

EShipSetupFailure ASimGameMode::GetSetupFailure() const
{
    return SetupFailure;
}

bool ASimGameMode::HasRuntimeCalculationError() const
{
    return RuntimeErrorState.bLatched;
}

EShipRuntimeCalculationError ASimGameMode::GetRuntimeCalculationError() const
{
    return RuntimeErrorState.FirstError;
}

int32 ASimGameMode::GetRuntimeCalculationErrorCount() const
{
    return RuntimeErrorState.ReportCount;
}

double ASimGameMode::GetResolvedSlideCm() const
{
    if (IsValid(CourseBuilder))
    {
        return CourseBuilder->GetResolvedSlideCm();
    }
    return ForcedSlideCm.IsSet() ? ForcedSlideCm.GetValue() : 0.0;
}

double ASimGameMode::GetElapsedRunSeconds() const
{
    return ElapsedRunSeconds;
}

AShipPawn* ASimGameMode::GetRunShip() const
{
    return RunShip.Get();
}

ACourseBuilder* ASimGameMode::GetCourseBuilder() const
{
    return CourseBuilder.Get();
}

AActor* ASimGameMode::GetCollisionActor() const
{
    return CollisionActor.Get();
}

UPrimitiveComponent* ASimGameMode::GetCollisionComponent() const
{
    return CollisionComponent.Get();
}
