#pragma once

#include "CoreMinimal.h"
#include "ShipNavigationTypes.generated.h"

UENUM()
enum class EShipRunResult : uint8
{
    Running,
    Success,
    Timeout,
    Collision
};

UENUM()
enum class EShipSetupFailure : uint8
{
    None,
    SlideOptionEmpty,
    SlideOptionMalformed,
    SlideOptionNonFinite,
    SlideOptionOutOfRange,
    WaterSurfaceUnavailable,
    WaterLocationExcluded,
    CourseSpawnFailed,
    MeshLoadFailed,
    ShipSpawnFailed,
    PlayerControllerUnavailable,
    AutonomyActivationFailed
};

UENUM()
enum class EShipRuntimeCalculationError : uint8
{
    None,
    InvalidNavigationPath,
    InvalidProgressProjection,
    InvalidLookaheadTarget,
    InvalidHeading,
    InvalidThrottle,
    InvalidStoppingDistance
};

struct FShipPathProgress
{
    int32 ActiveSegmentIndex = 0;
    double MonotonicDistanceCm = 0.0;
};
