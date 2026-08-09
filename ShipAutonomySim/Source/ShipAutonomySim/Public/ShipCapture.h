#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipCapture.generated.h"

class AShipPawn;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;

struct FShipCaptureFrameRecord
{
	int32 Index = INDEX_NONE;
	FString ColorLeafName;
	FString DepthLeafName;
	int64 TimeMs = 0;
};

#if WITH_DEV_AUTOMATION_TESTS
struct FShipCaptureRigSnapshot
{
    bool bSetupSucceeded = false;
    int32 CaptureMountCount = 0;
    int32 ColorCaptureCount = 0;
    int32 DepthCaptureCount = 0;
    int32 ShipCaptureCount = 0;
    bool bSameAttachParent = false;
    bool bIdentityRelativeTransforms = false;
    bool bSameWorldTransform = false;
    bool bPerspectiveProjection = false;
    bool bAutomaticCaptureDisabled = false;
    bool bColorSourceFinalColorLdr = false;
    bool bDepthSourceSceneDepth = false;
    bool bColorTargetBgra8 = false;
    bool bDepthTargetR32Float = false;
    bool bFixedColorExposure = false;
    int32 Resolution = 0;
    float FovDegrees = 0.0f;
};

struct FShipCaptureTransactionSnapshot
{
    bool bSucceeded = false;
    int32 ColorCaptureSceneCallCount = 0;
    int32 DepthCaptureSceneCallCount = 0;
    int64 ColorReadbackPixelCount = 0;
    int64 DepthReadbackPixelCount = 0;
    TArray<FLinearColor> RawDepthSamples;
    TArray64<uint8> ColorPngBytes;
    TArray64<uint8> DepthPngBytes;
};

struct FShipCaptureAutomationAccessor;
#endif

UCLASS()
class SHIPAUTONOMYSIM_API UShipCapture : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipCapture();

    void BindCaptureRig(
        USceneComponent* InCaptureMount,
        USceneCaptureComponent2D* InColorCapture,
        USceneCaptureComponent2D* InDepthCapture);
    bool StartCapture(double WallSlideCm);
    void StopAndFinalize(bool bSimulationSucceeded);
    bool IsCaptureActive() const;
    bool HasCaptureFailure() const;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason) override;

private:
    enum class EShipCaptureLifecycleState : uint8
    {
        NotStarted,
        Capturing,
        CaptureFailed,
        Finalizing,
        Finalized
    };

    enum class EShipCaptureFailureCategory : uint8
    {
        None,
        InvalidConfiguration,
        RigMismatch,
        TargetUnavailable,
        Readback,
        PixelCount,
        DepthNormalization,
        PngEncode
    };

    UPROPERTY(EditAnywhere, Category=Capture)
    int32 CaptureResolution = 512;

    UPROPERTY(EditAnywhere, Category=Capture)
    float CaptureFovDegrees = 90.0f;

    UPROPERTY(EditAnywhere, Category=Capture)
    FVector CaptureRelativeLocationCm = FVector(110.0, 0.0, 50.0);

    UPROPERTY(EditAnywhere, Category=Capture)
    FRotator CaptureRelativeRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, Category=Capture)
    int32 CaptureIntervalMs = 100;

    UPROPERTY(EditAnywhere, Category=Capture)
    double DepthNearCm = 0.0;

    UPROPERTY(EditAnywhere, Category=Capture)
    double DepthFarCm = 5000.0;

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> CaptureMount;

    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> ColorCapture;

    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> DepthCapture;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> ColorTarget;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> DepthTarget;

    EShipCaptureLifecycleState LifecycleState =
        EShipCaptureLifecycleState::NotStarted;
    bool bCaptureFailureLatched = false;
    EShipCaptureFailureCategory FirstFailureCategory =
        EShipCaptureFailureCategory::None;
    int32 FirstFailureFrameIndex = INDEX_NONE;

    bool SetupCaptureRig();
    bool HasOpticalEquality() const;
    bool CaptureAndEncodePair(
        int32 FrameIndex,
        double CaptureSeconds,
        TArray64<uint8>& OutColorPngBytes,
        TArray64<uint8>& OutDepthPngBytes);
    bool LatchCaptureFailure(
        EShipCaptureFailureCategory FailureCategory,
        int32 FrameIndex);

#if WITH_DEV_AUTOMATION_TESTS
    int32 TestColorCaptureSceneCallCount = 0;
    int32 TestDepthCaptureSceneCallCount = 0;
    int64 TestColorReadbackPixelCount = 0;
    int64 TestDepthReadbackPixelCount = 0;
    TArray<FLinearColor> TestRawDepthSamples;
    friend struct FShipCaptureAutomationAccessor;
#endif
};

#if WITH_DEV_AUTOMATION_TESTS
struct FShipCaptureAutomationAccessor
{
    static FShipCaptureRigSnapshot SetupRigOnly(AShipPawn& Pawn);
    static void SetCaptureResolution(UShipCapture& Capture, int32 Resolution);
    static void SetDepthRelativeLocationForTest(
        UShipCapture& Capture,
        const FVector& RelativeLocation);
    static FShipCaptureTransactionSnapshot CaptureSingleTransaction(
        UShipCapture& Capture,
        int32 FrameIndex,
        double CaptureSeconds);
};
#endif
