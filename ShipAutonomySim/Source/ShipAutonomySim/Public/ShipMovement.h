#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "ShipMovement.generated.h"

class UWaterBodyComponent;
struct FShipMotionParameters;

UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class SHIPAUTONOMYSIM_API UShipMovement : public UActorComponent
{
    GENERATED_BODY()

public:
    UShipMovement();
    void SetThrottle(float Value);
    void SetSteer(float Value);
    double GetSignedSpeedCmPerSecond() const;
    bool ConsumeBlockingHit(FHitResult& OutHit);

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0", ClampMax="2"))
    double LinearDragCoeff = 0.447501534;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0", ClampMax="0.002"))
    double QuadraticDragCoeff = 0.000400390770;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001", ClampMax="500"))
    double MaxThrustAccel = 105.5159376;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.1", ClampMax="50"))
    double StopSpeedThreshold = 5.0;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0", ClampMax="180"))
    double MaxYawRate = 45.83662361;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="1", ClampMax="1000"))
    double TurnRefSpeed = 200.0;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001", ClampMax="0.016666667"))
    double MaxSimulationStepSeconds = 1.0 / 120.0;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="1", ClampMax="32"))
    int32 MaxSubstepsPerTick = 8;

    UPROPERTY(EditAnywhere, Category=ShipMovement)
    double WaterlineOffsetCm = 0.0;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001"))
    double WaterHeightInterpSpeed = 5.0;

    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001"))
    double WaterNormalInterpSpeed = 5.0;

    UPROPERTY(EditAnywhere, Category=Debug)
    bool bShowMovementDebug = true;

    UPROPERTY(Transient)
    TWeakObjectPtr<UWaterBodyComponent> OceanBodyComponent;

    double ThrottleInput = 0.0;
    double SteerInput = 0.0;
    double SignedSpeedCmPerSecond = 0.0;
    FHitResult PendingBlockingHit;
    bool bHasPendingBlockingHit = false;
    double HorizontalYawDegrees = 0.0;
    double LastAcceleration = 0.0;
    double LastYawRate = 0.0;
    double LastSimulatedDeltaTime = 0.0;
    double LastDroppedDeltaTime = 0.0;
    double LastStepSeconds = 0.0;
    int32 LastSubsteps = 0;
    bool bHasLastValidSurface = false;
    double LastValidSurfaceZ = 0.0;
    FVector LastValidSurfaceNormal = FVector::UpVector;
    FVector AppliedUpVector = FVector::UpVector;
    double LastTargetSurfaceZ = 0.0;
    bool bLastWaterFallback = true;
    bool bLastBlockingHit = false;
    FVector LastImpactNormal = FVector::ZeroVector;
    FString LastHitName;
    uint8 LastWaterState = 255;
    uint8 LastParameterState = 255;
    bool bLoggedBadThrottle = false;
    bool bLoggedBadSteer = false;
    bool bWasDroppingTime = false;

    FShipMotionParameters BuildCandidateParameters() const;
    void DrawMovementDebug() const;

#if WITH_DEV_AUTOMATION_TESTS
    mutable int32 TestDebugDrawInvocationCount = 0;
    friend struct FShipMovementTestAccessor;
#endif
};
