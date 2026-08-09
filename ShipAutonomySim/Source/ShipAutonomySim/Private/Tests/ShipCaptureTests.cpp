#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShipCapture.h"
#include "ShipCaptureSimulation.h"
#include "ShipPawn.h"
#include "UObject/UObjectGlobals.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureReadbackAndEncodingTest,
    "ShipAutonomySim.ShipCapture.Image.ReadbackAndEncoding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCapturePairPublicationTest,
    "ShipAutonomySim.ShipCapture.File.PairPublication",
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

class FScopedAutomationCaptureCleanup
{
public:
    FScopedAutomationCaptureCleanup()
    {
        AutomationRoot = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() /
            TEXT("ShipCaptures/Automation"));
        FPaths::NormalizeDirectoryName(AutomationRoot);
        PreExistingChildren = DirectChildDirectories();
    }

    ~FScopedAutomationCaptureCleanup()
    {
        if (!bCleanupAttempted)
        {
            Cleanup();
        }
    }

    TArray<FString> DirectChildDirectories() const
    {
        TArray<FString> Children;
        IFileManager::Get().FindFiles(
            Children,
            *(AutomationRoot / TEXT("*")),
            false,
            true);
        Children.Sort();
        return Children;
    }

    bool Track(const FString& RunDirectory)
    {
        FString FullRunDirectory =
            FPaths::ConvertRelativePathToFull(RunDirectory);
        FPaths::NormalizeDirectoryName(FullRunDirectory);
        FString ParentDirectory = FPaths::GetPath(FullRunDirectory);
        FPaths::NormalizeDirectoryName(ParentDirectory);
        const FString LeafName = FPaths::GetCleanFilename(FullRunDirectory);
        if (ParentDirectory != AutomationRoot ||
            PreExistingChildren.Contains(LeafName))
        {
            return false;
        }
        CreatedRunDirectories.AddUnique(FullRunDirectory);
        return true;
    }

    bool Cleanup()
    {
        bCleanupAttempted = true;
        bool bAllDeleted = true;
        for (const FString& RunDirectory : CreatedRunDirectories)
        {
            bAllDeleted = IFileManager::Get().DeleteDirectory(
                *RunDirectory,
                true,
                true) && bAllDeleted;
        }
        return bAllDeleted;
    }

    FString AutomationRoot;
    TArray<FString> PreExistingChildren;

private:
    TArray<FString> CreatedRunDirectories;
    bool bCleanupAttempted = false;
};
}

void FShipCaptureAutomationAccessor::SetDepthRelativeLocationForTest(
    UShipCapture& Capture,
    const FVector& RelativeLocation)
{
    if (IsValid(Capture.DepthCapture))
    {
        Capture.DepthCapture->SetRelativeLocation(RelativeLocation);
    }
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

bool FShipCaptureReadbackAndEncodingTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    FScopedShipCaptureTestWorld TestWorld;
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Cube = TestWorld.World->SpawnActor<AStaticMeshActor>();
    TestNotNull(TEXT("Known cube mesh loads"), CubeMesh);
    TestNotNull(TEXT("Known cube actor spawns"), Cube);
    if (CubeMesh == nullptr || Cube == nullptr)
    {
        return false;
    }
    Cube->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
    Cube->SetActorLocation(FVector(600.0, 0.0, 50.0));
    Cube->SetActorScale3D(FVector(1.0, 4.0, 4.0));

    AShipPawn* MismatchPawn = TestWorld.World->SpawnActor<AShipPawn>();
    TestNotNull(TEXT("Mismatch pawn spawns"), MismatchPawn);
    if (MismatchPawn == nullptr || MismatchPawn->GetCapture() == nullptr)
    {
        return false;
    }
    UShipCapture& MismatchCapture = *MismatchPawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(MismatchCapture, 32);
    TestTrue(
        TEXT("Mismatch rig initially sets up"),
        FShipCaptureAutomationAccessor::SetupRigOnly(*MismatchPawn)
            .bSetupSucceeded);
    FShipCaptureAutomationAccessor::SetDepthRelativeLocationForTest(
        MismatchCapture,
        FVector(1.0, 0.0, 0.0));
    const FShipCaptureTransactionSnapshot Mismatch =
        FShipCaptureAutomationAccessor::CaptureSingleTransaction(
            MismatchCapture,
            0,
            0.0);
    TestFalse(TEXT("Optical mismatch rejects transaction"), Mismatch.bSucceeded);
    TestEqual(
        TEXT("Mismatch calls no color capture"),
        Mismatch.ColorCaptureSceneCallCount,
        0);
    TestEqual(
        TEXT("Mismatch calls no depth capture"),
        Mismatch.DepthCaptureSceneCallCount,
        0);
    MismatchPawn->Destroy();

    AShipPawn* Pawn = TestWorld.World->SpawnActor<AShipPawn>();
    TestNotNull(TEXT("Capture pawn spawns"), Pawn);
    if (Pawn == nullptr || Pawn->GetCapture() == nullptr)
    {
        return false;
    }
    UShipCapture& Capture = *Pawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(Capture, 32);
    TestTrue(
        TEXT("Capture rig sets up"),
        FShipCaptureAutomationAccessor::SetupRigOnly(*Pawn).bSetupSucceeded);

    const FShipCaptureTransactionSnapshot Snapshot =
        FShipCaptureAutomationAccessor::CaptureSingleTransaction(
            Capture,
            0,
            0.0);
    TestTrue(TEXT("Synchronous transaction succeeds"), Snapshot.bSucceeded);
    TestEqual(
        TEXT("Color CaptureScene is called once"),
        Snapshot.ColorCaptureSceneCallCount,
        1);
    TestEqual(
        TEXT("Depth CaptureScene is called once"),
        Snapshot.DepthCaptureSceneCallCount,
        1);
    TestEqual(
        TEXT("Color readback has 1024 pixels"),
        Snapshot.ColorReadbackPixelCount,
        int64{1024});
    TestEqual(
        TEXT("Depth readback has 1024 pixels"),
        Snapshot.DepthReadbackPixelCount,
        int64{1024});
    TestEqual(
        TEXT("Raw depth snapshot has 1024 samples"),
        Snapshot.RawDepthSamples.Num(),
        1024);
    for (int32 PixelIndex = 0;
         PixelIndex < Snapshot.RawDepthSamples.Num();
         ++PixelIndex)
    {
        const FLinearColor& Sample = Snapshot.RawDepthSamples[PixelIndex];
        if (!FMath::IsFinite(Sample.R) ||
            Sample.G != 0.0f ||
            Sample.B != 0.0f ||
            Sample.A != 1.0f)
        {
            AddError(FString::Printf(
                TEXT("Invalid raw depth sample at %d: %.9g %.9g %.9g %.9g"),
                PixelIndex,
                Sample.R,
                Sample.G,
                Sample.B,
                Sample.A));
            break;
        }
    }

    const uint8 PngSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    TestTrue(
        TEXT("Color PNG contains its signature"),
        Snapshot.ColorPngBytes.Num() >= 8 &&
            FMemory::Memcmp(
                Snapshot.ColorPngBytes.GetData(),
                PngSignature,
                UE_ARRAY_COUNT(PngSignature)) == 0);
    TestTrue(
        TEXT("Depth PNG contains its signature"),
        Snapshot.DepthPngBytes.Num() >= 8 &&
            FMemory::Memcmp(
                Snapshot.DepthPngBytes.GetData(),
                PngSignature,
                UE_ARRAY_COUNT(PngSignature)) == 0);

    IImageWrapperModule& ImageWrapper =
        FModuleManager::LoadModuleChecked<IImageWrapperModule>(
            TEXT("ImageWrapper"));
    FImage ColorImage;
    TestTrue(
        TEXT("Color PNG decodes"),
        ImageWrapper.DecompressImage(
            Snapshot.ColorPngBytes.GetData(),
            Snapshot.ColorPngBytes.Num(),
            ColorImage));
    TestEqual(TEXT("Color PNG width"), ColorImage.SizeX, 32);
    TestEqual(TEXT("Color PNG height"), ColorImage.SizeY, 32);
    TestEqual(
        TEXT("Color PNG is browser-readable BGRA8"),
        ColorImage.Format,
        ERawImageFormat::BGRA8);

    FImage DepthImage;
    TestTrue(
        TEXT("Depth PNG decodes"),
        ImageWrapper.DecompressImage(
            Snapshot.DepthPngBytes.GetData(),
            Snapshot.DepthPngBytes.Num(),
            DepthImage));
    TestEqual(TEXT("Depth PNG width"), DepthImage.SizeX, 32);
    TestEqual(TEXT("Depth PNG height"), DepthImage.SizeY, 32);
    TestEqual(
        TEXT("Depth PNG is 8-bit grayscale"),
        DepthImage.Format,
        ERawImageFormat::G8);
    if (DepthImage.Format == ERawImageFormat::G8 &&
        DepthImage.SizeX == 32 &&
        DepthImage.SizeY == 32)
    {
        const TArrayView64<const uint8> DepthPixels = DepthImage.AsG8();
        TestTrue(
            TEXT("Near cube is brighter than far background"),
            DepthPixels[16 * 32 + 16] > DepthPixels[0]);
    }

    return !HasAnyErrors();
}

bool FShipCapturePairPublicationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    FScopedAutomationCaptureCleanup Cleanup;
    FScopedShipCaptureTestWorld TestWorld;
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Cube = TestWorld.World->SpawnActor<AStaticMeshActor>();
    TestNotNull(TEXT("Pair test cube mesh loads"), CubeMesh);
    TestNotNull(TEXT("Pair test cube spawns"), Cube);
    if (CubeMesh == nullptr || Cube == nullptr)
    {
        return false;
    }
    Cube->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
    Cube->SetActorLocation(FVector(600.0, 0.0, 50.0));
    Cube->SetActorScale3D(FVector(1.0, 4.0, 4.0));

    AShipPawn* ValidPawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(ValidPawn != nullptr && ValidPawn->GetCapture() != nullptr);
    UShipCapture& ValidCapture = *ValidPawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(ValidCapture, 32);
    TestTrue(
        TEXT("Valid frame zero publishes"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            ValidCapture,
            0.0,
            100.0));
    const FString ValidRunDirectory =
        FShipCaptureAutomationAccessor::RunDirectory(ValidCapture);
    TestTrue(
        TEXT("Valid run is an owned direct Automation child"),
        Cleanup.Track(ValidRunDirectory));
    const TArray<FString> ChildrenAfterValid =
        Cleanup.DirectChildDirectories();
    TestEqual(
        TEXT("Valid start creates one unique direct child"),
        ChildrenAfterValid.Num(),
        Cleanup.PreExistingChildren.Num() + 1);
    TestFalse(
        TEXT("Color temp is not published"),
        IFileManager::Get().FileExists(
            *(ValidRunDirectory / TEXT(".color_000000.png.tmp"))));
    TestFalse(
        TEXT("Depth temp is not published"),
        IFileManager::Get().FileExists(
            *(ValidRunDirectory / TEXT(".depth_000000.png.tmp"))));
    const FString ValidColor =
        ValidRunDirectory / TEXT("color_000000.png");
    const FString ValidDepth =
        ValidRunDirectory / TEXT("depth_000000.png");
    TestTrue(
        TEXT("Color final exists with bytes"),
        IFileManager::Get().FileExists(*ValidColor) &&
            IFileManager::Get().FileSize(*ValidColor) > 0);
    TestTrue(
        TEXT("Depth final exists with bytes"),
        IFileManager::Get().FileExists(*ValidDepth) &&
            IFileManager::Get().FileSize(*ValidDepth) > 0);
    TestEqual(
        TEXT("Valid pair commits one record"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(ValidCapture),
        1);
    ValidCapture.StopAndFinalize(false);

    struct FFailureCase
    {
        const TCHAR* Label;
        EShipCaptureTestFailurePoint FailurePoint;
    };
    const FFailureCase FailureCases[] = {
        {TEXT("depth temp write"),
         EShipCaptureTestFailurePoint::DepthTempWrite},
        {TEXT("depth frame rename"),
         EShipCaptureTestFailurePoint::DepthFrameRename}};
    for (const FFailureCase& FailureCase : FailureCases)
    {
        AShipPawn* FailurePawn = TestWorld.World->SpawnActor<AShipPawn>();
        check(FailurePawn != nullptr && FailurePawn->GetCapture() != nullptr);
        UShipCapture& FailureCapture = *FailurePawn->GetCapture();
        FShipCaptureAutomationAccessor::SetCaptureResolution(
            FailureCapture,
            32);
        FShipCaptureAutomationAccessor::SetFailurePoint(
            FailureCapture,
            FailureCase.FailurePoint);
        TestFalse(
            *FString::Printf(TEXT("%s rejects frame zero"), FailureCase.Label),
            FShipCaptureAutomationAccessor::StartCaptureAt(
                FailureCapture,
                0.0,
                200.0));
        const FString FailureRunDirectory =
            FShipCaptureAutomationAccessor::RunDirectory(FailureCapture);
        TestTrue(
            *FString::Printf(TEXT("%s run is tracked"), FailureCase.Label),
            Cleanup.Track(FailureRunDirectory));
        for (const TCHAR* LeafName : {
                 TEXT("color_000000.png"),
                 TEXT("depth_000000.png"),
                 TEXT(".color_000000.png.tmp"),
                 TEXT(".depth_000000.png.tmp")})
        {
            TestFalse(
                *FString::Printf(
                    TEXT("%s cleans %s"),
                    FailureCase.Label,
                    LeafName),
                IFileManager::Get().FileExists(
                    *(FailureRunDirectory / LeafName)));
        }
        TestEqual(
            *FString::Printf(
                TEXT("%s commits no record or next index"),
                FailureCase.Label),
            FShipCaptureAutomationAccessor::CommittedFrameCount(
                FailureCapture),
            0);
    }

    AShipPawn* PriorFramePawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(PriorFramePawn != nullptr && PriorFramePawn->GetCapture() != nullptr);
    UShipCapture& PriorFrameCapture = *PriorFramePawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(
        PriorFrameCapture,
        32);
    TestTrue(
        TEXT("Prior-frame case commits frame zero"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            PriorFrameCapture,
            0.0,
            300.0));
    const FString PriorRunDirectory =
        FShipCaptureAutomationAccessor::RunDirectory(PriorFrameCapture);
    TestTrue(TEXT("Prior-frame run is tracked"), Cleanup.Track(PriorRunDirectory));
    FShipCaptureAutomationAccessor::SetFailurePoint(
        PriorFrameCapture,
        EShipCaptureTestFailurePoint::DepthFrameRename);
    FShipCaptureAutomationAccessor::TickAt(PriorFrameCapture, 300.1);
    TestTrue(
        TEXT("Prior color frame zero remains"),
        IFileManager::Get().FileSize(
            *(PriorRunDirectory / TEXT("color_000000.png"))) > 0);
    TestTrue(
        TEXT("Prior depth frame zero remains"),
        IFileManager::Get().FileSize(
            *(PriorRunDirectory / TEXT("depth_000000.png"))) > 0);
    for (const TCHAR* LeafName : {
             TEXT("color_000001.png"),
             TEXT("depth_000001.png"),
             TEXT(".color_000001.png.tmp"),
             TEXT(".depth_000001.png.tmp")})
    {
        TestFalse(
            *FString::Printf(TEXT("Failed frame one cleans %s"), LeafName),
            IFileManager::Get().FileExists(*(PriorRunDirectory / LeafName)));
    }
    TestEqual(
        TEXT("Frame one failure preserves only one committed record"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(
            PriorFrameCapture),
        1);

    AShipPawn* BoundaryPawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(BoundaryPawn != nullptr && BoundaryPawn->GetCapture() != nullptr);
    UShipCapture& BoundaryCapture = *BoundaryPawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(BoundaryCapture, 32);
    TestTrue(
        TEXT("Boundary rig sets up"),
        FShipCaptureAutomationAccessor::SetupRigOnly(*BoundaryPawn)
            .bSetupSucceeded);
    TestTrue(
        TEXT("Index 999999 captures and encodes"),
        FShipCaptureAutomationAccessor::CaptureSingleTransaction(
            BoundaryCapture,
            999999,
            999.999).bSucceeded);
    TestFalse(
        TEXT("Index 1000000 latches capture failure"),
        FShipCaptureAutomationAccessor::CaptureSingleTransaction(
            BoundaryCapture,
            1000000,
            1000.0).bSucceeded);
    for (const FString& RunDirectory : {
             ValidRunDirectory,
             PriorRunDirectory})
    {
        TestFalse(
            TEXT("No seven-digit color name is published"),
            IFileManager::Get().FileExists(
                *(RunDirectory / TEXT("color_1000000.png"))));
        TestFalse(
            TEXT("No seven-digit depth name is published"),
            IFileManager::Get().FileExists(
                *(RunDirectory / TEXT("depth_1000000.png"))));
    }

    TestTrue(
        TEXT("Only exact owned run directories are removed"),
        Cleanup.Cleanup());
    TestEqual(
        TEXT("Pre-existing Automation children are preserved"),
        FString::Join(Cleanup.DirectChildDirectories(), TEXT("|")),
        FString::Join(Cleanup.PreExistingChildren, TEXT("|")));
    return !HasAnyErrors();
}
#endif
