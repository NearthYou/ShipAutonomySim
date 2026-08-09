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
enum class EShipCaptureTestFailurePoint : uint8
{
    None,
    DepthTempWrite,
    DepthFrameRename,
    ManifestTempWrite
};

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
        PngEncode,
        DirectoryCreate,
        PathCollision,
        TempWrite,
        FrameRename,
        PairCleanup,
        FrameRecord,
        InvalidClock,
        ManifestSerialize,
        ManifestWrite,
        ManifestRename
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
    FString RunDirectoryPath;
    FString RunRelativePath;
    TArray<FShipCaptureFrameRecord> Frames;
    int32 NextFrameIndex = 0;
    double CapturedWallSlideCm = 0.0;
    double FirstCaptureSeconds = 0.0;
    double LastClockSeconds = 0.0;
    double AccumulatedRealSeconds = 0.0;
    int64 LastCommittedTimeMs = 0;
    bool bPairCleanupFailureLatched = false;
    int32 FinalizeAttemptCount = 0;
    TArray<double> TransactionDurationsMs;

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
    bool CreateUniqueRunDirectory();
    bool CaptureEncodeAndPublishFrame(
        int32 FrameIndex,
        double CaptureSeconds,
        int64 CaptureTimeMs);
    bool CleanupPairPaths(
        const FString& ColorTempPath,
        const FString& DepthTempPath,
        const FString& ColorFinalPath,
        const FString& DepthFinalPath);
    bool StartCaptureAt(double WallSlideCm, double NowSeconds);
    void TickAtTime(double NowSeconds);
    bool WriteManifest(bool bSimulationSucceeded);
    bool CleanupManifestPaths(
        const FString& ManifestTempPath,
        const FString& ManifestFinalPath);

#if WITH_DEV_AUTOMATION_TESTS
    EShipCaptureTestFailurePoint TestFailurePoint =
        EShipCaptureTestFailurePoint::None;
    int32 TestColorCaptureSceneCallCount = 0;
    int32 TestDepthCaptureSceneCallCount = 0;
    int64 TestColorReadbackPixelCount = 0;
    int64 TestDepthReadbackPixelCount = 0;
    TArray<FLinearColor> TestRawDepthSamples;
    int32 TestFailureLogCount = 0;
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
    static void SetFailurePoint(
        UShipCapture& Capture,
        EShipCaptureTestFailurePoint FailurePoint);
    static FShipCaptureTransactionSnapshot CaptureSingleTransaction(
        UShipCapture& Capture,
        int32 FrameIndex,
        double CaptureSeconds);
    static bool StartCaptureAt(
        UShipCapture& Capture,
        double WallSlideCm,
        double NowSeconds);
    static void TickAt(UShipCapture& Capture, double NowSeconds);
    static FString RunDirectory(const UShipCapture& Capture);
    static int32 CommittedFrameCount(const UShipCapture& Capture);
    static int32 FinalizeAttemptCount(const UShipCapture& Capture);
    static int32 FailureLogCount(const UShipCapture& Capture);
    static int32 CaptureResolution(const UShipCapture& Capture);
    static int32 CaptureIntervalMs(const UShipCapture& Capture);
    static float CaptureFovDegrees(const UShipCapture& Capture);
    static bool HasOpticalEquality(const UShipCapture& Capture);
    static TArray<double> TransactionDurationsMs(
        const UShipCapture& Capture);
};
#endif
