#if WITH_DEV_AUTOMATION_TESTS
#include <limits>

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShipCapture.h"
#include "ShipCaptureSimulation.h"
#include "ShipPawn.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureLifecycleTest,
    "ShipAutonomySim.ShipCapture.Component.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCaptureManifestFinalizationTest,
    "ShipAutonomySim.ShipCapture.Manifest.Finalization",
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
    AddExpectedError(
        TEXT("Stage5CaptureFailure"),
        EAutomationExpectedErrorFlags::Contains,
        1);

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
    AddExpectedError(
        TEXT("Stage5CaptureFailure"),
        EAutomationExpectedErrorFlags::Contains,
        4);

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

bool FShipCaptureLifecycleTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    AddExpectedError(
        TEXT("Stage5CaptureFailure"),
        EAutomationExpectedErrorFlags::Contains,
        1);

    FScopedAutomationCaptureCleanup Cleanup;
    FScopedShipCaptureTestWorld TestWorld;
    AShipPawn* BeforeStartPawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(BeforeStartPawn != nullptr && BeforeStartPawn->GetCapture() != nullptr);
    UShipCapture& BeforeStartCapture = *BeforeStartPawn->GetCapture();
    BeforeStartCapture.StopAndFinalize(false);
    TestTrue(
        TEXT("Finalize before start creates no run directory"),
        FShipCaptureAutomationAccessor::RunDirectory(BeforeStartCapture)
            .IsEmpty());
    TestEqual(
        TEXT("Finalize before start creates no Automation child"),
        Cleanup.DirectChildDirectories().Num(),
        Cleanup.PreExistingChildren.Num());
    TestEqual(
        TEXT("Finalize before start makes no attempt"),
        FShipCaptureAutomationAccessor::FinalizeAttemptCount(
            BeforeStartCapture),
        0);

    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Cube = TestWorld.World->SpawnActor<AStaticMeshActor>();
    check(CubeMesh != nullptr && Cube != nullptr);
    Cube->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
    Cube->SetActorLocation(FVector(600.0, 0.0, 50.0));
    Cube->SetActorScale3D(FVector(1.0, 4.0, 4.0));

    AShipPawn* Pawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(Pawn != nullptr && Pawn->GetCapture() != nullptr);
    UShipCapture& Capture = *Pawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(Capture, 32);
    TestTrue(
        TEXT("First start succeeds"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            Capture,
            125.0,
            100.0));
    const FString RunDirectory =
        FShipCaptureAutomationAccessor::RunDirectory(Capture);
    TestTrue(TEXT("Lifecycle run is tracked"), Cleanup.Track(RunDirectory));
    TestEqual(
        TEXT("Start commits frame zero immediately"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(Capture),
        1);
    TestTrue(TEXT("Capture is active after frame zero"), Capture.IsCaptureActive());
    TestTrue(TEXT("Tick is enabled after frame zero"), Capture.IsComponentTickEnabled());

    TestFalse(
        TEXT("Duplicate start is rejected"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            Capture,
            125.0,
            101.0));
    TestEqual(
        TEXT("Duplicate start preserves the run directory"),
        FShipCaptureAutomationAccessor::RunDirectory(Capture),
        RunDirectory);
    TestEqual(
        TEXT("Duplicate start creates no frame"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(Capture),
        1);
    TestEqual(
        TEXT("Duplicate start creates no directory"),
        Cleanup.DirectChildDirectories().Num(),
        Cleanup.PreExistingChildren.Num() + 1);

    FShipCaptureAutomationAccessor::TickAt(Capture, 100.099);
    TestEqual(
        TEXT("Ninety-nine milliseconds commits no frame"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(Capture),
        1);
    FShipCaptureAutomationAccessor::TickAt(Capture, 100.100);
    TestEqual(
        TEXT("One hundred milliseconds commits frame one"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(Capture),
        2);
    FShipCaptureAutomationAccessor::TickAt(Capture, 100.550);
    TestEqual(
        TEXT("A 550 ms hitch commits only one frame"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(Capture),
        3);
    FShipCaptureAutomationAccessor::TickAt(Capture, 100.550);
    TestEqual(
        TEXT("The same hitch clock cannot catch up"),
        FShipCaptureAutomationAccessor::CommittedFrameCount(Capture),
        3);
    TestEqual(
        TEXT("Successful pairs record three transaction durations"),
        FShipCaptureAutomationAccessor::TransactionDurationsMs(Capture).Num(),
        3);

    const FString ManifestPath = RunDirectory / TEXT("manifest.json");
    TestFalse(
        TEXT("Manifest is absent before terminal"),
        IFileManager::Get().FileExists(*ManifestPath));
    FShipCaptureAutomationAccessor::TickAt(Capture, 100.540);
    TestTrue(TEXT("Clock rollback latches capture failure"), Capture.HasCaptureFailure());
    TestFalse(TEXT("Clock failure disables tick"), Capture.IsComponentTickEnabled());
    TestEqual(
        TEXT("Clock failure is logged once"),
        FShipCaptureAutomationAccessor::FailureLogCount(Capture),
        1);
    TestFalse(
        TEXT("Clock failure still waits for terminal manifest"),
        IFileManager::Get().FileExists(*ManifestPath));

    Capture.StopAndFinalize(false);
    TestTrue(
        TEXT("Terminal finalizes the failed run"),
        IFileManager::Get().FileSize(*ManifestPath) > 0);
    TestEqual(
        TEXT("Lifecycle finalizes exactly once"),
        FShipCaptureAutomationAccessor::FinalizeAttemptCount(Capture),
        1);
    FString ManifestText;
    TSharedPtr<FJsonObject> ManifestObject;
    TestTrue(
        TEXT("Lifecycle manifest loads"),
        FFileHelper::LoadFileToString(ManifestText, *ManifestPath));
    const TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(ManifestText);
    TestTrue(
        TEXT("Lifecycle manifest parses"),
        FJsonSerializer::Deserialize(Reader, ManifestObject) &&
            ManifestObject.IsValid());
    if (ManifestObject.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* FrameValues = nullptr;
        TestTrue(
            TEXT("Lifecycle manifest has frames"),
            ManifestObject->TryGetArrayField(TEXT("frames"), FrameValues) &&
                FrameValues != nullptr);
        if (FrameValues != nullptr && FrameValues->Num() == 3)
        {
            int64 TimeZero = -1;
            int64 TimeOne = -1;
            int64 TimeTwo = -1;
            (*FrameValues)[0]->AsObject()->TryGetNumberField(
                TEXT("time_ms"), TimeZero);
            (*FrameValues)[1]->AsObject()->TryGetNumberField(
                TEXT("time_ms"), TimeOne);
            (*FrameValues)[2]->AsObject()->TryGetNumberField(
                TEXT("time_ms"), TimeTwo);
            TestEqual(TEXT("Frame zero time is zero"), TimeZero, int64{0});
            TestEqual(TEXT("Frame one time is actual 100 ms"), TimeOne, int64{100});
            TestEqual(TEXT("Hitch frame time is actual 550 ms"), TimeTwo, int64{550});
            TestTrue(
                TEXT("Lifecycle timestamps increase strictly"),
                TimeZero < TimeOne && TimeOne < TimeTwo);
        }
        else
        {
            AddError(TEXT("Lifecycle manifest must contain exactly three frames"));
        }
    }

    TestTrue(TEXT("Lifecycle run cleanup succeeds"), Cleanup.Cleanup());
    TestEqual(
        TEXT("Lifecycle cleanup preserves pre-existing children"),
        FString::Join(Cleanup.DirectChildDirectories(), TEXT("|")),
        FString::Join(Cleanup.PreExistingChildren, TEXT("|")));
    return !HasAnyErrors();
}

bool FShipCaptureManifestFinalizationTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    AddExpectedError(
        TEXT("Stage5CaptureFailure"),
        EAutomationExpectedErrorFlags::Contains,
        1);

    FScopedAutomationCaptureCleanup Cleanup;
    FScopedShipCaptureTestWorld TestWorld;
    UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    AStaticMeshActor* Cube = TestWorld.World->SpawnActor<AStaticMeshActor>();
    check(CubeMesh != nullptr && Cube != nullptr);
    Cube->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
    Cube->SetActorLocation(FVector(600.0, 0.0, 50.0));
    Cube->SetActorScale3D(FVector(1.0, 4.0, 4.0));

    const auto ValidateManifest = [this](
        const FString& RunDirectory,
        const FString& ExpectedResult,
        double ExpectedSlideCm,
        int32 ExpectedFrameCount)
    {
        const FString ManifestPath = RunDirectory / TEXT("manifest.json");
        FString JsonText;
        TSharedPtr<FJsonObject> Root;
        if (!TestTrue(
                TEXT("Manifest file loads"),
                FFileHelper::LoadFileToString(JsonText, *ManifestPath)))
        {
            return false;
        }
        const TSharedRef<TJsonReader<>> Reader =
            TJsonReaderFactory<>::Create(JsonText);
        if (!TestTrue(
                TEXT("Manifest JSON parses"),
                FJsonSerializer::Deserialize(Reader, Root) &&
                    Root.IsValid()))
        {
            return false;
        }

        TArray<FString> ActualTopKeys;
        Root->Values.GetKeys(ActualTopKeys);
        ActualTopKeys.Sort();
        TArray<FString> ExpectedTopKeys{
            TEXT("capture_resolution"),
            TEXT("depth_far_cm"),
            TEXT("depth_near_cm"),
            TEXT("frame_count"),
            TEXT("frames"),
            TEXT("interval_ms"),
            TEXT("result"),
            TEXT("wall_slide_cm")};
        ExpectedTopKeys.Sort();
        TestEqual(
            TEXT("Manifest has the exact eight top-level keys"),
            FString::Join(ActualTopKeys, TEXT("|")),
            FString::Join(ExpectedTopKeys, TEXT("|")));

        int32 FrameCount = -1;
        int32 IntervalMs = -1;
        double NearCm = -1.0;
        double FarCm = -1.0;
        double SlideCm = 0.0;
        FString Result;
        TestTrue(
            TEXT("Manifest frame_count is numeric"),
            Root->TryGetNumberField(TEXT("frame_count"), FrameCount));
        TestTrue(
            TEXT("Manifest interval_ms is numeric"),
            Root->TryGetNumberField(TEXT("interval_ms"), IntervalMs));
        TestTrue(
            TEXT("Manifest depth_near_cm is numeric"),
            Root->TryGetNumberField(TEXT("depth_near_cm"), NearCm));
        TestTrue(
            TEXT("Manifest depth_far_cm is numeric"),
            Root->TryGetNumberField(TEXT("depth_far_cm"), FarCm));
        TestTrue(
            TEXT("Manifest wall_slide_cm is numeric"),
            Root->TryGetNumberField(TEXT("wall_slide_cm"), SlideCm));
        TestTrue(
            TEXT("Manifest result is a string"),
            Root->TryGetStringField(TEXT("result"), Result));
        TestEqual(TEXT("Manifest frame count"), FrameCount, ExpectedFrameCount);
        TestEqual(TEXT("Manifest target interval"), IntervalMs, 100);
        TestEqual(TEXT("Manifest depth near"), NearCm, 0.0);
        TestEqual(TEXT("Manifest depth far"), FarCm, 5000.0);
        TestEqual(TEXT("Manifest wall slide"), SlideCm, ExpectedSlideCm);
        TestEqual(TEXT("Manifest result"), Result, ExpectedResult);

        const TArray<TSharedPtr<FJsonValue>>* ResolutionValues = nullptr;
        TestTrue(
            TEXT("Manifest resolution is an array"),
            Root->TryGetArrayField(
                TEXT("capture_resolution"),
                ResolutionValues) &&
                ResolutionValues != nullptr);
        if (ResolutionValues != nullptr && ResolutionValues->Num() == 2)
        {
            TestEqual(
                TEXT("Manifest width"),
                (*ResolutionValues)[0]->AsNumber(),
                32.0);
            TestEqual(
                TEXT("Manifest height"),
                (*ResolutionValues)[1]->AsNumber(),
                32.0);
        }
        else
        {
            AddError(TEXT("Manifest resolution must have exactly two values"));
        }

        const TArray<TSharedPtr<FJsonValue>>* FrameValues = nullptr;
        TestTrue(
            TEXT("Manifest frames is an array"),
            Root->TryGetArrayField(TEXT("frames"), FrameValues) &&
                FrameValues != nullptr);
        if (FrameValues == nullptr)
        {
            return false;
        }
        TestEqual(
            TEXT("Manifest frames match frame_count"),
            FrameValues->Num(),
            ExpectedFrameCount);
        int64 PreviousTimeMs = -1;
        for (int32 ExpectedIndex = 0;
             ExpectedIndex < FrameValues->Num();
             ++ExpectedIndex)
        {
            const TSharedPtr<FJsonValue>& FrameValue =
                (*FrameValues)[ExpectedIndex];
            if (!FrameValue.IsValid() ||
                FrameValue->Type != EJson::Object)
            {
                AddError(FString::Printf(
                    TEXT("Frame %d is not an object"),
                    ExpectedIndex));
                continue;
            }
            const TSharedPtr<FJsonObject> FrameObject =
                FrameValue->AsObject();
            TArray<FString> ActualFrameKeys;
            FrameObject->Values.GetKeys(ActualFrameKeys);
            ActualFrameKeys.Sort();
            TArray<FString> ExpectedFrameKeys{
                TEXT("color"),
                TEXT("depth"),
                TEXT("index"),
                TEXT("time_ms")};
            ExpectedFrameKeys.Sort();
            TestEqual(
                *FString::Printf(
                    TEXT("Frame %d has exact keys"),
                    ExpectedIndex),
                FString::Join(ActualFrameKeys, TEXT("|")),
                FString::Join(ExpectedFrameKeys, TEXT("|")));
            int32 Index = INDEX_NONE;
            int64 TimeMs = -1;
            FString ColorLeafName;
            FString DepthLeafName;
            FrameObject->TryGetNumberField(TEXT("index"), Index);
            FrameObject->TryGetNumberField(TEXT("time_ms"), TimeMs);
            FrameObject->TryGetStringField(
                TEXT("color"),
                ColorLeafName);
            FrameObject->TryGetStringField(
                TEXT("depth"),
                DepthLeafName);
            FString ExpectedColorLeafName;
            FString ExpectedDepthLeafName;
            MakeCaptureFrameLeafNames(
                ExpectedIndex,
                ExpectedColorLeafName,
                ExpectedDepthLeafName);
            TestEqual(TEXT("Frame index is contiguous"), Index, ExpectedIndex);
            TestEqual(
                TEXT("Frame color leaf is exact"),
                ColorLeafName,
                ExpectedColorLeafName);
            TestEqual(
                TEXT("Frame depth leaf is exact"),
                DepthLeafName,
                ExpectedDepthLeafName);
            TestTrue(
                TEXT("Frame color pair exists"),
                IFileManager::Get().FileSize(
                    *(RunDirectory / ColorLeafName)) > 0);
            TestTrue(
                TEXT("Frame depth pair exists"),
                IFileManager::Get().FileSize(
                    *(RunDirectory / DepthLeafName)) > 0);
            TestTrue(
                TEXT("Frame timestamp increases strictly"),
                ExpectedIndex == 0
                    ? TimeMs == 0
                    : TimeMs > PreviousTimeMs);
            PreviousTimeMs = TimeMs;
        }
        return true;
    };

    AShipPawn* SuccessPawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(SuccessPawn != nullptr && SuccessPawn->GetCapture() != nullptr);
    UShipCapture& SuccessCapture = *SuccessPawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(SuccessCapture, 32);
    TestTrue(
        TEXT("Success run starts"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            SuccessCapture,
            125.0,
            500.0));
    FShipCaptureAutomationAccessor::TickAt(SuccessCapture, 500.137);
    const FString SuccessRunDirectory =
        FShipCaptureAutomationAccessor::RunDirectory(SuccessCapture);
    TestTrue(TEXT("Success run is tracked"), Cleanup.Track(SuccessRunDirectory));
    SuccessCapture.StopAndFinalize(true);
    TestTrue(
        TEXT("Success manifest matches exact schema"),
        ValidateManifest(SuccessRunDirectory, TEXT("success"), 125.0, 2));

    const FString SuccessManifestPath =
        SuccessRunDirectory / TEXT("manifest.json");
    TArray64<uint8> OriginalManifestBytes;
    TestTrue(
        TEXT("Success manifest bytes load"),
        FFileHelper::LoadFileToArray(
            OriginalManifestBytes,
            *SuccessManifestPath));
    const FDateTime OriginalModifiedTime =
        IFileManager::Get().GetTimeStamp(*SuccessManifestPath);
    const int32 FinalizeCount =
        FShipCaptureAutomationAccessor::FinalizeAttemptCount(SuccessCapture);
    SuccessCapture.StopAndFinalize(false);
    SuccessPawn->DispatchBeginPlay();
    TestTrue(
        TEXT("Success capture component has begun play before EndPlay"),
        SuccessCapture.HasBegunPlay());
    SuccessCapture.EndPlay(EEndPlayReason::Quit);
    TArray64<uint8> RepeatedManifestBytes;
    TestTrue(
        TEXT("Repeated manifest bytes load"),
        FFileHelper::LoadFileToArray(
            RepeatedManifestBytes,
            *SuccessManifestPath));
    TestTrue(
        TEXT("Duplicate finalize preserves manifest bytes"),
        OriginalManifestBytes == RepeatedManifestBytes);
    TestEqual(
        TEXT("Duplicate finalize preserves modified time"),
        IFileManager::Get().GetTimeStamp(*SuccessManifestPath),
        OriginalModifiedTime);
    TestEqual(
        TEXT("Duplicate finalize and EndPlay make no new attempt"),
        FShipCaptureAutomationAccessor::FinalizeAttemptCount(
            SuccessCapture),
        FinalizeCount);

    AShipPawn* FailPawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(FailPawn != nullptr && FailPawn->GetCapture() != nullptr);
    UShipCapture& FailCapture = *FailPawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(FailCapture, 32);
    TestTrue(
        TEXT("Fail-result run starts"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            FailCapture,
            -500.0,
            600.0));
    const FString FailRunDirectory =
        FShipCaptureAutomationAccessor::RunDirectory(FailCapture);
    TestTrue(TEXT("Fail-result run is tracked"), Cleanup.Track(FailRunDirectory));
    FailCapture.StopAndFinalize(false);
    TestTrue(
        TEXT("Fail manifest matches exact schema"),
        ValidateManifest(FailRunDirectory, TEXT("fail"), -500.0, 1));

    AShipPawn* WriteFailurePawn = TestWorld.World->SpawnActor<AShipPawn>();
    check(WriteFailurePawn != nullptr &&
        WriteFailurePawn->GetCapture() != nullptr);
    UShipCapture& WriteFailureCapture = *WriteFailurePawn->GetCapture();
    FShipCaptureAutomationAccessor::SetCaptureResolution(
        WriteFailureCapture,
        32);
    TestTrue(
        TEXT("Manifest-write failure run starts"),
        FShipCaptureAutomationAccessor::StartCaptureAt(
            WriteFailureCapture,
            500.0,
            700.0));
    const FString WriteFailureRunDirectory =
        FShipCaptureAutomationAccessor::RunDirectory(WriteFailureCapture);
    TestTrue(
        TEXT("Manifest-write failure run is tracked"),
        Cleanup.Track(WriteFailureRunDirectory));
    FShipCaptureAutomationAccessor::SetFailurePoint(
        WriteFailureCapture,
        EShipCaptureTestFailurePoint::ManifestTempWrite);
    WriteFailureCapture.StopAndFinalize(true);
    TestFalse(
        TEXT("Failed manifest has no final"),
        IFileManager::Get().FileExists(
            *(WriteFailureRunDirectory / TEXT("manifest.json"))));
    TestFalse(
        TEXT("Failed manifest has no temp"),
        IFileManager::Get().FileExists(
            *(WriteFailureRunDirectory / TEXT(".manifest.json.tmp"))));
    TestTrue(
        TEXT("Manifest failure preserves prior color pair"),
        IFileManager::Get().FileSize(
            *(WriteFailureRunDirectory / TEXT("color_000000.png"))) > 0);
    TestTrue(
        TEXT("Manifest failure preserves prior depth pair"),
        IFileManager::Get().FileSize(
            *(WriteFailureRunDirectory / TEXT("depth_000000.png"))) > 0);
    TestTrue(
        TEXT("Manifest write failure latches capture failure"),
        WriteFailureCapture.HasCaptureFailure());
    TestEqual(
        TEXT("Manifest write is attempted once"),
        FShipCaptureAutomationAccessor::FinalizeAttemptCount(
            WriteFailureCapture),
        1);

    TestTrue(TEXT("Manifest run cleanup succeeds"), Cleanup.Cleanup());
    TestEqual(
        TEXT("Manifest cleanup preserves pre-existing children"),
        FString::Join(Cleanup.DirectChildDirectories(), TEXT("|")),
        FString::Join(Cleanup.PreExistingChildren, TEXT("|")));
    return !HasAnyErrors();
}
#endif
