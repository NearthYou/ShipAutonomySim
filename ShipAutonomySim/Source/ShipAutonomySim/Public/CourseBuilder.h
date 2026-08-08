#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipNavigationTypes.h"
#include "CourseBuilder.generated.h"

class AStaticMeshActor;
class ATargetPoint;

struct FShipCourseBuildResult
{
    TArray<FVector> WorldPath;
    ATargetPoint* StartTarget = nullptr;
    ATargetPoint* EndTarget = nullptr;
    AStaticMeshActor* WallActor = nullptr;
    int32 RandomSeed = 0;
    double SlideCm = 0.0;
    double WaterSurfaceZCm = 0.0;
};

UCLASS()
class SHIPAUTONOMYSIM_API ACourseBuilder : public AActor
{
	GENERATED_BODY()

public:
	ACourseBuilder();
    void SetForcedSlideCm(double InSlideCm);
    void ClearForcedSlide();
    bool BuildRuntimeCourse(
        FShipCourseBuildResult& OutResult,
        EShipSetupFailure& OutFailure);

    ATargetPoint* GetStartTarget() const;
    ATargetPoint* GetEndTarget() const;
    AStaticMeshActor* GetWallActor() const;
    const TArray<FVector>& GetWorldPath() const;
    int32 GetResolvedRandomSeed() const;
    double GetResolvedSlideCm() const;

private:
    void ClearSpawnedCourseActors();

    TOptional<double> ForcedSlideCm;

    UPROPERTY(Transient)
    TObjectPtr<ATargetPoint> StartTarget;

    UPROPERTY(Transient)
    TObjectPtr<ATargetPoint> EndTarget;

    UPROPERTY(Transient)
    TObjectPtr<AStaticMeshActor> WallActor;

    TArray<FVector> WorldPath;
    int32 ResolvedRandomSeed = 0;
    double ResolvedSlideCm = 0.0;
    bool bRandomCourseLogged = false;

#if WITH_DEV_AUTOMATION_TESTS
    FVector TestLastWaterQueryLocation = FVector::ZeroVector;
    int32 TestRandomCourseLogCount = 0;
    TOptional<double> TestWaterSurfaceOverrideCm;
    friend struct FCourseBuilderTestAccessor;
#endif
};
