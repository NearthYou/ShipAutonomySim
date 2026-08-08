#include "ShipNavigationSimulation.h"

#include "Internationalization/Regex.h"
#include "ShipMovementSimulation.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
    return FMath::IsFinite(Value.X)
        && FMath::IsFinite(Value.Y)
        && FMath::IsFinite(Value.Z);
}

bool GetSegmentLengthXY(
    const FVector& Start,
    const FVector& End,
    double& OutLengthCm)
{
    if (!IsFiniteVector(Start) || !IsFiniteVector(End))
    {
        return false;
    }
    const FVector2D Delta(End.X - Start.X, End.Y - Start.Y);
    const double LengthSquared = Delta.SizeSquared();
    if (!FMath::IsFinite(LengthSquared)
        || LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }
    OutLengthCm = FMath::Sqrt(LengthSquared);
    return FMath::IsFinite(OutLengthCm);
}

bool GetCompletedDistanceXY(
    const TArray<FVector>& WorldPath,
    int32 SegmentIndex,
    double& OutDistanceCm)
{
    OutDistanceCm = 0.0;
    for (int32 Index = 0; Index < SegmentIndex; ++Index)
    {
        double SegmentLengthCm = 0.0;
        if (!GetSegmentLengthXY(
                WorldPath[Index], WorldPath[Index + 1], SegmentLengthCm))
        {
            return false;
        }
        OutDistanceCm += SegmentLengthCm;
        if (!FMath::IsFinite(OutDistanceCm))
        {
            return false;
        }
    }
    return true;
}

double Cross2D(
    const FVector2D& Origin,
    const FVector2D& First,
    const FVector2D& Second)
{
    const FVector2D A = First - Origin;
    const FVector2D B = Second - Origin;
    return A.X * B.Y - A.Y * B.X;
}

bool IsFinitePoint(const FVector2D& Point)
{
    return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);
}

bool PointOnSegment(
    const FVector2D& Point,
    const FVector2D& Start,
    const FVector2D& End)
{
    constexpr double Tolerance = 1e-9;
    if (FMath::Abs(Cross2D(Start, End, Point)) > Tolerance)
    {
        return false;
    }
    return Point.X >= FMath::Min(Start.X, End.X) - Tolerance
        && Point.X <= FMath::Max(Start.X, End.X) + Tolerance
        && Point.Y >= FMath::Min(Start.Y, End.Y) - Tolerance
        && Point.Y <= FMath::Max(Start.Y, End.Y) + Tolerance;
}

bool SegmentsIntersect(
    const FVector2D& FirstStart,
    const FVector2D& FirstEnd,
    const FVector2D& SecondStart,
    const FVector2D& SecondEnd)
{
    constexpr double Tolerance = 1e-9;
    const double A = Cross2D(FirstStart, FirstEnd, SecondStart);
    const double B = Cross2D(FirstStart, FirstEnd, SecondEnd);
    const double C = Cross2D(SecondStart, SecondEnd, FirstStart);
    const double D = Cross2D(SecondStart, SecondEnd, FirstEnd);
    if (((A > Tolerance && B < -Tolerance)
            || (A < -Tolerance && B > Tolerance))
        && ((C > Tolerance && D < -Tolerance)
            || (C < -Tolerance && D > Tolerance)))
    {
        return true;
    }
    return (FMath::Abs(A) <= Tolerance
            && PointOnSegment(SecondStart, FirstStart, FirstEnd))
        || (FMath::Abs(B) <= Tolerance
            && PointOnSegment(SecondEnd, FirstStart, FirstEnd))
        || (FMath::Abs(C) <= Tolerance
            && PointOnSegment(FirstStart, SecondStart, SecondEnd))
        || (FMath::Abs(D) <= Tolerance
            && PointOnSegment(FirstEnd, SecondStart, SecondEnd));
}

bool ConvexHullContainsPoint(
    const TArray<FVector2D>& Hull,
    const FVector2D& Point)
{
    constexpr double Tolerance = 1e-9;
    bool bSawPositive = false;
    bool bSawNegative = false;
    for (int32 Index = 0; Index < Hull.Num(); ++Index)
    {
        const double Cross = Cross2D(
            Hull[Index], Hull[(Index + 1) % Hull.Num()], Point);
        bSawPositive = bSawPositive || Cross > Tolerance;
        bSawNegative = bSawNegative || Cross < -Tolerance;
        if (bSawPositive && bSawNegative)
        {
            return false;
        }
    }
    return true;
}

double PointToSegmentDistance(
    const FVector2D& Point,
    const FVector2D& Start,
    const FVector2D& End)
{
    const FVector2D Segment = End - Start;
    const double LengthSquared = Segment.SizeSquared();
    if (LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
    {
        return FVector2D::Distance(Point, Start);
    }
    const double Fraction = FMath::Clamp(
        FVector2D::DotProduct(Point - Start, Segment) / LengthSquared,
        0.0,
        1.0);
    return FVector2D::Distance(Point, Start + Fraction * Segment);
}

double SegmentDistance(
    const FVector2D& FirstStart,
    const FVector2D& FirstEnd,
    const FVector2D& SecondStart,
    const FVector2D& SecondEnd)
{
    if (SegmentsIntersect(
            FirstStart, FirstEnd, SecondStart, SecondEnd))
    {
        return 0.0;
    }
    return FMath::Min(
        FMath::Min(
            PointToSegmentDistance(FirstStart, SecondStart, SecondEnd),
            PointToSegmentDistance(FirstEnd, SecondStart, SecondEnd)),
        FMath::Min(
            PointToSegmentDistance(SecondStart, FirstStart, FirstEnd),
            PointToSegmentDistance(SecondEnd, FirstStart, FirstEnd)));
}
}

FStage4SlideOptionResult ClassifySlideOption(
    bool bHasOption,
    const FString& RawValue)
{
    if (!bHasOption)
    {
        return {
            EStage4SlideOptionState::Absent,
            0.0,
            EShipSetupFailure::None};
    }

    FString Value = RawValue;
    Value.TrimStartAndEndInline();
    if (Value.IsEmpty())
    {
        return {
            EStage4SlideOptionState::Empty,
            0.0,
            EShipSetupFailure::SlideOptionEmpty};
    }

    static const FRegexPattern DecimalPattern(
        TEXT("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    FRegexMatcher Matcher(DecimalPattern, Value);
    if (!Matcher.FindNext())
    {
        const FString Lower = Value.ToLower();
        const bool bNamedNonFinite =
            Lower.Contains(TEXT("nan")) || Lower.Contains(TEXT("inf"));
        return bNamedNonFinite
            ? FStage4SlideOptionResult{
                EStage4SlideOptionState::NonFinite,
                0.0,
                EShipSetupFailure::SlideOptionNonFinite}
            : FStage4SlideOptionResult{
                EStage4SlideOptionState::Malformed,
                0.0,
                EShipSetupFailure::SlideOptionMalformed};
    }

    double Parsed = 0.0;
    if (!LexTryParseString(Parsed, *Value))
    {
        return {
            EStage4SlideOptionState::Malformed,
            0.0,
            EShipSetupFailure::SlideOptionMalformed};
    }
    if (!FMath::IsFinite(Parsed))
    {
        return {
            EStage4SlideOptionState::NonFinite,
            0.0,
            EShipSetupFailure::SlideOptionNonFinite};
    }
    if (Parsed < -500.0 || Parsed > 500.0)
    {
        return {
            EStage4SlideOptionState::OutOfRange,
            Parsed,
            EShipSetupFailure::SlideOptionOutOfRange};
    }

    return {
        EStage4SlideOptionState::Valid,
        Parsed,
        EShipSetupFailure::None};
}

FShipCourseDefinition BuildCourseDefinition(
    const FTransform& CourseFrame,
    double WaterSurfaceZCm,
    double SlideCm)
{
    const FVector Origin = CourseFrame.GetLocation();
    const double Yaw = CourseFrame.Rotator().Yaw;
    const FTransform FlatFrame(
        FRotator(0.0, Yaw, 0.0),
        FVector(Origin.X, Origin.Y, 0.0));
    const auto ToWorld = [&FlatFrame, WaterSurfaceZCm](double X, double Y)
    {
        FVector World = FlatFrame.TransformPosition(FVector(X, Y, 0.0));
        World.Z = WaterSurfaceZCm;
        return World;
    };

    FShipCourseDefinition Definition;
    Definition.StartWorld = ToWorld(0.0, 0.0);
    Definition.EndWorld = ToWorld(2000.0, 0.0);
    Definition.WallWorld = ToWorld(1000.0, SlideCm);
    Definition.WallWorld.Z = WaterSurfaceZCm + 150.0;
    const double WaypointY = SlideCm >= 0.0
        ? SlideCm - 750.0
        : SlideCm + 750.0;
    Definition.WaypointWorld = ToWorld(1000.0, WaypointY);
    Definition.WorldPath = {
        Definition.StartWorld,
        Definition.WaypointWorld,
        Definition.EndWorld};
    return Definition;
}

bool AdvancePathProgress(
    const TArray<FVector>& WorldPath,
    const FVector& ShipWorldLocation,
    const FShipPathProgress& Previous,
    FShipPathProgress& OutProgress)
{
    OutProgress = Previous;
    if (WorldPath.Num() < 2
        || Previous.ActiveSegmentIndex < 0
        || Previous.ActiveSegmentIndex >= WorldPath.Num() - 1
        || !IsFiniteVector(ShipWorldLocation)
        || !FMath::IsFinite(Previous.MonotonicDistanceCm)
        || Previous.MonotonicDistanceCm < 0.0)
    {
        return false;
    }

    const FVector& A3 = WorldPath[Previous.ActiveSegmentIndex];
    const FVector& B3 = WorldPath[Previous.ActiveSegmentIndex + 1];
    const FVector2D A(A3.X, A3.Y);
    const FVector2D B(B3.X, B3.Y);
    const FVector2D P(ShipWorldLocation.X, ShipWorldLocation.Y);
    const FVector2D AB = B - A;
    const double LengthSquared = AB.SizeSquared();
    if (!IsFiniteVector(A3)
        || !IsFiniteVector(B3)
        || !FMath::IsFinite(LengthSquared)
        || LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }

    const double Fraction = FMath::Clamp(
        FVector2D::DotProduct(P - A, AB) / LengthSquared,
        0.0,
        1.0);
    const double SegmentLength = FMath::Sqrt(LengthSquared);
    double CompletedDistance = 0.0;
    if (!GetCompletedDistanceXY(
            WorldPath, Previous.ActiveSegmentIndex, CompletedDistance))
    {
        return false;
    }
    const double CandidateProgress =
        CompletedDistance + Fraction * SegmentLength;
    if (!FMath::IsFinite(Fraction)
        || !FMath::IsFinite(CandidateProgress))
    {
        return false;
    }

    OutProgress.MonotonicDistanceCm = FMath::Max(
        Previous.MonotonicDistanceCm,
        CandidateProgress);
    const FVector2D SegmentDirection = AB / SegmentLength;
    const bool bPassedEndpoint =
        FVector2D::DotProduct(P - B, SegmentDirection) >= 0.0;
    if (bPassedEndpoint
        && Previous.ActiveSegmentIndex < WorldPath.Num() - 2)
    {
        OutProgress.MonotonicDistanceCm = FMath::Max(
            OutProgress.MonotonicDistanceCm,
            CompletedDistance + SegmentLength);
        OutProgress.ActiveSegmentIndex = Previous.ActiveSegmentIndex + 1;
    }

    return FMath::IsFinite(OutProgress.MonotonicDistanceCm);
}

bool FindLookaheadTarget(
    const TArray<FVector>& WorldPath,
    const FShipPathProgress& Progress,
    double LookaheadDistanceCm,
    FVector& OutTarget)
{
    OutTarget = FVector::ZeroVector;
    if (WorldPath.Num() < 2
        || Progress.ActiveSegmentIndex < 0
        || Progress.ActiveSegmentIndex >= WorldPath.Num() - 1
        || !FMath::IsFinite(Progress.MonotonicDistanceCm)
        || Progress.MonotonicDistanceCm < 0.0
        || !FMath::IsFinite(LookaheadDistanceCm)
        || LookaheadDistanceCm < 0.0)
    {
        return false;
    }

    TArray<double> SegmentLengths;
    SegmentLengths.Reserve(WorldPath.Num() - 1);
    double TotalLengthCm = 0.0;
    for (int32 Index = 0; Index < WorldPath.Num() - 1; ++Index)
    {
        double SegmentLengthCm = 0.0;
        if (!GetSegmentLengthXY(
                WorldPath[Index], WorldPath[Index + 1], SegmentLengthCm))
        {
            return false;
        }
        SegmentLengths.Add(SegmentLengthCm);
        TotalLengthCm += SegmentLengthCm;
        if (!FMath::IsFinite(TotalLengthCm))
        {
            return false;
        }
    }

    const double TargetDistanceCm = FMath::Clamp(
        Progress.MonotonicDistanceCm + LookaheadDistanceCm,
        0.0,
        TotalLengthCm);
    double CompletedDistanceCm = 0.0;
    for (int32 Index = 0; Index < SegmentLengths.Num(); ++Index)
    {
        const double SegmentEndDistanceCm =
            CompletedDistanceCm + SegmentLengths[Index];
        if (TargetDistanceCm <= SegmentEndDistanceCm)
        {
            const double Alpha = FMath::Clamp(
                (TargetDistanceCm - CompletedDistanceCm)
                    / SegmentLengths[Index],
                0.0,
                1.0);
            OutTarget = FMath::Lerp(
                WorldPath[Index], WorldPath[Index + 1], Alpha);
            return IsFiniteVector(OutTarget);
        }
        CompletedDistanceCm = SegmentEndDistanceCm;
    }

    OutTarget = WorldPath.Last();
    return IsFiniteVector(OutTarget);
}

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
    float& OutThrottle)
{
    OutHeadingErrorDegrees = 0.0;
    OutSteer = 0.0f;
    OutThrottle = 0.0f;
    if (!IsFiniteVector(ShipForward)
        || !IsFiniteVector(ShipLocation)
        || !IsFiniteVector(LookaheadTarget)
        || !FMath::IsFinite(HeadingFullSteerDegrees)
        || HeadingFullSteerDegrees <= 0.0
        || !FMath::IsFinite(FullThrottleHeadingDegrees)
        || FullThrottleHeadingDegrees < 0.0
        || !FMath::IsFinite(MinimumThrottleHeadingDegrees)
        || MinimumThrottleHeadingDegrees <= FullThrottleHeadingDegrees
        || !FMath::IsFinite(MinimumThrottle)
        || MinimumThrottle < 0.0
        || MinimumThrottle > 1.0)
    {
        return false;
    }

    FVector2D ForwardXY(ShipForward.X, ShipForward.Y);
    FVector2D TargetXY(
        LookaheadTarget.X - ShipLocation.X,
        LookaheadTarget.Y - ShipLocation.Y);
    if (!ForwardXY.Normalize() || !TargetXY.Normalize())
    {
        return false;
    }

    const double Dot = FMath::Clamp(
        FVector2D::DotProduct(ForwardXY, TargetXY), -1.0, 1.0);
    const double Cross =
        ForwardXY.X * TargetXY.Y - ForwardXY.Y * TargetXY.X;
    OutHeadingErrorDegrees =
        FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
    if (!FMath::IsFinite(OutHeadingErrorDegrees))
    {
        return false;
    }

    OutSteer = static_cast<float>(FMath::Clamp(
        OutHeadingErrorDegrees / HeadingFullSteerDegrees,
        -1.0,
        1.0));
    const double AbsError = FMath::Abs(OutHeadingErrorDegrees);
    if (AbsError <= FullThrottleHeadingDegrees)
    {
        OutThrottle = 1.0f;
    }
    else if (AbsError >= MinimumThrottleHeadingDegrees)
    {
        OutThrottle = static_cast<float>(MinimumThrottle);
    }
    else
    {
        const double Alpha =
            (AbsError - FullThrottleHeadingDegrees)
            / (MinimumThrottleHeadingDegrees - FullThrottleHeadingDegrees);
        OutThrottle = static_cast<float>(
            FMath::Lerp(1.0, MinimumThrottle, Alpha));
    }

    return FMath::IsFinite(OutSteer)
        && FMath::IsFinite(OutThrottle)
        && OutSteer >= -1.0f
        && OutSteer <= 1.0f
        && OutThrottle >= 0.0f
        && OutThrottle <= 1.0f;
}

bool ComputeDynamicStoppingDistance(
    const FShipMotionParameters& Parameters,
    double InitialForwardSpeedCmPerSecond,
    double& OutStoppingDistanceCm)
{
    OutStoppingDistanceCm = 0.0;
    if (!FMath::IsFinite(InitialForwardSpeedCmPerSecond)
        || InitialForwardSpeedCmPerSecond < 0.0
        || !FMath::IsFinite(Parameters.LinearDragCoeff)
        || !FMath::IsFinite(Parameters.QuadraticDragCoeff)
        || !FMath::IsFinite(Parameters.MaxThrustAccel)
        || !FMath::IsFinite(Parameters.StopSpeedThreshold)
        || !FMath::IsFinite(Parameters.MaxYawRate)
        || !FMath::IsFinite(Parameters.TurnRefSpeed)
        || Parameters.StopSpeedThreshold < 0.0)
    {
        return false;
    }

    FShipMotionState State{InitialForwardSpeedCmPerSecond, 0.0};
    const FShipMotionInput CoastInput = MakeShipMotionInput(0.0, 0.0);
    for (int32 StepIndex = 0; StepIndex < 14400; ++StepIndex)
    {
        if (State.SignedSpeedCmPerSecond <= Parameters.StopSpeedThreshold)
        {
            return FMath::IsFinite(OutStoppingDistanceCm);
        }

        const FShipMotionStep Step = AdvanceShipMotion(
            State,
            CoastInput,
            Parameters,
            1.0 / 120.0);
        if (!Step.bValid
            || !FMath::IsFinite(Step.TravelCm)
            || Step.TravelCm < 0.0
            || !FMath::IsFinite(Step.NextState.SignedSpeedCmPerSecond)
            || Step.NextState.SignedSpeedCmPerSecond
                >= State.SignedSpeedCmPerSecond)
        {
            return false;
        }
        OutStoppingDistanceCm += Step.TravelCm;
        if (!FMath::IsFinite(OutStoppingDistanceCm))
        {
            return false;
        }
        State = Step.NextState;
    }

    return false;
}

void TransformBoxCornersToXY(
    const FBox& LocalBox,
    const FTransform& WorldTransform,
    TArray<FVector2D>& OutWorldCorners)
{
    OutWorldCorners.Reset(8);
    if (!LocalBox.IsValid)
    {
        return;
    }
    for (int32 XIndex = 0; XIndex < 2; ++XIndex)
    {
        for (int32 YIndex = 0; YIndex < 2; ++YIndex)
        {
            for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
            {
                const FVector Local(
                    XIndex == 0 ? LocalBox.Min.X : LocalBox.Max.X,
                    YIndex == 0 ? LocalBox.Min.Y : LocalBox.Max.Y,
                    ZIndex == 0 ? LocalBox.Min.Z : LocalBox.Max.Z);
                const FVector World = WorldTransform.TransformPosition(Local);
                OutWorldCorners.Emplace(World.X, World.Y);
            }
        }
    }
}

bool BuildConvexHullXY(
    const TArray<FVector2D>& Points,
    TArray<FVector2D>& OutHull)
{
    OutHull.Reset();
    TArray<FVector2D> Sorted = Points;
    for (const FVector2D& Point : Sorted)
    {
        if (!IsFinitePoint(Point))
        {
            return false;
        }
    }
    Sorted.Sort([](const FVector2D& Left, const FVector2D& Right)
    {
        return Left.X < Right.X
            || (Left.X == Right.X && Left.Y < Right.Y);
    });

    TArray<FVector2D> Unique;
    Unique.Reserve(Sorted.Num());
    for (const FVector2D& Point : Sorted)
    {
        if (Unique.IsEmpty() || !Unique.Last().Equals(Point, 1e-9))
        {
            Unique.Add(Point);
        }
    }
    if (Unique.Num() < 3)
    {
        return false;
    }

    TArray<FVector2D> Lower;
    for (const FVector2D& Point : Unique)
    {
        while (Lower.Num() >= 2
            && Cross2D(Lower[Lower.Num() - 2], Lower.Last(), Point) <= 1e-9)
        {
            Lower.Pop(EAllowShrinking::No);
        }
        Lower.Add(Point);
    }

    TArray<FVector2D> Upper;
    for (int32 Index = Unique.Num() - 1; Index >= 0; --Index)
    {
        const FVector2D& Point = Unique[Index];
        while (Upper.Num() >= 2
            && Cross2D(Upper[Upper.Num() - 2], Upper.Last(), Point) <= 1e-9)
        {
            Upper.Pop(EAllowShrinking::No);
        }
        Upper.Add(Point);
    }

    Lower.Pop(EAllowShrinking::No);
    Upper.Pop(EAllowShrinking::No);
    OutHull = MoveTemp(Lower);
    OutHull.Append(Upper);
    return OutHull.Num() >= 3;
}

bool ComputeConvexHullGapCm(
    const FBox& FirstLocalBox,
    const FTransform& FirstWorldTransform,
    const FBox& SecondLocalBox,
    const FTransform& SecondWorldTransform,
    double& OutGapCm)
{
    OutGapCm = 0.0;
    TArray<FVector2D> FirstCorners;
    TArray<FVector2D> SecondCorners;
    TransformBoxCornersToXY(
        FirstLocalBox, FirstWorldTransform, FirstCorners);
    TransformBoxCornersToXY(
        SecondLocalBox, SecondWorldTransform, SecondCorners);
    TArray<FVector2D> FirstHull;
    TArray<FVector2D> SecondHull;
    if (!BuildConvexHullXY(FirstCorners, FirstHull)
        || !BuildConvexHullXY(SecondCorners, SecondHull))
    {
        return false;
    }

    for (int32 FirstIndex = 0; FirstIndex < FirstHull.Num(); ++FirstIndex)
    {
        const FVector2D& FirstStart = FirstHull[FirstIndex];
        const FVector2D& FirstEnd =
            FirstHull[(FirstIndex + 1) % FirstHull.Num()];
        for (int32 SecondIndex = 0;
             SecondIndex < SecondHull.Num();
             ++SecondIndex)
        {
            if (SegmentsIntersect(
                    FirstStart,
                    FirstEnd,
                    SecondHull[SecondIndex],
                    SecondHull[(SecondIndex + 1) % SecondHull.Num()]))
            {
                return true;
            }
        }
    }
    if (ConvexHullContainsPoint(FirstHull, SecondHull[0])
        || ConvexHullContainsPoint(SecondHull, FirstHull[0]))
    {
        return true;
    }

    double MinimumDistanceCm = TNumericLimits<double>::Max();
    for (int32 FirstIndex = 0; FirstIndex < FirstHull.Num(); ++FirstIndex)
    {
        const FVector2D& FirstStart = FirstHull[FirstIndex];
        const FVector2D& FirstEnd =
            FirstHull[(FirstIndex + 1) % FirstHull.Num()];
        for (int32 SecondIndex = 0;
             SecondIndex < SecondHull.Num();
             ++SecondIndex)
        {
            MinimumDistanceCm = FMath::Min(
                MinimumDistanceCm,
                SegmentDistance(
                    FirstStart,
                    FirstEnd,
                    SecondHull[SecondIndex],
                    SecondHull[(SecondIndex + 1) % SecondHull.Num()]));
        }
    }
    if (!FMath::IsFinite(MinimumDistanceCm)
        || MinimumDistanceCm < 0.0)
    {
        return false;
    }
    OutGapCm = MinimumDistanceCm;
    return true;
}

EShipRunResult SelectTerminalResult(const FShipTerminalInputs& Inputs)
{
    if (Inputs.bCollision)
    {
        return EShipRunResult::Collision;
    }
    if (Inputs.bSuccessConditions && !Inputs.bRuntimeCalculationError)
    {
        return EShipRunResult::Success;
    }
    if (Inputs.bTimeout)
    {
        return EShipRunResult::Timeout;
    }
    return EShipRunResult::Running;
}

bool LatchRuntimeCalculationError(
    EShipRuntimeCalculationError Error,
    FShipRuntimeErrorState& InOutState)
{
    if (Error == EShipRuntimeCalculationError::None)
    {
        return false;
    }
    ++InOutState.ReportCount;
    if (InOutState.bLatched)
    {
        return false;
    }
    InOutState.bLatched = true;
    InOutState.FirstError = Error;
    return true;
}
