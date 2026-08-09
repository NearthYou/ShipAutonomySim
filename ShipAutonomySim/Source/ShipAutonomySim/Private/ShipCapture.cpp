#include "ShipCapture.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Modules/ModuleManager.h"
#include "ShipPawn.h"
#include "ShipCaptureSimulation.h"
#include "UnrealClient.h"

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
        false);
    ColorCapture->TextureTarget = ColorTarget;
    DepthCapture->TextureTarget = DepthTarget;
    return ColorTarget->GetFormat() == PF_B8G8R8A8 &&
        DepthTarget->GetFormat() == PF_R32_FLOAT;
}

bool UShipCapture::StartCapture(double WallSlideCm)
{
    if (LifecycleState != EShipCaptureLifecycleState::NotStarted ||
        !FMath::IsFinite(WallSlideCm) ||
        !SetupCaptureRig())
    {
        bCaptureFailureLatched = true;
        LifecycleState = EShipCaptureLifecycleState::CaptureFailed;
        SetComponentTickEnabled(false);
        return false;
    }

    LifecycleState = EShipCaptureLifecycleState::Capturing;
    SetComponentTickEnabled(true);
    return true;
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

void UShipCapture::StopAndFinalize(bool bSimulationSucceeded)
{
    (void)bSimulationSucceeded;
    if (LifecycleState == EShipCaptureLifecycleState::Finalized ||
        LifecycleState == EShipCaptureLifecycleState::Finalizing ||
        LifecycleState == EShipCaptureLifecycleState::NotStarted)
    {
        return;
    }

    LifecycleState = EShipCaptureLifecycleState::Finalizing;
    SetComponentTickEnabled(false);
    LifecycleState = EShipCaptureLifecycleState::Finalized;
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
#endif
