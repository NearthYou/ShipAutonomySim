#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "ShipCapture.h"
#include "ShipCaptureSimulation.h"
#include "ShipPawn.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureSchedulerTest,
    "ShipAutonomySim.ShipCapture.Unit.Scheduler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureDepthNormalizationTest,
    "ShipAutonomySim.ShipCapture.Unit.DepthNormalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureFrameRecordsTest,
    "ShipAutonomySim.ShipCapture.Unit.FrameRecords",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureRigConfigurationTest,
    "ShipAutonomySim.ShipCapture.Component.RigConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace
{
class FScopedShipCaptureTestWorld
{
public:
    FScopedShipCaptureTestWorld()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        check(World != nullptr && World->GetPhysicsScene() != nullptr);
    }

    ~FScopedShipCaptureTestWorld()
    {
        World->DestroyWorld(false);
    }

    UWorld* World = nullptr;
};
}

bool FShipCaptureSchedulerTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    double FirstCaptureSeconds = -1.0;
    double LastClockSeconds = -1.0;
    double AccumulatedRealSeconds = -1.0;
    int64 CaptureTimeMs = -1;

    TestTrue(
        TEXT("A finite first clock initializes the schedule"),
        InitializeCaptureClock(
            42.0,
            FirstCaptureSeconds,
            LastClockSeconds,
            AccumulatedRealSeconds,
            CaptureTimeMs));
    TestEqual(TEXT("Frame zero starts at time zero"), CaptureTimeMs, int64{0});

    FShipCaptureScheduleStep Step = AdvanceCaptureSchedule(
        42.099,
        FirstCaptureSeconds,
        LastClockSeconds,
        AccumulatedRealSeconds,
        CaptureTimeMs,
        100);
    TestEqual(
        TEXT("Ninety-nine milliseconds is not due"),
        Step.Decision,
        EShipCaptureScheduleDecision::NotDue);

    Step = AdvanceCaptureSchedule(
        42.100,
        FirstCaptureSeconds,
        Step.NextLastClockSeconds,
        Step.NextAccumulatedRealSeconds,
        CaptureTimeMs,
        100);
    TestEqual(
        TEXT("One hundred milliseconds is due"),
        Step.Decision,
        EShipCaptureScheduleDecision::Due);
    TestEqual(TEXT("Due time uses the actual wall clock"), Step.CaptureTimeMs, int64{100});

    TestTrue(
        TEXT("A second schedule initializes"),
        InitializeCaptureClock(
            10.0,
            FirstCaptureSeconds,
            LastClockSeconds,
            AccumulatedRealSeconds,
            CaptureTimeMs));
    Step = AdvanceCaptureSchedule(
        10.550,
        FirstCaptureSeconds,
        LastClockSeconds,
        AccumulatedRealSeconds,
        CaptureTimeMs,
        100);
    TestEqual(
        TEXT("A 550 ms hitch produces one due decision"),
        Step.Decision,
        EShipCaptureScheduleDecision::Due);
    TestEqual(TEXT("The hitch keeps its actual time"), Step.CaptureTimeMs, int64{550});

    const int64 HitchTimeMs = Step.CaptureTimeMs;
    Step = AdvanceCaptureSchedule(
        10.550,
        FirstCaptureSeconds,
        Step.NextLastClockSeconds,
        Step.NextAccumulatedRealSeconds,
        HitchTimeMs,
        100);
    TestEqual(
        TEXT("The same clock cannot catch up"),
        Step.Decision,
        EShipCaptureScheduleDecision::NotDue);

    Step = AdvanceCaptureSchedule(
        10.649,
        FirstCaptureSeconds,
        Step.NextLastClockSeconds,
        Step.NextAccumulatedRealSeconds,
        HitchTimeMs,
        100);
    TestEqual(
        TEXT("A fresh ninety-nine milliseconds is not due"),
        Step.Decision,
        EShipCaptureScheduleDecision::NotDue);

    Step = AdvanceCaptureSchedule(
        10.650,
        FirstCaptureSeconds,
        Step.NextLastClockSeconds,
        Step.NextAccumulatedRealSeconds,
        HitchTimeMs,
        100);
    TestEqual(
        TEXT("A fresh one hundred milliseconds is due"),
        Step.Decision,
        EShipCaptureScheduleDecision::Due);
    TestEqual(TEXT("The next due time remains actual"), Step.CaptureTimeMs, int64{650});

    TestEqual(
        TEXT("Clock rollback is invalid"),
        AdvanceCaptureSchedule(9.0, 10.0, 10.0, 0.0, 0, 100).Decision,
        EShipCaptureScheduleDecision::Invalid);
    TestEqual(
        TEXT("NaN clock is invalid"),
        AdvanceCaptureSchedule(
            std::numeric_limits<double>::quiet_NaN(),
            0.0,
            0.0,
            0.0,
            0,
            100).Decision,
        EShipCaptureScheduleDecision::Invalid);
    TestEqual(
        TEXT("Positive infinity clock is invalid"),
        AdvanceCaptureSchedule(
            std::numeric_limits<double>::infinity(),
            0.0,
            0.0,
            0.0,
            0,
            100).Decision,
        EShipCaptureScheduleDecision::Invalid);
    TestEqual(
        TEXT("A repeated rounded timestamp is invalid"),
        AdvanceCaptureSchedule(0.1004, 0.0, 0.0996, 0.0996, 100, 100).Decision,
        EShipCaptureScheduleDecision::Invalid);

    return true;
}

bool FShipCaptureDepthNormalizationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    TArray<FLinearColor> Samples{
        FLinearColor(-1.0f, 100.0f, 100.0f, 100.0f),
        FLinearColor(0.0f, 100.0f, 100.0f, 100.0f),
        FLinearColor(2500.0f, 100.0f, 100.0f, 100.0f),
        FLinearColor(5000.0f, 100.0f, 100.0f, 100.0f),
        FLinearColor(6000.0f, 100.0f, 100.0f, 100.0f),
        FLinearColor(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f),
        FLinearColor(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f)};
    TArray64<uint8> Pixels;

    TestTrue(
        TEXT("Valid scene depth samples normalize"),
        NormalizeSceneDepthToG8(Samples, Samples.Num(), 0.0, 5000.0, Pixels));
    TestEqual(TEXT("All samples produce pixels"), Pixels.Num(), int64{7});
    TestEqual(TEXT("Below-near depth is white"), Pixels[0], uint8{255});
    TestEqual(TEXT("Near depth is white"), Pixels[1], uint8{255});
    TestEqual(TEXT("Mid depth rounds to 128"), Pixels[2], uint8{128});
    TestEqual(TEXT("Far depth is black"), Pixels[3], uint8{0});
    TestEqual(TEXT("Beyond-far depth is black"), Pixels[4], uint8{0});
    TestEqual(TEXT("NaN depth is invalid black"), Pixels[5], uint8{0});
    TestEqual(TEXT("Infinite depth is invalid black"), Pixels[6], uint8{0});

    Pixels.Add(17);
    TestFalse(
        TEXT("Pixel-count mismatch is rejected"),
        NormalizeSceneDepthToG8(Samples, Samples.Num() - 1, 0.0, 5000.0, Pixels));
    TestEqual(TEXT("Mismatch clears output"), Pixels.Num(), int64{0});

    Pixels.Add(17);
    TestFalse(
        TEXT("Non-finite near bound is rejected"),
        NormalizeSceneDepthToG8(
            Samples,
            Samples.Num(),
            std::numeric_limits<double>::quiet_NaN(),
            5000.0,
            Pixels));
    TestEqual(TEXT("Invalid near clears output"), Pixels.Num(), int64{0});

    Pixels.Add(17);
    TestFalse(
        TEXT("Non-finite far bound is rejected"),
        NormalizeSceneDepthToG8(
            Samples,
            Samples.Num(),
            0.0,
            std::numeric_limits<double>::infinity(),
            Pixels));
    TestEqual(TEXT("Invalid far clears output"), Pixels.Num(), int64{0});

    Pixels.Add(17);
    TestFalse(
        TEXT("Equal bounds are rejected"),
        NormalizeSceneDepthToG8(Samples, Samples.Num(), 1.0, 1.0, Pixels));
    TestEqual(TEXT("Equal bounds clear output"), Pixels.Num(), int64{0});

    Pixels.Add(17);
    TestFalse(
        TEXT("Reversed bounds are rejected"),
        NormalizeSceneDepthToG8(Samples, Samples.Num(), 2.0, 1.0, Pixels));
    TestEqual(TEXT("Reversed bounds clear output"), Pixels.Num(), int64{0});

    return true;
}

bool FShipCaptureFrameRecordsTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    FString ColorLeafName;
    FString DepthLeafName;
    TestTrue(
        TEXT("Frame zero names are valid"),
        MakeCaptureFrameLeafNames(0, ColorLeafName, DepthLeafName));
    TestEqual(TEXT("Frame zero color name"), ColorLeafName, FString(TEXT("color_000000.png")));
    TestEqual(TEXT("Frame zero depth name"), DepthLeafName, FString(TEXT("depth_000000.png")));

    TestTrue(
        TEXT("The largest six-digit frame is valid"),
        MakeCaptureFrameLeafNames(999999, ColorLeafName, DepthLeafName));
    TestEqual(
        TEXT("Largest color name"),
        ColorLeafName,
        FString(TEXT("color_999999.png")));
    TestEqual(
        TEXT("Largest depth name"),
        DepthLeafName,
        FString(TEXT("depth_999999.png")));
    TestFalse(
        TEXT("A seven-digit frame is rejected"),
        MakeCaptureFrameLeafNames(1000000, ColorLeafName, DepthLeafName));
    TestFalse(
        TEXT("A negative frame is rejected"),
        MakeCaptureFrameLeafNames(-1, ColorLeafName, DepthLeafName));

    TArray<FShipCaptureFrameRecord> Frames;
    const FShipCaptureFrameRecord FrameZero{
        0,
        TEXT("color_000000.png"),
        TEXT("depth_000000.png"),
        0};
    TestTrue(
        TEXT("Frame zero appends to an empty list"),
        ValidateAndAppendCaptureFrame(FrameZero, Frames));
    TestEqual(TEXT("One frame is committed"), Frames.Num(), 1);
    TestFalse(
        TEXT("A duplicate index is rejected"),
        ValidateAndAppendCaptureFrame(FrameZero, Frames));

    const FShipCaptureFrameRecord GapFrame{
        2,
        TEXT("color_000002.png"),
        TEXT("depth_000002.png"),
        200};
    TestFalse(TEXT("An index gap is rejected"), ValidateAndAppendCaptureFrame(GapFrame, Frames));

    const FShipCaptureFrameRecord NegativeTime{
        1,
        TEXT("color_000001.png"),
        TEXT("depth_000001.png"),
        -1};
    TestFalse(
        TEXT("A negative timestamp is rejected"),
        ValidateAndAppendCaptureFrame(NegativeTime, Frames));

    const FShipCaptureFrameRecord FrameOne{
        1,
        TEXT("color_000001.png"),
        TEXT("depth_000001.png"),
        100};
    TestTrue(TEXT("The next frame appends"), ValidateAndAppendCaptureFrame(FrameOne, Frames));

    const FShipCaptureFrameRecord RepeatedTime{
        2,
        TEXT("color_000002.png"),
        TEXT("depth_000002.png"),
        100};
    TestFalse(
        TEXT("Timestamps must increase strictly"),
        ValidateAndAppendCaptureFrame(RepeatedTime, Frames));

    TestTrue(TEXT("Equal finite slides match"), IsValidCaptureWallSlide(0.0, 0.0));
    TestTrue(
        TEXT("Exactly one nanometer-equivalent tolerance matches"),
        IsValidCaptureWallSlide(0.0, 1.0e-9));
    TestFalse(
        TEXT("More than the tolerance is rejected"),
        IsValidCaptureWallSlide(0.0, 1.0001e-9));
    TestFalse(
        TEXT("Non-finite build slide is rejected"),
        IsValidCaptureWallSlide(std::numeric_limits<double>::quiet_NaN(), 0.0));
    TestFalse(
        TEXT("Non-finite resolved slide is rejected"),
        IsValidCaptureWallSlide(0.0, std::numeric_limits<double>::infinity()));

    return true;
}

bool FShipCaptureRigConfigurationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    FScopedShipCaptureTestWorld TestWorld;
    AShipPawn* Pawn = TestWorld.World->SpawnActor<AShipPawn>();
    TestNotNull(TEXT("Ship pawn spawns"), Pawn);
    if (Pawn == nullptr)
    {
        return false;
    }

    const FShipCaptureRigSnapshot Snapshot =
        FShipCaptureAutomationAccessor::SetupRigOnly(*Pawn);
    TestTrue(TEXT("Rig setup succeeds"), Snapshot.bSetupSucceeded);
    TestEqual(TEXT("One capture mount"), Snapshot.CaptureMountCount, 1);
    TestEqual(TEXT("One color capture"), Snapshot.ColorCaptureCount, 1);
    TestEqual(TEXT("One depth capture"), Snapshot.DepthCaptureCount, 1);
    TestEqual(TEXT("One ship capture"), Snapshot.ShipCaptureCount, 1);
    TestTrue(TEXT("Both captures share the mount"), Snapshot.bSameAttachParent);
    TestTrue(
        TEXT("Both captures keep identity relative transforms"),
        Snapshot.bIdentityRelativeTransforms);
    TestTrue(
        TEXT("Both captures have the same world transform"),
        Snapshot.bSameWorldTransform);
    TestTrue(TEXT("Both captures are perspective"), Snapshot.bPerspectiveProjection);
    TestTrue(
        TEXT("Automatic capture and persistent state are disabled"),
        Snapshot.bAutomaticCaptureDisabled);
    TestTrue(
        TEXT("Color source is FinalColorLDR"),
        Snapshot.bColorSourceFinalColorLdr);
    TestTrue(
        TEXT("Depth source is SceneDepth"),
        Snapshot.bDepthSourceSceneDepth);
    TestTrue(TEXT("Color target is BGRA8"), Snapshot.bColorTargetBgra8);
    TestTrue(TEXT("Depth target is R32 float"), Snapshot.bDepthTargetR32Float);
    TestTrue(TEXT("Color exposure is fixed"), Snapshot.bFixedColorExposure);
    TestEqual(TEXT("Default resolution is 512"), Snapshot.Resolution, 512);
    TestEqual(TEXT("Default FOV is 90 degrees"), Snapshot.FovDegrees, 90.0f);

    return true;
}
#endif
