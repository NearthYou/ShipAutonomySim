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

    bool SetupCaptureRig();

#if WITH_DEV_AUTOMATION_TESTS
    friend struct FShipCaptureAutomationAccessor;
#endif
};

#if WITH_DEV_AUTOMATION_TESTS
struct FShipCaptureAutomationAccessor
{
    static FShipCaptureRigSnapshot SetupRigOnly(AShipPawn& Pawn);
};
#endif
