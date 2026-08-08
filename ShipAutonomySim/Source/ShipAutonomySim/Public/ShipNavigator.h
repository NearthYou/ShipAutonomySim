#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipNavigationTypes.h"
#include "ShipNavigator.generated.h"

class ASimGameMode;
class AStaticMeshActor;
class UShipMovement;

UCLASS()
class SHIPAUTONOMYSIM_API UShipNavigator : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipNavigator();
    bool Configure(
        const TArray<FVector>& InWorldPath,
        UShipMovement* InMovement,
        AStaticMeshActor* InActualWall,
        ASimGameMode* InRunOwner);
    void SetNavigationEnabled(bool bEnabled);
    bool IsNavigationEnabled() const;

protected:
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UShipMovement> Movement;

    UPROPERTY(Transient)
    TWeakObjectPtr<AStaticMeshActor> ActualWall;

    UPROPERTY(Transient)
    TWeakObjectPtr<ASimGameMode> RunOwner;

    UPROPERTY(EditAnywhere, Category="Navigation|Guidance")
    double LookaheadDistanceCm = 300.0;

    UPROPERTY(EditAnywhere, Category="Navigation|Guidance")
    double HeadingFullSteerDegrees = 30.0;

    UPROPERTY(EditAnywhere, Category="Navigation|Throttle")
    double FullThrottleHeadingDegrees = 20.0;

    UPROPERTY(EditAnywhere, Category="Navigation|Throttle")
    double MinimumThrottleHeadingDegrees = 60.0;

    UPROPERTY(EditAnywhere, Category="Navigation|Throttle")
    double MinimumThrottle = 0.35;

    UPROPERTY(EditAnywhere, Category="Navigation|Stopping")
    double CoastMarginCm = 25.0;

    TArray<FVector> WorldPath;
    FShipPathProgress Progress;
    FVector LiveTarget = FVector::ZeroVector;
    bool bConfigured = false;
    bool bNavigationEnabled = false;
    bool bCoastLatched = false;

    void FailRuntimeCalculation(EShipRuntimeCalculationError Error);
    void DrawNavigationDebug() const;

#if WITH_DEV_AUTOMATION_TESTS
    TOptional<FVector> TestShipLocationOverride;
    TOptional<double> TestForwardSpeedOverride;
    float TestLastThrottleCommand = 0.0f;
    float TestLastSteerCommand = 0.0f;
    friend struct FShipNavigatorTestAccessor;
#endif
};
