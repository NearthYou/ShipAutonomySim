#include "ShipCapture.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "CoreGlobals.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShipCaptureBundle.h"
#include "ShipPawn.h"
#include "ShipCaptureSimulation.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace
{
const TCHAR* CaptureFailureCategoryName(uint8 CategoryValue)
{
    switch (CategoryValue)
    {
    case 1:
        return TEXT("invalid_configuration");
    case 2:
        return TEXT("rig_mismatch");
    case 3:
        return TEXT("target_unavailable");
    case 4:
        return TEXT("readback");
    case 5:
        return TEXT("pixel_count");
    case 6:
        return TEXT("depth_normalization");
    case 7:
        return TEXT("png_encode");
    case 8:
        return TEXT("directory_create");
    case 9:
        return TEXT("path_collision");
    case 10:
        return TEXT("temp_write");
    case 11:
        return TEXT("frame_rename");
    case 12:
        return TEXT("pair_cleanup");
    case 13:
        return TEXT("frame_record");
    case 14:
        return TEXT("invalid_clock");
    case 15:
        return TEXT("manifest_serialize");
    case 16:
        return TEXT("manifest_write");
    case 17:
        return TEXT("manifest_rename");
    default:
        return TEXT("none");
    }
}
}

UShipCapture::UShipCapture()
{
	PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UShipCapture::BindCaptureRig(
    USceneComponent* InCaptureMount,
    USceneCaptureComponent2D* InColorCapture,
    USceneCaptureComponent2D* InDepthCapture)
{
    CaptureMount = InCaptureMount;
    ColorCapture = InColorCapture;
    DepthCapture = InDepthCapture;
}

bool UShipCapture::SetupCaptureRig()
{
    if (!IsValid(CaptureMount) ||
        !IsValid(ColorCapture) ||
        !IsValid(DepthCapture) ||
        CaptureResolution < 1 ||
        !FMath::IsFinite(CaptureFovDegrees) ||
        CaptureFovDegrees <= 0.0f ||
        CaptureFovDegrees >= 180.0f ||
        CaptureRelativeLocationCm.ContainsNaN() ||
        CaptureRelativeRotation.ContainsNaN() ||
        CaptureIntervalMs < 1 ||
        !FMath::IsFinite(DepthNearCm) ||
        !FMath::IsFinite(DepthFarCm) ||
        DepthNearCm >= DepthFarCm ||
        ColorCapture->GetAttachParent() != CaptureMount ||
        DepthCapture->GetAttachParent() != CaptureMount ||
        !ColorCapture->GetRelativeTransform().Equals(FTransform::Identity) ||
        !DepthCapture->GetRelativeTransform().Equals(FTransform::Identity))
    {
        return false;
    }

    CaptureMount->SetRelativeLocationAndRotation(CaptureRelativeLocationCm, CaptureRelativeRotation);

    for (USceneCaptureComponent2D* Capture :
         {ColorCapture.Get(), DepthCapture.Get()})
    {
        Capture->ProjectionType = ECameraProjectionMode::Perspective;
        Capture->FOVAngle = CaptureFovDegrees;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->bAlwaysPersistRenderingState = false;
    }
    ColorCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    DepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;

    ColorCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    ColorCapture->PostProcessSettings.AutoExposureMethod =
        EAutoExposureMethod::AEM_Manual;
    ColorCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
    ColorCapture->PostProcessSettings.AutoExposureBias = 0.0f;
    ColorCapture->PostProcessSettings
        .bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    ColorCapture->PostProcessSettings
        .AutoExposureApplyPhysicalCameraExposure = false;
    ColorCapture->PostProcessBlendWeight = 1.0f;

    if (!IsValid(ColorTarget))
    {
        ColorTarget = NewObject<UTextureRenderTarget2D>(
            this, NAME_None, RF_Transient);
    }
    if (!IsValid(DepthTarget))
    {
        DepthTarget = NewObject<UTextureRenderTarget2D>(
            this, NAME_None, RF_Transient);
    }
    if (!IsValid(ColorTarget) || !IsValid(DepthTarget))
    {
        return false;
    }

    ColorTarget->InitCustomFormat(
        CaptureResolution,
        CaptureResolution,
        PF_B8G8R8A8,
        false);
    DepthTarget->InitCustomFormat(
        CaptureResolution,
        CaptureResolution,
        PF_R32_FLOAT,
        true);
    ColorCapture->TextureTarget = ColorTarget;
    DepthCapture->TextureTarget = DepthTarget;
    return ColorTarget->GetFormat() == PF_B8G8R8A8 &&
        DepthTarget->GetFormat() == PF_R32_FLOAT;
}

bool UShipCapture::StartCapture(double WallSlideCm)
{
    return StartCaptureAt(WallSlideCm, FPlatformTime::Seconds());
}

bool UShipCapture::CreateUniqueRunDirectory()
{
    FString RootDirectory =
        FPaths::ProjectSavedDir() / TEXT("ShipCaptures");
    FString RelativeRoot = TEXT("ShipCaptures");
#if WITH_DEV_AUTOMATION_TESTS
    if (GIsAutomationTesting)
    {
        RootDirectory /= TEXT("Automation");
        RelativeRoot /= TEXT("Automation");
    }
#endif
    FPaths::NormalizeDirectoryName(RootDirectory);
    if (!IFileManager::Get().MakeDirectory(*RootDirectory, true))
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::DirectoryCreate,
            NextFrameIndex);
    }

    const FString RunLeafName = FString::Printf(
        TEXT("%s_%s"),
        *FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S%sZ")),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString CandidateRunDirectory = RootDirectory / RunLeafName;
    if (IFileManager::Get().DirectoryExists(*CandidateRunDirectory) ||
        IFileManager::Get().FileExists(*CandidateRunDirectory))
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::PathCollision,
            NextFrameIndex);
    }
    if (!IFileManager::Get().MakeDirectory(
            *CandidateRunDirectory,
            false))
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::DirectoryCreate,
            NextFrameIndex);
    }

    RunDirectoryPath = CandidateRunDirectory;
    FPaths::NormalizeDirectoryName(RunDirectoryPath);
    RunRelativePath = RelativeRoot / RunLeafName;
    FPaths::NormalizeFilename(RunRelativePath);
    return true;
}

bool UShipCapture::StartCaptureAt(
    double WallSlideCm,
    double NowSeconds)
{
    if (LifecycleState != EShipCaptureLifecycleState::NotStarted)
    {
        return false;
    }
    double NewFirstCaptureSeconds = 0.0;
    double NewLastClockSeconds = 0.0;
    double NewAccumulatedRealSeconds = 0.0;
    int64 NewCaptureTimeMs = 0;
    if (!FMath::IsFinite(WallSlideCm) ||
        !InitializeCaptureClock(
            NowSeconds,
            NewFirstCaptureSeconds,
            NewLastClockSeconds,
            NewAccumulatedRealSeconds,
            NewCaptureTimeMs) ||
        !SetupCaptureRig())
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::InvalidConfiguration,
            NextFrameIndex);
    }
    CapturedWallSlideCm = WallSlideCm;
    if (!CreateUniqueRunDirectory() ||
        !CaptureEncodeAndPublishFrame(0, 0.0, 0))
    {
        return false;
    }

    FirstCaptureSeconds = NewFirstCaptureSeconds;
    LastClockSeconds = NewLastClockSeconds;
    AccumulatedRealSeconds = NewAccumulatedRealSeconds;
    LastCommittedTimeMs = NewCaptureTimeMs;
    LifecycleState = EShipCaptureLifecycleState::Capturing;
    SetComponentTickEnabled(true);
    UE_LOG(
        LogTemp,
        Display,
        TEXT("Stage5CaptureStarted run=%s resolution=%d interval_ms=%d"),
        *RunRelativePath,
        CaptureResolution,
        CaptureIntervalMs);
    return true;
}

bool UShipCapture::CleanupPairPaths(
    const FString& ColorTempPath,
    const FString& DepthTempPath,
    const FString& ColorFinalPath,
    const FString& DepthFinalPath)
{
    bool bCleanupSucceeded = true;
    for (const FString* Path : {
             &ColorTempPath,
             &DepthTempPath,
             &ColorFinalPath,
             &DepthFinalPath})
    {
        if (IFileManager::Get().FileExists(**Path) &&
            !IFileManager::Get().Delete(**Path, true, false, true))
        {
            bCleanupSucceeded = false;
        }
    }
    if (!bCleanupSucceeded)
    {
        bPairCleanupFailureLatched = true;
        if (!bCaptureFailureLatched)
        {
            LatchCaptureFailure(
                EShipCaptureFailureCategory::PairCleanup,
                NextFrameIndex);
        }
    }
    return bCleanupSucceeded;
}

bool UShipCapture::CaptureEncodeAndPublishFrame(
    int32 FrameIndex,
    double CaptureSeconds,
    int64 CaptureTimeMs)
{
    const double TransactionStartSeconds = FPlatformTime::Seconds();
    if (FrameIndex != NextFrameIndex ||
        !FMath::IsFinite(CaptureSeconds) ||
        CaptureSeconds < 0.0 ||
        CaptureTimeMs < 0 ||
        RunDirectoryPath.IsEmpty())
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::FrameRecord,
            FrameIndex);
    }

    FString ColorLeafName;
    FString DepthLeafName;
    if (!MakeCaptureFrameLeafNames(
            FrameIndex,
            ColorLeafName,
            DepthLeafName))
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::FrameRecord,
            FrameIndex);
    }
    const FString ColorTempPath =
        RunDirectoryPath /
        FString::Printf(TEXT(".%s.tmp"), *ColorLeafName);
    const FString DepthTempPath =
        RunDirectoryPath /
        FString::Printf(TEXT(".%s.tmp"), *DepthLeafName);
    const FString ColorFinalPath = RunDirectoryPath / ColorLeafName;
    const FString DepthFinalPath = RunDirectoryPath / DepthLeafName;
    for (const FString* Path : {
             &ColorTempPath,
             &DepthTempPath,
             &ColorFinalPath,
             &DepthFinalPath})
    {
        if (IFileManager::Get().FileExists(**Path) ||
            IFileManager::Get().DirectoryExists(**Path))
        {
            return LatchCaptureFailure(
                EShipCaptureFailureCategory::PathCollision,
                FrameIndex);
        }
    }

    TArray64<uint8> ColorPngBytes;
    TArray64<uint8> DepthPngBytes;
    if (!CaptureAndEncodePair(
            FrameIndex,
            CaptureSeconds,
            ColorPngBytes,
            DepthPngBytes))
    {
        return false;
    }

    const bool bColorTempWritten = FFileHelper::SaveArrayToFile(
        ColorPngBytes,
        *ColorTempPath);
    bool bDepthTempWritten = false;
#if WITH_DEV_AUTOMATION_TESTS
    if (TestFailurePoint !=
        EShipCaptureTestFailurePoint::DepthTempWrite)
#endif
    {
        bDepthTempWritten = FFileHelper::SaveArrayToFile(
            DepthPngBytes,
            *DepthTempPath);
    }
    if (!bColorTempWritten ||
        !bDepthTempWritten ||
        !IFileManager::Get().FileExists(*ColorTempPath) ||
        !IFileManager::Get().FileExists(*DepthTempPath) ||
        IFileManager::Get().FileSize(*ColorTempPath) <= 0 ||
        IFileManager::Get().FileSize(*DepthTempPath) <= 0)
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::TempWrite,
            FrameIndex);
        CleanupPairPaths(
            ColorTempPath,
            DepthTempPath,
            ColorFinalPath,
            DepthFinalPath);
        return false;
    }

    const bool bColorRenamed = IFileManager::Get().Move(
        *ColorFinalPath,
        *ColorTempPath,
        false,
        false,
        false,
        true);
    bool bDepthRenamed = false;
#if WITH_DEV_AUTOMATION_TESTS
    if (TestFailurePoint !=
        EShipCaptureTestFailurePoint::DepthFrameRename)
#endif
    {
        bDepthRenamed = IFileManager::Get().Move(
            *DepthFinalPath,
            *DepthTempPath,
            false,
            false,
            false,
            true);
    }
    if (!bColorRenamed ||
        !bDepthRenamed ||
        !IFileManager::Get().FileExists(*ColorFinalPath) ||
        !IFileManager::Get().FileExists(*DepthFinalPath) ||
        IFileManager::Get().FileSize(*ColorFinalPath) <= 0 ||
        IFileManager::Get().FileSize(*DepthFinalPath) <= 0)
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::FrameRename,
            FrameIndex);
        CleanupPairPaths(
            ColorTempPath,
            DepthTempPath,
            ColorFinalPath,
            DepthFinalPath);
        return false;
    }

    const FShipCaptureFrameRecord Candidate{
        FrameIndex,
        ColorLeafName,
        DepthLeafName,
        CaptureTimeMs};
    if (!ValidateAndAppendCaptureFrame(Candidate, Frames))
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::FrameRecord,
            FrameIndex);
        CleanupPairPaths(
            ColorTempPath,
            DepthTempPath,
            ColorFinalPath,
            DepthFinalPath);
        return false;
    }
    NextFrameIndex = Frames.Num();
    const double TransactionEndSeconds = FPlatformTime::Seconds();
    const double TransactionMilliseconds =
        FMath::Max(
            0.0,
            (TransactionEndSeconds - TransactionStartSeconds) * 1000.0);
    if (FMath::IsFinite(TransactionMilliseconds))
    {
        TransactionDurationsMs.Add(TransactionMilliseconds);
    }
    UE_LOG(
        LogTemp,
        Display,
        TEXT("Stage5CapturePair index=%d time_ms=%lld transaction_ms=%.3f run=%s"),
        FrameIndex,
        CaptureTimeMs,
        TransactionMilliseconds,
        *RunRelativePath);
    return true;
}

void UShipCapture::TickAtTime(double NowSeconds)
{
    if (LifecycleState != EShipCaptureLifecycleState::Capturing)
    {
        return;
    }
    const FShipCaptureScheduleStep Schedule = AdvanceCaptureSchedule(
        NowSeconds,
        FirstCaptureSeconds,
        LastClockSeconds,
        AccumulatedRealSeconds,
        LastCommittedTimeMs,
        CaptureIntervalMs);
    if (Schedule.Decision == EShipCaptureScheduleDecision::Invalid)
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::InvalidClock,
            NextFrameIndex);
        return;
    }
    LastClockSeconds = Schedule.NextLastClockSeconds;
    AccumulatedRealSeconds = Schedule.NextAccumulatedRealSeconds;
    if (Schedule.Decision == EShipCaptureScheduleDecision::NotDue)
    {
        return;
    }

    const double CaptureSeconds = NowSeconds - FirstCaptureSeconds;
    if (CaptureEncodeAndPublishFrame(
            NextFrameIndex,
            CaptureSeconds,
            Schedule.CaptureTimeMs))
    {
        LastCommittedTimeMs = Schedule.CaptureTimeMs;
    }
}

bool UShipCapture::HasOpticalEquality() const
{
    return IsValid(CaptureMount) &&
        IsValid(ColorCapture) &&
        IsValid(DepthCapture) &&
        IsValid(ColorTarget) &&
        IsValid(DepthTarget) &&
        ColorCapture->GetAttachParent() == CaptureMount &&
        DepthCapture->GetAttachParent() == CaptureMount &&
        ColorCapture->GetRelativeTransform().Equals(FTransform::Identity) &&
        DepthCapture->GetRelativeTransform().Equals(FTransform::Identity) &&
        ColorCapture->GetComponentTransform().Equals(
            DepthCapture->GetComponentTransform()) &&
        ColorCapture->ProjectionType == ECameraProjectionMode::Perspective &&
        DepthCapture->ProjectionType == ECameraProjectionMode::Perspective &&
        ColorCapture->FOVAngle == CaptureFovDegrees &&
        DepthCapture->FOVAngle == CaptureFovDegrees &&
        !ColorCapture->bCaptureEveryFrame &&
        !ColorCapture->bCaptureOnMovement &&
        !ColorCapture->bAlwaysPersistRenderingState &&
        !DepthCapture->bCaptureEveryFrame &&
        !DepthCapture->bCaptureOnMovement &&
        !DepthCapture->bAlwaysPersistRenderingState &&
        ColorCapture->CaptureSource ==
            ESceneCaptureSource::SCS_FinalColorLDR &&
        DepthCapture->CaptureSource == ESceneCaptureSource::SCS_SceneDepth &&
        ColorCapture->TextureTarget == ColorTarget &&
        DepthCapture->TextureTarget == DepthTarget &&
        ColorTarget->GetFormat() == PF_B8G8R8A8 &&
        DepthTarget->GetFormat() == PF_R32_FLOAT &&
        ColorTarget->SizeX == CaptureResolution &&
        ColorTarget->SizeY == CaptureResolution &&
        DepthTarget->SizeX == CaptureResolution &&
        DepthTarget->SizeY == CaptureResolution;
}

bool UShipCapture::LatchCaptureFailure(
    EShipCaptureFailureCategory FailureCategory,
    int32 FrameIndex)
{
    if (!bCaptureFailureLatched)
    {
        bCaptureFailureLatched = true;
        FirstFailureCategory = FailureCategory;
        FirstFailureFrameIndex = FrameIndex;
#if WITH_DEV_AUTOMATION_TESTS
        ++TestFailureLogCount;
#endif
        const FString SafeRelativePath = RunRelativePath.IsEmpty()
            ? FString(TEXT("ShipCaptures"))
            : RunRelativePath;
        UE_LOG(
            LogTemp,
            Error,
            TEXT("Stage5CaptureFailure category=%s index=%d run=%s"),
            CaptureFailureCategoryName(
                static_cast<uint8>(FailureCategory)),
            FrameIndex,
            *SafeRelativePath);
    }
    LifecycleState = EShipCaptureLifecycleState::CaptureFailed;
    SetComponentTickEnabled(false);
    return false;
}

bool UShipCapture::CaptureAndEncodePair(
    int32 FrameIndex,
    double CaptureSeconds,
    TArray64<uint8>& OutColorPngBytes,
    TArray64<uint8>& OutDepthPngBytes)
{
    OutColorPngBytes.Reset();
    OutDepthPngBytes.Reset();
    const int32 TransactionFrameIndex = FrameIndex;
    const double TransactionCaptureSeconds = CaptureSeconds;
    FString ColorLeafName;
    FString DepthLeafName;
    if (!FMath::IsFinite(TransactionCaptureSeconds) ||
        TransactionCaptureSeconds < 0.0 ||
        !MakeCaptureFrameLeafNames(
            TransactionFrameIndex,
            ColorLeafName,
            DepthLeafName) ||
        !HasOpticalEquality())
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::RigMismatch,
            TransactionFrameIndex);
    }

#if WITH_DEV_AUTOMATION_TESTS
    ++TestColorCaptureSceneCallCount;
    ++TestDepthCaptureSceneCallCount;
#endif
    ColorCapture->CaptureScene();
    DepthCapture->CaptureScene();

    FTextureRenderTargetResource* ColorResource =
        ColorTarget->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* DepthResource =
        DepthTarget->GameThread_GetRenderTargetResource();
    if (ColorResource == nullptr || DepthResource == nullptr)
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::TargetUnavailable,
            TransactionFrameIndex);
    }

    TArray<FColor> ColorPixels;
    TArray<FLinearColor> DepthSamples;
    const FIntRect ReadRect(
        0,
        0,
        CaptureResolution,
        CaptureResolution);
    const bool bColorRead = ColorResource->ReadPixels(
        ColorPixels,
        FReadSurfaceDataFlags(RCM_UNorm),
        ReadRect);
    const bool bDepthRead = DepthResource->ReadLinearColorPixels(
        DepthSamples,
        FReadSurfaceDataFlags(RCM_MinMax),
        ReadRect);
#if WITH_DEV_AUTOMATION_TESTS
    TestColorReadbackPixelCount = ColorPixels.Num();
    TestDepthReadbackPixelCount = DepthSamples.Num();
    TestRawDepthSamples = DepthSamples;
#endif
    if (!bColorRead || !bDepthRead)
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::Readback,
            TransactionFrameIndex);
    }

    const int64 ExpectedPixelCount =
        static_cast<int64>(CaptureResolution) * CaptureResolution;
    if (ExpectedPixelCount < 1 ||
        ExpectedPixelCount > MAX_int32 ||
        ColorPixels.Num() != ExpectedPixelCount ||
        DepthSamples.Num() != ExpectedPixelCount)
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::PixelCount,
            TransactionFrameIndex);
    }

    TArray64<uint8> DepthPixels;
    if (!NormalizeSceneDepthToG8(
            DepthSamples,
            static_cast<int32>(ExpectedPixelCount),
            DepthNearCm,
            DepthFarCm,
            DepthPixels))
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::DepthNormalization,
            TransactionFrameIndex);
    }

    IImageWrapperModule& ImageWrapper =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(
            TEXT("ImageWrapper"));
    const FImageView ColorView(
        ColorPixels.GetData(),
        CaptureResolution,
        CaptureResolution,
        EGammaSpace::sRGB);
    const FImageView DepthView(
        DepthPixels.GetData(),
        CaptureResolution,
        CaptureResolution,
        1,
        ERawImageFormat::G8,
        EGammaSpace::Linear);
    if (!ImageWrapper.CompressImage(
            OutColorPngBytes,
            EImageFormat::PNG,
            ColorView) ||
        !ImageWrapper.CompressImage(
            OutDepthPngBytes,
            EImageFormat::PNG,
            DepthView) ||
        OutColorPngBytes.IsEmpty() ||
        OutDepthPngBytes.IsEmpty())
    {
        OutColorPngBytes.Reset();
        OutDepthPngBytes.Reset();
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::PngEncode,
            TransactionFrameIndex);
    }

    return true;
}

bool UShipCapture::CleanupManifestPaths(
    const FString& ManifestTempPath,
    const FString& ManifestFinalPath)
{
    bool bCleanupSucceeded = true;
    for (const FString* Path : {
             &ManifestTempPath,
             &ManifestFinalPath})
    {
        if (IFileManager::Get().FileExists(**Path) &&
            !IFileManager::Get().Delete(**Path, true, false, true))
        {
            bCleanupSucceeded = false;
        }
    }
    if (!bCleanupSucceeded && !bCaptureFailureLatched)
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::PairCleanup,
            NextFrameIndex);
    }
    return bCleanupSucceeded;
}

bool UShipCapture::SerializeManifest(
    bool bSimulationSucceeded,
    FString& OutJsonText)
{
    OutJsonText.Reset();
    if (Frames.IsEmpty() || RunDirectoryPath.IsEmpty())
    {
        return false;
    }

    const bool bSuccessfulResult =
        bSimulationSucceeded && !bCaptureFailureLatched;
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("frame_count"), Frames.Num());
    Root->SetNumberField(TEXT("interval_ms"), CaptureIntervalMs);
    Root->SetNumberField(TEXT("depth_near_cm"), DepthNearCm);
    Root->SetNumberField(TEXT("depth_far_cm"), DepthFarCm);
    TArray<TSharedPtr<FJsonValue>> ResolutionValues;
    ResolutionValues.Add(MakeShared<FJsonValueNumber>(CaptureResolution));
    ResolutionValues.Add(MakeShared<FJsonValueNumber>(CaptureResolution));
    Root->SetArrayField(
        TEXT("capture_resolution"),
        MoveTemp(ResolutionValues));
    Root->SetNumberField(TEXT("wall_slide_cm"), CapturedWallSlideCm);
    Root->SetStringField(
        TEXT("result"),
        bSuccessfulResult ? TEXT("success") : TEXT("fail"));

    TArray<TSharedPtr<FJsonValue>> FrameValues;
    FrameValues.Reserve(Frames.Num());
    for (const FShipCaptureFrameRecord& Frame : Frames)
    {
        const TSharedRef<FJsonObject> FrameObject =
            MakeShared<FJsonObject>();
        FrameObject->SetNumberField(TEXT("index"), Frame.Index);
        FrameObject->SetStringField(TEXT("color"), Frame.ColorLeafName);
        FrameObject->SetStringField(TEXT("depth"), Frame.DepthLeafName);
        FrameObject->SetNumberField(TEXT("time_ms"), Frame.TimeMs);
        FrameValues.Add(MakeShared<FJsonValueObject>(FrameObject));
    }
    Root->SetArrayField(TEXT("frames"), MoveTemp(FrameValues));

    const TSharedRef<
        TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<
            TCHAR,
            TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonText);
    if (!FJsonSerializer::Serialize(Root, Writer) || OutJsonText.IsEmpty())
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::ManifestSerialize,
            NextFrameIndex);
    }
    return true;
}

bool UShipCapture::WriteManifest(const FString& JsonText)
{
    if (JsonText.IsEmpty() || RunDirectoryPath.IsEmpty())
    {
        return false;
    }

    const FString ManifestTempPath =
        RunDirectoryPath / TEXT(".manifest.json.tmp");
    const FString ManifestFinalPath =
        RunDirectoryPath / TEXT("manifest.json");
    if (IFileManager::Get().FileExists(*ManifestTempPath) ||
        IFileManager::Get().FileExists(*ManifestFinalPath) ||
        IFileManager::Get().DirectoryExists(*ManifestTempPath) ||
        IFileManager::Get().DirectoryExists(*ManifestFinalPath))
    {
        return LatchCaptureFailure(
            EShipCaptureFailureCategory::PathCollision,
            NextFrameIndex);
    }

    bool bTempWritten = false;
#if WITH_DEV_AUTOMATION_TESTS
    if (TestFailurePoint !=
        EShipCaptureTestFailurePoint::ManifestTempWrite)
#endif
    {
        bTempWritten = FFileHelper::SaveStringToFile(
            JsonText,
            *ManifestTempPath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    if (!bTempWritten ||
        !IFileManager::Get().FileExists(*ManifestTempPath) ||
        IFileManager::Get().FileSize(*ManifestTempPath) <= 0)
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::ManifestWrite,
            NextFrameIndex);
        CleanupManifestPaths(ManifestTempPath, ManifestFinalPath);
        return false;
    }

    const bool bManifestRenamed = IFileManager::Get().Move(
        *ManifestFinalPath,
        *ManifestTempPath,
        false,
        false,
        false,
        true);
    if (!bManifestRenamed ||
        !IFileManager::Get().FileExists(*ManifestFinalPath) ||
        IFileManager::Get().FileSize(*ManifestFinalPath) <= 0)
    {
        LatchCaptureFailure(
            EShipCaptureFailureCategory::ManifestRename,
            NextFrameIndex);
        CleanupManifestPaths(ManifestTempPath, ManifestFinalPath);
        return false;
    }
    return true;
}

bool UShipCapture::WriteBinaryBundle(const FString& ManifestJsonText)
{
    if (ManifestJsonText.IsEmpty() ||
        Frames.IsEmpty() ||
        RunDirectoryPath.IsEmpty())
    {
        return false;
    }

    TArray<FShipCaptureBundleAsset> Assets;
    Assets.Reserve(Frames.Num() * 2);
    for (const FShipCaptureFrameRecord& Frame : Frames)
    {
        for (const FString* LeafName : {
                 &Frame.ColorLeafName,
                 &Frame.DepthLeafName})
        {
            FShipCaptureBundleAsset Asset;
            Asset.Path = *LeafName;
            Asset.MediaType = TEXT("image/png");
            if (!FFileHelper::LoadFileToArray(
                    Asset.Bytes,
                    *(RunDirectoryPath / *LeafName)))
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("Stage5CaptureBundleReadFailed path=%s"),
                    **LeafName);
                return false;
            }
            Assets.Add(MoveTemp(Asset));
        }
    }

    TArray64<uint8> BundleBytes;
    if (!BuildShipCaptureBundle(
            ManifestJsonText,
            Assets,
            BundleBytes))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Stage5CaptureBundleBuildFailed frames=%d"),
            Frames.Num());
        return false;
    }

    const FString BundleTempPath =
        RunDirectoryPath / TEXT(".sequence.siv.tmp");
    const FString BundleFinalPath =
        RunDirectoryPath / TEXT("sequence.siv");
    if (IFileManager::Get().FileExists(*BundleTempPath) ||
        IFileManager::Get().FileExists(*BundleFinalPath) ||
        IFileManager::Get().DirectoryExists(*BundleTempPath) ||
        IFileManager::Get().DirectoryExists(*BundleFinalPath))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Stage5CaptureBundlePathCollision run=%s"),
            *RunRelativePath);
        return false;
    }

    const auto CleanupBundlePaths = [&BundleTempPath, &BundleFinalPath]()
    {
        for (const FString* Path : {&BundleTempPath, &BundleFinalPath})
        {
            if (IFileManager::Get().FileExists(**Path))
            {
                IFileManager::Get().Delete(**Path, true, false, true);
            }
        }
    };

    if (!FFileHelper::SaveArrayToFile(BundleBytes, *BundleTempPath) ||
        !IFileManager::Get().FileExists(*BundleTempPath) ||
        IFileManager::Get().FileSize(*BundleTempPath) != BundleBytes.Num())
    {
        CleanupBundlePaths();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Stage5CaptureBundleWriteFailed run=%s"),
            *RunRelativePath);
        return false;
    }

    const bool bBundleRenamed = IFileManager::Get().Move(
        *BundleFinalPath,
        *BundleTempPath,
        false,
        false,
        false,
        true);
    if (!bBundleRenamed ||
        !IFileManager::Get().FileExists(*BundleFinalPath) ||
        IFileManager::Get().FileSize(*BundleFinalPath) != BundleBytes.Num())
    {
        CleanupBundlePaths();
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Stage5CaptureBundleRenameFailed run=%s"),
            *RunRelativePath);
        return false;
    }
    return true;
}

void UShipCapture::StopAndFinalize(bool bSimulationSucceeded)
{
    SetComponentTickEnabled(false);
    if (LifecycleState == EShipCaptureLifecycleState::Finalized ||
        LifecycleState == EShipCaptureLifecycleState::Finalizing ||
        LifecycleState == EShipCaptureLifecycleState::NotStarted)
    {
        return;
    }

    LifecycleState = EShipCaptureLifecycleState::Finalizing;
    ++FinalizeAttemptCount;
    bool bManifestPublished = Frames.IsEmpty();
    bool bBundlePublished = false;
    if (!Frames.IsEmpty())
    {
        FString ManifestJsonText;
        bManifestPublished =
            SerializeManifest(bSimulationSucceeded, ManifestJsonText) &&
            WriteManifest(ManifestJsonText);
        if (bManifestPublished)
        {
            bBundlePublished = WriteBinaryBundle(ManifestJsonText);
        }
    }
    LifecycleState = EShipCaptureLifecycleState::Finalized;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("Stage5CaptureFinalized result=%s frames=%d manifest=%s bundle=%s run=%s"),
        bSimulationSucceeded && !bCaptureFailureLatched &&
                bManifestPublished
            ? TEXT("success")
            : TEXT("fail"),
        Frames.Num(),
        bManifestPublished && !Frames.IsEmpty()
            ? TEXT("published")
            : TEXT("not_published"),
        bBundlePublished
            ? TEXT("published")
            : TEXT("not_published"),
        RunRelativePath.IsEmpty()
            ? TEXT("ShipCaptures")
            : *RunRelativePath);
}

bool UShipCapture::IsCaptureActive() const
{
    return LifecycleState == EShipCaptureLifecycleState::Capturing;
}

bool UShipCapture::HasCaptureFailure() const
{
    return bCaptureFailureLatched;
}

void UShipCapture::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (LifecycleState == EShipCaptureLifecycleState::Capturing)
    {
        const double NowSeconds = FPlatformTime::Seconds();
        TickAtTime(NowSeconds);
    }
}

void UShipCapture::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopAndFinalize(false);
    Super::EndPlay(EndPlayReason);
}

#if WITH_DEV_AUTOMATION_TESTS
FShipCaptureRigSnapshot FShipCaptureAutomationAccessor::SetupRigOnly(
    AShipPawn& Pawn)
{
    FShipCaptureRigSnapshot Snapshot;
    UShipCapture* Capture = Pawn.GetCapture();
    if (!IsValid(Capture))
    {
        return Snapshot;
    }

    TInlineComponentArray<USceneComponent*> SceneComponents(&Pawn);
    for (USceneComponent* Component : SceneComponents)
    {
        Snapshot.CaptureMountCount +=
            Component == Capture->CaptureMount ? 1 : 0;
        Snapshot.ColorCaptureCount +=
            Component == Capture->ColorCapture ? 1 : 0;
        Snapshot.DepthCaptureCount +=
            Component == Capture->DepthCapture ? 1 : 0;
    }
    TInlineComponentArray<UShipCapture*> CaptureComponents(&Pawn);
    Snapshot.ShipCaptureCount = CaptureComponents.Num();

    Snapshot.bSetupSucceeded = Capture->SetupCaptureRig();
    if (!Snapshot.bSetupSucceeded)
    {
        return Snapshot;
    }

    const USceneCaptureComponent2D* Color = Capture->ColorCapture;
    const USceneCaptureComponent2D* Depth = Capture->DepthCapture;
    const UTextureRenderTarget2D* ColorTexture = Capture->ColorTarget;
    const UTextureRenderTarget2D* DepthTexture = Capture->DepthTarget;
    Snapshot.bSameAttachParent =
        Color->GetAttachParent() == Capture->CaptureMount &&
        Depth->GetAttachParent() == Capture->CaptureMount;
    Snapshot.bIdentityRelativeTransforms =
        Color->GetRelativeTransform().Equals(FTransform::Identity) &&
        Depth->GetRelativeTransform().Equals(FTransform::Identity);
    Snapshot.bSameWorldTransform =
        Color->GetComponentTransform().Equals(Depth->GetComponentTransform());
    Snapshot.bPerspectiveProjection =
        Color->ProjectionType == ECameraProjectionMode::Perspective &&
        Depth->ProjectionType == ECameraProjectionMode::Perspective;
    Snapshot.bAutomaticCaptureDisabled =
        !Color->bCaptureEveryFrame &&
        !Color->bCaptureOnMovement &&
        !Color->bAlwaysPersistRenderingState &&
        !Depth->bCaptureEveryFrame &&
        !Depth->bCaptureOnMovement &&
        !Depth->bAlwaysPersistRenderingState;
    Snapshot.bColorSourceFinalColorLdr =
        Color->CaptureSource == ESceneCaptureSource::SCS_FinalColorLDR;
    Snapshot.bDepthSourceSceneDepth =
        Depth->CaptureSource == ESceneCaptureSource::SCS_SceneDepth;
    Snapshot.bColorTargetBgra8 =
        IsValid(ColorTexture) && ColorTexture->GetFormat() == PF_B8G8R8A8;
    Snapshot.bDepthTargetR32Float =
        IsValid(DepthTexture) && DepthTexture->GetFormat() == PF_R32_FLOAT;
    Snapshot.bFixedColorExposure =
        Color->PostProcessSettings.bOverride_AutoExposureMethod &&
        Color->PostProcessSettings.AutoExposureMethod ==
            EAutoExposureMethod::AEM_Manual &&
        Color->PostProcessSettings.bOverride_AutoExposureBias &&
        Color->PostProcessSettings.AutoExposureBias == 0.0f &&
        Color->PostProcessSettings
            .bOverride_AutoExposureApplyPhysicalCameraExposure &&
        !Color->PostProcessSettings
            .AutoExposureApplyPhysicalCameraExposure &&
        Color->PostProcessBlendWeight == 1.0f;
    Snapshot.Resolution =
        ColorTexture->SizeX == Capture->CaptureResolution &&
        ColorTexture->SizeY == Capture->CaptureResolution &&
        DepthTexture->SizeX == Capture->CaptureResolution &&
        DepthTexture->SizeY == Capture->CaptureResolution
            ? Capture->CaptureResolution
            : 0;
    Snapshot.FovDegrees =
        Color->FOVAngle == Depth->FOVAngle
            ? Color->FOVAngle
            : 0.0f;
    return Snapshot;
}

void FShipCaptureAutomationAccessor::SetCaptureResolution(
    UShipCapture& Capture,
    int32 Resolution)
{
    if (!IsValid(Capture.ColorTarget) && !IsValid(Capture.DepthTarget))
    {
        Capture.CaptureResolution = Resolution;
    }
}

void FShipCaptureAutomationAccessor::SetFailurePoint(
    UShipCapture& Capture,
    EShipCaptureTestFailurePoint FailurePoint)
{
    Capture.TestFailurePoint = FailurePoint;
}

FShipCaptureTransactionSnapshot
FShipCaptureAutomationAccessor::CaptureSingleTransaction(
    UShipCapture& Capture,
    int32 FrameIndex,
    double CaptureSeconds)
{
    Capture.TestColorCaptureSceneCallCount = 0;
    Capture.TestDepthCaptureSceneCallCount = 0;
    Capture.TestColorReadbackPixelCount = 0;
    Capture.TestDepthReadbackPixelCount = 0;
    Capture.TestRawDepthSamples.Reset();

    FShipCaptureTransactionSnapshot Snapshot;
    Snapshot.bSucceeded = Capture.CaptureAndEncodePair(
        FrameIndex,
        CaptureSeconds,
        Snapshot.ColorPngBytes,
        Snapshot.DepthPngBytes);
    Snapshot.ColorCaptureSceneCallCount =
        Capture.TestColorCaptureSceneCallCount;
    Snapshot.DepthCaptureSceneCallCount =
        Capture.TestDepthCaptureSceneCallCount;
    Snapshot.ColorReadbackPixelCount =
        Capture.TestColorReadbackPixelCount;
    Snapshot.DepthReadbackPixelCount =
        Capture.TestDepthReadbackPixelCount;
    Snapshot.RawDepthSamples = Capture.TestRawDepthSamples;
    return Snapshot;
}

bool FShipCaptureAutomationAccessor::StartCaptureAt(
    UShipCapture& Capture,
    double WallSlideCm,
    double NowSeconds)
{
    return Capture.StartCaptureAt(WallSlideCm, NowSeconds);
}

void FShipCaptureAutomationAccessor::TickAt(
    UShipCapture& Capture,
    double NowSeconds)
{
    Capture.TickAtTime(NowSeconds);
}

FString FShipCaptureAutomationAccessor::RunDirectory(
    const UShipCapture& Capture)
{
    return Capture.RunDirectoryPath;
}

int32 FShipCaptureAutomationAccessor::CommittedFrameCount(
    const UShipCapture& Capture)
{
    return Capture.Frames.Num();
}

int32 FShipCaptureAutomationAccessor::FinalizeAttemptCount(
    const UShipCapture& Capture)
{
    return Capture.FinalizeAttemptCount;
}

int32 FShipCaptureAutomationAccessor::FailureLogCount(
    const UShipCapture& Capture)
{
    return Capture.TestFailureLogCount;
}

int32 FShipCaptureAutomationAccessor::CaptureResolution(
    const UShipCapture& Capture)
{
    return Capture.CaptureResolution;
}

int32 FShipCaptureAutomationAccessor::CaptureIntervalMs(
    const UShipCapture& Capture)
{
    return Capture.CaptureIntervalMs;
}

float FShipCaptureAutomationAccessor::CaptureFovDegrees(
    const UShipCapture& Capture)
{
    return Capture.CaptureFovDegrees;
}

bool FShipCaptureAutomationAccessor::HasOpticalEquality(
    const UShipCapture& Capture)
{
    return Capture.HasOpticalEquality();
}

TArray<double> FShipCaptureAutomationAccessor::TransactionDurationsMs(
    const UShipCapture& Capture)
{
    return Capture.TransactionDurationsMs;
}
#endif
