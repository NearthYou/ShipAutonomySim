#include "ShipCaptureSimulation.h"

namespace
{
constexpr double ScheduleBoundaryToleranceSeconds = 1.0e-9;
constexpr double WallSlideToleranceCm = 1.0e-9;
constexpr int32 LargestCaptureFrameIndex = 999999;
}

bool InitializeCaptureClock(
    double NowSeconds,
    double& OutFirstCaptureSeconds,
    double& OutLastClockSeconds,
    double& OutAccumulatedRealSeconds,
    int64& OutCaptureTimeMs)
{
    OutFirstCaptureSeconds = 0.0;
    OutLastClockSeconds = 0.0;
    OutAccumulatedRealSeconds = 0.0;
    OutCaptureTimeMs = 0;

    if (!FMath::IsFinite(NowSeconds))
    {
        return false;
    }

    OutFirstCaptureSeconds = NowSeconds;
    OutLastClockSeconds = NowSeconds;
    return true;
}

FShipCaptureScheduleStep AdvanceCaptureSchedule(
    double NowSeconds,
    double FirstCaptureSeconds,
    double LastClockSeconds,
    double AccumulatedRealSeconds,
    int64 LastCommittedTimeMs,
    int32 CaptureIntervalMs)
{
    FShipCaptureScheduleStep Result;
    if (!FMath::IsFinite(NowSeconds) ||
        !FMath::IsFinite(FirstCaptureSeconds) ||
        !FMath::IsFinite(LastClockSeconds) ||
        !FMath::IsFinite(AccumulatedRealSeconds) ||
        CaptureIntervalMs < 1 ||
        AccumulatedRealSeconds < 0.0 ||
        LastCommittedTimeMs < 0 ||
        LastClockSeconds < FirstCaptureSeconds ||
        NowSeconds < LastClockSeconds)
    {
        return Result;
    }

    const double ElapsedSeconds = NowSeconds - LastClockSeconds;
    const double NextAccumulatedRealSeconds =
        AccumulatedRealSeconds + ElapsedSeconds;
    if (!FMath::IsFinite(ElapsedSeconds) ||
        !FMath::IsFinite(NextAccumulatedRealSeconds) ||
        ElapsedSeconds < 0.0 ||
        NextAccumulatedRealSeconds < 0.0)
    {
        return Result;
    }

    Result.NextLastClockSeconds = NowSeconds;
    Result.NextAccumulatedRealSeconds = NextAccumulatedRealSeconds;
    Result.CaptureTimeMs = LastCommittedTimeMs;

    const double CaptureIntervalSeconds =
        static_cast<double>(CaptureIntervalMs) / 1000.0;
    if (NextAccumulatedRealSeconds + ScheduleBoundaryToleranceSeconds <
        CaptureIntervalSeconds)
    {
        Result.Decision = EShipCaptureScheduleDecision::NotDue;
        return Result;
    }

    const double ActualCaptureMilliseconds =
        (NowSeconds - FirstCaptureSeconds) * 1000.0;
    if (!FMath::IsFinite(ActualCaptureMilliseconds) ||
        ActualCaptureMilliseconds < 0.0 ||
        ActualCaptureMilliseconds >
            static_cast<double>(TNumericLimits<int64>::Max()))
    {
        return FShipCaptureScheduleStep{};
    }

    const int64 RoundedCaptureTimeMs =
        FMath::RoundToInt64(ActualCaptureMilliseconds);
    if (RoundedCaptureTimeMs <= LastCommittedTimeMs)
    {
        return FShipCaptureScheduleStep{};
    }

    Result.Decision = EShipCaptureScheduleDecision::Due;
    Result.NextAccumulatedRealSeconds = 0.0;
    Result.CaptureTimeMs = RoundedCaptureTimeMs;
    return Result;
}

bool NormalizeSceneDepthToG8(
    const TArray<FLinearColor>& DepthSamples,
    int32 ExpectedPixelCount,
    double DepthNearCm,
    double DepthFarCm,
    TArray64<uint8>& OutPixels)
{
    OutPixels.Reset();
    if (ExpectedPixelCount < 0 ||
        DepthSamples.Num() != ExpectedPixelCount ||
        !FMath::IsFinite(DepthNearCm) ||
        !FMath::IsFinite(DepthFarCm) ||
        DepthNearCm >= DepthFarCm)
    {
        return false;
    }

    const double DepthRangeCm = DepthFarCm - DepthNearCm;
    if (!FMath::IsFinite(DepthRangeCm) || DepthRangeCm <= 0.0)
    {
        return false;
    }

    OutPixels.SetNumUninitialized(ExpectedPixelCount);
    for (int32 PixelIndex = 0; PixelIndex < ExpectedPixelCount; ++PixelIndex)
    {
        const double SceneDepthCm =
            static_cast<double>(DepthSamples[PixelIndex].R);
        if (!FMath::IsFinite(SceneDepthCm))
        {
            OutPixels[PixelIndex] = 0;
            continue;
        }

        const double NormalizedDepth =
            (SceneDepthCm - DepthNearCm) / DepthRangeCm;
        if (!FMath::IsFinite(NormalizedDepth))
        {
            OutPixels[PixelIndex] = 0;
            continue;
        }

        const double ClampedDepth = FMath::Clamp(NormalizedDepth, 0.0, 1.0);
        const double GrayscaleValue = (1.0 - ClampedDepth) * 255.0;
        if (!FMath::IsFinite(GrayscaleValue))
        {
            OutPixels[PixelIndex] = 0;
            continue;
        }

        OutPixels[PixelIndex] = static_cast<uint8>(
            FMath::Clamp(FMath::RoundToInt(GrayscaleValue), 0, 255));
    }

    return true;
}

bool MakeCaptureFrameLeafNames(
    int32 FrameIndex,
    FString& OutColorLeafName,
    FString& OutDepthLeafName)
{
    OutColorLeafName.Reset();
    OutDepthLeafName.Reset();
    if (FrameIndex < 0 || FrameIndex > LargestCaptureFrameIndex)
    {
        return false;
    }

    OutColorLeafName = FString::Printf(TEXT("color_%06d.png"), FrameIndex);
    OutDepthLeafName = FString::Printf(TEXT("depth_%06d.png"), FrameIndex);
    return true;
}

bool ValidateAndAppendCaptureFrame(
    const FShipCaptureFrameRecord& Candidate,
    TArray<FShipCaptureFrameRecord>& InOutFrames)
{
    FString ExpectedColorLeafName;
    FString ExpectedDepthLeafName;
    if (Candidate.Index != InOutFrames.Num() ||
        Candidate.TimeMs < 0 ||
        !MakeCaptureFrameLeafNames(
            Candidate.Index,
            ExpectedColorLeafName,
            ExpectedDepthLeafName) ||
        Candidate.ColorLeafName != ExpectedColorLeafName ||
        Candidate.DepthLeafName != ExpectedDepthLeafName)
    {
        return false;
    }

    if ((Candidate.Index == 0 && Candidate.TimeMs != 0) ||
        (Candidate.Index > 0 &&
         Candidate.TimeMs <= InOutFrames.Last().TimeMs))
    {
        return false;
    }

    InOutFrames.Add(Candidate);
    return true;
}

bool IsValidCaptureWallSlide(
    double BuildResultSlideCm,
    double ResolvedSlideCm)
{
    if (!FMath::IsFinite(BuildResultSlideCm) ||
        !FMath::IsFinite(ResolvedSlideCm))
    {
        return false;
    }

    const double DifferenceCm = BuildResultSlideCm - ResolvedSlideCm;
    return FMath::IsFinite(DifferenceCm) &&
        FMath::Abs(DifferenceCm) <= WallSlideToleranceCm;
}
