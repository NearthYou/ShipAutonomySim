#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ShipPawn.generated.h"

class UBoxComponent;
class ASimGameMode;
class AStaticMeshActor;
class UCameraComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UShipMovement;
class UShipNavigator;
class UShipCapture;
class USceneCaptureComponent2D;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS()
class SHIPAUTONOMYSIM_API AShipPawn : public APawn
{
    GENERATED_BODY()

public:
    AShipPawn();
    virtual void SetupPlayerInputComponent(
        UInputComponent* PlayerInputComponent) override;
    virtual void UnPossessed() override;
    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason) override;
    bool EnterAutonomy(
        const TArray<FVector>& WorldPath,
        AStaticMeshActor* ActualWall,
        ASimGameMode* RunOwner);
    UShipNavigator* GetNavigator() const;
    UShipCapture* GetCapture() const;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category=Ship)
    TObjectPtr<UBoxComponent> CollisionRoot;

    UPROPERTY(VisibleAnywhere, Category=Ship)
    TObjectPtr<UStaticMeshComponent> VisualMesh;

    UPROPERTY(VisibleAnywhere, Category=Ship)
    TObjectPtr<UShipMovement> ShipMovement;

    UPROPERTY(VisibleAnywhere, Category=Autonomy)
    TObjectPtr<UShipNavigator> Navigator;

    UPROPERTY(VisibleAnywhere, Category=Capture)
    TObjectPtr<USceneComponent> CaptureMount;

    UPROPERTY(VisibleAnywhere, Category=Capture)
    TObjectPtr<USceneCaptureComponent2D> ColorCapture;

    UPROPERTY(VisibleAnywhere, Category=Capture)
    TObjectPtr<USceneCaptureComponent2D> DepthCapture;

    UPROPERTY(VisibleAnywhere, Category=Capture)
    TObjectPtr<UShipCapture> ShipCapture;

    UPROPERTY(VisibleAnywhere, Category=Camera)
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, Category=Camera)
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(EditAnywhere, Category=Camera)
    double CameraArmLengthCm = 600.0;

    UPROPERTY(EditAnywhere, Category=Camera)
    double CameraPitchDegrees = -20.0;

    UPROPERTY(EditAnywhere, Category=Camera)
    double CameraSocketHeightCm = 100.0;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ThrottleAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SteerAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> ManualControlMapping;

    UPROPERTY(Transient)
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> RegisteredInputSubsystem;

    bool bManualMappingRegistered = false;
    bool bManualInputActive = false;
    bool bAutonomyInputLocked = false;

    void BuildManualInputObjects();
    void ResetManualInput();
    void DeactivateManualInput();
    void LockManualInputForAutonomy();
    void HandleThrottle(const FInputActionValue& Value);
    void HandleSteer(const FInputActionValue& Value);
    void HandleThrottleCompleted();
    void HandleThrottleCanceled();
    void HandleSteerCompleted();
    void HandleSteerCanceled();
    void HandleThrottleReleased();
    void HandleSteerReleased();

#if WITH_DEV_AUTOMATION_TESTS
    int32 TestManualMappingRemovalCount = 0;
    int32 TestThrottleCompletedCount = 0;
    int32 TestThrottleCanceledCount = 0;
    int32 TestSteerCompletedCount = 0;
    int32 TestSteerCanceledCount = 0;
    friend struct FShipPawnTestAccessor;
#endif
};
