#pragma once

#include "CoreMinimal.h"
#include "ShipNavigationTypes.h"

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

FStage4SlideOptionResult ClassifySlideOption(
    bool bHasOption,
    const FString& RawValue);
FShipCourseDefinition BuildCourseDefinition(
    const FTransform& CourseFrame,
    double WaterSurfaceZCm,
    double SlideCm);
