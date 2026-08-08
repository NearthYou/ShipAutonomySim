#pragma once

#include "CoreMinimal.h"
#include "ShipNavigationTypes.h"

struct FShipMotionParameters;

enum class EStage4SlideOptionState : uint8
{
    Absent,
    Valid,
    Empty,
    Malformed,
    NonFinite,
    OutOfRange
};

struct FStage4SlideOptionResult
{
    EStage4SlideOptionState State = EStage4SlideOptionState::Absent;
    double SlideCm = 0.0;
    EShipSetupFailure Failure = EShipSetupFailure::None;
};

struct FShipCourseDefinition
{
    FVector StartWorld = FVector::ZeroVector;
    FVector WallWorld = FVector::ZeroVector;
    FVector WaypointWorld = FVector::ZeroVector;
    FVector EndWorld = FVector::ZeroVector;
    FVector WallScale = FVector(1.0, 10.0, 5.0);
    TArray<FVector> WorldPath;
};

struct FShipTerminalInputs
{
    bool bCollision = false;
    bool bSuccessConditions = false;
    bool bTimeout = false;
    bool bRuntimeCalculationError = false;
};

FStage4SlideOptionResult ClassifySlideOption(
    bool bHasOption,
    const FString& RawValue);
FShipCourseDefinition BuildCourseDefinition(
    const FTransform& CourseFrame,
    double WaterSurfaceZCm,
    double SlideCm);
bool ValidateWaterReference(
    bool bInExclusionVolume,
    double SurfaceZCm,
    double& OutSurfaceZCm,
    EShipSetupFailure& OutFailure);
bool AdvancePathProgress(
    const TArray<FVector>& WorldPath,
    const FVector& ShipWorldLocation,
    const FShipPathProgress& Previous,
    FShipPathProgress& OutProgress);
bool FindLookaheadTarget(
    const TArray<FVector>& WorldPath,
    const FShipPathProgress& Progress,
    double LookaheadDistanceCm,
    FVector& OutTarget);
bool ComputeGuidanceCommands(
    const FVector& ShipForward,
    const FVector& ShipLocation,
    const FVector& LookaheadTarget,
    double HeadingFullSteerDegrees,
    double FullThrottleHeadingDegrees,
    double MinimumThrottleHeadingDegrees,
    double MinimumThrottle,
    double& OutHeadingErrorDegrees,
    float& OutSteer,
    float& OutThrottle);
bool ComputeDynamicStoppingDistance(
    const FShipMotionParameters& Parameters,
    double InitialForwardSpeedCmPerSecond,
    double& OutStoppingDistanceCm);
void TransformBoxCornersToXY(
    const FBox& LocalBox,
    const FTransform& WorldTransform,
    TArray<FVector2D>& OutWorldCorners);
bool BuildConvexHullXY(
    const TArray<FVector2D>& Points,
    TArray<FVector2D>& OutHull);
bool ComputeConvexHullGapCm(
    const FBox& FirstLocalBox,
    const FTransform& FirstWorldTransform,
    const FBox& SecondLocalBox,
    const FTransform& SecondWorldTransform,
    double& OutGapCm);
EShipRunResult SelectTerminalResult(const FShipTerminalInputs& Inputs);
bool LatchRuntimeCalculationError(
    EShipRuntimeCalculationError Error,
    FShipRuntimeErrorState& InOutState);
