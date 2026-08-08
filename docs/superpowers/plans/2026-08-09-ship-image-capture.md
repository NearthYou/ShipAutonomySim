# Stage 5 선박 이미지 캡처 구현 계획

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:test-driven-development for every implementation task. Execute each checkbox in order and stop at the first failed gate.

Goal: UE 5.5.4 MainLevel 자율주행 실행에서 동일 광학 transform의 컬러와 SceneDepth 프레임을 실제 시계 100 ms 간격으로 동기 캡처하고, complete pair만 unique Saved run directory와 exact manifest로 게시한다.

Architecture: `AShipPawn`은 공통 mount, 두 `USceneCaptureComponent2D`, `UShipCapture`를 소유한다. `ShipCaptureSimulation`은 시계 scheduling, depth 정규화, frame 이름과 record 불변식을 순수 함수로 고정한다. `UShipCapture`는 동기 capture, 공개 readback, PNG 인코딩, pair publication과 idempotent finalize를 담당하고, `ASimGameMode`는 wall slide 검증, start, tick prerequisite와 terminal 결과 전달만 담당한다.

Tech Stack: Unreal Engine 5.5.4 C++20, Automation Framework, SceneCapture2D, RenderCore와 RHI 공개 readback, ImageCore, ImageWrapper, Json, Core file/path/clock API, PowerShell, Node.js, TypeScript, Python 표준 라이브러리, 현재 정적 웹 뷰어.

---

## 구현 시작 게이트

구현자는 저장소 루트의 PowerShell에서 다음 gate를 먼저 실행한다. 하나라도 실패하면 파일을 수정하거나 생성하지 않고 실제 값만 보고한다. 계획 commit은 자기 자신을 SHA로 고정할 수 없으므로 현재 HEAD가 이 계획 파일을 마지막으로 변경한 commit인지 동적으로 검증한다.

```powershell
$Repo = (git rev-parse --show-toplevel).Trim()
$Plan = 'docs/superpowers/plans/2026-08-09-ship-image-capture.md'
$PlanCommit = (git log -1 --format=%H -- $Plan).Trim()
$Branch = (git branch --show-current).Trim()
$Head = (git rev-parse HEAD).Trim()
$Status = @(git status --porcelain=v1)
$EditorCount = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count
$EditorCmdCount = @(Get-Process UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count

if ($Branch -ne 'feat/ship-image-capture') {
    throw "No-Go branch=$Branch"
}
if ($Head -ne $PlanCommit) {
    throw "No-Go HEAD=$Head plan_commit=$PlanCommit"
}
if ($Status.Count -ne 0) {
    throw "No-Go dirty paths=$($Status.Count)"
}
if ($EditorCount -ne 0 -or $EditorCmdCount -ne 0) {
    throw "No-Go UnrealEditor=$EditorCount UnrealEditor-Cmd=$EditorCmdCount"
}
git diff --quiet HEAD -- $Plan
if ($LASTEXITCODE -ne 0) {
    throw 'No-Go plan differs from HEAD'
}
```

구현 중 예상 밖 tracked 또는 untracked 파일이 나타나면 reset, restore, clean, 삭제를 하지 않는다. 현재 task가 만든 정확한 Automation capture directory만 아래 테스트 절차에 따라 정리할 수 있다.

---

## 고정 범위와 비범위

구현 범위는 다음과 같다.

- 두 SceneCapture의 공통 optical rig와 제품 기본값 512 x 512, 90 degree FOV
- 컬러 `SCS_FinalColorLDR`, 고정 수동 노출, BGRA8 PNG
- 깊이 `SCS_SceneDepth`, `PF_R32_FLOAT`, `RCM_MinMax`, 0 cm에서 5000 cm 역선형 G8 PNG
- `FPlatformTime::Seconds()`만 사용하는 frame 0 시각 0과 100 ms no-catch-up scheduler
- unique `Saved/ShipCaptures` run directory, complete pair publication, exact manifest와 idempotent finalize
- GameMode의 wall slide 검증, setup failure, tick 순서와 terminal/EndPlay 연결
- 기존 12개 ShipMovement, 19개 EditorContext ShipNavigation, 1개 ClientContext actual-world 테스트의 경계 보존
- capture-off 11-slide, capture-on 3-slide, A-B와 B-A 네 성능 run, 기존 웹 뷰어 실제 재생 확인

다음은 변경하지 않는다.

- `ShipAutonomySim/Content/Maps/MainLevel.umap`
- `ShipAutonomySim/Config` 전체 tree
- `ShipAutonomySim/ShipAutonomySim.uproject`
- `src`, `tests`, `scripts` 전체 tree와 `index.html`, `styles.css`, `package.json`, `package-lock.json`, `tsconfig.json`
- Stage 3 이동 적분, Water query, swept transform writer
- Stage 4 course, Navigator, terminal enum 값과 terminal 우선순위
- MainLevel의 DirectionalLight, SkyLight, SkyAtmosphere actor와 값
- async GPU readback, background writer, direct RHI, half float, 해상도 축소, adaptive resolution, PNG 외 format

성능 결과의 영향이 커도 마지막 항목의 대안은 보고만 한다. 별도 승인 전 구현 task나 commit에 넣지 않는다.

---

## 전체 파일 목록과 책임

### 새 파일

| 파일 | 책임 |
| --- | --- |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.h` | UObject와 file I/O가 없는 scheduler, depth 정규화, 6자리 이름과 frame record 순수 계약 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp` | 위 순수 함수의 최소 구현 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp` | 9개 EditorContext 테스트, 순수 helper, rig, readback, PNG, pair, manifest, lifecycle, GameMode |

### 수정 파일

| 파일 | 책임 |
| --- | --- |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h` | lifecycle public API, UPROPERTY 기본값, transient UObject 소유, 상태, test-only accessor |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp` | rig와 target 설정, actual-clock tick, 동기 capture/readback, PNG, file transaction, manifest, failure latch |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h` | mount, color capture, depth capture, ShipCapture 소유와 `GetCapture()` |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp` | 네 default subobject 생성, 공통 attachment, rig bind |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h` | EndPlay override, start/finalize helper와 test-only capture-off 상태 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp` | slide 검증, automatic start, prerequisite, setup/terminal/EndPlay finalize |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h` | setup enum 끝에 `CaptureInitializationFailed`만 추가 |
| `ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs` | private dependency `ImageCore`, `Json` 추가 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp` | capture mount의 component-only transform 한 줄 whitelist, `ShipCapture.cpp` actor mutator 0 고정 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp` | test-only `Stage5Capture=0`, wall slide setup과 terminal 회귀 assertion |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp` | 기존 단일 ClientContext 테스트를 validation mode와 performance mode로 확장 |
| `README.md` | Stage 5 현재 범위, Saved dataset과 현재 뷰어 재생 절차 |
| `ShipAutonomySim/SETUP.md` | 자동 시작, output 구조와 수동 확인 절차 |
| `ShipAutonomySim/AGENTS.md` | Stage 5 경계, local API 확인, output과 보호 규칙 |

이 목록 밖 파일은 구현 diff에 없어야 한다.

---

## 고정 타입과 함수 계약

### `ShipCaptureSimulation` 순수 계약

`ShipCaptureSimulation.h`에 다음 exact 이름과 의미를 둔다.

```cpp
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
```

`IsValidCaptureWallSlide`는 두 값이 유한하고 절대 오차가 `1e-9` 이하일 때만 true다. `NormalizeSceneDepthToG8`은 bound가 유한하고 `near < far`, pixel count가 정확할 때만 성공한다. 각 sample은 non-finite이면 far로 치환하고, 범위 clip 뒤 `RoundToInt((1.0 - t) * 255.0)`를 사용한다.

### frame record와 component public surface

`ShipCapture.h`의 plain value record와 public 함수는 다음으로 고정한다.

```cpp
struct FShipCaptureFrameRecord
{
    int32 Index = INDEX_NONE;
    FString ColorLeafName;
    FString DepthLeafName;
    int64 TimeMs = 0;
};

UShipCapture();

void BindCaptureRig(
    USceneComponent* InCaptureMount,
    USceneCaptureComponent2D* InColorCapture,
    USceneCaptureComponent2D* InDepthCapture);

bool StartCapture(double WallSlideCm);
void StopAndFinalize(bool bSimulationSucceeded);

bool IsCaptureActive() const;
bool HasCaptureFailure() const;
```

`TickComponent`와 `EndPlay`는 override하되 public lifecycle에 추가 저장 API를 노출하지 않는다. `AShipPawn`에는 `UShipCapture* GetCapture() const;`만 추가한다.

### UPROPERTY 기본값

| 이름 | 형식 | 기본값 | 검증 |
| --- | --- | ---: | --- |
| `CaptureResolution` | `int32` | 512 | 1 이상, square 공통 크기 |
| `CaptureFovDegrees` | `float` | 90.0 | 유한, 0보다 크고 180보다 작음 |
| `CaptureRelativeLocationCm` | `FVector` | 110, 0, 50 | 세 축 유한 |
| `CaptureRelativeRotation` | `FRotator` | 0, 0, 0 | 세 축 유한 |
| `CaptureIntervalMs` | `int32` | 100 | 1 이상 |
| `DepthNearCm` | `double` | 0.0 | 유한 |
| `DepthFarCm` | `double` | 5000.0 | 유한, near보다 큼 |

모두 `EditAnywhere`이고 category는 `Capture`로 통일한다. invalid 값을 fallback으로 바꾸지 않는다.

### lifecycle와 failure 상태

private enum은 다음 상태를 정확히 구분한다.

```cpp
enum class EShipCaptureLifecycleState : uint8
{
    NotStarted,
    Capturing,
    CaptureFailed,
    Finalizing,
    Finalized
};
```

첫 failure category와 frame index만 latch하고 같은 오류를 반복 로그하지 않는다. 최소 category는 invalid configuration, rig mismatch, invalid clock, timestamp regression, directory create, path collision, target unavailable, readback, pixel count, depth normalization, PNG encode, temp write, frame rename, pair cleanup, manifest serialize, manifest write, manifest rename을 구분한다.

`StartCapture`는 `NotStarted`에서만 성공한다. frame 0 pair가 완전히 게시된 뒤 `Capturing`과 tick enabled가 된다. runtime failure는 `CaptureFailed`와 tick disabled만 만들고 주행이나 terminal을 바꾸거나 즉시 finalize하지 않는다. `StopAndFinalize`는 `Finalizing`을 거쳐 한 번만 `Finalized`가 되며 start 전 호출은 file I/O 없는 no-op다.

runtime log marker는 `Stage5CaptureStarted`, `Stage5CapturePair`, `Stage5CaptureFailure`, `Stage5CaptureFinalized` 네 이름으로 고정한다. pair log에는 shared index, actual `time_ms`, transaction milliseconds와 project-relative run path를 넣는다. failure log에는 최초 category, index와 relative path만 넣고 한 failure당 한 번만 기록한다. 시작과 finalize marker도 run당 한 번만 기록한다.

### test-only seam

`WITH_DEV_AUTOMATION_TESTS` 내부에만 다음 failure point와 accessor를 둔다. production public surface와 Shipping binary에는 존재하지 않는다.

```cpp
#if WITH_DEV_AUTOMATION_TESTS
class AShipPawn;

enum class EShipCaptureTestFailurePoint : uint8
{
    None,
    DepthTempWrite,
    DepthFrameRename,
    ManifestTempWrite
};

struct FShipCaptureRigSnapshot
{
    bool bSetupSucceeded = false;
    int32 CaptureMountCount = 0;
    int32 ColorCaptureCount = 0;
    int32 DepthCaptureCount = 0;
    int32 ShipCaptureCount = 0;
    bool bSameAttachParent = false;
    bool bIdentityRelativeTransforms = false;
    bool bSameWorldTransform = false;
    bool bPerspectiveProjection = false;
    bool bAutomaticCaptureDisabled = false;
    bool bColorSourceFinalColorLdr = false;
    bool bDepthSourceSceneDepth = false;
    bool bColorTargetBgra8 = false;
    bool bDepthTargetR32Float = false;
    bool bFixedColorExposure = false;
    int32 Resolution = 0;
    float FovDegrees = 0.0f;
};

struct FShipCaptureTransactionSnapshot
{
    bool bSucceeded = false;
    int32 ColorCaptureSceneCallCount = 0;
    int32 DepthCaptureSceneCallCount = 0;
    int64 ColorReadbackPixelCount = 0;
    int64 DepthReadbackPixelCount = 0;
    TArray<FLinearColor> RawDepthSamples;
    TArray64<uint8> ColorPngBytes;
    TArray64<uint8> DepthPngBytes;
};

struct FShipCaptureAutomationAccessor
{
    static FShipCaptureRigSnapshot SetupRigOnly(AShipPawn& Pawn);
    static void SetCaptureResolution(UShipCapture& Capture, int32 Resolution);
    static void SetDepthRelativeLocationForTest(
        UShipCapture& Capture,
        const FVector& RelativeLocation);
    static void SetFailurePoint(
        UShipCapture& Capture,
        EShipCaptureTestFailurePoint FailurePoint);
    static FShipCaptureTransactionSnapshot CaptureSingleTransaction(
        UShipCapture& Capture,
        int32 FrameIndex,
        double CaptureSeconds);
    static bool StartCaptureAt(
        UShipCapture& Capture,
        double WallSlideCm,
        double NowSeconds);
    static void TickAt(UShipCapture& Capture, double NowSeconds);
    static FString RunDirectory(const UShipCapture& Capture);
    static int32 CommittedFrameCount(const UShipCapture& Capture);
    static int32 FinalizeAttemptCount(const UShipCapture& Capture);
    static int32 FailureLogCount(const UShipCapture& Capture);
    static int32 CaptureResolution(const UShipCapture& Capture);
    static int32 CaptureIntervalMs(const UShipCapture& Capture);
    static float CaptureFovDegrees(const UShipCapture& Capture);
    static bool HasOpticalEquality(const UShipCapture& Capture);
    static TArray<double> TransactionDurationsMs(
        const UShipCapture& Capture);
};
#endif
```

`SetupRigOnly`는 Pawn에 이미 bind된 rig의 target 생성, 설정과 validation까지만 수행해 실제 component에서 읽은 `FShipCaptureRigSnapshot`을 반환한다. `bAutomaticCaptureDisabled`는 두 capture의 every-frame, movement, persistent-state flag가 모두 false일 때만 true고, `bFixedColorExposure`는 manual method, bias 0, physical exposure false, blend weight 1이 모두 맞을 때만 true다. `Resolution`과 `FovDegrees`는 두 capture가 같은 값일 때만 그 값을 담고 불일치하면 0을 담는다. clock, run directory, `CaptureScene`, readback, file I/O와 lifecycle 전이는 금지한다. `CaptureSingleTransaction`은 test counter를 0으로 초기화한 뒤 production private transaction을 정확히 한 번 호출하고 filesystem 게시 없이 결과를 snapshot으로 복사한다. `SetDepthRelativeLocationForTest`는 transaction preflight 실패를 만들기 위한 유일한 rig 변형 경로다. Task 2와 Task 3 test는 component나 private buffer를 직접 읽지 않고 이 세 함수와 두 snapshot만으로 production 상태와 결과를 단언한다.

production `StartCapture`은 private `StartCaptureAt(WallSlideCm, FPlatformTime::Seconds())`를 호출하고 `TickComponent`는 private `TickAtTime(FPlatformTime::Seconds())`를 호출한다. 각 public 경로는 clock을 한 번만 읽는다. accessor는 같은 private path에 명시적인 `NowSeconds`만 전달하며 clock interface나 service object를 만들지 않는다.

`SimGameMode.h`에는 별도 `FShipCaptureGameModeTestAccessor` friend와 start/finalize call count, last finalize success argument만 compile guard 안에 둔다. 기존 Navigation test accessor와 이름이나 정의를 공유하지 않는다.

tick prerequisite의 최종 순서는 `UShipNavigator -> UShipMovement -> UShipCapture -> ASimGameMode`다. 기존 GameMode의 Movement prerequisite도 제거하지 않는다. terminal tick에 scheduler가 due이면 이동 뒤 pair를 먼저 게시하고, due가 아니면 terminal용 frame을 강제로 추가하지 않는다. GameMode는 기존 terminal을 먼저 latch하고 입력을 정지한 뒤 capture를 finalize한다.

GameMode는 `FShipCourseBuildResult::SlideCm`과 `ACourseBuilder::GetResolvedSlideCm()`을 각각 읽어 pure wall slide gate에 전달한다. 둘 다 유한하고 차이가 `1e-9 cm` 이하여야 `StartCapture(BuildResult.SlideCm)`를 호출한다. 기존 terminal 선택 순서 `Collision -> Success -> Timeout -> Running`과 `EShipRunResult` 값은 유지한다. Success라도 runtime calculation 또는 capture failure가 latch됐으면 manifest만 fail이고 terminal 자체는 Success 그대로다.

Automation 중 `GIsAutomationTesting`이 true면 output root에 `Automation` 한 segment를 추가한다.

```text
Saved/ShipCaptures/Automation/YYYYMMDDTHHMMSSmmmZ_GUIDDIGITS/
```

일반 실행은 다음을 사용한다.

```text
Saved/ShipCaptures/YYYYMMDDTHHMMSSmmmZ_GUIDDIGITS/
```

test cleanup은 accessor가 반환한 exact run directory만 대상으로 하며, parent가 canonical Automation root와 같고 테스트가 기록한 directory인지 재검증한 뒤 `DeleteDirectory`를 호출한다. Automation root 자체, pre-existing child, 일반 run directory, 다른 Saved 항목은 삭제하지 않는다.

컬러와 깊이는 별도 counter를 갖지 않는다. 하나의 `NextFrameIndex`와 하나의 `FShipCaptureFrameRecord`를 공유하며 두 final 파일이 모두 존재한 뒤에만 같은 index가 한 번 증가한다.

### exact manifest 예시

두 frame이 commit된 wall slide 125 cm 성공 run의 JSON shape은 다음과 같다. key를 추가하거나 이름과 type을 바꾸지 않는다.

```json
{
  "frame_count": 2,
  "interval_ms": 100,
  "depth_near_cm": 0.0,
  "depth_far_cm": 5000.0,
  "capture_resolution": [512, 512],
  "wall_slide_cm": 125.0,
  "result": "success",
  "frames": [
    {
      "index": 0,
      "color": "color_000000.png",
      "depth": "depth_000000.png",
      "time_ms": 0
    },
    {
      "index": 1,
      "color": "color_000001.png",
      "depth": "depth_000001.png",
      "time_ms": 137
    }
  ]
}
```

`interval_ms`는 목표값이고 `time_ms`는 actual clock 값이다. `result`는 `success` 또는 `fail`만 허용한다. frame object는 같은 shared index의 두 leaf filename만 기록한다. frame 0 commit 실패 시 frame count 0 manifest를 게시하지 않는다.

---

## UE 5.5.4에서 고정한 API

엔진 `Build.version`은 5.5.4, changelist 40574608이다. 구현자는 deprecated wrapper나 다른 format으로 대체하지 않는다.

| 기능 | 엔진 상대 위치와 실제 선언 또는 구현 |
| --- | --- |
| actual clock | `Core/Public/Windows/WindowsPlatformTime.h`: `static FORCEINLINE double Seconds()`, 내부 `Windows::QueryPerformanceCounter(&Cycles)` |
| 즉시 capture | `Engine/Classes/Components/SceneCaptureComponent2D.h`: `ENGINE_API void CaptureScene();` |
| capture source | `Engine/Classes/Engine/EngineTypes.h`: `SCS_FinalColorLDR`, `SCS_SceneDepth`이며 후자는 SceneDepth in R |
| 자동 capture 제어 | `SceneCaptureComponent.h`: `uint8 bCaptureEveryFrame : 1;`, `uint8 bCaptureOnMovement : 1;`, `bool bAlwaysPersistRenderingState;` |
| projection과 FOV | `SceneCaptureComponent2D.h`: `TEnumAsByte<ECameraProjectionMode::Type> ProjectionType;`, `float FOVAngle;` |
| target 연결 | `SceneCaptureComponent2D.h`: `TObjectPtr<UTextureRenderTarget2D> TextureTarget;` |
| capture post process | `SceneCaptureComponent2D.h`: `FPostProcessSettings PostProcessSettings;`, `float PostProcessBlendWeight;` |
| exposure fields | `Engine/Classes/Engine/Scene.h`: `TEnumAsByte<EAutoExposureMethod> AutoExposureMethod;`, `float AutoExposureBias;`, `uint32 AutoExposureApplyPhysicalCameraExposure : 1;`와 대응 `bOverride_` bit fields |
| target 생성 | `TextureRenderTarget2D.h`: `void InitCustomFormat(uint32, uint32, EPixelFormat, bool)` |
| target format 확인 | `TextureRenderTarget.h`와 `TextureRenderTarget2D.h`: `GetFormat() const`가 실제 `EPixelFormat`을 반환 |
| target resource | `TextureRenderTarget.h`: `FTextureRenderTargetResource* GameThread_GetRenderTargetResource();` |
| color readback | `UnrealClient.h`: `bool ReadPixels(TArray<FColor>&, FReadSurfaceDataFlags, FIntRect)` |
| float readback | `UnrealClient.h`: `bool ReadLinearColorPixels(TArray<FLinearColor>&, FReadSurfaceDataFlags, FIntRect)` |
| raw range | `RHITypes.h`: `FReadSurfaceDataFlags(ERangeCompressionMode = RCM_UNorm, ECubeFace = CubeFace_MAX)`; `RCM_MinMax`는 값을 바꾸지 않음 |
| R32 변환 | `RHISurfaceDataConversion.h`: `PF_R32_FLOAT` source를 `FLinearColor(SrcPtr[0], 0.f, 0.f, 1.f)`로 복사 |
| SceneDepth 의미 | `SceneCapturePixelShader.usf`: `float4(CalcSceneDepth(Input.UV), 0, 0, 0)`; `SceneTexturesCommon.ush`는 device Z를 camera-forward view-Z world distance로 변환 |
| image view | `ImageCore.h`: color `FImageView(const FColor*, int32, int32, EGammaSpace)`, G8 `FImageView(void*, int32, int32, int32, ERawImageFormat::Type, EGammaSpace)` |
| PNG encode | `IImageWrapperModule.h`: `bool CompressImage(TArray64<uint8>&, EImageFormat, const FImageView&, int32 Quality = 0)` |
| PNG decode test | `IImageWrapperModule.h`: `bool DecompressImage(const void*, int64, FImage&)` |
| binary write | `FileHelper.h`: `bool SaveArrayToFile(TArrayView64<const uint8>, const TCHAR*, IFileManager*, uint32)` |
| JSON text write | `FileHelper.h`: `bool SaveStringToFile(FStringView, const TCHAR*, EEncodingOptions, IFileManager*, uint32)` |
| directory | `FileManager.h`: `bool MakeDirectory(const TCHAR*, bool Tree)` |
| rename | `FileManager.h`: `bool Move(const TCHAR* Dest, const TCHAR* Src, bool Replace, bool EvenIfReadOnly, bool Attributes, bool bDoNotRetryOrError)` |
| file 검사 | `FileManager.h`: `bool FileExists(const TCHAR*)`, `int64 FileSize(const TCHAR*)` |
| exact cleanup | `FileManager.h`: `bool Delete(const TCHAR*, bool RequireExists, bool EvenReadOnly, bool Quiet)`, `bool DeleteDirectory(const TCHAR*, bool RequireExists, bool Tree)` |
| JSON writer | `JsonWriter.h`: `TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(FString*, int32)` |
| JSON serializer | `JsonSerializer.h`: `Serialize(const TSharedRef<FJsonObject>&, const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>&, bool)` |
| JSON fields | `Dom/JsonObject.h`: `void SetNumberField(const FString&, double)`, `void SetStringField(const FString&, const FString&)`, `void SetArrayField(const FString&, const TArray<TSharedPtr<FJsonValue>>&)` |
| Saved path | `Paths.h`: `static const FString& ProjectSavedDir();` |
| unique directory | `DateTime.h`: `static FDateTime UtcNow();`, `FString ToString(const TCHAR*) const`; `Guid.h`: `static FGuid NewGuid();`, `FString ToString(EGuidFormats) const` |
| Automation output guard | `CoreGlobals.h`: `extern CORE_API bool GIsAutomationTesting;` |

컬러 view는 `EGammaSpace::sRGB`, depth G8 view는 `EGammaSpace::Linear`를 명시한다. `CreateImageWrapper`, `SetRaw`, `GetCompressed` 조합은 사용하지 않는다.

---

## 공통 TDD 실행 명령

각 C++ RED와 GREEN 전에 Editor target을 incremental build한다. 최종 검증에서는 다시 clean source tree의 build를 먼저 실행한다.

```powershell
$Repo = (git rev-parse --show-toplevel).Trim()
$ProjectRoot = Join-Path $Repo 'ShipAutonomySim'
$Project = Join-Path $ProjectRoot 'ShipAutonomySim.uproject'
$UE = Join-Path ${env:ProgramFiles} 'Epic Games\UE_5.5'
$Build = Join-Path $UE 'Engine\Build\BatchFiles\Build.bat'
$Editor = Join-Path $UE 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

function Invoke-ShipEditorBuild {
    & $Build ShipAutonomySimEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) {
        throw "ShipAutonomySimEditor build failed exit=$LASTEXITCODE"
    }
}

function Invoke-RenderedEditorAutomation {
    param(
        [Parameter(Mandatory=$true)][string]$Filter,
        [Parameter(Mandatory=$true)][string]$LogName,
        [Parameter(Mandatory=$true)][int]$ExpectedCount
    )
    $Log = Join-Path $ProjectRoot "Saved\Logs\$LogName"
    $Args = @(
        $Project,
        '-Unattended',
        '-NoSplash',
        '-RenderOffscreen',
        '-NoAudio',
        '-NoPause',
        '-NoP4',
        '-nowrite',
        '-TestExit="Automation Test Queue Empty"',
        "-ExecCmds=`"Automation RunTests $Filter;SoftQuit;`"",
        "-abslog=$Log"
    )
    $Process = Start-Process -FilePath $Editor -ArgumentList $Args -PassThru -WindowStyle Hidden
    if (-not $Process.WaitForExit(600000)) {
        $Process.Kill()
        throw "Automation timeout filter=$Filter"
    }
    if ($Process.ExitCode -ne 0) {
        throw "Automation failed filter=$Filter exit=$($Process.ExitCode)"
    }
    $Text = Get-Content -LiteralPath $Log -Raw -Encoding UTF8
    if ($Text -notmatch 'TEST COMPLETE\. EXIT CODE: 0') {
        throw "Missing success marker filter=$Filter"
    }
    if ($Text -match 'Result=\{Fail\}|Unknown test|Fatal error|Ensure condition failed') {
        throw "Failure marker filter=$Filter"
    }
    $Started = ([regex]::Matches($Text, 'Test Started\. Name=')).Count
    if ($Started -ne $ExpectedCount) {
        throw "Expected $ExpectedCount tests, found $Started for $Filter"
    }
}

function Invoke-NullRhiEditorAutomation {
    param(
        [Parameter(Mandatory=$true)][string]$Filter,
        [Parameter(Mandatory=$true)][string]$LogName,
        [Parameter(Mandatory=$true)][int]$ExpectedCount
    )
    $Log = Join-Path $ProjectRoot "Saved\Logs\$LogName"
    $Args = @(
        $Project,
        '-Unattended',
        '-NoSplash',
        '-NullRHI',
        '-NoAudio',
        '-NoPause',
        '-NoP4',
        '-nowrite',
        '-TestExit="Automation Test Queue Empty"',
        "-ExecCmds=`"Automation RunTests $Filter;SoftQuit;`"",
        "-abslog=$Log"
    )
    $Process = Start-Process -FilePath $Editor -ArgumentList $Args -PassThru -WindowStyle Hidden
    if (-not $Process.WaitForExit(600000)) {
        $Process.Kill()
        throw "Automation timeout filter=$Filter"
    }
    if ($Process.ExitCode -ne 0) {
        throw "Automation failed filter=$Filter exit=$($Process.ExitCode)"
    }
    $Text = Get-Content -LiteralPath $Log -Raw -Encoding UTF8
    if ($Text -notmatch 'TEST COMPLETE\. EXIT CODE: 0') {
        throw "Missing success marker filter=$Filter"
    }
    if ($Text -match 'Result=\{Fail\}|Unknown test|Fatal error|Ensure condition failed') {
        throw "Failure marker filter=$Filter"
    }
    $Started = ([regex]::Matches($Text, 'Test Started\. Name=')).Count
    if ($Started -ne $ExpectedCount) {
        throw "Expected $ExpectedCount tests, found $Started for $Filter"
    }
}

function Assert-Stage5ActualWorldRunLog {
    param(
        [Parameter(Mandatory=$true)][string]$Log,
        [Parameter(Mandatory=$true)]
        [ValidateSet('Validation', 'Performance')][string]$Mode
    )

    $Text = Get-Content -LiteralPath $Log -Raw -Encoding UTF8
    $Started = ([regex]::Matches($Text, 'Test Started\. Name=')).Count
    $CompletedSuccess = ([regex]::Matches(
        $Text,
        'Test Completed\. Result=\{Success\}')).Count
    if ($Started -ne 1 -or $CompletedSuccess -ne 1) {
        throw "Stage5 $Mode expected one started and one successful test; started=$Started successful=$CompletedSuccess"
    }
    if ($Text -match 'Result=\{(?:Fail(?:ure)?|Error|Unknown)\}|Unknown test|Fatal error:|Ensure condition failed') {
        throw "Stage5 $Mode failure, error, unknown or ensure marker found"
    }
    if ($Text -notmatch 'TEST COMPLETE\. EXIT CODE: 0') {
        throw "Stage5 $Mode TEST COMPLETE exit marker missing"
    }
    if ($Text -notmatch 'LogExit: Exiting\.') {
        throw "Stage5 $Mode normal LogExit marker missing"
    }

    if ($Mode -eq 'Validation') {
        $Rows = @([regex]::Matches(
            $Text,
            '(?m)^.*Stage5ActualWorldCase\b[^\r\n]*$') | ForEach-Object Value)
        $SuccessRows = @($Rows | Where-Object { $_ -match '\boutcome=success\b' })
        $CaptureOffRows = @($Rows | Where-Object { $_ -match '\bphase=capture_off\b' })
        $CaptureOnRows = @($Rows | Where-Object { $_ -match '\bphase=capture_on\b' })
        $Summary = 'Stage5ActualWorldSummary cases=14 success=14 failure=0 error=0 unknown=0 ensure=0'
        if ($Rows.Count -ne 14 -or $SuccessRows.Count -ne 14 -or
            $CaptureOffRows.Count -ne 11 -or $CaptureOnRows.Count -ne 3) {
            throw "Stage5 validation rows total/success/off/on=$($Rows.Count)/$($SuccessRows.Count)/$($CaptureOffRows.Count)/$($CaptureOnRows.Count)"
        }
        if (([regex]::Matches($Text, 'Stage5ActualWorldSummary\b')).Count -ne 1 -or
            ([regex]::Matches($Text, [regex]::Escape($Summary))).Count -ne 1) {
            throw 'Stage5 validation exact zero-failure summary missing or duplicated'
        }
        return
    }

    $RunRows = @([regex]::Matches(
        $Text,
        '(?m)^.*Stage5PerformanceRun\b[^\r\n]*$') | ForEach-Object Value)
    $AggregateRows = ([regex]::Matches($Text, 'Stage5PerformanceAggregate\b')).Count
    $TransactionRows = ([regex]::Matches($Text, 'Stage5PerformanceTransaction\b')).Count
    $IntervalRows = ([regex]::Matches($Text, 'Stage5PerformanceInterval\b')).Count
    $SuccessRows = @($RunRows | Where-Object { $_ -match '\boutcome=success\b' })
    $ExpectedOrder = @(
        'order=1 condition=A',
        'order=2 condition=B',
        'order=3 condition=B',
        'order=4 condition=A'
    )
    if ($RunRows.Count -ne 4 -or $AggregateRows -ne 2 -or
        $TransactionRows -ne 2 -or $IntervalRows -ne 2 -or
        $SuccessRows.Count -ne 4) {
        throw "Stage5 performance row counts run/aggregate/transaction/interval/success=$($RunRows.Count)/$AggregateRows/$TransactionRows/$IntervalRows/$($SuccessRows.Count)"
    }
    for ($Index = 0; $Index -lt $ExpectedOrder.Count; ++$Index) {
        if ($RunRows[$Index] -notmatch [regex]::Escape($ExpectedOrder[$Index])) {
            throw "Stage5 performance order mismatch row=$($Index + 1)"
        }
    }
    $Summary = 'Stage5PerformanceSummary runs=4 success=4 failure=0 error=0 unknown=0 ensure=0'
    if (([regex]::Matches($Text, 'Stage5PerformanceSummary\b')).Count -ne 1 -or
        ([regex]::Matches($Text, [regex]::Escape($Summary))).Count -ne 1) {
        throw 'Stage5 performance exact zero-failure summary missing or duplicated'
    }
}
```

Test-first source가 아직 선언되지 않은 symbol을 참조해 compile이 실패하면 해당 compiler error와 non-zero exit가 RED 증거다. compile되는 RED는 targeted Automation의 named assertion failure와 non-zero test result를 보존한다. GREEN은 방금 추가한 test뿐 아니라 각 task가 지정한 회귀 shard까지 통과해야 한다.

---

### Task 1: 순수 scheduler, depth, 이름과 wall slide 계약을 RED로 고정

#### Files

- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.h`
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp`
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h`

#### Steps

- [ ] 3분: production header나 source를 건드리지 않은 채 `ShipCaptureTests.cpp`를 먼저 만들고 아직 없는 `ShipCaptureSimulation.h`를 include한 뒤 exact `InitializeCaptureClock`, `AdvanceCaptureSchedule`, `NormalizeSceneDepthToG8`, `MakeCaptureFrameLeafNames`, `ValidateAndAppendCaptureFrame`, `IsValidCaptureWallSlide`를 호출한다.
- [ ] 4분: 같은 test file에 `ShipAutonomySim.ShipCapture.Unit.Scheduler`를 등록하고 frame 0 time 0, 99 ms NotDue, 100 ms Due를 assertion한다.
- [ ] 4분: 같은 test에 550 ms hitch가 한 번만 Due이고 바로 다음 같은 시각은 NotDue, 이후 새 99 ms는 NotDue, 새 100 ms는 Due인 case를 추가한다.
- [ ] 3분: actual `time_ms`가 frame index 곱이 아니라 주입한 `NowSeconds - FirstCaptureSeconds`의 rounded 값인지 검사한다.
- [ ] 3분: clock rollback, NaN, positive infinity, frame 1 이후 이전과 같은 rounded timestamp가 `Invalid`인지 검사한다.
- [ ] 4분: `ShipAutonomySim.ShipCapture.Unit.DepthNormalization`을 등록하고 기본 bound에서 -1 cm 255, 0 cm 255, 2500 cm 128, 5000 cm 0, 6000 cm 0을 검사한다.
- [ ] 3분: 같은 test에 NaN과 infinity가 0, sample count mismatch와 non-finite bound, `near >= far`가 false를 반환하고 output을 비우는지 추가한다.
- [ ] 4분: `ShipAutonomySim.ShipCapture.Unit.FrameRecords`를 등록하고 `000000`, `999999`, 1000000 거부, index 0 연속 append와 duplicate, gap, 음수 time 거부를 검사한다.
- [ ] 3분: 같은 test에서 build slide와 resolved slide가 각각 non-finite인 경우, 차이가 `1e-9` 초과인 경우 false, 정확히 `1e-9` 이하는 true인지 검사한다.
- [ ] 2분: 이 시점에는 test file만 추가된 상태로 `Invoke-ShipEditorBuild`를 실행한다. `ShipCaptureSimulation.h` 부재와 그 exact symbol을 사용할 수 없다는 compiler error, non-zero exit를 첫 RED로 보존하며 이 RED 전에는 `ShipCapture.h`를 포함한 production 파일을 수정하지 않는다.
- [ ] 4분: 첫 RED 뒤에만 `FShipCaptureFrameRecord`를 `ShipCapture.h`에 추가해 generated header가 마지막 include라는 기존 규칙을 유지하고, `ShipCaptureSimulation.h`에 위 여섯 exact 함수와 관련 enum/step 선언만 추가한다. `ShipCaptureSimulation.cpp` 정의는 아직 추가하지 않는다.
- [ ] 2분: `Invoke-ShipEditorBuild`를 다시 실행해 compile은 통과하지만 exact `InitializeCaptureClock`, `AdvanceCaptureSchedule`, `NormalizeSceneDepthToG8`를 포함한 helper 정의가 없어서 `LNK2019 unresolved external symbol`과 non-zero exit가 나는 두 번째 RED를 보존한다.
- [ ] 5분: 이제 `ShipCaptureSimulation.cpp`에 고정 함수의 최소 정의만 구현한다. scheduler는 `while`, world time, `DeltaTime`, catch-up count를 사용하지 않는다.
- [ ] 4분: depth 구현은 각 sample의 R만 읽고 G/B/A를 normalization 입력으로 사용하지 않는다. intermediate와 output이 유한한지 검사한다.
- [ ] 3분: record append는 candidate index가 `Frames.Num()`, leaf 이름이 helper 결과와 일치, frame 0 time 0, 이후 time strictly increasing일 때만 append한다.
- [ ] 3분: build 후 `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture.Unit' 'Stage5-Task1-GREEN.log' 3`을 실행한다.
- [ ] 2분: `rg -n "FPlatformTime|FFileHelper|IFileManager|UObject" ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.*` 출력이 비어 있는지 확인한다.
- [ ] 3분: task 파일 네 개만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp
git commit -m "test: 캡처 순수 계약 고정" `
  -m "변경 이유`n실제 시계와 깊이 변환 경계를 구현 전에 고정" `
  -m "핵심 변경`nno-catch-up scheduler, G8 정규화, frame record와 slide 검증" `
  -m "검증 방법`nShipCapture.Unit 3개 Automation과 순수 의존성 scan"
```

---

### Task 2: UShipCapture 기본 상태, Pawn 공통 rig와 Build.cs 의존성 구현

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp`

#### Steps

- [ ] 4분: `ShipAutonomySim.ShipCapture.Component.RigConfiguration`을 먼저 등록하고 Pawn 생성 뒤 `FShipCaptureAutomationAccessor::SetupRigOnly(*Pawn)`을 호출한다. test는 반환된 `FShipCaptureRigSnapshot`만 읽어 setup 성공과 mount, color, depth, capture component가 각각 하나인지 검사한다.
- [ ] 4분: 같은 snapshot만으로 두 capture의 attach parent가 같은 mount, relative transform identity, world transform equal, perspective, FOV 90, default resolution 512인지 assertion한다.
- [ ] 4분: 같은 snapshot의 auto capture disabled, `SCS_FinalColorLDR`, `SCS_SceneDepth`, `PF_B8G8R8A8`, `PF_R32_FLOAT`, manual exposure, bias 0, physical exposure false 결과를 assertion한다. test에서 Pawn subobject, SceneCapture, render target이나 post-process field를 직접 읽지 않는다.
- [ ] 2분: build RED에서 missing Pawn subobjects, `GetCapture`, bind와 target 설정 실패를 보존한다.
- [ ] 4분: `ShipCapture.h`에 고정 public API, UPROPERTY 기본값, transient mount/capture/target `TObjectPtr`, lifecycle와 failure state를 선언한다.
- [ ] 3분: constructor를 `bCanEverTick = true`, `bStartWithTickEnabled = false`로 바꾸고 초기 state `NotStarted`를 유지한다.
- [ ] 4분: Build.cs의 기존 private `RenderCore`, `RHI`, `ImageWrapper`를 유지하고 `ImageCore`, `Json`만 추가한다. `JsonUtilities`는 추가하지 않는다.
- [ ] 4분: Pawn header에 `CaptureMount`, `ColorCapture`, `DepthCapture`, `ShipCapture`를 `VisibleAnywhere` `TObjectPtr`로 추가하고 `GetCapture()`를 선언한다.
- [ ] 5분: Pawn constructor에서 mount를 CollisionRoot에, 두 SceneCapture를 mount에 attach하고 ShipCapture를 만든 뒤 `BindCaptureRig`을 한 번 호출한다.
- [ ] 3분: `StartCapture`의 rig 설정 단계에서 mount에 다음 component-only mutator를 정확히 한 번 사용하고 두 capture relative transform은 identity인지 검증만 한다.

```cpp
CaptureMount->SetRelativeLocationAndRotation(
    CaptureRelativeLocationCm,
    CaptureRelativeRotation);
```

- [ ] 5분: 두 capture에 perspective, 동일 FOV, auto flag false를 설정하고 color와 depth target을 `NewObject<UTextureRenderTarget2D>(this)`로 만든 뒤 각각 `InitCustomFormat`한다.
- [ ] 4분: color post process를 manual exposure, bias 0, physical camera exposure false, blend weight 1로 고정한다.
- [ ] 3분: 위 exact `SetupRigOnly`와 `FShipCaptureRigSnapshot`을 `WITH_DEV_AUTOMATION_TESTS` 안에서 구현하고 일반 public API에는 노출하지 않는다.
- [ ] 4분: `ShipMovementTests.cpp`의 whitelist에 위 compact component line 한 개만 추가하고 actor mutator file count에 `ShipCapture.cpp` expected 0을 추가한다.
- [ ] 3분: build 후 `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture.Component.RigConfiguration' 'Stage5-Task2-Rig-GREEN.log' 1`을 실행한다.
- [ ] 3분: `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipMovement.Runtime.TransformOwnership' 'Stage5-Task2-Transform-GREEN.log' 1`을 실행한다.
- [ ] 2분: `rg -n "SetActor|AddActor|TeleportTo|MoveComponent" ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp` 출력이 비어 있는지 확인한다.
- [ ] 3분: task 파일 일곱 개만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp
git commit -m "feat: 선박 캡처 공통 리그 구성" `
  -m "변경 이유`n컬러와 깊이가 같은 optical transform을 공유하도록 소유권 고정" `
  -m "핵심 변경`nPawn subobject, render target 기본값, fixed exposure와 모듈 의존성" `
  -m "검증 방법`nRigConfiguration과 transform ownership Automation"
```

---

### Task 3: 동기 컬러와 float depth readback, PNG 메모리 인코딩 구현

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`

#### Steps

- [ ] 5분: `ShipAutonomySim.ShipCapture.Image.ReadbackAndEncoding`을 먼저 등록하고 작은 rendered test world, capture 앞의 known cube를 준비한 뒤 accessor의 `SetupRigOnly`, `SetCaptureResolution(..., 32)`, `CaptureSingleTransaction(..., 0, 0.0)`만 호출한다.
- [ ] 4분: `SetDepthRelativeLocationForTest`로 optical mismatch를 만든 뒤 반환된 `FShipCaptureTransactionSnapshot`의 실패와 color/depth capture call count 0을 검사한다. test가 capture component를 직접 바꾸거나 읽지 않는다.
- [ ] 4분: 정상 snapshot에서 color와 depth `CaptureScene()` call count가 각각 1이고 color/depth readback pixel count가 각각 1024인지 검사한다.
- [ ] 4분: 같은 snapshot의 `RawDepthSamples`가 정확히 1024개이고 각 R이 유한한 view-Z이며 G 0, B 0, A 1인지 검사한다.
- [ ] 4분: snapshot의 `ColorPngBytes`만 decode해 PNG signature, width와 height 32, BGRA8 8-bit browser-readable 결과를 검사한다.
- [ ] 4분: snapshot의 `DepthPngBytes`만 `DecompressImage`로 decode해 `ERawImageFormat::G8`, 8-bit, 32 x 32이며 near cube sample이 far background보다 밝은지 검사한다. Task 3의 모든 production 결과 assertion은 이 단일 transaction snapshot을 통한다.
- [ ] 2분: build RED에서 missing capture transaction 또는 failed assertions를 기록한다.
- [ ] 4분: `CaptureAndEncodePair` private path가 frame index와 capture seconds를 먼저 값으로 고정하고 optical equality를 재검증하게 한다.
- [ ] 3분: `ColorCapture->CaptureScene();` 다음 줄 흐름에서 world tick, actor write, readback 없이 `DepthCapture->CaptureScene();`을 호출한다.
- [ ] 5분: 두 `GameThread_GetRenderTargetResource()` null을 검사하고 color는 `ReadPixels`, depth는 explicit `FReadSurfaceDataFlags(RCM_MinMax)`의 `ReadLinearColorPixels`를 사용한다.
- [ ] 3분: expected count를 64-bit 곱으로 검사한 뒤 `TArray` 수와 같을 때만 계속한다.
- [ ] 4분: color는 `FImageView(FColor*, Width, Height, EGammaSpace::sRGB)`, depth는 순수 normalization output과 G8 linear view를 만든다.
- [ ] 3분: `LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"))`와 `CompressImage(ColorPng, EImageFormat::PNG, ColorView)`, `CompressImage(DepthPng, EImageFormat::PNG, DepthView)`로 두 PNG를 모두 메모리에 만든다.
- [ ] 3분: 어느 단계든 실패하면 first failure와 index를 latch하고 component tick을 끄되 filesystem과 terminal은 아직 만지지 않는다.
- [ ] 3분: build 후 `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture.Image.ReadbackAndEncoding' 'Stage5-Task3-GREEN.log' 1`을 실행한다.
- [ ] 3분: ShipCapture 전체 5개 test가 되도록 `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture' 'Stage5-Task3-Full-GREEN.log' 5`를 실행한다.
- [ ] 3분: task 파일 세 개만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp
git commit -m "feat: 컬러와 깊이 PNG 캡처 구현" `
  -m "변경 이유`n동일 시점의 컬러와 SceneDepth를 공개 동기 경로로 확보" `
  -m "핵심 변경`nFinalColorLDR, R32 float readback, BGRA8와 G8 PNG 인코딩" `
  -m "검증 방법`nrendered readback과 PNG decode Automation"
```

---

### Task 4: unique directory와 complete pair publication 구현

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`

#### Steps

- [ ] 5분: `ShipAutonomySim.ShipCapture.File.PairPublication`을 먼저 등록하고 Automation root의 pre-existing child 목록을 snapshot한다.
- [ ] 4분: valid pair에서 unique direct child 하나, `.color_000000.png.tmp`, `.depth_000000.png.tmp` 없음, final 두 파일 존재와 size 양수를 요구한다.
- [ ] 4분: test-only depth temp write와 depth rename failure를 table-driven으로 주입해 color final, depth final, 두 temp가 모두 정리되고 `Frames.Num()`과 next index가 0인지 검사한다.
- [ ] 4분: 이미 commit된 frame 0 뒤 frame 1 failure를 주입해 frame 0 pair는 남고 실패한 frame 1 파일과 record만 없는지 검사한다.
- [ ] 3분: index 999999는 게시 가능하고 다음 index는 capture failure이며 7자리 이름이 생기지 않는 순수 경계와 연결한다.
- [ ] 2분: build RED와 named file assertion 실패를 기록한다.
- [ ] 4분: run folder leaf를 `UtcNow().ToString(TEXT("%Y%m%dT%H%M%S%sZ"))`와 `NewGuid().ToString(EGuidFormats::Digits)`로 만들고 existing path에는 덮어쓰지 않는다.
- [ ] 3분: 일반 root와 `GIsAutomationTesting` root를 compile guard 안에서 분리하고 log에는 `ShipCaptures`로 시작하는 프로젝트 상대 경로만 쓴다.
- [ ] 4분: 두 encoded buffer를 frame index를 6자리로 넣은 `.color_000000.png.tmp`, `.depth_000000.png.tmp` 형식에 `SaveArrayToFile`하고 존재와 양수 size를 모두 확인한다.
- [ ] 4분: color와 depth 순서로 `Move`를 `Replace=false`로 호출하고 두 final 존재 뒤에만 `ValidateAndAppendCaptureFrame`을 호출한다.
- [ ] 4분: 두 번째 rename 실패 시 이번 index의 첫 final과 두 temp만 exact path로 `Delete`하고 prior frames는 보존한다.
- [ ] 3분: cleanup 자체 실패도 별도 failure로 latch하고 반복 삭제나 상위 directory 삭제를 시도하지 않는다.
- [ ] 3분: pair commit 뒤에만 next index가 증가하게 하고 manifest leaf에는 Saved prefix나 directory를 넣지 않는다.
- [ ] 3분: build 후 `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture.File.PairPublication' 'Stage5-Task4-GREEN.log' 1`을 실행한다.
- [ ] 3분: test가 accessor로 받은 exact Automation run directories만 지웠고 snapshot의 pre-existing child가 그대로인지 검사한다.
- [ ] 3분: task 파일 세 개만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp
git commit -m "feat: 캡처 pair 게시 경계 구현" `
  -m "변경 이유`n부분 컬러 또는 깊이 파일이 dataset record가 되는 상태 방지" `
  -m "핵심 변경`nunique run directory, temp write, non-replace rename과 exact cleanup" `
  -m "검증 방법`nPairPublication failure injection Automation"
```

---

### Task 5: actual-clock tick, exact manifest와 idempotent lifecycle 구현

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`

#### Steps

- [ ] 4분: `ShipAutonomySim.ShipCapture.Component.Lifecycle`을 먼저 등록하고 finalize-before-start가 directory와 manifest를 만들지 않는지 검사한다.
- [ ] 4분: 첫 valid `StartCapture`가 frame 0을 즉시 commit하고 time 0, active true, tick enabled가 되는지 검사한다.
- [ ] 3분: duplicate start는 false, 두 번째 directory나 frame 0을 만들지 않는지 검사한다.
- [ ] 4분: injected actual-clock sequence 99 ms, 100 ms, 550 ms hitch, 같은 tick 재호출에서 frame count가 1, 2, 3, 3이고 timestamp가 strictly increasing인지 검사한다.
- [ ] 4분: clock rollback 또는 non-finite에서 failure가 한 번 latch되고 tick만 꺼지며 manifest는 terminal 전 나타나지 않는지 검사한다.
- [ ] 4분: `ShipAutonomySim.ShipCapture.Manifest.Finalization`을 등록하고 success와 fail manifest를 각각 exact schema로 parse한다.
- [ ] 4분: top-level key 집합이 `frame_count`, `interval_ms`, `depth_near_cm`, `depth_far_cm`, `capture_resolution`, `wall_slide_cm`, `result`, `frames`와 정확히 같은지 검사한다.
- [ ] 4분: 각 frame object key가 `index`, `color`, `depth`, `time_ms`와 정확히 같고 count, 연속 index, leaf names, 실제 pair 존재가 일치하는지 검사한다.
- [ ] 3분: duplicate finalize와 GameMode 뒤 component EndPlay가 manifest bytes, modified time, finalize attempt count를 바꾸지 않는지 검사한다.
- [ ] 4분: manifest temp write failure를 주입해 `.manifest.json.tmp`와 truncated `manifest.json`이 없고 prior complete pairs는 남는지 검사한다.
- [ ] 2분: build RED와 lifecycle 또는 schema assertion 실패를 기록한다.
- [ ] 4분: `StartCapture`가 실제 clock을 한 번 읽고 frame 0 pair 성공 뒤에만 first/last clock, accumulator 0, last committed time 0을 저장한다.
- [ ] 4분: `TickComponent`는 tick마다 actual clock을 한 번 읽고 pure step을 적용하며 Due일 때 pair 하나만 시도하고 accumulator backlog를 0으로 버린다.
- [ ] 3분: `while`, `DeltaTime`, `GetTimeSeconds`, world delta, nominal index timestamp를 scheduler path에서 사용하지 않는다.
- [ ] 5분: manifest DOM을 insertion 순서대로 만들고 condensed writer로 serialize한 뒤 `ForceUTF8WithoutBOM`으로 `.manifest.json.tmp`에 저장한다.
- [ ] 3분: temp 존재와 양수 size를 검사한 뒤 `Move(*ManifestPath, *ManifestTempPath, false, false, false, true)`로 `manifest.json`을 게시한다.
- [ ] 4분: success string은 simulation success이고 runtime capture failure가 없을 때만 `success`, 나머지는 `fail`로 고정한다. frame 0이 없으면 manifest를 쓰지 않는다.
- [ ] 3분: `StopAndFinalize` 시작과 component `EndPlay`에서 tick을 먼저 끄고 같은 method를 사용한다.
- [ ] 3분: 각 successful pair의 전체 transaction 전후를 `FPlatformTime::Seconds`로 재고 milliseconds를 log와 test-only duration array에 남긴다.
- [ ] 3분: build 후 `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture.Component.Lifecycle' 'Stage5-Task5-Lifecycle-GREEN.log' 1`을 실행한다.
- [ ] 3분: `Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture.Manifest.Finalization' 'Stage5-Task5-Manifest-GREEN.log' 1`을 실행한다.
- [ ] 3분: ShipCapture 전체 8개를 실행하고 exact Automation directory cleanup을 확인한다.

```powershell
Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture' 'Stage5-Task5-Full-GREEN.log' 8
```

- [ ] 3분: task 파일 세 개만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp
git commit -m "feat: 캡처 시계와 manifest lifecycle 구현" `
  -m "변경 이유`n실제 관측 시각과 terminal dataset 게시를 한 번만 확정" `
  -m "핵심 변경`n100 ms no-catch-up tick, exact JSON, idempotent finalize" `
  -m "검증 방법`nLifecycle과 Manifest Automation 8개 full shard"
```

---

### Task 6: GameMode wall slide, setup failure, start와 terminal orchestration 연결

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`

#### Steps

- [ ] 5분: `ShipAutonomySim.ShipCapture.GameMode.Orchestration`을 먼저 등록하고 valid build slide가 `StartCapture`로 한 번 전달되는지 test accessor로 검사한다.
- [ ] 4분: build slide NaN, resolved slide infinity, 차이 `1e-9` 초과에서 setup failure가 `CaptureInitializationFailed`, start call 0, directory와 manifest 0인지 검사한다.
- [ ] 4분: capture rig 또는 first pair start failure가 같은 setup failure가 되고 `bRunActive`가 false인지 검사한다.
- [ ] 4분: Success terminal은 success finalize, Collision과 Timeout은 fail finalize, terminal result와 우선순위 값은 변하지 않는지 검사한다.
- [ ] 4분: runtime calculation error와 capture runtime error 발생 순간 finalize call 0, 이후 Timeout 또는 EndPlay에서 fail finalize 정확히 1인지 검사한다.
- [ ] 3분: early setup failure의 defensive finalize가 file I/O 없이 안전한지 검사한다.
- [ ] 4분: Capture가 Movement prerequisite를 갖고 GameMode가 Capture와 기존 Movement prerequisite를 모두 갖는지 검사한다.
- [ ] 4분: `ShipNavigation.Unit.GameMode.OptionBootstrap`에 `Stage5Capture=0` exact 값만 test build에서 capture를 끄고 missing, empty, 1, junk는 켜는 assertion을 추가한다.
- [ ] 2분: build RED에서 missing enum, GameMode helper, EndPlay와 test-only state를 기록한다.
- [ ] 3분: `CaptureInitializationFailed`를 `EShipSetupFailure`의 기존 마지막 `AutonomyActivationFailed` 뒤에 추가한다. `EShipRunResult`는 한 글자도 바꾸지 않는다.
- [ ] 4분: GameMode private에 `StartRunCapture(double BuildSlideCm, double ResolvedSlideCm)`와 `FinalizeRunCapture(bool bSimulationSucceeded)`를 추가한다.
- [ ] 4분: course build success 직후 `BuildResult.SlideCm`과 `CourseBuilder->GetResolvedSlideCm()`을 pure helper로 검사하고 invalid면 capture start 전 `RecordSetupFailure(CaptureInitializationFailed)`로 종료한다.
- [ ] 4분: ship spawn, possession, `EnterAutonomy` 성공 뒤 capture를 마지막 setup 단계로 시작하고 성공한 뒤에만 `bRunActive = true`로 둔다.
- [ ] 3분: `WITH_DEV_AUTOMATION_TESTS` 내부에서만 `UGameplayStatics::HasOption`과 `ParseOption`으로 exact `Stage5Capture=0`을 해석한다. guard 밖 제품 경로는 항상 capture-on이다.
- [ ] 3분: capture-off에서도 slide validation, autonomy와 기존 Movement prerequisite는 그대로 수행하고 capture start와 capture prerequisite만 생략한다.
- [ ] 4분: capture-on에서 `ShipCapture->AddTickPrerequisiteComponent(Movement)`, GameMode의 capture prerequisite와 기존 movement prerequisite를 모두 추가한다.
- [ ] 4분: `LatchTerminalResult`가 기존 result를 먼저 latch하고 input을 정지하고 terminal log를 남긴 뒤 finalize helper를 호출하게 한다.
- [ ] 3분: `RecordSetupFailure`는 defensive fail finalize, GameMode `EndPlay`는 fail finalize 뒤 `Super::EndPlay`을 호출한다.
- [ ] 3분: capture failure는 GameMode terminal을 바꾸지 않고 component final result만 fail이 되게 한다.
- [ ] 3분: build 후 ShipCapture 9개를 실행한다.

```powershell
Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture' 'Stage5-Task6-Capture-GREEN.log' 9
```

- [ ] 3분: Navigation editor 19개를 기존 count 그대로 실행하고 option, terminal test가 통과하는지 확인한다.

```powershell
Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage5-Task6-Navigation-GREEN.log' 19
```

- [ ] 3분: source scan으로 `Stage5Capture` 문자열의 parse와 state가 `WITH_DEV_AUTOMATION_TESTS` guard 안에만 있는지 확인한다.
- [ ] 3분: task 파일 일곱 개만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h `
  ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 캡처 실행 orchestration 연결" `
  -m "변경 이유`n유효한 course와 terminal lifecycle에만 dataset 저장 연결" `
  -m "핵심 변경`nwall slide gate, setup failure, tick order와 terminal EndPlay finalize" `
  -m "검증 방법`nShipCapture 9개와 ShipNavigation editor 19개 Automation"
```

---

### Task 7: 단일 actual-world 테스트에 capture-off 11회와 capture-on 3회 추가

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp`

#### Steps

- [ ] 4분: 기존 test 이름 `ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep`과 `ClientContext | ProductFilter` flag를 그대로 유지한다.
- [ ] 4분: 기존 11개 slide case를 validation mode의 첫 phase로 두고 모든 OpenLevel URL에 exact `Stage5Capture=0`을 추가한다.
- [ ] 4분: capture-off phase의 기존 acceptance를 11 success, collision 0, timeout 0, setup 0, runtime 0, 각 minimum wall gap 0 초과로 그대로 유지한다.
- [ ] 4분: validation mode 두 번째 phase에 `-500`, `0`, `500` 세 capture-on case를 추가하고 option에서 `Stage5Capture`를 생략한다.
- [ ] 4분: 세 case가 각각 fresh MainLevel world identity인지, product 512 x 512, FOV 90, interval 100인지 accessor와 manifest로 검사한다.
- [ ] 5분: terminal Success, capture failure 0, result success, exact wall slide, frame count 1 이상, color/depth/frame count 동일을 검사한다.
- [ ] 4분: 모든 파일명이 6자리 연속 pair, dimensions 512 x 512, frame 0 time 0, 이후 time strictly increasing인지 검사한다.
- [ ] 4분: runtime optical equality, setup/runtime/collision/timeout/ensure/crash 0과 minimum wall gap 0 초과를 요구한다.
- [ ] 4분: 각 capture-on run의 exact Automation directory를 accessor로 기록하고 manifest 검사가 끝난 뒤에만 안전 parent check를 통과한 exact 세 directory를 정리한다.
- [ ] 3분: final state에서 validation result를 capture-off 11행과 capture-on 3행으로 별도 log table에 출력한다.
- [ ] 3분: 각 행 marker를 exact `Stage5ActualWorldCase`로 고정해 `phase=capture_off|capture_on`, slide와 `outcome=success`를 넣고, 마지막에 exact `Stage5ActualWorldSummary cases=14 success=14 failure=0 error=0 unknown=0 ensure=0`을 한 번 출력한다.
- [ ] 2분: 아직 구현하지 않은 phase enum과 acceptance를 먼저 빌드해 RED compile 또는 case count failure를 기록한다.
- [ ] 5분: latent command에 `RegressionCaptureOff`, `CaptureOnAcceptance`, `Finished` phase와 phase별 case 배열을 최소 추가한다.
- [ ] 4분: 기존 fresh-world identity, watchdog, hull gap 계산과 result 구조를 재사용하고 별도 actual-world test를 등록하지 않는다.
- [ ] 3분: build 후 아래 game-context 명령으로 GREEN을 실행한다. 예상 등록 test 1, fresh world 14다.

```powershell
$Log = Join-Path $ProjectRoot 'Saved\Logs\Stage5-Task7-ActualWorld-GREEN.log'
$Args = @(
    $Project,
    '/Game/Maps/MainLevel?Stage4Slide=-500?Stage5Capture=0',
    '-game',
    '-Unattended',
    '-NoSplash',
    '-RenderOffscreen',
    '-NoAudio',
    '-NoPause',
    '-NoP4',
    '-nowrite',
    '-TestExit="Automation Test Queue Empty"',
    '-ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep;SoftQuit;"',
    "-abslog=$Log"
)
$Process = Start-Process -FilePath $Editor -ArgumentList $Args -PassThru -WindowStyle Hidden
if (-not $Process.WaitForExit(1200000)) {
    $Process.Kill()
    throw 'Actual-world validation timeout'
}
if ($Process.ExitCode -ne 0) {
    throw "Actual-world validation failed exit=$($Process.ExitCode)"
}
Assert-Stage5ActualWorldRunLog -Log $Log -Mode Validation
```

- [ ] 3분: 공통 `Assert-Stage5ActualWorldRunLog`가 test started 1, completed success 1, case row 14, row success 14, summary의 failure/error/unknown/ensure 0, `TEST COMPLETE. EXIT CODE: 0`, `LogExit: Exiting.`을 자동 판정하는지 확인한다.
- [ ] 3분: `git diff --name-only -- ShipAutonomySim/Content ShipAutonomySim/Config ShipAutonomySim/ShipAutonomySim.uproject` 출력이 비어 있는지 확인한다.
- [ ] 3분: 이 test 파일 하나만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp
git commit -m "test: 실제 월드 캡처 회귀 추가" `
  -m "변경 이유`n항법 회귀와 제품 설정 캡처를 fresh world에서 분리 검증" `
  -m "핵심 변경`ncapture-off 11 slide와 capture-on 3 slide acceptance" `
  -m "검증 방법`n단일 ClientContext test의 14 fresh-world run"
```

---

### Task 8: 같은 actual-world 테스트에 A-B, B-A 성능 측정 mode 추가

#### Files

- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp`

#### Steps

- [ ] 4분: test code 안에서만 `-Stage5Performance` command-line param을 읽고 false면 Task 7 validation mode가 그대로 실행되는지 먼저 고정한다.
- [ ] 4분: performance mode run 배열을 A capture-off, B capture-on, B capture-on, A capture-off 순서와 slide 0으로 정확히 만든다.
- [ ] 4분: 네 case가 모두 fresh world이고 같은 build, MainLevel, slide 0, FOV, 조명, 512 x 512와 100 ms 조건인지 검사한다.
- [ ] 4분: 각 run의 첫 1.0 actual second를 warm-up으로 제외하고 이후 latent command Update 시작 시각 간격을 terminal까지 `FPlatformTime::Seconds()`로 수집한다.
- [ ] 5분: 각 run별 sample count, frame-time minimum, median, p95, maximum을 계산한다. median은 even count의 가운데 두 값 평균, p95는 sorted nearest-rank `ceil(0.95 * N) - 1`을 사용한다.
- [ ] 4분: A 두 run과 B 두 run을 각각 합친 aggregate에서도 sample count와 같은 네 통계를 계산한다.
- [ ] 4분: B run은 accessor에서 pair transaction milliseconds를 복사해 run별 count, min, median, p95, max를 출력한다. A run의 transaction count는 0이어야 한다.
- [ ] 5분: B manifest의 adjacent `time_ms`로 pair count, 목표 100 ms, interval min/median/p95/max와 absolute deviation min/median/p95/max를 계산한다.
- [ ] 3분: interval 200 ms 이상 count, duplicate timestamp count, 100 ms 미만 catch-up count를 run별과 B aggregate로 출력한다.
- [ ] 4분: per-run marker는 exact `Stage5PerformanceRun`, A/B aggregate는 `Stage5PerformanceAggregate`, B transaction은 `Stage5PerformanceTransaction`, B interval과 deviation, 200 ms missed-slot, under-100 ms catch-up은 `Stage5PerformanceInterval`로 고정한다. run marker 네 행에는 순서대로 `order=1 condition=A`, `order=2 condition=B`, `order=3 condition=B`, `order=4 condition=A`와 `outcome=success`를 넣는다.
- [ ] 2분: 마지막에 exact `Stage5PerformanceSummary runs=4 success=4 failure=0 error=0 unknown=0 ensure=0`을 한 번 출력한다.
- [ ] 3분: p95, maximum, frame-time 변화에 pass/fail threshold를 추가하지 않고 관측값으로만 출력한다. data absence, duplicate, catch-up과 invalid manifest는 계약 실패로 유지한다.
- [ ] 3분: A/B run의 terminal, setup, runtime, collision, timeout acceptance와 exact Automation directory cleanup은 Task 7과 동일하게 적용한다.
- [ ] 2분: reporting 함수와 performance phase가 없는 상태의 RED compile 또는 mode assertion 실패를 기록한다.
- [ ] 5분: test-local stats helper와 `Performance` phase만 구현하고 product code, CSV writer, JSON schema를 바꾸지 않는다.
- [ ] 3분: build 후 Task 7 validation mode를 다시 실행해 14-case regression이 그대로인지 확인한다.
- [ ] 3분: final verification 순서에서는 no-write gate 뒤 아래 명령으로 performance mode를 실행한다. 이 task 개발 중에는 targeted GREEN으로 같은 명령을 사용할 수 있다.

```powershell
$Log = Join-Path $ProjectRoot 'Saved\Logs\Stage5-Performance-ABBA.log'
$Args = @(
    $Project,
    '/Game/Maps/MainLevel?Stage4Slide=0?Stage5Capture=0',
    '-game',
    '-Stage5Performance',
    '-Unattended',
    '-NoSplash',
    '-RenderOffscreen',
    '-NoAudio',
    '-NoPause',
    '-NoP4',
    '-nowrite',
    '-TestExit="Automation Test Queue Empty"',
    '-ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep;SoftQuit;"',
    "-abslog=$Log"
)
$Process = Start-Process -FilePath $Editor -ArgumentList $Args -PassThru -WindowStyle Hidden
if (-not $Process.WaitForExit(1200000)) {
    $Process.Kill()
    throw 'Performance A-B-B-A timeout'
}
if ($Process.ExitCode -ne 0) {
    throw "Performance A-B-B-A failed exit=$($Process.ExitCode)"
}
Assert-Stage5ActualWorldRunLog -Log $Log -Mode Performance
```

- [ ] 4분: 공통 `Assert-Stage5ActualWorldRunLog`가 test started 1, completed success 1, A-B-B-A run 4행과 success 4, aggregate 2행, transaction 2행, interval 2행, summary의 failure/error/unknown/ensure 0, `TEST COMPLETE. EXIT CODE: 0`, `LogExit: Exiting.`을 자동 판정하는지 확인한다.
- [ ] 3분: measured impact가 커도 async, lower resolution, alternative format 문자열이 implementation diff에 들어오지 않았는지 inspect한다.
- [ ] 3분: test 파일 하나만 stage하고 commit한다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp
git commit -m "test: 캡처 성능 A-B-B-A 측정 추가" `
  -m "변경 이유`n동기 capture 비용과 실제 저장 간격을 조건 순서 편향 없이 기록" `
  -m "핵심 변경`n네 fresh-world run, frame과 transaction 통계, missed-slot 보고" `
  -m "검증 방법`nvalidation mode 회귀와 performance mode report"
```

---

### Task 9: Stage 5 문서 정합성과 최종 보호 절차 확정

#### Files

- Modify: `README.md`
- Modify: `ShipAutonomySim/SETUP.md`
- Modify: `ShipAutonomySim/AGENTS.md`

#### Steps

- [ ] 4분: 아래 inline PowerShell assertion을 문서 수정 전에 그대로 실행한다. 세 stale exact 문구가 각각 한 번 있어 `Stage5DocumentStaleExactMatches=3`과 child process exit 1을 내는 RED를 보존한다. 별도 assertion 파일은 만들지 않는다.

```powershell
powershell -NoProfile -Command {
    $StaleExact = [ordered]@{
        'README.md' = '이 저장소에는 정적 이미지 시퀀스 웹 뷰어와 별도의 Unreal Engine 5.5.4 과제 경로인 `ShipAutonomySim`이 함께 있습니다. Unreal 과제는 `/Game/Maps/MainLevel`과 Stage 3 수동 선박 이동이 준비된 상태이며, 코스 생성과 자율주행을 연결하는 Stage 4가 이번 구현 범위입니다.'
        'ShipAutonomySim/SETUP.md' = '- 이미지 캡처와 관련 Blueprint는 이번 Stage 4 범위에서 만들지 않는다.'
        'ShipAutonomySim/AGENTS.md' = '- PCG, 이미지 캡처와 웹 뷰어 연동은 이번 Stage 4 범위에 포함하지 않는다.'
    }
    $Matches = @(
        foreach ($Entry in $StaleExact.GetEnumerator()) {
            $Text = Get-Content -LiteralPath $Entry.Key -Raw -Encoding UTF8
            if ($Text.IndexOf($Entry.Value, [StringComparison]::Ordinal) -ge 0) {
                $Entry.Key
            }
        }
    )
    Write-Output "Stage5DocumentStaleExactMatches=$($Matches.Count)"
    if ($Matches.Count -ne 0) {
        $Matches | ForEach-Object { Write-Error "stale exact match: $_" }
        exit 1
    }
    exit 0
}
if ($LASTEXITCODE -ne 0) {
    throw "Stage 5 document assertion failed exit=$LASTEXITCODE"
}
```

- [ ] 4분: root README에 Stage 5 자동 capture, `Saved/ShipCaptures/YYYYMMDDTHHMMSSmmmZ_GUIDDIGITS`, exact manifest와 viewer source 무변경 재생 절차를 최소 추가한다.
- [ ] 4분: SETUP에 MainLevel Play 자동 시작, terminal 뒤 color/depth/manifest 위치, early PIE stop fail 결과와 수동 확인을 추가한다.
- [ ] 4분: AGENTS에 Stage 5 책임, UE 5.5.4 local header 검증, UObject `UPROPERTY`, Build.cs dependency, project-relative log와 output, protected file 규칙을 추가한다.
- [ ] 3분: 문서 어디에도 local absolute path, 사용자명, 외부 ID, 실제 완료하지 않은 성능 수치가 없는지 검사한다.
- [ ] 3분: README의 viewer copy 절차가 선택 run의 `manifest.json`, `color_*.png`, `depth_*.png`만 root에 복사하고 확인 뒤 그 복사본만 exact cleanup하게 하는지 확인한다.
- [ ] 3분: 세 문서 수정 뒤 같은 inline assertion을 다시 실행해 `Stage5DocumentStaleExactMatches=0`, child process exit 0인 GREEN을 확인하고 viewer source 변경 지시가 없는지 확인한다.
- [ ] 3분: `git diff -- README.md ShipAutonomySim/SETUP.md ShipAutonomySim/AGENTS.md`로 범위와 사실 표현을 검토한다.
- [ ] 2분: `git diff --check`를 실행한다.
- [ ] 3분: 세 문서만 stage하고 commit한다.

```powershell
git add -- README.md ShipAutonomySim/SETUP.md ShipAutonomySim/AGENTS.md
git commit -m "docs: Stage 5 캡처 운영 절차 정리" `
  -m "변경 이유`n현재 구현 범위와 생성 dataset 확인 절차를 문서에 반영" `
  -m "핵심 변경`n자동 시작, Saved output, 웹 재생과 보호 경계" `
  -m "검증 방법`n문서 assertion, 변경 경로와 whitespace 검사"
```

---

## 최종 검증 순서

아래 순서를 바꾸지 않는다. 앞 단계가 실패하면 뒤 단계를 실행하지 않는다. 이 절은 구현 완료 후 새 evidence를 만들기 위한 절차이며 계획 작성 시 실행 대상이 아니다.

### 1. Editor target build

- [ ] `Invoke-ShipEditorBuild`를 실행하고 process exit 0, compile error 0을 확인한다. warning은 별도 기록한다.
- [ ] build 성공 직후 protected baseline을 기록한다.

```powershell
$Protected = @(
    'ShipAutonomySim/Content/Maps/MainLevel.umap',
    'ShipAutonomySim/ShipAutonomySim.uproject',
    'index.html',
    'styles.css',
    'package.json',
    'package-lock.json',
    'tsconfig.json'
)
$Protected += @(git ls-files -- ShipAutonomySim/Config src tests scripts)
$Protected = @($Protected | Sort-Object -Unique)
$BeforeHashes = @{}
foreach ($Relative in $Protected) {
    $Path = Join-Path $Repo $Relative
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Protected file missing: $Relative"
    }
    $BeforeHashes[$Relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}
$BeforeStatus = @(git status --porcelain=v1)
```

### 2. Automation shards

- [ ] rendered EditorContext ShipCapture 9개를 실행한다.

```powershell
Invoke-RenderedEditorAutomation 'ShipAutonomySim.ShipCapture' 'Stage5-Final-ShipCapture.log' 9
```

- [ ] NullRHI를 쓰는 별도 process에서 기존 ShipMovement 12개를 실행한다.

```powershell
Invoke-NullRhiEditorAutomation 'ShipAutonomySim.ShipMovement' 'Stage5-Final-ShipMovement.log' 12
```

- [ ] 별도 EditorContext process에서 ShipNavigation editor 19개를 실행한다.

```powershell
Invoke-NullRhiEditorAutomation 'ShipAutonomySim.ShipNavigation' 'Stage5-Final-ShipNavigation-Editor.log' 19
```
- [ ] 별도 game ClientContext process에서 Task 7 command로 actual-world test 1개와 14 fresh-world validation case를 실행한다.
- [ ] 네 process 모두 command에 `Automation` prefix가 한 번, 마지막 command가 unprefixed `SoftQuit`, expected count exact, failure/error/unknown/ensure 0, `TEST COMPLETE. EXIT CODE: 0`, normal shutdown인지 확인한다.
- [ ] test cleanup 뒤 `Saved/ShipCaptures/Automation`의 pre-existing entries가 보존되고 이번 test exact directories만 없는지 확인한다.

### 3. MainLevel no-write load

- [ ] `/Game/Maps/MainLevel?Stage5Capture=0`을 다음 command로 load한다. 기존 log가 있으면 덮어쓰지 않고 중단한다.

```powershell
$CaptureRoot = Join-Path $ProjectRoot 'Saved\ShipCaptures'
$Log = Join-Path $ProjectRoot 'Saved\Logs\Stage5-MainLevel-NoWrite.log'
if (Test-Path -LiteralPath $Log) {
    throw "Refusing to overwrite pre-existing no-write log: $Log"
}

function Get-ShipCapturesSnapshot {
    param([Parameter(Mandatory=$true)][string]$Root)
    if (-not (Test-Path -LiteralPath $Root)) {
        return '<absent>'
    }
    $CanonicalRoot = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar)
    "D|.|$((Get-Item -LiteralPath $Root).LastWriteTimeUtc.Ticks)"
    Get-ChildItem -LiteralPath $Root -Force -Recurse |
        Sort-Object FullName |
        ForEach-Object {
            $Relative = $_.FullName.Substring($CanonicalRoot.Length).TrimStart(
                [IO.Path]::DirectorySeparatorChar)
            if ($_.PSIsContainer) {
                "D|$Relative|$($_.LastWriteTimeUtc.Ticks)"
            } else {
                $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
                "F|$Relative|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)|$Hash"
            }
        }
}

$BeforeCaptureSnapshot = @(Get-ShipCapturesSnapshot -Root $CaptureRoot)
$CreatedLog = $false
try {
    & $Editor $Project '/Game/Maps/MainLevel?Stage5Capture=0' `
      -Unattended -NoSplash -NullRHI -NoAudio -NoPause -NoP4 -nowrite `
      -ExecCmds='QUIT_EDITOR' "-abslog=$Log"
    $CreatedLog = Test-Path -LiteralPath $Log
    if ($LASTEXITCODE -ne 0) {
        throw "MainLevel no-write failed exit=$LASTEXITCODE"
    }
    if (-not $CreatedLog) {
        throw 'MainLevel no-write log missing'
    }
    $Text = Get-Content -LiteralPath $Log -Raw -Encoding UTF8
    if ($Text -notmatch 'Load map complete') {
        throw 'MainLevel load marker missing'
    }
    if ($Text -match 'LoadErrors|Fatal error|Ensure condition failed|MapCheck: Error|Crash') {
        throw 'MainLevel no-write failure marker found'
    }
    if ($Text -match 'Stage5Capture(?:Started|Pair|Failure|Finalized)') {
        throw 'Capture marker found while Stage5Capture=0'
    }
    if ($Text -notmatch 'LogWorld:.*CleanupWorld' -or
        $Text -notmatch 'LogExit: Exiting\.') {
        throw 'MainLevel cleanup or normal LogExit marker missing'
    }
    $AfterCaptureSnapshot = @(Get-ShipCapturesSnapshot -Root $CaptureRoot)
    if (@(Compare-Object $BeforeCaptureSnapshot $AfterCaptureSnapshot).Count -ne 0) {
        throw 'Saved/ShipCaptures changed during capture-off no-write load; preserve entries for inspection'
    }
} finally {
    if ($CreatedLog -and (Test-Path -LiteralPath $Log)) {
        Remove-Item -LiteralPath $Log -Force
    }
}
if (Test-Path -LiteralPath $Log) {
    throw 'Exact no-write log cleanup failed'
}
```

- [ ] 자동 판정이 `Saved/ShipCaptures`의 directory, file length, mtime, SHA-256 사전/사후 snapshot 동일성, capture marker 부재, world cleanup, MapCheck error 0, LoadErrors 0, fatal/ensure/crash 0, 정상 `LogExit`와 exact test log cleanup을 모두 요구하는지 확인한다. snapshot이 다르면 새 entry를 임의 삭제하지 않고 그대로 실패시킨다.

### 4. A-B, B-A 성능 실행

- [ ] Task 8 performance command를 no-write gate 뒤 한 번 실행한다.
- [ ] A-B-B-A 네 fresh worlds와 per-run/aggregate frame time, B transaction time, actual interval과 deviation, 200 ms missed-slot, duplicate와 under-100 ms catch-up count를 원값 그대로 보존한다.
- [ ] 임의 pass/fail threshold를 만들지 않는다. 200 ms 이상 interval이 있으면 정상 100 ms 달성으로 요약하지 않는다.
- [ ] high-impact 결과가 있어도 async, lower resolution, format 변경은 제안과 승인 대기 항목으로만 보고한다.

### 5. 웹 뷰어 정적 회귀

- [ ] 저장소 root에서 다음을 실행한다.

```powershell
npm ci
if ($LASTEXITCODE -ne 0) { throw 'npm ci failed' }
npm test
if ($LASTEXITCODE -ne 0) { throw 'npm test failed' }
npm run build
if ($LASTEXITCODE -ne 0) { throw 'TypeScript build failed' }
python -m unittest tests.test_generate_dummy_data
if ($LASTEXITCODE -ne 0) { throw 'Python dummy-data test failed' }
```

- [ ] 현재 expected Node test count와 Python test count, TypeScript compile error 0을 기록한다.
- [ ] root에 생성된 dummy manifest와 PNG는 이 command가 만든 exact filenames인지 확인한 뒤 기존 script의 documented cleanup 방식만 사용한다. pre-existing 파일이면 삭제하지 않고 중단한다.

### 6. hash, diff와 Git cleanliness

- [ ] 모든 protected hash를 baseline과 비교한다.

```powershell
foreach ($Relative in $Protected) {
    $After = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Repo $Relative)).Hash
    if ($After -ne $BeforeHashes[$Relative]) {
        throw "Protected file changed: $Relative"
    }
}
git diff --check
if ($LASTEXITCODE -ne 0) {
    throw 'git diff --check failed'
}
git diff --name-only -- ShipAutonomySim/Content ShipAutonomySim/Config `
  ShipAutonomySim/ShipAutonomySim.uproject src tests scripts index.html styles.css `
  package.json package-lock.json tsconfig.json
```

- [ ] 마지막 command 출력이 비어 있는지 확인한다.
- [ ] `git status --porcelain=v1`에는 구현자가 의도한 source와 세 문서 외 경로가 없어야 한다. 모든 task commit 뒤 최종 상태는 clean이어야 한다.
- [ ] UnrealEditor와 UnrealEditor-Cmd process count가 각각 0인지 확인한다.

### 7. 실제 브라우저 playback

- [ ] capture-on acceptance가 만든 실제 run 하나를 별도 수동 실행으로 다시 만들고 terminal 뒤 finalized directory를 선택한다.
- [ ] `manifest.json`, 모든 `color_*.png`, `depth_*.png`를 repository root에 복사하기 전에 같은 이름의 pre-existing 파일이 없는지 확인한다. 있으면 덮어쓰거나 삭제하지 않고 중단한다.
- [ ] root에서 `python -m http.server 8000`을 실행하고 실제 Chromium 계열 브라우저로 `http://localhost:8000`을 연다.
- [ ] preload success, color/depth 동일 index 재생, frame 이동, actual elapsed time, depth original과 colormap 전환을 확인한다.
- [ ] browser console error, failed request, rAF playback 정지와 final-frame 오류가 없는지 확인한다.
- [ ] 서버를 정상 종료하고 이번 확인을 위해 복사한 exact manifest와 PNG만 제거한다. Saved source run과 다른 root artifact는 삭제하지 않는다.
- [ ] cleanup 뒤 Git clean과 protected hash를 다시 확인한다.

### 8. 사람이 수행할 최종 체크리스트

- [ ] UE 5.5.4 MainLevel을 Lit로 열고 DirectionalLight, SkyLight, SkyAtmosphere와 값이 변경 전과 같은지 확인한다.
- [ ] Selected Viewport PIE에서 Play만 누르고 입력 없이 ship, autonomy와 capture가 자동 시작되는지 확인한다.
- [ ] Success, Collision 또는 Timeout 뒤 finalize log와 unique Saved run directory가 한 번 나타나는지 확인한다.
- [ ] color와 depth 첫, 중간, 마지막 pair가 같은 방향과 장면인지 비교한다.
- [ ] color 연속 frame에서 exposure pumping이 없는지 확인한다.
- [ ] depth에서 가까운 전방 물체가 밝고 5000 cm 밖과 하늘이 어두운지 확인한다.
- [ ] 실제 웹 뷰어에서 두 canvas index와 시간이 함께 움직이는지 확인한다.
- [ ] depth 원본과 colormap 전환 시 가까운 물체가 따뜻한 색인지 확인한다.
- [ ] terminal 뒤 frame 수가 더 늘지 않고 manifest가 다시 쓰이지 않는지 확인한다.
- [ ] terminal 전 PIE를 중지한 별도 run이 `result: fail` manifest와 clean shutdown을 남기는지 확인한다.

이 체크리스트를 사람이 실제 수행하기 전에는 viewport 품질, color 품질, depth 형태와 browser playback을 완료로 보고하지 않는다.

---

## 예상 Automation 구성

| prefix | context | test 수 | 내용 |
| --- | --- | ---: | --- |
| `ShipAutonomySim.ShipCapture` | EditorContext | 9 | pure 3, rig 1, image 1, pair 1, lifecycle 1, manifest 1, GameMode 1 |
| `ShipAutonomySim.ShipMovement` | EditorContext | 12 | 기존 Stage 3 회귀, transform ownership 포함 |
| `ShipAutonomySim.ShipNavigation.Unit` | EditorContext | 19 | 기존 Stage 4 editor tests 그대로 |
| `ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep` | ClientContext | 1 | validation mode 14 cases, 별도 performance invocation 4 cases |

등록된 고유 test는 EditorContext 40개와 ClientContext 1개다. performance mode는 같은 ClientContext test의 별도 invocation이며 새 test를 등록하지 않는다.

---

## 구현 완료 보고 형식

완료 보고는 다음 순서를 사용하고 검증한 사실과 미수행 항목을 섞지 않는다.

1. 고정 조건
   - branch와 final HEAD
   - clean status
   - UnrealEditor와 UnrealEditor-Cmd 0
2. 변경 파일
   - 새 파일 3개
   - 수정 파일 14개
   - 보호 파일 diff 0
3. 구현된 설계
   - optical rig와 defaults
   - color/depth capture와 normalization
   - actual-clock scheduler
   - pair와 manifest publication
   - GameMode와 finalize lifecycle
4. 확인한 UE 5.5.4 API
   - `CaptureScene`, target, readback, R32 conversion
   - `CompressImage`, JSON, file와 clock signatures
5. 자동 검증
   - build exit와 warning 구분
   - ShipCapture 9, ShipMovement 12, ShipNavigation editor 19, actual-world 1
   - capture-off 11, capture-on 3
   - no-write와 hash 결과
   - npm, TypeScript, Python 결과
6. 성능 원자료
   - A-B-B-A per-run과 aggregate frame time
   - B transaction duration
   - actual interval, deviation, missed-slot, duplicate, catch-up counts
7. 실제 브라우저와 사람 확인
   - 실제 수행한 항목만 완료 표시
   - 수행하지 않은 viewport, PIE, visual quality, browser 항목은 미확인으로 분리
8. 승인 경계
   - async readback, lower resolution, 다른 format을 구현하지 않았음
   - 측정상 필요하면 별도 설계와 승인 요청만 제시

---

## 계획 self-review 체크리스트

- [ ] 9개 task가 각각 exact files, RED, 명령, 최소 GREEN, 회귀와 commit scope를 가진다.
- [ ] 새 파일 3개와 수정 파일 14개의 책임이 task와 일치한다.
- [ ] 두 capture가 같은 mount와 optical transform, 512 x 512, FOV 90, auto flags off, explicit `CaptureScene`을 사용한다.
- [ ] depth가 `PF_R32_FLOAT`, `SCS_SceneDepth`, `RCM_MinMax`, 0/5000 reverse G8이며 invalid가 0이다.
- [ ] color가 `SCS_FinalColorLDR`, manual exposure, BGRA8 PNG다.
- [ ] actual clock, frame 0 time 0, 100 ms, one-pair hitch와 no catch-up이 고정돼 있다.
- [ ] unique directory, temp pair publication, exact manifest, no empty manifest와 idempotent finalize가 고정돼 있다.
- [ ] runtime calculation error와 capture error가 즉시 새 terminal이나 finalize를 만들지 않는다.
- [ ] wall slide finite와 `1e-9` match, setup enum append와 tick order가 고정돼 있다.
- [ ] `Stage5Capture=0` parse와 state가 test compile guard 안에만 있다.
- [ ] 기존 Movement 12, Navigation editor 19, actual-world test 1의 경계가 유지된다.
- [ ] capture-off 11, capture-on -500/0/+500, A-B-B-A 4 fresh worlds가 모두 있다.
- [ ] performance는 per-run과 aggregate, transaction, interval, deviation, missed-slot, catch-up을 보고하고 임의 threshold를 만들지 않는다.
- [ ] Build, Automation, no-write, performance, npm/TS/Python, hashes, browser와 사람 확인 순서가 명확하다.
- [ ] map, Config, uproject, viewer source, user Saved artifact 보호가 명시돼 있다.
- [ ] 문서와 command에 사용자명, 민감 식별자와 repository absolute path가 없다.
- [ ] 구현, build, Editor, browser와 사람 확인을 이미 완료한 사실처럼 쓰지 않았다.
