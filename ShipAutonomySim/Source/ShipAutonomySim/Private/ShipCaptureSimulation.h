#pragma once

#include "CoreMinimal.h"
#include "ShipCapture.h"

enum class EShipCaptureScheduleDecision : uint8
{
    NotDue,
    Due,
    Invalid
};

struct FShipCaptureScheduleStep
{
    EShipCaptureScheduleDecision Decision =
        EShipCaptureScheduleDecision::Invalid;
    double NextLastClockSeconds = 0.0;
    double NextAccumulatedRealSeconds = 0.0;
    int64 CaptureTimeMs = 0;
};

bool InitializeCaptureClock(
    double NowSeconds,
    double& OutFirstCaptureSeconds,
    double& OutLastClockSeconds,
    double& OutAccumulatedRealSeconds,
    int64& OutCaptureTimeMs);

FShipCaptureScheduleStep AdvanceCaptureSchedule(
    double NowSeconds,
    double FirstCaptureSeconds,
    double LastClockSeconds,
    double AccumulatedRealSeconds,
    int64 LastCommittedTimeMs,
    int32 CaptureIntervalMs);

bool NormalizeSceneDepthToG8(
    const TArray<FLinearColor>& DepthSamples,
    int32 ExpectedPixelCount,
    double DepthNearCm,
    double DepthFarCm,
    TArray64<uint8>& OutPixels);

bool MakeCaptureFrameLeafNames(
    int32 FrameIndex,
    FString& OutColorLeafName,
    FString& OutDepthLeafName);

bool ValidateAndAppendCaptureFrame(
    const FShipCaptureFrameRecord& Candidate,
    TArray<FShipCaptureFrameRecord>& InOutFrames);

bool IsValidCaptureWallSlide(
    double BuildResultSlideCm,
    double ResolvedSlideCm);
