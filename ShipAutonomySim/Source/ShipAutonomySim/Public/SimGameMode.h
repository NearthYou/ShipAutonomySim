#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShipNavigationTypes.h"
#include "SimGameMode.generated.h"

class ACourseBuilder;
class AShipPawn;
class UPrimitiveComponent;
class UShipCapture;

UCLASS()
class SHIPAUTONOMYSIM_API ASimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimGameMode();
    virtual void InitGame(
        const FString& MapName,
        const FString& Options,
        FString& ErrorMessage) override;
    virtual void Tick(float DeltaSeconds) override;
    void ReportRuntimeCalculationError(EShipRuntimeCalculationError Error);

    EShipRunResult GetRunResult() const;
    EShipSetupFailure GetSetupFailure() const;
    bool HasRuntimeCalculationError() const;
    EShipRuntimeCalculationError GetRuntimeCalculationError() const;
    int32 GetRuntimeCalculationErrorCount() const;
    double GetResolvedSlideCm() const;
    double GetElapsedRunSeconds() const;
    AShipPawn* GetRunShip() const;
    ACourseBuilder* GetCourseBuilder() const;
    AActor* GetCollisionActor() const;
    UPrimitiveComponent* GetCollisionComponent() const;

protected:
	virtual void BeginPlay() override;
    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(EditAnywhere, Category=Stage3Test)
	TSubclassOf<AShipPawn> ShipPawnClass;

    UPROPERTY(Transient)
    TObjectPtr<AShipPawn> RunShip;

    UPROPERTY(Transient)
    TObjectPtr<ACourseBuilder> CourseBuilder;

    UPROPERTY(Transient)
    TWeakObjectPtr<AActor> CollisionActor;

    UPROPERTY(Transient)
    TWeakObjectPtr<UPrimitiveComponent> CollisionComponent;

    UPROPERTY(EditAnywhere, Category="Autonomy|Terminal")
    double TimeoutSeconds = 45.0;

    UPROPERTY(EditAnywhere, Category="Autonomy|Terminal")
    double GoalRadiusCm = 100.0;

    UPROPERTY(EditAnywhere, Category="Autonomy|Terminal")
    double SuccessSpeedThresholdCmPerSecond = 5.0;

    FShipRuntimeErrorState RuntimeErrorState;
    EShipRunResult RunResult = EShipRunResult::Running;
    EShipSetupFailure SetupFailure = EShipSetupFailure::None;
    TOptional<double> ForcedSlideCm;
    double ElapsedRunSeconds = 0.0;
    bool bUseRandomSlide = true;
    bool bRunActive = false;
    bool bSetupFailureLogged = false;
    bool bTerminalLogged = false;
    bool bCaptureFinalizeRequested = false;

    void RecordSetupFailure(EShipSetupFailure Failure);
    bool StartRunCapture(
        double BuildSlideCm,
        double ResolvedSlideCm);
    void FinalizeRunCapture(bool bSimulationSucceeded);
    void DisableNavigatorAndZeroInputs();
    void LatchTerminalResult(EShipRunResult Candidate);
    void LogTerminalOnce(EShipRunResult Result);

#if WITH_DEV_AUTOMATION_TESTS
	int32 TestBeginPlayInvocationCount = 0;
    int32 TestEnterAutonomyCallCount = 0;
    int32 TestSetupFailureLogCount = 0;
    int32 TestTerminalLogCount = 0;
    int32 TestRuntimeErrorLogCount = 0;
    int32 TestCaptureStartCallCount = 0;
    int32 TestCaptureFinalizeCallCount = 0;
    double TestLastCaptureStartSlideCm = 0.0;
    bool bTestLastCaptureFinalizeSuccess = false;
    bool bStage5CaptureEnabled = true;
    bool bTestSkipStage4Orchestration = false;
    TOptional<double> TestWaterSurfaceOverrideCm;
	friend struct FSimGameModeTestAccessor;
    friend struct FShipNavigationGameModeTestAccessor;
    friend struct FShipCaptureGameModeTestAccessor;
#endif
};
