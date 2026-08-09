#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CourseBuilder.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShipCapture.h"
#include "ShipCaptureSimulation.h"
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

const TArray<double>& GetStage5CaptureOnSlides()
{
    static const TArray<double> CaptureOnSlides{-500.0, 0.0, 500.0};
    return CaptureOnSlides;
}

int32 GetStage5ValidationCaseCount()
{
    return ExpectedSlides.Num() + GetStage5CaptureOnSlides().Num();
}

struct FStage5PerformanceCondition
{
    const TCHAR* Name = TEXT("A");
    bool bCaptureEnabled = false;
};

const TArray<FStage5PerformanceCondition>&
GetStage5PerformanceConditions()
{
    static const TArray<FStage5PerformanceCondition> Conditions{
        {TEXT("A"), false},
        {TEXT("B"), true},
        {TEXT("B"), true},
        {TEXT("A"), false}};
    return Conditions;
}

const TArray<double>& GetStage5PerformanceSlides()
{
    static const TArray<double> Slides{0.0, 0.0, 0.0, 0.0};
    return Slides;
}

struct FStage5Statistics
{
    int32 SampleCount = 0;
    double Minimum = 0.0;
    double Median = 0.0;
    double P95 = 0.0;
    double Maximum = 0.0;
    bool bValid = false;
};

FStage5Statistics CalculateStage5Statistics(
    const TArray<double>& Samples)
{
    FStage5Statistics Result;
    Result.SampleCount = Samples.Num();
    if (Samples.IsEmpty())
    {
        return Result;
    }
    for (const double Sample : Samples)
    {
        if (!FMath::IsFinite(Sample) || Sample < 0.0)
        {
            return Result;
        }
    }

    TArray<double> SortedSamples = Samples;
    SortedSamples.Sort();
    Result.Minimum = SortedSamples[0];
    Result.Maximum = SortedSamples.Last();
    const int32 MiddleIndex = SortedSamples.Num() / 2;
    Result.Median = SortedSamples.Num() % 2 == 0
        ? (SortedSamples[MiddleIndex - 1] +
            SortedSamples[MiddleIndex]) /
            2.0
        : SortedSamples[MiddleIndex];
    const int32 P95Index = FMath::Clamp(
        FMath::CeilToInt(0.95 * SortedSamples.Num()) - 1,
        0,
        SortedSamples.Num() - 1);
    Result.P95 = SortedSamples[P95Index];
    Result.bValid = true;
    return Result;
}

enum class EStage5ValidationPhase : uint8
{
    RegressionCaptureOff,
    CaptureOnAcceptance,
    Performance,
    Finished
};

const TCHAR* Stage5ValidationPhaseName(EStage5ValidationPhase Phase)
{
    switch (Phase)
    {
    case EStage5ValidationPhase::RegressionCaptureOff:
        return TEXT("capture_off");
    case EStage5ValidationPhase::CaptureOnAcceptance:
        return TEXT("capture_on");
    case EStage5ValidationPhase::Performance:
        return TEXT("performance");
    case EStage5ValidationPhase::Finished:
        return TEXT("finished");
    }
    return TEXT("unknown");
}

struct FStage5CaseResult
{
    EStage5ValidationPhase Phase =
        EStage5ValidationPhase::RegressionCaptureOff;
    double SlideCm = 0.0;
    bool bSuccess = false;
    double ElapsedSeconds = 0.0;
    double MinimumWallDistanceCm = TNumericLimits<double>::Max();
    int32 WorldId = INDEX_NONE;
    int32 FrameCount = 0;
    int64 LastFrameTimeMs = 0;
    FString RunDirectory;
    TArray<double> FrameTimesMs;
    TArray<double> TransactionDurationsMs;
    TArray<double> CaptureIntervalsMs;
};

class FStage5CaptureRunTracker
{
public:
    FStage5CaptureRunTracker()
    {
        AutomationRoot = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() /
            TEXT("ShipCaptures/Automation"));
        FPaths::NormalizeDirectoryName(AutomationRoot);
        PreExistingChildren = DirectChildDirectories();
    }

    TArray<FString> DirectChildDirectories() const
    {
        TArray<FString> Children;
        IFileManager::Get().FindFiles(
            Children,
            *(AutomationRoot / TEXT("*")),
            false,
            true);
        Children.Sort();
        return Children;
    }

    bool Track(const FString& RunDirectory)
    {
        FString FullRunDirectory =
            FPaths::ConvertRelativePathToFull(RunDirectory);
        FPaths::NormalizeDirectoryName(FullRunDirectory);
        FString ParentDirectory = FPaths::GetPath(FullRunDirectory);
        FPaths::NormalizeDirectoryName(ParentDirectory);
        const FString LeafName =
            FPaths::GetCleanFilename(FullRunDirectory);
        if (ParentDirectory != AutomationRoot ||
            PreExistingChildren.Contains(LeafName) ||
            !IFileManager::Get().DirectoryExists(*FullRunDirectory))
        {
            return false;
        }
        CreatedRunDirectories.AddUnique(FullRunDirectory);
        return true;
    }

    bool CleanupExact(const FString& RunDirectory)
    {
        FString FullRunDirectory =
            FPaths::ConvertRelativePathToFull(RunDirectory);
        FPaths::NormalizeDirectoryName(FullRunDirectory);
        FString ParentDirectory = FPaths::GetPath(FullRunDirectory);
        FPaths::NormalizeDirectoryName(ParentDirectory);
        if (ParentDirectory != AutomationRoot ||
            !CreatedRunDirectories.Contains(FullRunDirectory) ||
            CleanedRunDirectories.Contains(FullRunDirectory) ||
            !IFileManager::Get().DirectoryExists(*FullRunDirectory))
        {
            return false;
        }
        if (!IFileManager::Get().DeleteDirectory(
                *FullRunDirectory,
                true,
                true))
        {
            return false;
        }
        CleanedRunDirectories.Add(FullRunDirectory);
        return true;
    }

    bool AllTrackedRunsCleaned() const
    {
        return CreatedRunDirectories.Num() ==
            CleanedRunDirectories.Num();
    }

    FString AutomationRoot;
    TArray<FString> PreExistingChildren;

private:
    TArray<FString> CreatedRunDirectories;
    TArray<FString> CleanedRunDirectories;
};

bool ReadPngDimensions(
    const FString& Path,
    int32& OutWidth,
    int32& OutHeight)
{
    TArray64<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) ||
        Bytes.Num() < 24)
    {
        return false;
    }
    constexpr uint8 PngSignature[] = {
        0x89,
        0x50,
        0x4e,
        0x47,
        0x0d,
        0x0a,
        0x1a,
        0x0a};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(PngSignature); ++Index)
    {
        if (Bytes[Index] != PngSignature[Index])
        {
            return false;
        }
    }
    const auto ReadBigEndianInt32 = [&Bytes](int32 Offset)
    {
        return static_cast<int32>(
            (static_cast<uint32>(Bytes[Offset]) << 24) |
            (static_cast<uint32>(Bytes[Offset + 1]) << 16) |
            (static_cast<uint32>(Bytes[Offset + 2]) << 8) |
            static_cast<uint32>(Bytes[Offset + 3]));
    };
    OutWidth = ReadBigEndianInt32(16);
    OutHeight = ReadBigEndianInt32(20);
    return true;
}

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
    FRunShipNavigationSweepCommand(
        FAutomationTestBase* InTest,
        bool bInPerformanceMode)
        : Test(InTest)
        , OverallDeadlineSeconds(FPlatformTime::Seconds() + 720.0)
        , WorldLoadDeadlineSeconds(FPlatformTime::Seconds() + 15.0)
    {
        bPerformanceMode = bInPerformanceMode;
        if (bPerformanceMode)
        {
            ValidationPhase = EStage5ValidationPhase::Performance;
        }
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

    const TArray<double>& CurrentSlides() const
    {
        if (bPerformanceMode)
        {
            return GetStage5PerformanceSlides();
        }
        return ValidationPhase ==
                EStage5ValidationPhase::RegressionCaptureOff
            ? ExpectedSlides
            : GetStage5CaptureOnSlides();
    }

    double CurrentExpectedSlide() const
    {
        return CurrentSlides()[CurrentCaseIndex];
    }

    bool IsCaptureOnPhase() const
    {
        if (bPerformanceMode)
        {
            return GetStage5PerformanceConditions()[CurrentCaseIndex]
                .bCaptureEnabled;
        }
        return ValidationPhase ==
            EStage5ValidationPhase::CaptureOnAcceptance;
    }

    bool CheckAcceptance(
        bool bCondition,
        const FString& Reason)
    {
        if (!bCondition)
        {
            RecordFailure(Reason);
        }
        return bCondition;
    }

    bool PrepareCurrentCapture(ASimGameMode& GameMode)
    {
        AShipPawn* RunShip = GameMode.GetRunShip();
        UShipCapture* Capture = IsValid(RunShip)
            ? RunShip->GetCapture()
            : nullptr;
        if (!CheckAcceptance(
                IsValid(Capture),
                FString::Printf(
                    TEXT("Stage5CaptureComponentMissing case=%d"),
                    CurrentCaseIndex)))
        {
            return false;
        }

        const FString RunDirectory =
            FShipCaptureAutomationAccessor::RunDirectory(*Capture);
        if (!IsCaptureOnPhase())
        {
            bool bValid = true;
            if (bPerformanceMode)
            {
                bValid = CheckAcceptance(
                    FShipCaptureAutomationAccessor::CaptureResolution(
                        *Capture) == 512,
                    FString::Printf(
                        TEXT("Stage5PerformanceResolution case=%d"),
                        CurrentCaseIndex)) && bValid;
                bValid = CheckAcceptance(
                    FShipCaptureAutomationAccessor::CaptureIntervalMs(
                        *Capture) == 100,
                    FString::Printf(
                        TEXT("Stage5PerformanceInterval case=%d"),
                        CurrentCaseIndex)) && bValid;
                bValid = CheckAcceptance(
                    FMath::IsNearlyEqual(
                        FShipCaptureAutomationAccessor
                            ::CaptureFovDegrees(*Capture),
                        90.0f),
                    FString::Printf(
                        TEXT("Stage5PerformanceFov case=%d"),
                        CurrentCaseIndex)) && bValid;
            }
            bValid = CheckAcceptance(
                RunDirectory.IsEmpty(),
                FString::Printf(
                    TEXT("Stage5CaptureOffRunDirectory case=%d"),
                    CurrentCaseIndex)) && bValid;
            bValid = CheckAcceptance(
                !Capture->IsCaptureActive(),
                FString::Printf(
                    TEXT("Stage5CaptureOffActive case=%d"),
                    CurrentCaseIndex)) && bValid;
            bValid = CheckAcceptance(
                !Capture->HasCaptureFailure(),
                FString::Printf(
                    TEXT("Stage5CaptureOffFailure case=%d"),
                    CurrentCaseIndex)) && bValid;
            return bValid;
        }

        bool bValid = true;
        bValid = CheckAcceptance(
            Capture->IsCaptureActive(),
            FString::Printf(
                TEXT("Stage5CaptureNotActive case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            !Capture->HasCaptureFailure(),
            FString::Printf(
                TEXT("Stage5CaptureSetupFailure case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            FShipCaptureAutomationAccessor::CaptureResolution(*Capture) ==
                512,
            FString::Printf(
                TEXT("Stage5CaptureResolution case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            FShipCaptureAutomationAccessor::CaptureIntervalMs(*Capture) ==
                100,
            FString::Printf(
                TEXT("Stage5CaptureInterval case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            FMath::IsNearlyEqual(
                FShipCaptureAutomationAccessor::CaptureFovDegrees(
                    *Capture),
                90.0f),
            FString::Printf(
                TEXT("Stage5CaptureFov case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            FShipCaptureAutomationAccessor::HasOpticalEquality(*Capture),
            FString::Printf(
                TEXT("Stage5CaptureOpticalMismatch case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            !RunDirectory.IsEmpty(),
            FString::Printf(
                TEXT("Stage5CaptureRunDirectoryMissing case=%d"),
                CurrentCaseIndex)) && bValid;
        if (!RunDirectory.IsEmpty())
        {
            CurrentResult.RunDirectory = RunDirectory;
            bValid = CheckAcceptance(
                CaptureRunTracker.Track(RunDirectory),
                FString::Printf(
                    TEXT("Stage5CaptureRunDirectoryUnsafe case=%d path=%s"),
                    CurrentCaseIndex,
                    *RunDirectory)) && bValid;
        }
        return bValid;
    }

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
        const double ExpectedSlide = CurrentExpectedSlide();
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

        CurrentResult.WorldId = World->GetUniqueID();
        const TWeakObjectPtr<UWorld> CurrentWorldIdentity(World);
        const bool bWorldIdentitySeen =
            ObservedWorldIdentities.ContainsByPredicate(
                [&CurrentWorldIdentity](
                    const TWeakObjectPtr<UWorld>& ObservedIdentity)
                {
                    return ObservedIdentity.HasSameIndexAndSerialNumber(
                        CurrentWorldIdentity);
                });
        if (bWorldIdentitySeen)
        {
            RecordFailure(FString::Printf(
                TEXT("Stage5FreshWorldIdentityReused case=%d world=%d"),
                CurrentCaseIndex,
                CurrentResult.WorldId));
            Results.Add(CurrentResult);
            return StartNextCase();
        }
        ObservedWorldIdentities.Add(CurrentWorldIdentity);
        if (!PrepareCurrentCapture(*GameMode))
        {
            Results.Add(CurrentResult);
            return StartNextCase();
        }

        CurrentWorld = World;
        CaseDeadlineSeconds = NowSeconds + 60.0;
        if (bPerformanceMode)
        {
            PerformanceWarmupEndSeconds = NowSeconds + 1.0;
            LastPerformanceUpdateSeconds = -1.0;
        }
        State = EState::ObservingCase;
        return false;
    }

    void RecordPerformanceFrameTime(double NowSeconds)
    {
        if (!bPerformanceMode ||
            NowSeconds < PerformanceWarmupEndSeconds)
        {
            return;
        }
        if (LastPerformanceUpdateSeconds >= 0.0)
        {
            const double FrameTimeMs =
                (NowSeconds - LastPerformanceUpdateSeconds) * 1000.0;
            if (!FMath::IsFinite(FrameTimeMs) || FrameTimeMs <= 0.0)
            {
                RecordFailure(FString::Printf(
                    TEXT("Stage5PerformanceFrameTimeInvalid case=%d value=%.6f"),
                    CurrentCaseIndex,
                    FrameTimeMs));
            }
            else
            {
                CurrentResult.FrameTimesMs.Add(FrameTimeMs);
            }
        }
        LastPerformanceUpdateSeconds = NowSeconds;
    }

    bool ValidateCapturePng(
        const FString& Path,
        const TCHAR* StreamName,
        int32 FrameIndex)
    {
        int32 Width = 0;
        int32 Height = 0;
        return CheckAcceptance(
            ReadPngDimensions(Path, Width, Height) &&
                Width == 512 &&
                Height == 512,
            FString::Printf(
                TEXT("Stage5CapturePngInvalid case=%d stream=%s frame=%d width=%d height=%d"),
                CurrentCaseIndex,
                StreamName,
                FrameIndex,
                Width,
                Height));
    }

    bool ValidateCaptureManifest(
        UShipCapture& Capture,
        FStage5CaseResult& Result)
    {
        bool bValid = true;
        bValid = CheckAcceptance(
            !Capture.HasCaptureFailure(),
            FString::Printf(
                TEXT("Stage5CaptureFailureLatched case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            FShipCaptureAutomationAccessor::FailureLogCount(Capture) == 0,
            FString::Printf(
                TEXT("Stage5CaptureFailureLog case=%d count=%d"),
                CurrentCaseIndex,
                FShipCaptureAutomationAccessor::FailureLogCount(Capture))) &&
            bValid;

        const FString ManifestPath =
            Result.RunDirectory / TEXT("manifest.json");
        FString JsonText;
        if (!CheckAcceptance(
                FFileHelper::LoadFileToString(JsonText, *ManifestPath),
                FString::Printf(
                    TEXT("Stage5ManifestMissing case=%d path=%s"),
                    CurrentCaseIndex,
                    *ManifestPath)))
        {
            return false;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader =
            TJsonReaderFactory<>::Create(JsonText);
        if (!CheckAcceptance(
                FJsonSerializer::Deserialize(Reader, Root) &&
                    Root.IsValid(),
                FString::Printf(
                    TEXT("Stage5ManifestParse case=%d"),
                    CurrentCaseIndex)))
        {
            return false;
        }

        TArray<FString> ActualTopKeys;
        Root->Values.GetKeys(ActualTopKeys);
        ActualTopKeys.Sort();
        TArray<FString> ExpectedTopKeys{
            TEXT("capture_resolution"),
            TEXT("depth_far_cm"),
            TEXT("depth_near_cm"),
            TEXT("frame_count"),
            TEXT("frames"),
            TEXT("interval_ms"),
            TEXT("result"),
            TEXT("wall_slide_cm")};
        ExpectedTopKeys.Sort();
        bValid = CheckAcceptance(
            ActualTopKeys == ExpectedTopKeys,
            FString::Printf(
                TEXT("Stage5ManifestTopSchema case=%d actual=%s"),
                CurrentCaseIndex,
                *FString::Join(ActualTopKeys, TEXT("|")))) && bValid;

        int32 FrameCount = -1;
        int32 IntervalMs = -1;
        double NearCm = -1.0;
        double FarCm = -1.0;
        double SlideCm = 0.0;
        FString ManifestResult;
        bValid = CheckAcceptance(
            Root->TryGetNumberField(TEXT("frame_count"), FrameCount),
            FString::Printf(
                TEXT("Stage5ManifestFrameCountType case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            Root->TryGetNumberField(TEXT("interval_ms"), IntervalMs),
            FString::Printf(
                TEXT("Stage5ManifestIntervalType case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            Root->TryGetNumberField(TEXT("depth_near_cm"), NearCm),
            FString::Printf(
                TEXT("Stage5ManifestNearType case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            Root->TryGetNumberField(TEXT("depth_far_cm"), FarCm),
            FString::Printf(
                TEXT("Stage5ManifestFarType case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            Root->TryGetNumberField(TEXT("wall_slide_cm"), SlideCm),
            FString::Printf(
                TEXT("Stage5ManifestSlideType case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            Root->TryGetStringField(TEXT("result"), ManifestResult),
            FString::Printf(
                TEXT("Stage5ManifestResultType case=%d"),
                CurrentCaseIndex)) && bValid;
        bValid = CheckAcceptance(
            FrameCount >= 1,
            FString::Printf(
                TEXT("Stage5ManifestNoFrames case=%d count=%d"),
                CurrentCaseIndex,
                FrameCount)) && bValid;
        bValid = CheckAcceptance(
            IntervalMs == 100,
            FString::Printf(
                TEXT("Stage5ManifestInterval case=%d value=%d"),
                CurrentCaseIndex,
                IntervalMs)) && bValid;
        bValid = CheckAcceptance(
            NearCm == 0.0 && FarCm == 5000.0,
            FString::Printf(
                TEXT("Stage5ManifestDepthRange case=%d near=%.3f far=%.3f"),
                CurrentCaseIndex,
                NearCm,
                FarCm)) && bValid;
        bValid = CheckAcceptance(
            FMath::IsNearlyEqual(SlideCm, CurrentExpectedSlide(), 1e-9),
            FString::Printf(
                TEXT("Stage5ManifestSlide case=%d expected=%.3f actual=%.3f"),
                CurrentCaseIndex,
                CurrentExpectedSlide(),
                SlideCm)) && bValid;
        bValid = CheckAcceptance(
            ManifestResult == TEXT("success"),
            FString::Printf(
                TEXT("Stage5ManifestResult case=%d value=%s"),
                CurrentCaseIndex,
                *ManifestResult)) && bValid;

        const TArray<TSharedPtr<FJsonValue>>* ResolutionValues = nullptr;
        const bool bHasResolution = Root->TryGetArrayField(
            TEXT("capture_resolution"),
            ResolutionValues);
        bValid = CheckAcceptance(
            bHasResolution &&
                ResolutionValues != nullptr &&
                ResolutionValues->Num() == 2 &&
                (*ResolutionValues)[0]->AsNumber() == 512.0 &&
                (*ResolutionValues)[1]->AsNumber() == 512.0,
            FString::Printf(
                TEXT("Stage5ManifestResolution case=%d"),
                CurrentCaseIndex)) && bValid;

        const TArray<TSharedPtr<FJsonValue>>* FrameValues = nullptr;
        if (!CheckAcceptance(
                Root->TryGetArrayField(TEXT("frames"), FrameValues) &&
                    FrameValues != nullptr,
                FString::Printf(
                    TEXT("Stage5ManifestFramesType case=%d"),
                    CurrentCaseIndex)))
        {
            return false;
        }
        bValid = CheckAcceptance(
            FrameValues->Num() == FrameCount,
            FString::Printf(
                TEXT("Stage5ManifestFramesCount case=%d declared=%d actual=%d"),
                CurrentCaseIndex,
                FrameCount,
                FrameValues->Num())) && bValid;
        bValid = CheckAcceptance(
            FShipCaptureAutomationAccessor::CommittedFrameCount(Capture) ==
                FrameCount,
            FString::Printf(
                TEXT("Stage5CaptureCommittedCount case=%d manifest=%d component=%d"),
                CurrentCaseIndex,
                FrameCount,
                FShipCaptureAutomationAccessor::CommittedFrameCount(
                    Capture))) && bValid;

        TArray<FString> ExpectedFiles{TEXT("manifest.json")};
        int64 PreviousTimeMs = -1;
        for (int32 ExpectedIndex = 0;
             ExpectedIndex < FrameValues->Num();
             ++ExpectedIndex)
        {
            const TSharedPtr<FJsonValue>& FrameValue =
                (*FrameValues)[ExpectedIndex];
            if (!CheckAcceptance(
                    FrameValue.IsValid() &&
                        FrameValue->Type == EJson::Object,
                    FString::Printf(
                        TEXT("Stage5ManifestFrameObject case=%d frame=%d"),
                        CurrentCaseIndex,
                        ExpectedIndex)))
            {
                bValid = false;
                continue;
            }
            const TSharedPtr<FJsonObject> FrameObject =
                FrameValue->AsObject();
            TArray<FString> ActualFrameKeys;
            FrameObject->Values.GetKeys(ActualFrameKeys);
            ActualFrameKeys.Sort();
            TArray<FString> ExpectedFrameKeys{
                TEXT("color"),
                TEXT("depth"),
                TEXT("index"),
                TEXT("time_ms")};
            ExpectedFrameKeys.Sort();
            bValid = CheckAcceptance(
                ActualFrameKeys == ExpectedFrameKeys,
                FString::Printf(
                    TEXT("Stage5ManifestFrameSchema case=%d frame=%d"),
                    CurrentCaseIndex,
                    ExpectedIndex)) && bValid;

            int32 Index = INDEX_NONE;
            int64 TimeMs = -1;
            FString ColorLeafName;
            FString DepthLeafName;
            const bool bFieldsValid =
                FrameObject->TryGetNumberField(TEXT("index"), Index) &&
                FrameObject->TryGetNumberField(TEXT("time_ms"), TimeMs) &&
                FrameObject->TryGetStringField(
                    TEXT("color"),
                    ColorLeafName) &&
                FrameObject->TryGetStringField(
                    TEXT("depth"),
                    DepthLeafName);
            bValid = CheckAcceptance(
                bFieldsValid,
                FString::Printf(
                    TEXT("Stage5ManifestFrameFields case=%d frame=%d"),
                    CurrentCaseIndex,
                    ExpectedIndex)) && bValid;

            FString ExpectedColorLeafName;
            FString ExpectedDepthLeafName;
            const bool bNamesMade = MakeCaptureFrameLeafNames(
                ExpectedIndex,
                ExpectedColorLeafName,
                ExpectedDepthLeafName);
            bValid = CheckAcceptance(
                bNamesMade &&
                    Index == ExpectedIndex &&
                    ColorLeafName == ExpectedColorLeafName &&
                    DepthLeafName == ExpectedDepthLeafName,
                FString::Printf(
                    TEXT("Stage5ManifestFrameIdentity case=%d frame=%d index=%d color=%s depth=%s"),
                    CurrentCaseIndex,
                    ExpectedIndex,
                    Index,
                    *ColorLeafName,
                    *DepthLeafName)) && bValid;
            bValid = CheckAcceptance(
                ExpectedIndex == 0
                    ? TimeMs == 0
                    : TimeMs > PreviousTimeMs,
                FString::Printf(
                    TEXT("Stage5ManifestFrameTime case=%d frame=%d previous=%lld actual=%lld"),
                    CurrentCaseIndex,
                    ExpectedIndex,
                    PreviousTimeMs,
                    TimeMs)) && bValid;
            if (ExpectedIndex > 0)
            {
                Result.CaptureIntervalsMs.Add(
                    static_cast<double>(TimeMs - PreviousTimeMs));
            }
            PreviousTimeMs = TimeMs;

            const FString ColorPath =
                Result.RunDirectory / ExpectedColorLeafName;
            const FString DepthPath =
                Result.RunDirectory / ExpectedDepthLeafName;
            bValid = ValidateCapturePng(
                ColorPath,
                TEXT("color"),
                ExpectedIndex) && bValid;
            bValid = ValidateCapturePng(
                DepthPath,
                TEXT("depth"),
                ExpectedIndex) && bValid;
            ExpectedFiles.Add(ExpectedColorLeafName);
            ExpectedFiles.Add(ExpectedDepthLeafName);
        }

        if (bPerformanceMode)
        {
            int32 DuplicateCount = 0;
            int32 CatchUpCount = 0;
            for (const double FrameIntervalMs :
                 Result.CaptureIntervalsMs)
            {
                DuplicateCount += FrameIntervalMs <= 0.0 ? 1 : 0;
                CatchUpCount +=
                    FrameIntervalMs > 0.0 && FrameIntervalMs < 100.0
                        ? 1
                        : 0;
            }
            bValid = CheckAcceptance(
                Result.CaptureIntervalsMs.Num() ==
                    FMath::Max(0, FrameCount - 1) &&
                    !Result.CaptureIntervalsMs.IsEmpty(),
                FString::Printf(
                    TEXT("Stage5PerformanceIntervalDataMissing case=%d frames=%d intervals=%d"),
                    CurrentCaseIndex,
                    FrameCount,
                    Result.CaptureIntervalsMs.Num())) && bValid;
            bValid = CheckAcceptance(
                DuplicateCount == 0,
                FString::Printf(
                    TEXT("Stage5PerformanceDuplicateTimestamp case=%d count=%d"),
                    CurrentCaseIndex,
                    DuplicateCount)) && bValid;
            bValid = CheckAcceptance(
                CatchUpCount == 0,
                FString::Printf(
                    TEXT("Stage5PerformanceCatchUp case=%d count=%d"),
                    CurrentCaseIndex,
                    CatchUpCount)) && bValid;
        }

        TArray<FString> ActualFiles;
        IFileManager::Get().FindFiles(
            ActualFiles,
            *(Result.RunDirectory / TEXT("*")),
            true,
            false);
        ActualFiles.Sort();
        ExpectedFiles.Sort();
        bValid = CheckAcceptance(
            ActualFiles == ExpectedFiles,
            FString::Printf(
                TEXT("Stage5CapturePublishedFiles case=%d expected=%d actual=%d"),
                CurrentCaseIndex,
                ExpectedFiles.Num(),
                ActualFiles.Num())) && bValid;
        TArray<FString> ChildDirectories;
        IFileManager::Get().FindFiles(
            ChildDirectories,
            *(Result.RunDirectory / TEXT("*")),
            false,
            true);
        bValid = CheckAcceptance(
            ChildDirectories.IsEmpty(),
            FString::Printf(
                TEXT("Stage5CaptureUnexpectedDirectory case=%d count=%d"),
                CurrentCaseIndex,
                ChildDirectories.Num())) && bValid;

        Result.FrameCount = FrameCount;
        Result.LastFrameTimeMs = PreviousTimeMs;
        return bValid;
    }

    bool UpdateObservingCase(double NowSeconds)
    {
        RecordPerformanceFrameTime(NowSeconds);
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
        UShipCapture* Capture = IsValid(RunShip)
            ? RunShip->GetCapture()
            : nullptr;
        if (IsCaptureOnPhase() &&
            (!IsValid(Capture) || Capture->HasCaptureFailure()))
        {
            ++CaptureFailureCount;
            CurrentResult.ElapsedSeconds =
                GameMode->GetElapsedRunSeconds();
            RecordFailure(FString::Printf(
                TEXT("Stage5CaptureRuntimeFailure case=%d missing=%d"),
                CurrentCaseIndex,
                IsValid(Capture) ? 0 : 1));
            Results.Add(CurrentResult);
            return StartNextCase();
        }
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
        if (bPerformanceMode && IsValid(Capture))
        {
            CurrentResult.TransactionDurationsMs =
                FShipCaptureAutomationAccessor::TransactionDurationsMs(
                    *Capture);
            if (!IsCaptureOnPhase() &&
                !CurrentResult.TransactionDurationsMs.IsEmpty())
            {
                RecordFailure(FString::Printf(
                    TEXT("Stage5PerformanceCaptureOffTransaction case=%d count=%d"),
                    CurrentCaseIndex,
                    CurrentResult.TransactionDurationsMs.Num()));
                CurrentResult.bSuccess = false;
            }
        }
        Results.Add(CurrentResult);
        if (IsCaptureOnPhase())
        {
            const bool bManifestValid =
                RunResult == EShipRunResult::Success &&
                IsValid(Capture) &&
                ValidateCaptureManifest(*Capture, CurrentResult);
            const bool bCleanupSucceeded =
                bManifestValid &&
                CaptureRunTracker.CleanupExact(
                    CurrentResult.RunDirectory);
            if (bManifestValid && !bCleanupSucceeded)
            {
                RecordFailure(FString::Printf(
                    TEXT("Stage5CaptureCleanupFailed case=%d path=%s"),
                    CurrentCaseIndex,
                    *CurrentResult.RunDirectory));
            }
            CurrentResult.bSuccess =
                CurrentResult.bSuccess &&
                bManifestValid &&
                bCleanupSucceeded;
            Results.Last() = CurrentResult;
        }
        else if (IsValid(Capture))
        {
            const FString CaptureOffRunDirectory =
                FShipCaptureAutomationAccessor::RunDirectory(*Capture);
            const bool bCaptureOffUnchanged = CheckAcceptance(
                CaptureOffRunDirectory.IsEmpty() &&
                    !Capture->IsCaptureActive() &&
                    !Capture->HasCaptureFailure(),
                FString::Printf(
                    TEXT("Stage5CaptureOffTerminalWrite case=%d"),
                    CurrentCaseIndex));
            CurrentResult.bSuccess =
                CurrentResult.bSuccess && bCaptureOffUnchanged;
            Results.Last() = CurrentResult;
        }
        return StartNextCase();
    }

    void BeginCurrentResult(const ASimGameMode* GameMode)
    {
        CurrentResult = FStage5CaseResult{};
        CurrentResult.Phase = ValidationPhase;
        CurrentResult.SlideCm = CurrentExpectedSlide();
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
        if (CurrentCaseIndex >= CurrentSlides().Num())
        {
            if (ValidationPhase ==
                EStage5ValidationPhase::RegressionCaptureOff)
            {
                ValidationPhase =
                    EStage5ValidationPhase::CaptureOnAcceptance;
                CurrentCaseIndex = 0;
            }
            else
            {
                ValidationPhase = EStage5ValidationPhase::Finished;
                Finalize();
                return true;
            }
        }
        if (!IsValid(World))
        {
            RecordFailure(TEXT("Stage4TravelSourceWorldUnavailable"));
            Finalize();
            return true;
        }

        const FString Options = IsCaptureOnPhase()
            ? FString::Printf(
                TEXT("Stage4Slide=%.0f"),
                CurrentExpectedSlide())
            : FString::Printf(
                TEXT("Stage4Slide=%.0f?Stage5Capture=0"),
                CurrentExpectedSlide());
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

    void FinalizePerformance()
    {
        const TArray<FStage5PerformanceCondition>& Conditions =
            GetStage5PerformanceConditions();
        TArray<double> ConditionAFrameTimes;
        TArray<double> ConditionBFrameTimes;
        TArray<double> AggregateBIntervals;

        const int32 ComparableCount =
            FMath::Min(Results.Num(), Conditions.Num());
        for (int32 Index = 0; Index < ComparableCount; ++Index)
        {
            FStage5CaseResult& Result = Results[Index];
            const FStage5PerformanceCondition& Condition =
                Conditions[Index];
            bool bResultValid = Result.bSuccess;
            bResultValid = CheckAcceptance(
                Result.Phase == EStage5ValidationPhase::Performance,
                FString::Printf(
                    TEXT("Stage5PerformancePhase order=%d"),
                    Index + 1)) && bResultValid;
            bResultValid = CheckAcceptance(
                FMath::IsNearlyZero(Result.SlideCm, 1e-9),
                FString::Printf(
                    TEXT("Stage5PerformanceSlide order=%d value=%.6f"),
                    Index + 1,
                    Result.SlideCm)) && bResultValid;
            bResultValid = CheckAcceptance(
                FMath::IsFinite(Result.MinimumWallDistanceCm) &&
                    Result.MinimumWallDistanceCm > 0.0 &&
                    Result.MinimumWallDistanceCm <
                        TNumericLimits<double>::Max(),
                FString::Printf(
                    TEXT("Stage5PerformanceWallGap order=%d value=%.6f"),
                    Index + 1,
                    Result.MinimumWallDistanceCm)) && bResultValid;

            const FStage5Statistics FrameStatistics =
                CalculateStage5Statistics(Result.FrameTimesMs);
            const bool bFrameSamplesValid =
                FrameStatistics.bValid &&
                !Result.FrameTimesMs.ContainsByPredicate(
                    [](double Sample)
                    {
                        return !FMath::IsFinite(Sample) || Sample <= 0.0;
                    });
            bResultValid = CheckAcceptance(
                bFrameSamplesValid,
                FString::Printf(
                    TEXT("Stage5PerformanceFrameSamples order=%d count=%d"),
                    Index + 1,
                    Result.FrameTimesMs.Num())) && bResultValid;

            if (Condition.bCaptureEnabled)
            {
                const FStage5Statistics TransactionStatistics =
                    CalculateStage5Statistics(
                        Result.TransactionDurationsMs);
                const FStage5Statistics IntervalStatistics =
                    CalculateStage5Statistics(
                        Result.CaptureIntervalsMs);
                bResultValid = CheckAcceptance(
                    TransactionStatistics.bValid &&
                        Result.TransactionDurationsMs.Num() ==
                            Result.FrameCount,
                    FString::Printf(
                        TEXT("Stage5PerformanceTransactionData order=%d frames=%d transactions=%d"),
                        Index + 1,
                        Result.FrameCount,
                        Result.TransactionDurationsMs.Num())) &&
                    bResultValid;
                bResultValid = CheckAcceptance(
                    IntervalStatistics.bValid &&
                        Result.CaptureIntervalsMs.Num() ==
                            Result.FrameCount - 1,
                    FString::Printf(
                        TEXT("Stage5PerformanceIntervalData order=%d frames=%d intervals=%d"),
                        Index + 1,
                        Result.FrameCount,
                        Result.CaptureIntervalsMs.Num())) && bResultValid;
                ConditionBFrameTimes.Append(Result.FrameTimesMs);
                AggregateBIntervals.Append(Result.CaptureIntervalsMs);
            }
            else
            {
                bResultValid = CheckAcceptance(
                    Result.FrameCount == 0 &&
                        Result.TransactionDurationsMs.IsEmpty() &&
                        Result.CaptureIntervalsMs.IsEmpty() &&
                        Result.RunDirectory.IsEmpty(),
                    FString::Printf(
                        TEXT("Stage5PerformanceCaptureOffWrite order=%d frames=%d transactions=%d intervals=%d"),
                        Index + 1,
                        Result.FrameCount,
                        Result.TransactionDurationsMs.Num(),
                        Result.CaptureIntervalsMs.Num())) && bResultValid;
                ConditionAFrameTimes.Append(Result.FrameTimesMs);
            }
            Result.bSuccess = bResultValid;
        }

        const FStage5Statistics ConditionAStatistics =
            CalculateStage5Statistics(ConditionAFrameTimes);
        const FStage5Statistics ConditionBStatistics =
            CalculateStage5Statistics(ConditionBFrameTimes);
        const FStage5Statistics AggregateBIntervalStatistics =
            CalculateStage5Statistics(AggregateBIntervals);
        TArray<double> AggregateBDeviations;
        for (const double IntervalMs : AggregateBIntervals)
        {
            AggregateBDeviations.Add(FMath::Abs(IntervalMs - 100.0));
        }
        const FStage5Statistics AggregateBDeviationStatistics =
            CalculateStage5Statistics(AggregateBDeviations);
        CheckAcceptance(
            ConditionAStatistics.bValid,
            TEXT("Stage5PerformanceAggregateAEmpty"));
        CheckAcceptance(
            ConditionBStatistics.bValid,
            TEXT("Stage5PerformanceAggregateBEmpty"));
        CheckAcceptance(
            AggregateBIntervalStatistics.bValid &&
                AggregateBDeviationStatistics.bValid,
            TEXT("Stage5PerformanceAggregateIntervalEmpty"));

        int32 SuccessCount = 0;
        int32 ConditionACount = 0;
        int32 ConditionBCount = 0;
        for (int32 Index = 0; Index < Results.Num(); ++Index)
        {
            SuccessCount += Results[Index].bSuccess ? 1 : 0;
            if (Index < Conditions.Num() &&
                Conditions[Index].bCaptureEnabled)
            {
                ++ConditionBCount;
            }
            else
            {
                ++ConditionACount;
            }
        }

        const TArray<FString> FinalCaptureChildren =
            CaptureRunTracker.DirectChildDirectories();
        Test->TestEqual(
            TEXT("performance run count"),
            Results.Num(),
            Conditions.Num());
        Test->TestEqual(
            TEXT("performance success count"),
            SuccessCount,
            4);
        Test->TestEqual(TEXT("condition A count"), ConditionACount, 2);
        Test->TestEqual(TEXT("condition B count"), ConditionBCount, 2);
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
        Test->TestEqual(
            TEXT("capture failure count"),
            CaptureFailureCount,
            0);
        Test->TestEqual(
            TEXT("performance fresh world identity count"),
            ObservedWorldIdentities.Num(),
            Conditions.Num());
        Test->TestTrue(
            TEXT("performance capture runs are cleaned"),
            CaptureRunTracker.AllTrackedRunsCleaned());
        Test->TestEqual(
            TEXT("performance preserves pre-existing capture children"),
            FString::Join(FinalCaptureChildren, TEXT("|")),
            FString::Join(
                CaptureRunTracker.PreExistingChildren,
                TEXT("|")));

        for (int32 Index = 0; Index < ComparableCount; ++Index)
        {
            const FStage5CaseResult& Result = Results[Index];
            const FStage5Statistics Statistics =
                CalculateStage5Statistics(Result.FrameTimesMs);
            UE_LOG(
                LogTemp,
                Display,
                TEXT("Stage5PerformanceRun order=%d condition=%s outcome=%s slide=%.0f world=%d sample_count=%d frame_min_ms=%.3f frame_median_ms=%.3f frame_p95_ms=%.3f frame_max_ms=%.3f elapsed_s=%.3f min_wall_gap_cm=%.3f frames=%d"),
                Index + 1,
                Conditions[Index].Name,
                Result.bSuccess ? TEXT("success") : TEXT("failure"),
                Result.SlideCm,
                Result.WorldId,
                Statistics.SampleCount,
                Statistics.Minimum,
                Statistics.Median,
                Statistics.P95,
                Statistics.Maximum,
                Result.ElapsedSeconds,
                Result.MinimumWallDistanceCm,
                Result.FrameCount);
        }
        for (const TPair<const TCHAR*, const FStage5Statistics*> Aggregate : {
                 TPair<const TCHAR*, const FStage5Statistics*>(
                     TEXT("A"),
                     &ConditionAStatistics),
                 TPair<const TCHAR*, const FStage5Statistics*>(
                     TEXT("B"),
                     &ConditionBStatistics)})
        {
            UE_LOG(
                LogTemp,
                Display,
                TEXT("Stage5PerformanceAggregate condition=%s sample_count=%d frame_min_ms=%.3f frame_median_ms=%.3f frame_p95_ms=%.3f frame_max_ms=%.3f"),
                Aggregate.Key,
                Aggregate.Value->SampleCount,
                Aggregate.Value->Minimum,
                Aggregate.Value->Median,
                Aggregate.Value->P95,
                Aggregate.Value->Maximum);
        }

        int32 AggregateMissedSlotCount = 0;
        int32 AggregateDuplicateCount = 0;
        int32 AggregateCatchUpCount = 0;
        for (const double IntervalMs : AggregateBIntervals)
        {
            AggregateMissedSlotCount += IntervalMs >= 200.0 ? 1 : 0;
            AggregateDuplicateCount += IntervalMs <= 0.0 ? 1 : 0;
            AggregateCatchUpCount +=
                IntervalMs > 0.0 && IntervalMs < 100.0 ? 1 : 0;
        }
        for (int32 Index = 0; Index < ComparableCount; ++Index)
        {
            if (!Conditions[Index].bCaptureEnabled)
            {
                continue;
            }
            const FStage5CaseResult& Result = Results[Index];
            const FStage5Statistics TransactionStatistics =
                CalculateStage5Statistics(
                    Result.TransactionDurationsMs);
            UE_LOG(
                LogTemp,
                Display,
                TEXT("Stage5PerformanceTransaction order=%d condition=B count=%d min_ms=%.3f median_ms=%.3f p95_ms=%.3f max_ms=%.3f"),
                Index + 1,
                TransactionStatistics.SampleCount,
                TransactionStatistics.Minimum,
                TransactionStatistics.Median,
                TransactionStatistics.P95,
                TransactionStatistics.Maximum);

            const FStage5Statistics IntervalStatistics =
                CalculateStage5Statistics(Result.CaptureIntervalsMs);
            TArray<double> Deviations;
            int32 MissedSlotCount = 0;
            int32 DuplicateCount = 0;
            int32 CatchUpCount = 0;
            for (const double IntervalMs : Result.CaptureIntervalsMs)
            {
                Deviations.Add(FMath::Abs(IntervalMs - 100.0));
                MissedSlotCount += IntervalMs >= 200.0 ? 1 : 0;
                DuplicateCount += IntervalMs <= 0.0 ? 1 : 0;
                CatchUpCount += IntervalMs > 0.0 && IntervalMs < 100.0
                    ? 1
                    : 0;
            }
            const FStage5Statistics DeviationStatistics =
                CalculateStage5Statistics(Deviations);
            UE_LOG(
                LogTemp,
                Display,
                TEXT("Stage5PerformanceInterval order=%d condition=B pair_count=%d target_ms=100 interval_min_ms=%.3f interval_median_ms=%.3f interval_p95_ms=%.3f interval_max_ms=%.3f deviation_min_ms=%.3f deviation_median_ms=%.3f deviation_p95_ms=%.3f deviation_max_ms=%.3f missed_ge_200=%d duplicate=%d catch_up_under_100=%d aggregate_pair_count=%d aggregate_interval_min_ms=%.3f aggregate_interval_median_ms=%.3f aggregate_interval_p95_ms=%.3f aggregate_interval_max_ms=%.3f aggregate_deviation_min_ms=%.3f aggregate_deviation_median_ms=%.3f aggregate_deviation_p95_ms=%.3f aggregate_deviation_max_ms=%.3f aggregate_missed_ge_200=%d aggregate_duplicate=%d aggregate_catch_up_under_100=%d"),
                Index + 1,
                IntervalStatistics.SampleCount,
                IntervalStatistics.Minimum,
                IntervalStatistics.Median,
                IntervalStatistics.P95,
                IntervalStatistics.Maximum,
                DeviationStatistics.Minimum,
                DeviationStatistics.Median,
                DeviationStatistics.P95,
                DeviationStatistics.Maximum,
                MissedSlotCount,
                DuplicateCount,
                CatchUpCount,
                AggregateBIntervalStatistics.SampleCount,
                AggregateBIntervalStatistics.Minimum,
                AggregateBIntervalStatistics.Median,
                AggregateBIntervalStatistics.P95,
                AggregateBIntervalStatistics.Maximum,
                AggregateBDeviationStatistics.Minimum,
                AggregateBDeviationStatistics.Median,
                AggregateBDeviationStatistics.P95,
                AggregateBDeviationStatistics.Maximum,
                AggregateMissedSlotCount,
                AggregateDuplicateCount,
                AggregateCatchUpCount);
        }

        for (const TPair<FString, int32>& Failure : FailureCounts)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("Stage5PerformanceFailure reason=%s count=%d"),
                *Failure.Key,
                Failure.Value);
        }
        const bool bSummarySucceeded =
            Results.Num() == Conditions.Num() &&
            SuccessCount == Conditions.Num() &&
            ConditionACount == 2 &&
            ConditionBCount == 2 &&
            CollisionCount == 0 &&
            TimeoutCount == 0 &&
            SetupFailureCount == 0 &&
            RuntimeCalculationErrorCount == 0 &&
            OtherBlockingCollisionCount == 0 &&
            CaptureFailureCount == 0 &&
            FailureCounts.IsEmpty() &&
            ConditionAStatistics.bValid &&
            ConditionBStatistics.bValid &&
            AggregateBIntervalStatistics.bValid &&
            AggregateBDeviationStatistics.bValid &&
            CaptureRunTracker.AllTrackedRunsCleaned() &&
            FinalCaptureChildren ==
                CaptureRunTracker.PreExistingChildren;
        UE_LOG(
            LogTemp,
            Display,
            TEXT("Stage5PerformanceSummary runs=%d success=%d failure=%d error=%d unknown=0 ensure=0"),
            Results.Num(),
            SuccessCount,
            FMath::Max(0, Conditions.Num() - SuccessCount),
            bSummarySucceeded ? 0 : 1);
    }

    void Finalize()
    {
        if (State == EState::Finished)
        {
            return;
        }
        State = EState::Finished;

        if (bPerformanceMode)
        {
            FinalizePerformance();
            return;
        }

        int32 SuccessCount = 0;
        int32 CaptureOffCount = 0;
        int32 CaptureOnCount = 0;
        for (const FStage5CaseResult& Result : Results)
        {
            SuccessCount += Result.bSuccess ? 1 : 0;
            CaptureOffCount += Result.Phase ==
                    EStage5ValidationPhase::RegressionCaptureOff
                ? 1
                : 0;
            CaptureOnCount += Result.Phase ==
                    EStage5ValidationPhase::CaptureOnAcceptance
                ? 1
                : 0;
        }
        Test->TestEqual(TEXT("fresh world case count"),
            Results.Num(), GetStage5ValidationCaseCount());
        Test->TestEqual(TEXT("success count"), SuccessCount, 14);
        Test->TestEqual(TEXT("capture-off count"), CaptureOffCount, 11);
        Test->TestEqual(TEXT("capture-on count"), CaptureOnCount, 3);
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
        Test->TestEqual(
            TEXT("capture failure count"),
            CaptureFailureCount,
            0);
        Test->TestEqual(
            TEXT("fresh world identity count"),
            ObservedWorldIdentities.Num(),
            GetStage5ValidationCaseCount());
        Test->TestTrue(
            TEXT("all tracked capture runs are cleaned"),
            CaptureRunTracker.AllTrackedRunsCleaned());
        const TArray<FString> FinalCaptureChildren =
            CaptureRunTracker.DirectChildDirectories();
        Test->TestEqual(
            TEXT("pre-existing capture children are preserved"),
            FString::Join(FinalCaptureChildren, TEXT("|")),
            FString::Join(
                CaptureRunTracker.PreExistingChildren,
                TEXT("|")));

        const int32 ComparableCount =
            FMath::Min(
                Results.Num(),
                GetStage5ValidationCaseCount());
        for (int32 Index = 0; Index < ComparableCount; ++Index)
        {
            const bool bExpectedCaptureOn = Index >= ExpectedSlides.Num();
            const int32 PhaseIndex = bExpectedCaptureOn
                ? Index - ExpectedSlides.Num()
                : Index;
            const double ExpectedSlide = bExpectedCaptureOn
                ? GetStage5CaptureOnSlides()[PhaseIndex]
                : ExpectedSlides[PhaseIndex];
            Test->TestTrue(
                *FString::Printf(TEXT("slide order %d"), Index),
                FMath::IsNearlyEqual(
                    Results[Index].SlideCm,
                    ExpectedSlide,
                    1e-6));
            Test->TestEqual(
                *FString::Printf(TEXT("phase order %d"), Index),
                Results[Index].Phase,
                bExpectedCaptureOn
                    ? EStage5ValidationPhase::CaptureOnAcceptance
                    : EStage5ValidationPhase::RegressionCaptureOff);
            Test->TestTrue(
                *FString::Printf(TEXT("minimum wall distance case %d"), Index),
                FMath::IsFinite(Results[Index].MinimumWallDistanceCm)
                    && Results[Index].MinimumWallDistanceCm > 0.0
                    && Results[Index].MinimumWallDistanceCm
                        < TNumericLimits<double>::Max());
            if (bExpectedCaptureOn)
            {
                Test->TestTrue(
                    *FString::Printf(
                        TEXT("capture-on frame count %d"),
                        Index),
                    Results[Index].FrameCount >= 1);
                Test->TestTrue(
                    *FString::Printf(
                        TEXT("capture-on final time %d"),
                        Index),
                    Results[Index].LastFrameTimeMs >= 0);
            }
        }

        for (const FStage5CaseResult& Result : Results)
        {
            UE_LOG(
                LogTemp,
                Display,
                TEXT("Stage5ActualWorldCase phase=%s slide=%.0f outcome=%s world=%d elapsed_s=%.3f min_wall_gap_cm=%.3f frames=%d last_time_ms=%lld"),
                Stage5ValidationPhaseName(Result.Phase),
                Result.SlideCm,
                Result.bSuccess ? TEXT("success") : TEXT("failure"),
                Result.WorldId,
                Result.ElapsedSeconds,
                Result.MinimumWallDistanceCm,
                Result.FrameCount,
                Result.LastFrameTimeMs);
        }
        for (const TPair<FString, int32>& Failure : FailureCounts)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("Stage5ActualWorldFailure reason=%s count=%d"),
                *Failure.Key,
                Failure.Value);
        }

        const bool bSummarySucceeded =
            Results.Num() == GetStage5ValidationCaseCount() &&
            SuccessCount == GetStage5ValidationCaseCount() &&
            CaptureOffCount == ExpectedSlides.Num() &&
            CaptureOnCount == GetStage5CaptureOnSlides().Num() &&
            CollisionCount == 0 &&
            TimeoutCount == 0 &&
            SetupFailureCount == 0 &&
            RuntimeCalculationErrorCount == 0 &&
            OtherBlockingCollisionCount == 0 &&
            CaptureFailureCount == 0 &&
            FailureCounts.IsEmpty() &&
            CaptureRunTracker.AllTrackedRunsCleaned() &&
            FinalCaptureChildren ==
                CaptureRunTracker.PreExistingChildren;
        UE_LOG(
            LogTemp,
            Display,
            TEXT("Stage5ActualWorldSummary cases=%d success=%d failure=%d error=%d unknown=0 ensure=0"),
            Results.Num(),
            SuccessCount,
            FMath::Max(0, GetStage5ValidationCaseCount() - SuccessCount),
            bSummarySucceeded ? 0 : 1);
    }

    FAutomationTestBase* Test = nullptr;
    EState State = EState::WaitingForWorld;
    bool bPerformanceMode = false;
    EStage5ValidationPhase ValidationPhase =
        EStage5ValidationPhase::RegressionCaptureOff;
    int32 CurrentCaseIndex = 0;
    int32 PreviousWorldId = INDEX_NONE;
    TWeakObjectPtr<UWorld> PreviousWorldIdentity;
    TWeakObjectPtr<UWorld> CurrentWorld;
    double OverallDeadlineSeconds = 0.0;
    double WorldLoadDeadlineSeconds = 0.0;
    double CaseDeadlineSeconds = 0.0;
    double PerformanceWarmupEndSeconds = 0.0;
    double LastPerformanceUpdateSeconds = -1.0;
    FStage5CaseResult CurrentResult;
    TArray<FStage5CaseResult> Results;
    TMap<FString, int32> FailureCounts;
    TArray<TWeakObjectPtr<UWorld>> ObservedWorldIdentities;
    FStage5CaptureRunTracker CaptureRunTracker;
    int32 CollisionCount = 0;
    int32 TimeoutCount = 0;
    int32 SetupFailureCount = 0;
    int32 RuntimeCalculationErrorCount = 0;
    int32 OtherBlockingCollisionCount = 0;
    int32 CaptureFailureCount = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipNavigationActualWorldSweepTest,
    "ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FShipNavigationActualWorldSweepTest::RunTest(const FString&)
{
    TestEqual(
        TEXT("Stage 5 validation has fourteen fresh worlds"),
        GetStage5ValidationCaseCount(),
        14);
    TestEqual(
        TEXT("Stage 5 validation has three capture-on slides"),
        GetStage5CaptureOnSlides().Num(),
        3);
    TestEqual(
        TEXT("Stage 5 performance has A-B-B-A runs"),
        GetStage5PerformanceConditions().Num(),
        4);
    const FStage5Statistics Statistics =
        CalculateStage5Statistics({4.0, 1.0, 3.0, 2.0});
    TestEqual(TEXT("Statistics sample count"), Statistics.SampleCount, 4);
    TestEqual(TEXT("Statistics even median"), Statistics.Median, 2.5);
    TestEqual(TEXT("Statistics nearest-rank p95"), Statistics.P95, 4.0);
    const bool bPerformanceMode = FParse::Param(
        FCommandLine::Get(),
        TEXT("Stage5Performance"));
    ADD_LATENT_AUTOMATION_COMMAND(
        FRunShipNavigationSweepCommand(this, bPerformanceMode));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
