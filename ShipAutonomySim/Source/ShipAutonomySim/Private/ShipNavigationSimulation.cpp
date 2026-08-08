#include "ShipNavigationSimulation.h"

#include "Internationalization/Regex.h"

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
