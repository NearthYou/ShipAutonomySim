# Ship Movement Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 스로틀과 조향만으로 동작하는 3단계 선박 이동 모델, Water 수면 정렬, swept 충돌, 런타임 WASD, 3인칭 카메라와 PIE 테스트 부트스트랩을 구현한다.

**Architecture:** 월드와 UObject에 독립적인 ShipMovementSimulation 값 타입과 순수 함수가 선형 및 이차 저항, yaw, bounded substep, Water 결과 분류와 직교기저를 계산한다. UShipMovement만 actor transform을 swept SetActorLocationAndRotation으로 바꾸며, AShipPawn과 이후 Navigator는 SetThrottle과 SetSteer만 호출한다. AShipPawn은 엔진 기본 hull, 카메라와 Enhanced Input adapter를 소유하고 ASimGameMode는 3단계 검증용 spawn과 possession만 담당한다.

**Tech Stack:** Unreal Engine 5.5.4, Unreal C++, Water, Enhanced Input, Unreal Automation Test

## Global Constraints

- 작업 브랜치는 feat/ship-movement-model이며 76461c18681998d8d8e954b1e0fddf79d874ffeb가 조상이어야 한다.
- 구현 시작과 각 커밋 전후에 branch, HEAD, git status를 확인하고 예상 밖 tracked change가 있으면 복구하지 말고 즉시 No-Go로 중단한다.
- 3단계만 구현한다. ShipNavigator, CourseBuilder, ShipCapture, 코스, 벽, 시작점, 종료점, 캡처, 횡미끄러짐, Chaos 구동은 수정하거나 추가하지 않는다.
- 외부 C++ 라이브러리, Starter Content, Fab 및 Marketplace 에셋을 추가하지 않는다. 런타임 의존성은 현재 엔진 내장 모듈만 사용한다.
- ShipAutonomySim.Build.cs의 기존 private InputCore, EnhancedInput, Water 의존성을 유지하고 새 모듈을 추가하지 않는다.
- actor transform의 런타임 변경 권한은 UShipMovement만 가진다. AShipPawn과 이후 자동주행은 SetThrottle(float)과 SetSteer(float)만 사용한다.
- 선형 및 이차 저항 기본값은 C1 0.447501534, C2 0.000400390770, A 105.5159376이다.
- 정지 임계값은 5 cm/s, 평형 속도는 약 200 cm/s, 180 cm/s 최초 도달은 약 4초, 타력 거리는 약 400 cm, 최고속도 선회반경은 약 250 cm다.
- forward Euler 내부 최대 스텝은 1/120초, tick당 최대 8회다. 최대 1/15초만 모사하고 초과 시간은 누적하거나 재생하지 않고 폐기한다.
- public UObject 파생 포인터에는 UPROPERTY를 붙이고 generated.h를 해당 헤더 include 목록의 마지막에 둔다.
- 저장소 비공개 식별정보를 제품과 커밋 이력에 기록하지 않는다.
- 커밋 제목 접두사는 feat, fix, test, docs, chore, perf, refactor, build 중 하나를 사용하고 제목은 한글 명사형으로 쓴다.
- 각 구현 커밋 본문에는 변경 이유, 핵심 변경, 검증 방법을 각각 별도 -m 본문으로 넣는다.

## File Structure

### 생성

- ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h: 모듈 private 값 타입, 기본 상수와 순수 함수 선언
- ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp: longitudinal 및 yaw 적분, parameter 검증, bounded schedule, Water 분류와 직교기저 계산
- ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp: 12개 Unreal Automation Test와 transient world fixture
- ShipAutonomySim/Config/DefaultInput.ini: Enhanced PlayerInput, Enhanced InputComponent, viewport focus loss key flush

### 수정

- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h: 두 입력 setter, component lifecycle, 확정 튜닝 UPROPERTY와 런타임 상태
- ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp: parameter snapshot, substep, Water query, 보간, 직교기저 회전, sweep, hit와 화면 debug
- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h: hull, movement, camera, 런타임 입력 UObject와 입력 lifecycle
- ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp: 기본 cube, collision, camera, runtime WASD mapping과 공통 입력 해제
- ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h: constructor, test ship class와 spawn transform
- ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp: default pawn 억제, ship spawn과 possession

### 확인만 하고 수정하지 않음

- ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs: Core, CoreUObject, Engine public과 InputCore, EnhancedInput, Water private 의존성이 이미 충족된다.
- ShipAutonomySim/Source/ShipAutonomySim.Target.cs와 ShipAutonomySimEditor.Target.cs: BuildSettingsVersion.V5와 Unreal5_5 include order를 유지한다.
- ShipAutonomySim/Config/DefaultEngine.ini, ShipAutonomySim/ShipAutonomySim.uproject, ShipAutonomySim/Content/Maps/MainLevel.umap, ShipAutonomySim/SETUP.md
- ShipNavigator.h 및 cpp, CourseBuilder.h 및 cpp, ShipCapture.h 및 cpp

## Verified UE 5.5.4 Contracts

- WaterSubsystem.h: static UWaterSubsystem* GetWaterSubsystem(const UWorld* InWorld), TWeakObjectPtr<UWaterBodyComponent> GetOceanBodyComponent()
- WaterBodyComponent.h: bool HasWaves() const, QueryWaterInfoClosestToWorldLocation(const FVector&, EWaterBodyQueryFlags, const TOptional<float>&) const
- WaterBodyTypes.h: ComputeLocation, ComputeNormal, IncludeWaves, GetQueryFlags, GetWaterSurfaceLocation, GetWaterSurfaceNormal, IsInExclusionVolume
- Enhanced Input: UInputMappingContext::MapKey(const UInputAction*, FKey), FEnhancedActionKeyMapping::Modifiers, AddMappingContext, RemoveMappingContext, Triggered, Completed, Canceled
- GameInstance.h와 LocalPlayer.h: UGameInstance::InitializeStandalone, AddLocalPlayer, RemoveLocalPlayer, ULocalPlayer::SpawnPlayActor, ULocalPlayer::GetSubsystem
- EnhancedInputSubsystems.h와 EnhancedInputSubsystemInterface.h: `FModifyContextOptions()`는 `bForceImmediately=false`이며 `virtual void RequestRebuildControlMappings(const FModifyContextOptions& Options = FModifyContextOptions(), EInputMappingRebuildType RebuildType = EInputMappingRebuildType::Rebuild)`. 같은 frame의 입력 전에 mapping이 필요하면 `Options.bForceImmediately = true`로 호출한다.
- PlayerController.h와 PlayerInput.h: APlayerController::InputKey(const FInputKeyParams&), PlayerTick, FlushPressedKeys와 FInputKeyParams(FKey, EInputEvent, double, bool, FInputDeviceId)
- World.h와 Actor.h: UWorld::SetGameMode(const FURL&), InitializeActorsForPlay(const FURL&), BeginPlay(), AActor::HasActorBegunPlay(). GameMode BeginPlay test는 이 정상 world lifecycle을 사용한다.
- Actor.h: bool SetActorLocationAndRotation(FVector, const FQuat&, bool, FHitResult*, ETeleportType). bSweep가 true일 때 root component만 sweep한다.
- RotationMatrix.h와 Matrix.h: FRotationMatrix::MakeFromXZ(XAxis, ZAxis).ToQuat()
- AutomationTest.h: IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, PrettyName, Flags), EditorContext와 ProductFilter

## 공통 TDD 검증 게이트

각 Task의 RED 단계는 제품 구현 전에 아래 함수를 현재 PowerShell 세션에 정의한 뒤 해당 Task의 누적 test 수 4, 6, 8, 11, 12와 실패 패턴으로 호출한다. 먼저 test cpp가 유일한 opening 및 closing guard와 해당 시점의 정확한 macro 수를 갖는지 강제하므로 Task 1부터 독립적으로 전처리할 수 있다. build가 성공하면 RED 실패로 처리하고 editor는 실행하지 않는다. 각 GREEN 단계도 같은 guard 검사를 통과한 뒤 build와 automation을 실행한다. build exit 0을 확인하기 전에는 editor를 시작하지 않으며, 발견 수, Success 수, failure/error 수와 clean exit marker를 계산해 하나라도 계약과 다르면 throw한다. 새 tracked 검증 script는 만들지 않는다.

~~~powershell
function Assert-ShipTestTranslationUnitGuard {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][ValidateSet(4,6,8,11,12)][int]$ExpectedTests
    )
    $TestSourcePath = Join-Path $RepoRoot "ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp"
    $TestSource = Get-Content -LiteralPath $TestSourcePath -Raw
    $GuardOpenCount = [regex]::Matches(
        $TestSource, '(?m)^#if WITH_DEV_AUTOMATION_TESTS\s*$').Count
    $GuardCloseCount = [regex]::Matches(
        $TestSource, '(?m)^#endif // WITH_DEV_AUTOMATION_TESTS\s*$').Count
    $MacroCount = [regex]::Matches(
        $TestSource, '(?m)^IMPLEMENT_SIMPLE_AUTOMATION_TEST\(').Count
    if (!$TestSource.TrimStart().StartsWith("#if WITH_DEV_AUTOMATION_TESTS") -or
        !$TestSource.TrimEnd().EndsWith("#endif // WITH_DEV_AUTOMATION_TESTS") -or
        $GuardOpenCount -ne 1 -or $GuardCloseCount -ne 1 -or
        $MacroCount -ne $ExpectedTests) {
        throw "No-Go: test guard mismatch open=$GuardOpenCount close=$GuardCloseCount tests=$MacroCount expected=$ExpectedTests"
    }
}

function Invoke-ExpectedRedBuild {
    param(
        [Parameter(Mandatory=$true)][string]$Stage,
        [Parameter(Mandatory=$true)][ValidateSet(4,6,8,11,12)][int]$ExpectedTests,
        [Parameter(Mandatory=$true)][string]$ExpectedFailurePattern
    )
    $RepoRoot = (git rev-parse --show-toplevel).Trim()
    if ($LASTEXITCODE -ne 0) { throw "No-Go: repository root unavailable" }
    Assert-ShipTestTranslationUnitGuard -RepoRoot $RepoRoot -ExpectedTests $ExpectedTests
    $EngineRoot = Join-Path $env:ProgramFiles "Epic Games/UE_5.5"
    $Project = Join-Path $RepoRoot "ShipAutonomySim/ShipAutonomySim.uproject"
    $BuildLog = Join-Path $RepoRoot "ShipAutonomySim/Saved/Logs/ShipMovement-$Stage-RED-build.log"
    & "$EngineRoot/Engine/Build/BatchFiles/Build.bat" ShipAutonomySimEditor Win64 Development "-Project=$Project" -WaitMutex 2>&1 |
        Tee-Object -FilePath $BuildLog
    $BuildExit = $LASTEXITCODE
    if ($BuildExit -eq 0) { throw "RED unexpectedly passed: $Stage" }
    $BuildText = Get-Content -LiteralPath $BuildLog -Raw
    if ($BuildText -notmatch $ExpectedFailurePattern) {
        throw "RED failed for an unrelated reason: $Stage"
    }
}

function Invoke-ShipGreenGate {
    param(
        [Parameter(Mandatory=$true)][ValidateRange(1,12)][int]$ExpectedTests,
        [Parameter(Mandatory=$true)][string]$Stage
    )
    $RepoRoot = (git rev-parse --show-toplevel).Trim()
    if ($LASTEXITCODE -ne 0) { throw "No-Go: repository root unavailable" }
    Assert-ShipTestTranslationUnitGuard -RepoRoot $RepoRoot -ExpectedTests $ExpectedTests
    $EngineRoot = Join-Path $env:ProgramFiles "Epic Games/UE_5.5"
    $Project = Join-Path $RepoRoot "ShipAutonomySim/ShipAutonomySim.uproject"
    $BuildLog = Join-Path $RepoRoot "ShipAutonomySim/Saved/Logs/ShipMovement-$Stage-build.log"
    $AutomationLog = Join-Path $RepoRoot "ShipAutonomySim/Saved/Logs/ShipMovement-$Stage-automation.log"
    $TrackedBefore = (git status --porcelain=v1 --untracked-files=no | Out-String)

    & "$EngineRoot/Engine/Build/BatchFiles/Build.bat" ShipAutonomySimEditor Win64 Development "-Project=$Project" -WaitMutex 2>&1 |
        Tee-Object -FilePath $BuildLog
    $BuildExit = $LASTEXITCODE
    if ($BuildExit -ne 0) { throw "No-Go: $Stage build failed with exit $BuildExit; editor was not started" }

    & "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $Project -Unattended -NoSplash -NoSound -NullRHI -NoP4 -NoAssetRegistryCache -nowrite -NoAnalytics -NoEpicPortal -stdout -FullStdOutLogOutput "-abslog=$AutomationLog" '-ExecCmds=Automation RunTests ShipAutonomySim.ShipMovement;Automation Quit'
    $EditorExit = $LASTEXITCODE
    if ($EditorExit -ne 0) { throw "No-Go: $Stage automation process failed with exit $EditorExit" }

    $LogText = Get-Content -LiteralPath $AutomationLog -Raw
    $FoundMatches = [regex]::Matches($LogText, 'Found\s+(\d+)\s+automation tests')
    $Found = if ($FoundMatches.Count -eq 0) { -1 } else { [int]$FoundMatches[$FoundMatches.Count - 1].Groups[1].Value }
    $Success = [regex]::Matches($LogText, 'Test Completed.*Result=\{Success\}').Count
    $Failure = [regex]::Matches($LogText, 'Test Completed.*Result=\{(?:Fail|Failed|Error|NotRun)\}').Count
    $AutomationErrors = [regex]::Matches($LogText, '(?m)^.*(?:LogAutomationController|LogAutomationCommandLine):\s+Error:|Fatal error:').Count
    $CleanExitMarkers = [regex]::Matches($LogText, 'LogExit: (?:Exiting\.|Editor shut down)').Count
    if ($Found -ne $ExpectedTests -or $Success -ne $ExpectedTests -or
        $Failure -ne 0 -or $AutomationErrors -ne 0 -or $CleanExitMarkers -lt 1) {
        throw "No-Go: $Stage found=$Found success=$Success failure=$Failure errors=$AutomationErrors cleanExit=$CleanExitMarkers"
    }

    $TrackedAfter = (git status --porcelain=v1 --untracked-files=no | Out-String)
    if ($TrackedBefore -cne $TrackedAfter) {
        throw "No-Go: unexpected tracked change during $Stage; do not reset, restore, clean, or delete"
    }
}
~~~

## Public and Private Interfaces

~~~cpp
// ShipMovement.h public surface
void SetThrottle(float Value);
void SetSteer(float Value);
~~~

~~~cpp
// ShipMovementSimulation.h module-private surface
enum class EShipMotionParameterState : uint8 { Defaults, Tuned, TuningFallback };
enum class EShipWaterState : uint8 { ValidWaves, ValidNoWaves, Excluded, QueryInvalid, ComponentInvalid };

struct FShipMotionParameters;
struct FShipMotionState;
struct FShipMotionInput;
struct FShipMotionStep;
struct FShipSubstepSchedule;
struct FShipSurfaceSample;
struct FShipSurfaceBasis;

FShipMotionInput MakeShipMotionInput(double Throttle, double Steer);
FShipMotionStep AdvanceShipMotion(
    const FShipMotionState& State,
    const FShipMotionInput& Input,
    const FShipMotionParameters& Parameters,
    double StepSeconds);
FShipSubstepSchedule BuildShipSubstepSchedule(
    double DeltaTimeSeconds,
    double MaxSimulationStepSeconds,
    int32 MaxSubstepsPerTick);
FShipValidatedMotionParameters ValidateShipMotionParameters(
    const FShipMotionParameters& Candidate);
FShipSurfaceSample ResolveWaterSurfaceSample(
    bool bSubsystemValid,
    bool bComponentValid,
    bool bHasWaves,
    const FWaterBodyQueryResult* QueryResult,
    const TOptional<FShipSurfaceSample>& LastValidSample,
    double CurrentActorZ);
FShipSurfaceBasis BuildShipSurfaceBasis(
    double YawDegrees,
    const FVector& SurfaceNormal,
    const TOptional<FVector>& LastValidNormal);
~~~

## Spec Coverage

| 설계 요구 | 구현 Task | 자동 또는 수동 검증 |
| --- | --- | --- |
| 선형 및 이차 저항, 정지 5, 최고속도 200, t90 4초, 타력 400 | Task 1 | Motion.Dynamics, Motion.Targets |
| 250 cm 선회반경, 속도 비례 yaw, 제자리 회전 금지, 횡미끄러짐 제외 | Task 1 | Motion.Dynamics, Motion.FrameRatesAndHitch |
| 1/120초, 최대 8회, 초과 시간 폐기와 catch-up 금지 | Task 1 | Motion.FrameRatesAndHitch |
| 편집 범위, 평형 속도, Euler 안정성, snapshot 전체 fallback | Task 1, Task 3 | Motion.ParameterValidation, runtime debug |
| Water 파도 및 무파도, exclusion, invalid, component fallback | Task 2, Task 3 | Water.Classification, PIE water 상태 |
| 경사 수면에서 world XY yaw를 보존하는 직교기저 | Task 2, Task 3 | Water.SurfaceBasis, PIE 횡경사 |
| UShipMovement만 swept transform 변경, blocking hit speed 0 | Task 3 | Runtime.FallbackAndBlockingHit, 전체 runtime mutator denylist, Task 3의 ShipPawn child mutator 기대값 0과 Task 4 RED의 기대값 1 전환 |
| 200 x 100 x 100 hull, 물리 비활성화, 3인칭 카메라 | Task 4 | Pawn.Construction, PIE |
| runtime Enhanced Input WASD, UObject 수명, 양쪽 키 상쇄 | Task 4 | Pawn.InputLifecycle, PIE |
| Completed, Canceled, focus loss, UnPossessed, EndPlay reset | Task 4 | 즉시 mapping rebuild, W/A 비영 입력 선행 단언, FlushPressedKeys automation, OS focus PIE |
| 향후 autopilot 활성 중 수동 입력 무시, 같은 두 setter 사용 | Task 4 | queued Completed와 Canceled 뒤 setter 값 보존 |
| GameMode test spawn, 중복 방지와 possession | Task 5 | 정상 BeginPlay 한 번과 idempotent helper 두 번, PIE |
| 15, 30, 60, 120 FPS 합격표와 hitch | Task 1, Final Verification | 자동화 표와 PIE 기록 |
| full Build, 전체 자동화, no-write MainLevel과 tracked-change No-Go | Final Verification | project-scoped UBT clean, compile/link action, 강제 log count, hash/time/Git 상태 |

---

### Task 1: 순수 수치 이동과 bounded substep

**Files:**

- Create: ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h
- Create: ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp
- Create: ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp

**Interfaces:**

- Produces: FShipMotionParameters::Defaults(), MakeShipMotionInput, AdvanceShipMotion, BuildShipSubstepSchedule, ValidateShipMotionParameters
- Produces tests: ShipAutonomySim.ShipMovement.Motion.Dynamics, Motion.Targets, Motion.ParameterValidation, Motion.FrameRatesAndHitch
- Consumed by: Task 2와 Task 3

- [ ] **Step 1: 네 수치 RED 테스트 선언**

ShipMovementTests.cpp를 만들고 다음 test 계약을 추가한다.

~~~cpp
#if WITH_DEV_AUTOMATION_TESTS
#include <limits>
#include "Misc/AutomationTest.h"
#include "ShipMovementSimulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipMotionDynamicsTest,
    "ShipAutonomySim.ShipMovement.Motion.Dynamics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipMotionTargetsTest,
    "ShipAutonomySim.ShipMovement.Motion.Targets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipMotionParameterValidationTest,
    "ShipAutonomySim.ShipMovement.Motion.ParameterValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipMotionFrameRatesAndHitchTest,
    "ShipAutonomySim.ShipMovement.Motion.FrameRatesAndHitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
#endif // WITH_DEV_AUTOMATION_TESTS
~~~

Task 1이 파일을 처음 만들 때부터 이 closing `#endif`를 마지막 줄에 둔다. 아래 RunTest 본문을 추가할 때는 마지막 `#endif`를 잠시 아래로 옮겨 본문을 guard 안에 삽입하고 즉시 파일 마지막 줄에 다시 둔다. Step 2 RED build와 Task 1 GREEN 및 commit 시점 모두 opening 1개, closing 1개, macro 4개로 균형이 맞아야 한다.

RunTest 본문은 기존 closing `#endif` 바로 앞에 삽입하고 다음 수치를 직접 단언한다.

~~~cpp
const FShipMotionParameters P = FShipMotionParameters::Defaults();
TestTrue(TEXT("forward equilibrium"),
    FMath::Abs(AdvanceShipMotion({200.0, 0.0}, MakeShipMotionInput(1.0, 0.0), P, 1.0 / 120.0).AccelerationCmPerSecondSquared) <= 1e-6);
TestTrue(TEXT("reverse equilibrium"),
    FMath::Abs(AdvanceShipMotion({-200.0, 0.0}, MakeShipMotionInput(-1.0, 0.0), P, 1.0 / 120.0).AccelerationCmPerSecondSquared) <= 1e-6);
TestTrue(TEXT("positive drag opposes speed"),
    AdvanceShipMotion({100.0, 0.0}, MakeShipMotionInput(0.0, 0.0), P, 1.0 / 120.0).AccelerationCmPerSecondSquared < 0.0);
TestTrue(TEXT("negative drag opposes speed"),
    AdvanceShipMotion({-100.0, 0.0}, MakeShipMotionInput(0.0, 0.0), P, 1.0 / 120.0).AccelerationCmPerSecondSquared > 0.0);
TestEqual(TEXT("input high clamp"), MakeShipMotionInput(2.0, -3.0).Throttle, 1.0);
TestEqual(TEXT("input low clamp"), MakeShipMotionInput(2.0, -3.0).Steer, -1.0);
TestEqual(TEXT("non-finite input replacement"),
    MakeShipMotionInput(std::numeric_limits<double>::quiet_NaN(), 0.0).Throttle, 0.0);
TestEqual(TEXT("zero-speed steer has no yaw"),
    AdvanceShipMotion({0.0, 10.0}, MakeShipMotionInput(0.0, 1.0), P, 1.0 / 120.0).NextState.HorizontalYawDegrees, 10.0);
TestTrue(TEXT("full-speed yaw rate"),
    FMath::Abs(AdvanceShipMotion({200.0, 0.0}, MakeShipMotionInput(1.0, 1.0), P, 1.0 / 120.0).YawRateDegreesPerSecond - 45.83662361) <= 1e-8);
~~~

- [ ] **Step 2: RED build 확인**

~~~powershell
Invoke-ExpectedRedBuild -Stage "Task1" -ExpectedTests 4 -ExpectedFailurePattern 'ShipMovementSimulation\.h|FShipMotionParameters|AdvanceShipMotion'
~~~

Expected: ShipMovementSimulation.h가 아직 없어 compile 실패한다. 기존 ShipMovement 골격의 오류가 아니라 새 RED test가 요구한 seam 부재가 실패 원인이다.

- [ ] **Step 3: 값 타입과 확정 기본값 구현**

ShipMovementSimulation.h에 다음 정의를 넣는다.

~~~cpp
#pragma once

#include "CoreMinimal.h"
#include "WaterBodyTypes.h"

inline constexpr double ShipSupportedSpeedCmPerSecond = 500.0;
inline constexpr double ShipMinSurfaceNormalZ = 0.1;

enum class EShipMotionParameterState : uint8 { Defaults, Tuned, TuningFallback };
enum class EShipWaterState : uint8 { ValidWaves, ValidNoWaves, Excluded, QueryInvalid, ComponentInvalid };

struct FShipMotionParameters
{
    double LinearDragCoeff = 0.447501534;
    double QuadraticDragCoeff = 0.000400390770;
    double MaxThrustAccel = 105.5159376;
    double StopSpeedThreshold = 5.0;
    double MaxYawRate = 45.83662361;
    double TurnRefSpeed = 200.0;
    double MaxSimulationStepSeconds = 1.0 / 120.0;
    int32 MaxSubstepsPerTick = 8;

    static FShipMotionParameters Defaults() { return FShipMotionParameters{}; }
};

struct FShipMotionState
{
    double SignedSpeedCmPerSecond = 0.0;
    double HorizontalYawDegrees = 0.0;
};

struct FShipMotionInput
{
    double Throttle = 0.0;
    double Steer = 0.0;
};

struct FShipMotionStep
{
    FShipMotionState NextState;
    double TravelCm = 0.0;
    double YawRateDegreesPerSecond = 0.0;
    double AccelerationCmPerSecondSquared = 0.0;
    bool bValid = false;
};

struct FShipSubstepSchedule
{
    double SimulatedDeltaTimeSeconds = 0.0;
    double DroppedDeltaTimeSeconds = 0.0;
    double StepSeconds = 0.0;
    int32 NumSteps = 0;
    bool bValid = false;
};

struct FShipValidatedMotionParameters
{
    FShipMotionParameters Parameters;
    EShipMotionParameterState State = EShipMotionParameterState::TuningFallback;
};

struct FShipSurfaceSample
{
    EShipWaterState State = EShipWaterState::ComponentInvalid;
    double SurfaceZ = 0.0;
    FVector Normal = FVector::UpVector;
    bool bUsedFallback = true;
};

struct FShipSurfaceBasis
{
    FVector Forward = FVector::ForwardVector;
    FVector Right = FVector::RightVector;
    FVector Up = FVector::UpVector;
    bool bUsedFallback = false;
};

FShipMotionInput MakeShipMotionInput(double Throttle, double Steer);
FShipMotionStep AdvanceShipMotion(const FShipMotionState&, const FShipMotionInput&, const FShipMotionParameters&, double StepSeconds);
FShipSubstepSchedule BuildShipSubstepSchedule(double DeltaTimeSeconds, double MaxSimulationStepSeconds, int32 MaxSubstepsPerTick);
FShipValidatedMotionParameters ValidateShipMotionParameters(const FShipMotionParameters& Candidate);
FShipSurfaceSample ResolveWaterSurfaceSample(bool bSubsystemValid, bool bComponentValid, bool bHasWaves,
    const FWaterBodyQueryResult* QueryResult, const TOptional<FShipSurfaceSample>& LastValidSample, double CurrentActorZ);
FShipSurfaceBasis BuildShipSurfaceBasis(double YawDegrees, const FVector& SurfaceNormal,
    const TOptional<FVector>& LastValidNormal);
~~~

- [ ] **Step 4: parameter 검증과 schedule 구현**

ShipMovementSimulation.cpp의 검증은 모든 필드의 유한성, metadata 범위, C1과 C2 동시 0 금지, Veq와 Euler 안정성을 한 번에 검사한다.

~~~cpp
#include "ShipMovementSimulation.h"
#include "Math/Rotator.h"

namespace
{
bool InRange(double V, double Min, double Max)
{
    return FMath::IsFinite(V) && V >= Min && V <= Max;
}

double EquilibriumSpeed(const FShipMotionParameters& P)
{
    return P.QuadraticDragCoeff > 0.0
        ? (2.0 * P.MaxThrustAccel) /
            (P.LinearDragCoeff + FMath::Sqrt(
                P.LinearDragCoeff * P.LinearDragCoeff +
                4.0 * P.QuadraticDragCoeff * P.MaxThrustAccel))
        : P.MaxThrustAccel / P.LinearDragCoeff;
}
}

FShipValidatedMotionParameters ValidateShipMotionParameters(const FShipMotionParameters& C)
{
    const bool bRanges =
        InRange(C.LinearDragCoeff, 0.0, 2.0) &&
        InRange(C.QuadraticDragCoeff, 0.0, 0.002) &&
        InRange(C.MaxThrustAccel, 0.001, 500.0) &&
        InRange(C.StopSpeedThreshold, 0.1, 50.0) &&
        InRange(C.MaxYawRate, 0.0, 180.0) &&
        InRange(C.TurnRefSpeed, 1.0, 1000.0) &&
        InRange(C.MaxSimulationStepSeconds, 0.001, 0.016666667) &&
        C.MaxSubstepsPerTick >= 1 && C.MaxSubstepsPerTick <= 32;
    const bool bHasDrag = C.LinearDragCoeff > 0.0 || C.QuadraticDragCoeff > 0.0;
    const double Veq = bRanges && bHasDrag ? EquilibriumSpeed(C) : 0.0;
    const double LambdaMax = C.LinearDragCoeff +
        2.0 * C.QuadraticDragCoeff * ShipSupportedSpeedCmPerSecond;
    const double Stability = C.MaxSimulationStepSeconds * LambdaMax;
    const bool bDerived = FMath::IsFinite(Veq) &&
        Veq > C.StopSpeedThreshold && Veq <= ShipSupportedSpeedCmPerSecond &&
        FMath::IsFinite(Stability) && Stability <= 0.5;
    if (!bRanges || !bHasDrag || !bDerived)
    {
        return {FShipMotionParameters::Defaults(), EShipMotionParameterState::TuningFallback};
    }

    const FShipMotionParameters D = FShipMotionParameters::Defaults();
    const bool bDefaults =
        FMath::IsNearlyEqual(C.LinearDragCoeff, D.LinearDragCoeff, 1e-12) &&
        FMath::IsNearlyEqual(C.QuadraticDragCoeff, D.QuadraticDragCoeff, 1e-15) &&
        FMath::IsNearlyEqual(C.MaxThrustAccel, D.MaxThrustAccel, 1e-9) &&
        FMath::IsNearlyEqual(C.StopSpeedThreshold, D.StopSpeedThreshold, 1e-12) &&
        FMath::IsNearlyEqual(C.MaxYawRate, D.MaxYawRate, 1e-9) &&
        FMath::IsNearlyEqual(C.TurnRefSpeed, D.TurnRefSpeed, 1e-12) &&
        FMath::IsNearlyEqual(C.MaxSimulationStepSeconds, D.MaxSimulationStepSeconds, 1e-12) &&
        C.MaxSubstepsPerTick == D.MaxSubstepsPerTick;
    return {C, bDefaults ? EShipMotionParameterState::Defaults : EShipMotionParameterState::Tuned};
}

FShipSubstepSchedule BuildShipSubstepSchedule(
    double DeltaTimeSeconds, double MaxStepSeconds, int32 MaxSteps)
{
    FShipSubstepSchedule R;
    if (!FMath::IsFinite(DeltaTimeSeconds) || !FMath::IsFinite(MaxStepSeconds) ||
        DeltaTimeSeconds < 0.0 || MaxStepSeconds <= 0.0 || MaxSteps <= 0)
    {
        return R;
    }
    if (DeltaTimeSeconds == 0.0)
    {
        R.bValid = true;
        return R;
    }
    const double MaxSimulated = MaxStepSeconds * MaxSteps;
    R.SimulatedDeltaTimeSeconds = FMath::Min(DeltaTimeSeconds, MaxSimulated);
    R.DroppedDeltaTimeSeconds = DeltaTimeSeconds - R.SimulatedDeltaTimeSeconds;
    R.NumSteps = FMath::Clamp(
        FMath::CeilToInt(R.SimulatedDeltaTimeSeconds / MaxStepSeconds), 1, MaxSteps);
    R.StepSeconds = R.SimulatedDeltaTimeSeconds / R.NumSteps;
    R.bValid = FMath::IsFinite(R.StepSeconds) && R.StepSeconds <= MaxStepSeconds + 1e-12;
    return R;
}
~~~

- [ ] **Step 5: forward Euler 단일 스텝 구현**

~~~cpp
FShipMotionInput MakeShipMotionInput(double Throttle, double Steer)
{
    const double SafeThrottle = FMath::IsFinite(Throttle) ? Throttle : 0.0;
    const double SafeSteer = FMath::IsFinite(Steer) ? Steer : 0.0;
    return {FMath::Clamp(SafeThrottle, -1.0, 1.0),
            FMath::Clamp(SafeSteer, -1.0, 1.0)};
}

FShipMotionStep AdvanceShipMotion(
    const FShipMotionState& State,
    const FShipMotionInput& Input,
    const FShipMotionParameters& P,
    double H)
{
    FShipMotionStep R;
    if (!FMath::IsFinite(State.SignedSpeedCmPerSecond) ||
        !FMath::IsFinite(State.HorizontalYawDegrees) ||
        !FMath::IsFinite(H) || H <= 0.0 ||
        FMath::Abs(State.SignedSpeedCmPerSecond) > ShipSupportedSpeedCmPerSecond)
    {
        R.NextState = {0.0, 0.0};
        return R;
    }
    const double V = State.SignedSpeedCmPerSecond;
    R.AccelerationCmPerSecondSquared =
        P.MaxThrustAccel * Input.Throttle -
        P.LinearDragCoeff * V -
        P.QuadraticDragCoeff * V * FMath::Abs(V);
    R.YawRateDegreesPerSecond =
        P.MaxYawRate * Input.Steer *
        FMath::Clamp(FMath::Abs(V) / P.TurnRefSpeed, 0.0, 1.0);
    R.TravelCm = V * H;
    double NextSpeed = V + R.AccelerationCmPerSecondSquared * H;
    if (FMath::IsNearlyZero(Input.Throttle) &&
        FMath::Abs(NextSpeed) <= P.StopSpeedThreshold)
    {
        NextSpeed = 0.0;
    }
    const double NextYaw = FRotator::NormalizeAxis(
        State.HorizontalYawDegrees + R.YawRateDegreesPerSecond * H);
    R.bValid = FMath::IsFinite(NextSpeed) && FMath::IsFinite(NextYaw) &&
        FMath::IsFinite(R.TravelCm) &&
        FMath::Abs(NextSpeed) <= ShipSupportedSpeedCmPerSecond;
    R.NextState = R.bValid ? FShipMotionState{NextSpeed, NextYaw}
                           : FShipMotionState{0.0, State.HorizontalYawDegrees};
    return R;
}
~~~

- [ ] **Step 6: 목표, parameter와 FPS test 본문 완성**

120 Hz 적분 helper를 test 파일에 두고 3.9917초, 399.9615 cm, 반대 throttle 0 통과, 5 cm/s 비제로 throttle 보존을 단언한다. Parameter test는 Defaults와 유효한 MaxYawRate 40의 Tuned를 확인하고 음수, NaN, 범위 초과, C1과 C2 동시 0, Veq 5 이하, Veq 500 초과, stability 0.5 초과가 모두 snapshot 전체 Defaults와 TuningFallback을 반환하는지 확인한다.

~~~cpp
struct FScenarioResult
{
    FShipMotionState State;
    double DistanceCm = 0.0;
    double FirstReached180Seconds = -1.0;
};

FScenarioResult RunScenario(
    int32 FramesPerSecond,
    double DurationSeconds,
    FShipMotionState State,
    FShipMotionInput Input)
{
    FScenarioResult Result{State};
    const double FrameSeconds = 1.0 / FramesPerSecond;
    const int32 FrameCount = FMath::RoundToInt(DurationSeconds * FramesPerSecond);
    double Elapsed = 0.0;
    for (int32 Frame = 0; Frame < FrameCount; ++Frame)
    {
        const FShipSubstepSchedule Schedule =
            BuildShipSubstepSchedule(FrameSeconds, 1.0 / 120.0, 8);
        for (int32 StepIndex = 0; StepIndex < Schedule.NumSteps; ++StepIndex)
        {
            const FShipMotionStep Step = AdvanceShipMotion(
                Result.State, Input, FShipMotionParameters::Defaults(),
                Schedule.StepSeconds);
            check(Step.bValid);
            Result.DistanceCm += FMath::Abs(Step.TravelCm);
            Result.State = Step.NextState;
            Elapsed += Schedule.StepSeconds;
            if (Result.FirstReached180Seconds < 0.0 &&
                Result.State.SignedSpeedCmPerSecond >= 180.0)
            {
                Result.FirstReached180Seconds = Elapsed;
            }
        }
    }
    return Result;
}

bool FShipMotionTargetsTest::RunTest(const FString&)
{
    const FScenarioResult Accel = RunScenario(
        120, 4.0, {0.0, 0.0}, MakeShipMotionInput(1.0, 0.0));
    TestTrue(TEXT("180 first reach"), FMath::Abs(Accel.FirstReached180Seconds - 3.9917) <= 0.01);
    const FScenarioResult Coast = RunScenario(
        120, 8.0, {200.0, 0.0}, MakeShipMotionInput(0.0, 0.0));
    TestTrue(TEXT("coast distance"), FMath::Abs(Coast.DistanceCm - 399.9615) <= 0.1);
    TestEqual(TEXT("coast stops"), Coast.State.SignedSpeedCmPerSecond, 0.0);
    return true;
}
~~~

FPS test는 다음 표를 코드의 data row로 사용한다.

| 시나리오 | 초기 상태와 입력 | 시간 | 절대 합격 | 120 FPS 대비 최대 차이 |
| --- | --- | --- | --- | --- |
| 가속 speed | v 0, yaw 0, T 1, S 0 | 4.0 s | 180 ± 1 cm/s, 최초 도달 4.0 ± 0.05 s | 0.1 cm/s |
| 타력 distance | v 200, yaw 0, T 0, S 0 | 8.0 s | speed 0, 거리 400 ± 5 cm | 0.5 cm |
| yaw | v 200, yaw 0, T 1, S 1 | 2.0 s | 91.67324722 ± 0.1 deg | 0.1 deg |

0.5초 hitch에는 NumSteps 8, SimulatedDeltaTime 0.066666667, DroppedDeltaTime 0.433333333을 1e-6 안에서 단언하고 다음 정상 tick이 폐기 시간을 재생하지 않는지 확인한다.

- [ ] **Step 7: GREEN build와 네 test 실행**

~~~powershell
Invoke-ShipGreenGate -ExpectedTests 4 -Stage "Task1"
~~~

Expected: guard opening 및 closing 각 1개와 macro 4개를 먼저 확인하고, build exit 0 뒤 발견 4, Success 4, failure 및 automation error 0, clean exit marker 1개 이상이다.

- [ ] **Step 8: Task 1 commit**

~~~powershell
git add ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp
git diff --cached --check
git commit -m "feat: 선박 수치 이동 모델 구현" -m "변경 이유: 프레임과 월드에서 분리된 재현 가능한 선박 운동 계산이 필요합니다." -m "핵심 변경: 선형 및 이차 저항, 속도 비례 yaw, parameter 검증과 bounded substep 순수 함수를 구현했습니다." -m "검증 방법: Motion 자동화 테스트 4개와 ShipAutonomySimEditor 빌드를 실행했습니다."
~~~

---

### Task 2: Water 분류와 yaw 보존 직교기저

**Files:**

- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp

**Interfaces:**

- Consumes: FWaterBodyQueryResult, EWaterBodyQueryFlags
- Produces: ResolveWaterSurfaceSample, BuildShipSurfaceBasis
- Produces tests: ShipAutonomySim.ShipMovement.Water.Classification, Water.SurfaceBasis

- [ ] **Step 1: 두 Water RED test 추가**

ShipMovementTests.cpp의 마지막 closing `#endif` 바로 앞에 두 macro와 두 RunTest 본문을 삽입한 뒤 closing을 다시 파일 마지막 줄에 둔다. 아래 block은 Task 2 완료 시점의 파일 tail이며, opening 1개, closing 1개, macro 6개를 유지한다.

~~~cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipWaterClassificationTest,
    "ShipAutonomySim.ShipMovement.Water.Classification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipSurfaceBasisTest,
    "ShipAutonomySim.ShipMovement.Water.SurfaceBasis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
#endif // WITH_DEV_AUTOMATION_TESTS
~~~

두 RunTest 정의는 closing `#endif` 바로 앞에 넣고 다음 단언을 담는다. Classification은 ComputeLocation | ComputeNormal | IncludeWaves 결과를 만들고 HasWaves true면 ValidWaves, false면 ValidNoWaves를 단언한다. IsInExclusionVolume은 Excluded, flag 누락과 NaN 및 N.Z 0.1 미만은 QueryInvalid, subsystem 또는 component false는 ComponentInvalid를 단언한다. 유효 sample 뒤 실패는 마지막 Z와 normal, 첫 실패는 현재 actor Z와 world up을 사용해야 한다. 두 본문을 추가하거나 고칠 때도 closing `#endif`는 항상 파일 마지막에 되돌린 뒤 RED 또는 GREEN build를 실행한다.

- [ ] **Step 2: RED build 확인**

~~~powershell
Invoke-ExpectedRedBuild -Stage "Task2" -ExpectedTests 6 -ExpectedFailurePattern 'ResolveWaterSurfaceSample|BuildShipSurfaceBasis|unresolved external symbol'
~~~

Expected: 두 함수가 선언만 있고 정의되지 않은 link 실패를 확인하며 editor는 실행하지 않는다.

- [ ] **Step 3: Water sample 분류 구현**

~~~cpp
namespace
{
bool IsFiniteVector(const FVector& V)
{
    return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
}

FShipSurfaceSample MakeFallback(
    EShipWaterState State,
    const TOptional<FShipSurfaceSample>& Last,
    double CurrentZ)
{
    if (Last.IsSet())
    {
        return {State, Last->SurfaceZ, Last->Normal, true};
    }
    return {State, CurrentZ, FVector::UpVector, true};
}
}

FShipSurfaceSample ResolveWaterSurfaceSample(
    bool bSubsystemValid,
    bool bComponentValid,
    bool bHasWaves,
    const FWaterBodyQueryResult* Query,
    const TOptional<FShipSurfaceSample>& Last,
    double CurrentZ)
{
    if (!bSubsystemValid || !bComponentValid || Query == nullptr)
    {
        return MakeFallback(EShipWaterState::ComponentInvalid, Last, CurrentZ);
    }
    if (Query->IsInExclusionVolume())
    {
        return MakeFallback(EShipWaterState::Excluded, Last, CurrentZ);
    }
    const EWaterBodyQueryFlags Required =
        EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeNormal;
    if (!EnumHasAllFlags(Query->GetQueryFlags(), Required))
    {
        return MakeFallback(EShipWaterState::QueryInvalid, Last, CurrentZ);
    }
    const FVector Location = Query->GetWaterSurfaceLocation();
    FVector Normal = Query->GetWaterSurfaceNormal();
    if (!IsFiniteVector(Location) || !IsFiniteVector(Normal) ||
        !Normal.Normalize() || Normal.Z < ShipMinSurfaceNormalZ)
    {
        return MakeFallback(EShipWaterState::QueryInvalid, Last, CurrentZ);
    }
    return {bHasWaves ? EShipWaterState::ValidWaves : EShipWaterState::ValidNoWaves,
            Location.Z, Normal, false};
}
~~~

- [ ] **Step 4: world XY yaw 보존 직교기저 구현**

~~~cpp
FShipSurfaceBasis BuildShipSurfaceBasis(
    double YawDegrees,
    const FVector& SurfaceNormal,
    const TOptional<FVector>& LastValidNormal)
{
    const double Psi = FMath::DegreesToRadians(YawDegrees);
    const FVector H(FMath::Cos(Psi), FMath::Sin(Psi), 0.0);
    FVector N = SurfaceNormal;
    const auto AcceptNormal = [](FVector& Candidate)
    {
        return IsFiniteVector(Candidate) && Candidate.Normalize() &&
            Candidate.Z >= ShipMinSurfaceNormalZ;
    };
    bool bFallback = !AcceptNormal(N);
    if (bFallback && LastValidNormal.IsSet())
    {
        N = LastValidNormal.GetValue();
        bFallback = !AcceptNormal(N);
    }
    if (bFallback)
    {
        N = FVector::UpVector;
    }

    FVector F = H - FVector::UpVector * (FVector::DotProduct(H, N) / N.Z);
    FVector R = FVector::CrossProduct(N, F);
    FVector U = FVector::CrossProduct(F, R);
    const bool bValid = IsFiniteVector(F) && IsFiniteVector(R) && IsFiniteVector(U) &&
        F.Normalize() && R.Normalize() && U.Normalize();
    if (!bValid)
    {
        return {H, FVector::CrossProduct(FVector::UpVector, H).GetSafeNormal(),
                FVector::UpVector, true};
    }
    return {F, R, U, bFallback};
}
~~~

- [ ] **Step 5: 기하와 fallback GREEN 단언**

H (1,0,0)과 normalize(1,1,1), yaw 45와 normalize(0,1,1)에서 atan2 결과 yaw 오차가 0.01도 이하여야 한다. Forward, Right, Up 길이와 쌍별 내적 오차는 1e-5 이하이며 dot(cross(F,R),U)는 0.99999 이상이어야 한다. N.Z 0.1 미만, 영벡터, NaN은 마지막 valid normal 또는 world up으로 유한한 기저를 만든다.

- [ ] **Step 6: 전체 순수 test와 회귀 실행**

~~~powershell
Invoke-ShipGreenGate -ExpectedTests 6 -Stage "Task2"
~~~

Expected: guard opening 및 closing 각 1개와 macro 6개를 먼저 확인하고, build exit 0 뒤 발견 6, Success 6, failure 및 automation error 0, clean exit marker 1개 이상이다.

- [ ] **Step 7: Task 2 commit**

~~~powershell
git add ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp
git diff --cached --check
git commit -m "feat: 선박 수면 정렬 계산 구현" -m "변경 이유: Water 결과의 파도 여부와 실패 원인을 구분하고 경사면에서도 수평 yaw를 보존해야 합니다." -m "핵심 변경: Water 다섯 상태, 마지막 수면 fallback과 world XY heading 보존 직교기저를 구현했습니다." -m "검증 방법: Water 및 Motion 자동화 테스트 6개와 editor 빌드를 실행했습니다."
~~~

---

### Task 3: UShipMovement Water query, 보간과 swept transform

**Files:**

- Modify: ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp

**Interfaces:**

- Consumes: Task 1과 2의 모든 pure 함수
- Produces: SetThrottle(float), SetSteer(float), TickComponent
- Produces tests: ShipAutonomySim.ShipMovement.Runtime.FallbackAndBlockingHit, Runtime.TransformOwnership

- [ ] **Step 1: runtime RED test 두 개 추가**

Task 2 파일의 마지막 closing `#endif`를 아래 두 macro, include, fixture, accessor와 두 RunTest 본문 뒤로 옮긴다. 아래 여러 C++ block은 모두 같은 outer guard 내부에 이어 붙이는 조각이며, Step 2 전에 closing을 파일 마지막 줄에 복원한다.

FallbackAndBlockingHit는 physics scene을 만든 transient UWorld에 box-root AActor와 UShipMovement를 등록한다. Water가 없는 상태에서 Z를 유지하며 X가 증가하는지 확인하고, WorldStatic box 앞에서 sweep 후 penetration 없이 speed 0과 남은 substep 중단을 확인한다.

TransformOwnership은 `Source/ShipAutonomySim` 아래 runtime h와 cpp 전체를 재귀 검사하고 `Private/Tests`만 제외한다. actor 이동, teleport, actor local 및 world offset, scene component world 및 relative transform, MoveComponent 계열을 denylist로 검사한다. runtime 이동 whitelist는 ShipMovement.cpp의 swept `Owner->SetActorLocationAndRotation(NewLocation, NewRotation, true, &Hit, ETeleportType::None)` 한 문장뿐이다. ShipPawn.cpp의 고정 child mesh scale과 camera boom 초기 pitch는 Task 4 산출물이므로 Task 3에서는 두 exact line의 `ExpectedCount`를 각각 0으로 둔다. Task 3에 두 문장이 미리 나타나거나 다른 파일, receiver, 인수 또는 추가 발생이 있으면 실패한다. Task 4 RED에서 두 기대값만 1로 전환한다.

~~~cpp
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShipMovement.h"
#include "ShipPawn.h"
#include "SimGameMode.h"

class FScopedShipTestWorld
{
public:
    FScopedShipTestWorld()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        check(World != nullptr && World->GetPhysicsScene() != nullptr);
    }
    ~FScopedShipTestWorld()
    {
        World->DestroyWorld(false);
    }
    UWorld* World = nullptr;
};

struct FShipMovementTestAccessor
{
    static void BeginPlay(UShipMovement& Movement) { Movement.BeginPlay(); }
    static void SetState(UShipMovement& Movement, double Speed, double Yaw)
    {
        Movement.SignedSpeedCmPerSecond = Speed;
        Movement.HorizontalYawDegrees = Yaw;
    }
    static void Tick(UShipMovement& Movement, float DeltaTime)
    {
        Movement.TickComponent(DeltaTime, LEVELTICK_All, nullptr);
    }
    static double Speed(const UShipMovement& Movement)
    {
        return Movement.SignedSpeedCmPerSecond;
    }
    static double Throttle(const UShipMovement& Movement)
    {
        return Movement.ThrottleInput;
    }
    static double Steer(const UShipMovement& Movement)
    {
        return Movement.SteerInput;
    }
    static double Acceleration(const UShipMovement& Movement)
    {
        return Movement.LastAcceleration;
    }
    static bool BlockingHit(const UShipMovement& Movement)
    {
        return Movement.bLastBlockingHit;
    }
    static int32 DebugDrawCalls(const UShipMovement& Movement)
    {
        return Movement.TestDebugDrawInvocationCount;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipMovementRuntimeTest,
    "ShipAutonomySim.ShipMovement.Runtime.FallbackAndBlockingHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipTransformOwnershipTest,
    "ShipAutonomySim.ShipMovement.Runtime.TransformOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
~~~

TransformOwnership의 RunTest 본문은 다음과 같이 고정한다.

~~~cpp
bool FShipTransformOwnershipTest::RunTest(const FString&)
{
    const FString RuntimeRoot = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir() / TEXT("Source/ShipAutonomySim"));
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *RuntimeRoot, TEXT("*.h"), true, false);
    IFileManager::Get().FindFilesRecursive(Files, *RuntimeRoot, TEXT("*.cpp"), true, false);

    const TArray<FString> DeniedMutators = {
        TEXT("SetActorTransform("), TEXT("SetActorLocation("),
        TEXT("SetActorRotation("), TEXT("SetActorLocationAndRotation("),
        TEXT("SetActorScale3D("), TEXT("TeleportTo("),
        TEXT("AddActorWorldOffset("),
        TEXT("AddActorLocalOffset("), TEXT("AddActorWorldRotation("),
        TEXT("AddActorLocalRotation("), TEXT("AddActorWorldTransform("),
        TEXT("AddActorLocalTransform("), TEXT("SetWorldTransform("),
        TEXT("SetWorldLocation("), TEXT("SetWorldRotation("),
        TEXT("SetWorldLocationAndRotation("), TEXT("SetWorldScale3D("),
        TEXT("AddWorldOffset("),
        TEXT("AddLocalOffset("), TEXT("AddWorldRotation("),
        TEXT("AddLocalRotation("), TEXT("AddWorldTransform("),
        TEXT("AddLocalTransform("), TEXT("MoveComponent("),
        TEXT("MoveComponentImpl("), TEXT("SetRelativeTransform("),
        TEXT("SetRelativeLocation("), TEXT("SetRelativeRotation("),
        TEXT("SetRelativeLocationAndRotation("), TEXT("SetRelativeScale3D("),
        TEXT("AddRelativeLocation("), TEXT("AddRelativeRotation("),
        TEXT("AddRelativeTransform("), TEXT("K2_SetWorldLocation("),
        TEXT("K2_SetWorldRotation("), TEXT("K2_SetWorldTransform("),
        TEXT("K2_AddWorldOffset("), TEXT("K2_AddLocalOffset("),
        TEXT("K2_AddWorldRotation("), TEXT("K2_AddLocalRotation(")
    };
    struct FAllowedLine
    {
        FString FileSuffix;
        FString CompactLine;
        int32 ExpectedCount;
        int32 ActualCount = 0;
    };
    TArray<FAllowedLine> Allowed = {
        {TEXT("/Private/ShipMovement.cpp"),
         TEXT("Owner->SetActorLocationAndRotation("), 1},
        {TEXT("/Private/ShipPawn.cpp"),
         TEXT("VisualMesh->SetRelativeScale3D(FVector(2.0,1.0,1.0));"), 0},
        {TEXT("/Private/ShipPawn.cpp"),
         TEXT("CameraBoom->SetRelativeRotation(FRotator(CameraPitchDegrees,0.0,0.0));"), 0}
    };
    const auto Compact = [](FString Value)
    {
        Value.ReplaceInline(TEXT(" "), TEXT(""));
        Value.ReplaceInline(TEXT("\t"), TEXT(""));
        Value.ReplaceInline(TEXT("\r"), TEXT(""));
        Value.ReplaceInline(TEXT("\n"), TEXT(""));
        return Value;
    };

    FString ShipMovementSource;
    for (const FString& File : Files)
    {
        FString NormalizedFile = File;
        NormalizedFile.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (NormalizedFile.Contains(TEXT("/Private/Tests/")))
        {
            continue;
        }
        FString Source;
        if (!TestTrue(*FString::Printf(TEXT("read %s"), *NormalizedFile),
            FFileHelper::LoadFileToString(Source, *File)))
        {
            continue;
        }
        if (NormalizedFile.EndsWith(TEXT("/Private/ShipMovement.cpp")))
        {
            ShipMovementSource = Source;
        }
        TArray<FString> Lines;
        Source.ParseIntoArrayLines(Lines, false);
        for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
        {
            const FString CompactLine = Compact(Lines[LineIndex]);
            for (const FString& Mutator : DeniedMutators)
            {
                if (!CompactLine.Contains(Mutator))
                {
                    continue;
                }
                bool bAllowed = false;
                for (FAllowedLine& Entry : Allowed)
                {
                    if (NormalizedFile.EndsWith(Entry.FileSuffix) &&
                        CompactLine == Entry.CompactLine)
                    {
                        ++Entry.ActualCount;
                        bAllowed = true;
                        break;
                    }
                }
                if (!bAllowed)
                {
                    AddError(FString::Printf(TEXT("transform mutator denied: %s:%d: %s"),
                        *NormalizedFile, LineIndex + 1, *Lines[LineIndex]));
                }
                break;
            }
        }
    }
    for (const FAllowedLine& Entry : Allowed)
    {
        TestEqual(*FString::Printf(TEXT("whitelist count %s"), *Entry.CompactLine),
            Entry.ActualCount, Entry.ExpectedCount);
    }
    TestTrue(TEXT("only approved swept actor move statement"),
        Compact(ShipMovementSource).Contains(
            TEXT("Owner->SetActorLocationAndRotation(NewLocation,NewRotation,true,&Hit,ETeleportType::None);")));
    return !HasAnyErrors();
}
~~~

Task 3의 단계별 ownership count는 swept actor move 1, ShipPawn mesh scale 0, ShipPawn camera rotation 0이다. 따라서 Task 3 GREEN은 Task 4의 child setup 문장을 미리 요구하지 않는다.

FallbackAndBlockingHit RunTest는 ship root를 ECC_Pawn QueryOnly, blocker root를 ECC_WorldStatic QueryOnly로 설정하고 서로 block하도록 만든다.

~~~cpp
FShipMovementTestAccessor::SetState(*Movement, 120.0, 0.0);
const double StartZ = Ship->GetActorLocation().Z;
FShipMovementTestAccessor::Tick(*Movement, 1.0f / 120.0f);
TestTrue(TEXT("fallback moves horizontally"), Ship->GetActorLocation().X > 0.0);
TestTrue(TEXT("fallback preserves Z"),
    FMath::Abs(Ship->GetActorLocation().Z - StartZ) <= 1e-6);
TestEqual(TEXT("fallback tick draws debug exactly once"),
    FShipMovementTestAccessor::DebugDrawCalls(*Movement), 1);

Ship->SetActorLocation(FVector(0.0, 0.0, StartZ));
Blocker->SetActorLocation(FVector(25.0, 0.0, StartZ));
FShipMovementTestAccessor::SetState(*Movement, 200.0, 0.0);
FShipMovementTestAccessor::Tick(*Movement, 1.0f / 15.0f);
TestTrue(TEXT("blocking hit recorded"),
    FShipMovementTestAccessor::BlockingHit(*Movement));
TestEqual(TEXT("blocking hit stops speed"),
    FShipMovementTestAccessor::Speed(*Movement), 0.0);
TestTrue(TEXT("sweep prevents penetration"), Ship->GetActorLocation().X <= 5.01);
const int32 BeforeInvalid = FShipMovementTestAccessor::DebugDrawCalls(*Movement);
FShipMovementTestAccessor::Tick(
    *Movement, std::numeric_limits<float>::quiet_NaN());
TestEqual(TEXT("invalid tick draws debug exactly once"),
    FShipMovementTestAccessor::DebugDrawCalls(*Movement), BeforeInvalid + 1);
~~~

두 Runtime RunTest 본문 뒤 파일 tail은 반드시 다음 한 줄로 끝난다. 이 상태에서 opening 1개, closing 1개, macro 8개다.

~~~cpp
#endif // WITH_DEV_AUTOMATION_TESTS
~~~

- [ ] **Step 2: RED build 확인**

~~~powershell
Invoke-ExpectedRedBuild -Stage "Task3" -ExpectedTests 8 -ExpectedFailurePattern 'UShipMovement|SetThrottle|SetSteer|TickComponent|SetActorLocationAndRotation'
~~~

Expected: UShipMovement의 새 setter, TickComponent 또는 승인된 swept call 부재로 compile 또는 test 계약이 실패하고 editor는 실행하지 않는다. 이 RED 시점의 TransformOwnership 기대값은 swept actor move 1, ShipPawn mesh scale 0, ShipPawn camera rotation 0이다.

- [ ] **Step 3: UShipMovement public header 구현**

~~~cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShipMovement.generated.h"

class UWaterBodyComponent;
struct FShipMotionParameters;

UCLASS(ClassGroup=(Ship), meta=(BlueprintSpawnableComponent))
class SHIPAUTONOMYSIM_API UShipMovement : public UActorComponent
{
    GENERATED_BODY()

public:
    UShipMovement();
    void SetThrottle(float Value);
    void SetSteer(float Value);

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0", ClampMax="2"))
    double LinearDragCoeff = 0.447501534;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0", ClampMax="0.002"))
    double QuadraticDragCoeff = 0.000400390770;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001", ClampMax="500"))
    double MaxThrustAccel = 105.5159376;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.1", ClampMax="50"))
    double StopSpeedThreshold = 5.0;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0", ClampMax="180"))
    double MaxYawRate = 45.83662361;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="1", ClampMax="1000"))
    double TurnRefSpeed = 200.0;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001", ClampMax="0.016666667"))
    double MaxSimulationStepSeconds = 1.0 / 120.0;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="1", ClampMax="32"))
    int32 MaxSubstepsPerTick = 8;
    UPROPERTY(EditAnywhere, Category=ShipMovement)
    double WaterlineOffsetCm = 0.0;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001"))
    double WaterHeightInterpSpeed = 5.0;
    UPROPERTY(EditAnywhere, Category=ShipMovement, meta=(ClampMin="0.001"))
    double WaterNormalInterpSpeed = 5.0;
    UPROPERTY(EditAnywhere, Category=Debug)
    bool bShowMovementDebug = true;
    UPROPERTY(Transient)
    TWeakObjectPtr<UWaterBodyComponent> OceanBodyComponent;

    double ThrottleInput = 0.0;
    double SteerInput = 0.0;
    double SignedSpeedCmPerSecond = 0.0;
    double HorizontalYawDegrees = 0.0;
    double LastAcceleration = 0.0;
    double LastYawRate = 0.0;
    double LastSimulatedDeltaTime = 0.0;
    double LastDroppedDeltaTime = 0.0;
    double LastStepSeconds = 0.0;
    int32 LastSubsteps = 0;
    bool bHasLastValidSurface = false;
    double LastValidSurfaceZ = 0.0;
    FVector LastValidSurfaceNormal = FVector::UpVector;
    FVector AppliedUpVector = FVector::UpVector;
    double LastTargetSurfaceZ = 0.0;
    bool bLastWaterFallback = true;
    bool bLastBlockingHit = false;
    FVector LastImpactNormal = FVector::ZeroVector;
    FString LastHitName;
    uint8 LastWaterState = 255;
    uint8 LastParameterState = 255;
    bool bLoggedBadThrottle = false;
    bool bLoggedBadSteer = false;
    bool bWasDroppingTime = false;

    FShipMotionParameters BuildCandidateParameters() const;
    void DrawMovementDebug() const;

#if WITH_DEV_AUTOMATION_TESTS
    mutable int32 TestDebugDrawInvocationCount = 0;
    friend struct FShipMovementTestAccessor;
#endif
};
~~~

ShipMovement.h의 private 함수가 FShipMotionParameters를 반환하므로 class declaration 앞에 struct FShipMotionParameters;를 전방 선언한다. Public header는 Private header를 include하지 않는다.

- [ ] **Step 4: constructor, setter와 lifecycle 구현**

~~~cpp
#include "ShipMovement.h"
#include "ShipMovementSimulation.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Math/RotationMatrix.h"
#include "Misc/ScopeExit.h"
#include "WaterBodyComponent.h"
#include "WaterSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipMovement, Log, All);

UShipMovement::UShipMovement()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UShipMovement::SetThrottle(float Value)
{
    if (!FMath::IsFinite(Value))
    {
        ThrottleInput = 0.0;
        if (!bLoggedBadThrottle)
        {
            UE_LOG(LogShipMovement, Warning, TEXT("Non-finite throttle replaced with zero"));
            bLoggedBadThrottle = true;
        }
        return;
    }
    ThrottleInput = FMath::Clamp(static_cast<double>(Value), -1.0, 1.0);
}

void UShipMovement::SetSteer(float Value)
{
    if (!FMath::IsFinite(Value))
    {
        SteerInput = 0.0;
        if (!bLoggedBadSteer)
        {
            UE_LOG(LogShipMovement, Warning, TEXT("Non-finite steer replaced with zero"));
            bLoggedBadSteer = true;
        }
        return;
    }
    SteerInput = FMath::Clamp(static_cast<double>(Value), -1.0, 1.0);
}

void UShipMovement::BeginPlay()
{
    Super::BeginPlay();
    AActor* Owner = GetOwner();
    if (!IsValid(Owner) || !IsValid(Owner->GetRootComponent()))
    {
        UE_LOG(LogShipMovement, Error, TEXT("ShipMovement requires an owner with a root component"));
        SetComponentTickEnabled(false);
        return;
    }
    HorizontalYawDegrees = Owner->GetActorRotation().Yaw;
    AppliedUpVector = Owner->GetActorUpVector();
    if (!AppliedUpVector.Normalize())
    {
        AppliedUpVector = FVector::UpVector;
    }
}
~~~

- [ ] **Step 5: bounded tick, Water와 sweep 구현**

각 내부 스텝은 현재 yaw로 horizontal travel을 만들고 다음 XY와 현재 Z에서 Water query를 수행한다. Query flags는 다음 값으로 고정한다.

~~~cpp
FShipMotionParameters UShipMovement::BuildCandidateParameters() const
{
    FShipMotionParameters P;
    P.LinearDragCoeff = LinearDragCoeff;
    P.QuadraticDragCoeff = QuadraticDragCoeff;
    P.MaxThrustAccel = MaxThrustAccel;
    P.StopSpeedThreshold = StopSpeedThreshold;
    P.MaxYawRate = MaxYawRate;
    P.TurnRefSpeed = TurnRefSpeed;
    P.MaxSimulationStepSeconds = MaxSimulationStepSeconds;
    P.MaxSubstepsPerTick = MaxSubstepsPerTick;
    return P;
}
~~~

~~~cpp
const EWaterBodyQueryFlags QueryFlags =
    EWaterBodyQueryFlags::ComputeLocation |
    EWaterBodyQueryFlags::ComputeNormal |
    EWaterBodyQueryFlags::IncludeWaves;
~~~

Tick의 핵심 loop를 다음 순서로 구현한다. `DrawMovementDebug` 호출은 함수 첫머리의 scope-exit 한 곳에만 둔다. 정상 처리, Water fallback, invalid DeltaTime, invalid motion과 blocking hit의 모든 제어 흐름이 같은 scope를 벗어나므로 frame당 정확히 한 번 호출된다. loop 안과 각 return 앞에는 별도 debug 호출을 넣지 않는다.

~~~cpp
void UShipMovement::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
ON_SCOPE_EXIT
{
    DrawMovementDebug();
};

if (!IsValid(GetOwner()) || !IsValid(GetOwner()->GetRootComponent()) ||
    !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
{
    SignedSpeedCmPerSecond = 0.0;
    LastSubsteps = 0;
    LastSimulatedDeltaTime = 0.0;
    LastDroppedDeltaTime = 0.0;
    return;
}

const FShipValidatedMotionParameters Validated =
    ValidateShipMotionParameters(BuildCandidateParameters());
const FShipSubstepSchedule Schedule = BuildShipSubstepSchedule(
    DeltaTime, Validated.Parameters.MaxSimulationStepSeconds,
    Validated.Parameters.MaxSubstepsPerTick);
if (!Schedule.bValid)
{
    SignedSpeedCmPerSecond = 0.0;
    return;
}
LastParameterState = static_cast<uint8>(Validated.State);
LastSubsteps = Schedule.NumSteps;
LastStepSeconds = Schedule.StepSeconds;
LastSimulatedDeltaTime = Schedule.SimulatedDeltaTimeSeconds;
LastDroppedDeltaTime = Schedule.DroppedDeltaTimeSeconds;

for (int32 StepIndex = 0; StepIndex < Schedule.NumSteps; ++StepIndex)
{
    const FShipMotionState Before{SignedSpeedCmPerSecond, HorizontalYawDegrees};
    const FShipMotionStep Motion = AdvanceShipMotion(
        Before, MakeShipMotionInput(ThrottleInput, SteerInput),
        Validated.Parameters, Schedule.StepSeconds);
    if (!Motion.bValid)
    {
        SignedSpeedCmPerSecond = 0.0;
        break;
    }

    const double Heading = FMath::DegreesToRadians(Before.HorizontalYawDegrees);
    const FVector HorizontalForward(FMath::Cos(Heading), FMath::Sin(Heading), 0.0);
    AActor* Owner = GetOwner();
    const FVector CurrentLocation = Owner->GetActorLocation();
    FVector NewLocation = CurrentLocation + HorizontalForward * Motion.TravelCm;

    UWaterSubsystem* Water = UWaterSubsystem::GetWaterSubsystem(GetWorld());
    if (IsValid(Water) && !OceanBodyComponent.IsValid())
    {
        OceanBodyComponent = Water->GetOceanBodyComponent();
    }
    UWaterBodyComponent* Ocean = OceanBodyComponent.Get();
    TOptional<FWaterBodyQueryResult> Query;
    if (IsValid(Water) && IsValid(Ocean))
    {
        Query.Emplace(Ocean->QueryWaterInfoClosestToWorldLocation(
            FVector(NewLocation.X, NewLocation.Y, CurrentLocation.Z), QueryFlags));
    }
    TOptional<FShipSurfaceSample> Last;
    if (bHasLastValidSurface)
    {
        Last.Emplace(FShipSurfaceSample{
            EShipWaterState::ValidNoWaves, LastValidSurfaceZ,
            LastValidSurfaceNormal, false});
    }
    const FShipSurfaceSample Surface = ResolveWaterSurfaceSample(
        IsValid(Water), IsValid(Ocean), IsValid(Ocean) && Ocean->HasWaves(),
        Query.IsSet() ? &Query.GetValue() : nullptr, Last, CurrentLocation.Z);
    LastWaterState = static_cast<uint8>(Surface.State);
    bLastWaterFallback = Surface.bUsedFallback;
    LastTargetSurfaceZ = Surface.SurfaceZ + WaterlineOffsetCm;
    if (!Surface.bUsedFallback)
    {
        bHasLastValidSurface = true;
        LastValidSurfaceZ = Surface.SurfaceZ;
        LastValidSurfaceNormal = Surface.Normal;
    }

    const double HeightSpeed =
        FMath::IsFinite(WaterHeightInterpSpeed) && WaterHeightInterpSpeed > 0.0
        ? WaterHeightInterpSpeed : 5.0;
    const double NormalSpeed =
        FMath::IsFinite(WaterNormalInterpSpeed) && WaterNormalInterpSpeed > 0.0
        ? WaterNormalInterpSpeed : 5.0;
    const double HeightAlpha = 1.0 - FMath::Exp(-HeightSpeed * Schedule.StepSeconds);
    const double NormalAlpha = 1.0 - FMath::Exp(-NormalSpeed * Schedule.StepSeconds);
    NewLocation.Z = FMath::Lerp(
        CurrentLocation.Z, LastTargetSurfaceZ, HeightAlpha);
    FVector BlendedUp = FMath::Lerp(AppliedUpVector, Surface.Normal, NormalAlpha);
    AppliedUpVector = BlendedUp.Normalize() ? BlendedUp : FVector::UpVector;

    const FShipSurfaceBasis Basis = BuildShipSurfaceBasis(
        Motion.NextState.HorizontalYawDegrees, AppliedUpVector,
        bHasLastValidSurface
            ? TOptional<FVector>(LastValidSurfaceNormal)
            : TOptional<FVector>());
    const FQuat NewRotation =
        FRotationMatrix::MakeFromXZ(Basis.Forward, Basis.Up).ToQuat();
    FHitResult Hit;
    Owner->SetActorLocationAndRotation(
        NewLocation, NewRotation, true, &Hit, ETeleportType::None);

    SignedSpeedCmPerSecond = Motion.NextState.SignedSpeedCmPerSecond;
    HorizontalYawDegrees = Motion.NextState.HorizontalYawDegrees;
    LastAcceleration = Motion.AccelerationCmPerSecondSquared;
    LastYawRate = Motion.YawRateDegreesPerSecond;
    bLastBlockingHit = Hit.bBlockingHit;
    if (Hit.bBlockingHit)
    {
        SignedSpeedCmPerSecond = 0.0;
        LastImpactNormal = Hit.ImpactNormal;
        LastHitName = IsValid(Hit.GetActor()) ? Hit.GetActor()->GetName() : TEXT("Unknown");
        break;
    }
}
}
~~~

Loop 전후에 LastSubsteps, LastStepSeconds, simulated 및 dropped DeltaTime, Water와 parameter state를 저장한다. dropped가 0에서 양수로 전환될 때만 warning을 남기고 다음 tick에 시간을 더하지 않는다. Water와 parameter state도 값이 바뀔 때만 로그를 남긴다.

- [ ] **Step 6: 화면 debug와 오류 fallback 구현**

Shipping을 제외하고 고정 key 0x53484950으로 throttle, steer, speed, acceleration, yaw rate, substep, step seconds, simulated, dropped, Water 상태, fallback, target 및 applied Z, normal 및 up, hit, input mode Manual, parameter 상태를 한 블록에 갱신한다. owner/root 부재는 tick 비활성화, 비유한 DeltaTime이나 motion 결과는 transform 미적용과 speed 0으로 처리한다.

~~~cpp
namespace
{
const TCHAR* WaterStateName(uint8 Value)
{
    switch (static_cast<EShipWaterState>(Value))
    {
    case EShipWaterState::ValidWaves: return TEXT("ValidWaves");
    case EShipWaterState::ValidNoWaves: return TEXT("ValidNoWaves");
    case EShipWaterState::Excluded: return TEXT("Excluded");
    case EShipWaterState::QueryInvalid: return TEXT("QueryInvalid");
    case EShipWaterState::ComponentInvalid: return TEXT("ComponentInvalid");
    }
    return TEXT("Uninitialized");
}

const TCHAR* ParameterStateName(uint8 Value)
{
    switch (static_cast<EShipMotionParameterState>(Value))
    {
    case EShipMotionParameterState::Defaults: return TEXT("Defaults");
    case EShipMotionParameterState::Tuned: return TEXT("Tuned");
    case EShipMotionParameterState::TuningFallback: return TEXT("TuningFallback");
    }
    return TEXT("Uninitialized");
}
}

void UShipMovement::DrawMovementDebug() const
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestDebugDrawInvocationCount;
#endif
#if !UE_BUILD_SHIPPING
    if (!bShowMovementDebug || !GEngine)
    {
        return;
    }
    const FString Message = FString::Printf(
        TEXT("Ship Manual  T %.2f  S %.2f\n")
        TEXT("speed %.3f cm/s  accel %.3f cm/s^2  yawRate %.3f deg/s\n")
        TEXT("substeps %d  h %.6f  simulated %.6f  dropped %.6f\n")
        TEXT("water %s  fallback %s  targetZ %.3f  appliedZ %.3f  offset %.3f\n")
        TEXT("normal %s  up %s\n")
        TEXT("hit %s  impact %s  parameter %s"),
        ThrottleInput, SteerInput, SignedSpeedCmPerSecond, LastAcceleration,
        LastYawRate, LastSubsteps, LastStepSeconds, LastSimulatedDeltaTime,
        LastDroppedDeltaTime, WaterStateName(LastWaterState),
        bLastWaterFallback ? TEXT("yes") : TEXT("no"), LastTargetSurfaceZ,
        GetOwner() ? GetOwner()->GetActorLocation().Z : 0.0,
        WaterlineOffsetCm, *LastValidSurfaceNormal.ToCompactString(),
        *AppliedUpVector.ToCompactString(),
        bLastBlockingHit ? *LastHitName : TEXT("none"),
        *LastImpactNormal.ToCompactString(), ParameterStateName(LastParameterState));
    GEngine->AddOnScreenDebugMessage(0x53484950, 0.0f, FColor::Cyan, Message);
#endif
}
~~~

- [ ] **Step 7: runtime test GREEN과 전체 회귀**

~~~powershell
Invoke-ShipGreenGate -ExpectedTests 8 -Stage "Task3"
~~~

Expected: guard opening 및 closing 각 1개와 macro 8개를 먼저 확인하고, build exit 0 뒤 발견 8, Success 8, failure 및 automation error 0, clean exit marker 1개 이상이다. TransformOwnership의 actual count는 swept actor move 1, ShipPawn mesh scale 0, ShipPawn camera rotation 0과 일치한다. Runtime test는 fallback, invalid 입력과 blocking hit tick에서 debug call 증가가 각각 정확히 1이고, sweep 위치 유지, speed 0과 remaining substep 중단을 단언한다.

- [ ] **Step 8: Task 3 commit**

~~~powershell
git add ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp
git diff --cached --check
git commit -m "feat: 선박 이동 컴포넌트 완성" -m "변경 이유: 수치 상태를 Water 수면과 충돌 가능한 actor transform으로 안전하게 적용해야 합니다." -m "핵심 변경: bounded tick, Water query와 fallback, 지수 보간, yaw 보존 회전, swept blocking 정책과 debug를 구현했습니다." -m "검증 방법: Runtime, Water, Motion 자동화 테스트 8개와 editor 빌드를 실행했습니다."
~~~

---

### Task 4: hull, 카메라와 runtime Enhanced Input

**Files:**

- Modify: ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp
- Create: ShipAutonomySim/Config/DefaultInput.ini
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp

**Interfaces:**

- Consumes: UShipMovement::SetThrottle, SetSteer
- Produces: ResetManualInput, DeactivateManualInput, runtime Axis1D actions와 mapping
- Produces tests: ShipAutonomySim.ShipMovement.Pawn.Construction, Pawn.InputLifecycle, Pawn.FocusLossAndAutopilotGuard

- [ ] **Step 1: 실제 LocalPlayer 입력 fixture와 Pawn RED test 세 개 추가**

Task 3 파일의 마지막 closing `#endif`를 세 Pawn macro, include, fixture, accessor와 세 RunTest 본문 뒤로 옮긴다. 아래 C++ block들은 같은 outer guard 안에 이어 붙이고 Step 2 전에 closing을 파일 마지막 줄에 복원한다. 같은 RED 편집에서 기존 TransformOwnership의 ShipPawn child 두 항목만 아래 값으로 교체해 `ExpectedCount`를 0에서 1로 전환한다. ShipMovement.cpp의 swept actor move 기대값 1은 바꾸지 않는다. Construction은 root extent 100,50,50, visual scale 2,1,1, collision과 physics off, movement, spring arm과 camera 존재를 단언한다. 나머지 두 test는 private handler를 직접 호출하지 않는다. UE 5.5.4의 정상 경로대로 GameInstance에 LocalPlayer를 등록하고, project GameMode로 world actor를 초기화하고, `ULocalPlayer::SpawnPlayActor`가 만든 local APlayerController와 실제 `UEnhancedInputLocalPlayerSubsystem`, `UEnhancedPlayerInput`, `UEnhancedInputComponent`를 사용한다. 키 입력은 `APlayerController::InputKey(FInputKeyParams(...))` 뒤 `PlayerTick`으로 처리한다.

~~~cpp
{TEXT("/Private/ShipPawn.cpp"),
 TEXT("VisualMesh->SetRelativeScale3D(FVector(2.0,1.0,1.0));"), 1},
{TEXT("/Private/ShipPawn.cpp"),
 TEXT("CameraBoom->SetRelativeRotation(FRotator(CameraPitchDegrees,0.0,0.0));"), 1}
~~~

이 교체 뒤 Task 4 RED와 이후 파일의 ownership count 계약은 swept actor move 1, ShipPawn mesh scale 1, ShipPawn camera rotation 1이다.

~~~cpp
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/URL.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputTriggers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPawnConstructionTest,
    "ShipAutonomySim.ShipMovement.Pawn.Construction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPawnInputLifecycleTest,
    "ShipAutonomySim.ShipMovement.Pawn.InputLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPawnFocusLossTest,
    "ShipAutonomySim.ShipMovement.Pawn.FocusLossAndAutopilotGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

class FScopedShipInputWorld
{
public:
    static constexpr float InputFrameSeconds = 1.0f / 60.0f;

    FScopedShipInputWorld()
    {
        GameInstance = NewObject<UGameInstance>(GEngine);
        GameInstance->AddToRoot();
        const FName WorldName = MakeUniqueObjectName(
            GetTransientPackage(), UWorld::StaticClass(), TEXT("ShipInputAutomationWorld"));
        GameInstance->InitializeStandalone(WorldName, GetTransientPackage());
        World = GameInstance->GetWorld();
        check(World != nullptr && World->SetGameMode(FURL()));

        LocalPlayer = NewObject<ULocalPlayer>(GEngine, GEngine->LocalPlayerClass);
        check(GameInstance->AddLocalPlayer(LocalPlayer, 0) == 0);
        World->InitializeActorsForPlay(FURL());
        FString SpawnError;
        check(LocalPlayer->SpawnPlayActor(TEXT(""), SpawnError, World));
        Controller = LocalPlayer->GetPlayerController(World);
        Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
        check(Controller != nullptr && Subsystem != nullptr);
        check(Cast<UEnhancedPlayerInput>(Controller->PlayerInput) != nullptr);
        check(Cast<UEnhancedInputComponent>(Controller->InputComponent) != nullptr);
    }

    ~FScopedShipInputWorld()
    {
        if (World != nullptr && World->HasBegunPlay())
        {
            World->EndPlay(EEndPlayReason::Quit);
        }
        if (GameInstance != nullptr)
        {
            GameInstance->Shutdown();
            GameInstance->RemoveFromRoot();
        }
        if (World != nullptr)
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    }

    void StartPlay()
    {
        if (!World->HasBegunPlay())
        {
            World->BeginPlay();
        }
    }

    AShipPawn& PossessShip()
    {
        StartPlay();
        AShipPawn* Ship = Cast<AShipPawn>(Controller->GetPawn());
        if (Ship == nullptr)
        {
            Ship = World->SpawnActor<AShipPawn>();
            Controller->Possess(Ship);
        }
        check(Ship != nullptr && Ship->HasActorBegunPlay());
        check(Cast<UEnhancedInputComponent>(Ship->InputComponent) != nullptr);
        FModifyContextOptions RebuildOptions;
        RebuildOptions.bForceImmediately = true;
        Subsystem->RequestRebuildControlMappings(
            RebuildOptions, EInputMappingRebuildType::Rebuild);
        TickInput();
        return *Ship;
    }

    void Press(const FKey& Key)
    {
        Controller->InputKey(FInputKeyParams(Key, IE_Pressed, 1.0));
        TickInput();
    }

    void QueueRelease(const FKey& Key)
    {
        Controller->InputKey(FInputKeyParams(Key, IE_Released, 0.0));
    }

    void Release(const FKey& Key)
    {
        QueueRelease(Key);
        TickInput();
    }

    void TickInput()
    {
        Controller->PlayerTick(InputFrameSeconds);
    }

    UGameInstance* GameInstance = nullptr;
    UWorld* World = nullptr;
    ULocalPlayer* LocalPlayer = nullptr;
    APlayerController* Controller = nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
};

struct FShipPawnTestAccessor
{
    static UShipMovement& Movement(AShipPawn& Pawn) { return *Pawn.ShipMovement; }
    static UInputMappingContext& Mapping(AShipPawn& Pawn) { return *Pawn.ManualControlMapping; }
    static int32 MappingRemovalCount(const AShipPawn& Pawn)
    {
        return Pawn.TestManualMappingRemovalCount;
    }
    static int32 ThrottleCompletedCount(const AShipPawn& Pawn)
    {
        return Pawn.TestThrottleCompletedCount;
    }
    static int32 SteerCanceledCount(const AShipPawn& Pawn)
    {
        return Pawn.TestSteerCanceledCount;
    }
    static void ConfigureSteerAsOngoingHold(
        AShipPawn& Pawn, UEnhancedInputLocalPlayerSubsystem& Subsystem)
    {
        UInputTriggerHold* Hold = NewObject<UInputTriggerHold>(Pawn.SteerAction);
        Hold->HoldTimeThreshold = 10.0f;
        Pawn.SteerAction->Triggers.Add(Hold);
        FModifyContextOptions Options;
        Options.bForceImmediately = true;
        Subsystem.RequestRebuildControlMappings(Options);
    }
    static void DeactivateForAutopilot(AShipPawn& Pawn)
    {
        Pawn.DeactivateManualInput();
    }
};
~~~

`SetupPlayerInputComponent`의 `AddMappingContext`는 기본 option으로 pending rebuild를 만들 수 있으므로 fixture는 `PlayerTick`이 이를 처리한다고 가정하지 않는다. `PossessShip()`은 첫 `InputKey`보다 먼저 `RequestRebuildControlMappings(RebuildOptions, EInputMappingRebuildType::Rebuild)`를 호출하고 `bForceImmediately=true`로 같은 frame에 live mapping을 만든다.

InputLifecycle은 실제 Completed, Canceled, UnPossessed, EndPlay와 mapping 제거를 다음처럼 실행한다. 첫 fixture의 W release는 Triggered 뒤 Completed를 발생시키고 `UnPossess`가 reset과 한 번의 context 제거를 수행한다. 두 번째 fixture는 test-only로 steer action을 긴 Hold 상태로 재구성해 Ongoing에서 release하도록 하며 실제 Canceled binding을 통과시킨다. 세 번째 fixture의 `Destroy()`는 AActor의 정상 EndPlay 경로를 실행한다.

~~~cpp
bool FShipPawnInputLifecycleTest::RunTest(const FString&)
{
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        TestEqual(TEXT("four WASD mappings"),
            FShipPawnTestAccessor::Mapping(Pawn).GetMappings().Num(), 4);
        TestTrue(TEXT("context registered"),
            Input.Subsystem->HasMappingContext(&FShipPawnTestAccessor::Mapping(Pawn)));
        Input.Press(EKeys::W);
        TestEqual(TEXT("Triggered forwards throttle"),
            FShipMovementTestAccessor::Throttle(FShipPawnTestAccessor::Movement(Pawn)), 1.0);
        Input.Release(EKeys::W);
        TestEqual(TEXT("Completed resets throttle"),
            FShipMovementTestAccessor::Throttle(FShipPawnTestAccessor::Movement(Pawn)), 0.0);
        TestEqual(TEXT("Completed handler count"),
            FShipPawnTestAccessor::ThrottleCompletedCount(Pawn), 1);

        Input.Press(EKeys::W);
        Input.Controller->UnPossess();
        TestEqual(TEXT("UnPossessed resets throttle"),
            FShipMovementTestAccessor::Throttle(FShipPawnTestAccessor::Movement(Pawn)), 0.0);
        TestEqual(TEXT("UnPossessed removes context once"),
            FShipPawnTestAccessor::MappingRemovalCount(Pawn), 1);
        TestFalse(TEXT("context removed"),
            Input.Subsystem->HasMappingContext(&FShipPawnTestAccessor::Mapping(Pawn)));
    }
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        FShipPawnTestAccessor::ConfigureSteerAsOngoingHold(Pawn, *Input.Subsystem);
        Input.Press(EKeys::D);
        Input.Release(EKeys::D);
        TestEqual(TEXT("Canceled binding executed"),
            FShipPawnTestAccessor::SteerCanceledCount(Pawn), 1);
        Input.Controller->UnPossess();
        TestEqual(TEXT("Canceled fixture lifecycle removes context once"),
            FShipPawnTestAccessor::MappingRemovalCount(Pawn), 1);
    }
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        Input.Press(EKeys::W);
        Pawn.Destroy();
        TestEqual(TEXT("EndPlay resets throttle"),
            FShipMovementTestAccessor::Throttle(FShipPawnTestAccessor::Movement(Pawn)), 0.0);
        TestEqual(TEXT("EndPlay removes context once"),
            FShipPawnTestAccessor::MappingRemovalCount(Pawn), 1);
    }
    return true;
}
~~~

FocusLossAndAutopilotGuard는 headless automation이 OS window focus를 만들었다고 가장하지 않는다. 먼저 ini가 viewport focus loss에서 `APlayerController::FlushPressedKeys()`로 이어지는 engine policy를 켰는지 확인한다. 즉시 rebuild를 마친 fixture에서 W와 A를 실제 주입하고 throttle이 양수, steer가 음수임을 먼저 단언해 이미 0인 값으로 통과하는 false GREEN을 차단한다. 그런 다음 같은 controller의 검증된 `FlushPressedKeys()` 경로를 실행해 두 입력이 0이고 다음 movement step이 thrust와 steer 없이 drag만 적용하는지 단언한다. 실제 viewport focus 이동은 Final Verification의 PIE 항목에서 별도로 확인한다. 두 번째 fixture는 release를 queue한 뒤 manual mode를 비활성화하고 autopilot 값을 두 setter로 기록한 다음 Completed와 Canceled를 처리해 늦은 release가 값을 지우지 않는지 검사한다.

~~~cpp
bool FShipPawnFocusLossTest::RunTest(const FString&)
{
    FString InputConfig;
    TestTrue(TEXT("DefaultInput.ini readable"), FFileHelper::LoadFileToString(
        InputConfig, *(FPaths::ProjectConfigDir() / TEXT("DefaultInput.ini"))));
    TestTrue(TEXT("enhanced player input"), InputConfig.Contains(
        TEXT("DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput")));
    TestTrue(TEXT("enhanced component"), InputConfig.Contains(
        TEXT("DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent")));
    TestTrue(TEXT("focus loss requests pressed-key flush"), InputConfig.Contains(
        TEXT("bShouldFlushPressedKeysOnViewportFocusLost=True")));

    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        UShipMovement& Movement = FShipPawnTestAccessor::Movement(Pawn);
        Input.Press(EKeys::W);
        TestTrue(TEXT("W produces non-zero throttle before flush"),
            FShipMovementTestAccessor::Throttle(Movement) > 0.99);
        Input.Press(EKeys::A);
        TestTrue(TEXT("A produces non-zero steer before flush"),
            FShipMovementTestAccessor::Steer(Movement) < -0.99);
        FShipMovementTestAccessor::SetState(Movement, 100.0, 0.0);
        Input.Controller->FlushPressedKeys();
        Input.TickInput();
        FShipMovementTestAccessor::Tick(Movement, 1.0f / 120.0f);
        TestEqual(TEXT("flush clears throttle"),
            FShipMovementTestAccessor::Throttle(Movement), 0.0);
        TestEqual(TEXT("flush clears steer"),
            FShipMovementTestAccessor::Steer(Movement), 0.0);
        const double ExpectedDrag =
            -(0.447501534 * 100.0 + 0.000400390770 * 100.0 * 100.0);
        TestTrue(TEXT("next motion step is drag only"),
            FMath::Abs(FShipMovementTestAccessor::Acceleration(Movement) - ExpectedDrag) <= 1e-8);
    }
    {
        FScopedShipInputWorld Input;
        AShipPawn& Pawn = Input.PossessShip();
        UShipMovement& Movement = FShipPawnTestAccessor::Movement(Pawn);
        FShipPawnTestAccessor::ConfigureSteerAsOngoingHold(Pawn, *Input.Subsystem);
        Input.Press(EKeys::W);
        Input.Press(EKeys::D);
        const int32 CompletedBefore = FShipPawnTestAccessor::ThrottleCompletedCount(Pawn);
        const int32 CanceledBefore = FShipPawnTestAccessor::SteerCanceledCount(Pawn);
        Input.QueueRelease(EKeys::W);
        Input.QueueRelease(EKeys::D);
        FShipPawnTestAccessor::DeactivateForAutopilot(Pawn);
        Movement.SetThrottle(0.65f);
        Movement.SetSteer(-0.25f);
        Input.TickInput();
        TestEqual(TEXT("late Completed was delivered"),
            FShipPawnTestAccessor::ThrottleCompletedCount(Pawn), CompletedBefore + 1);
        TestEqual(TEXT("late Canceled was delivered"),
            FShipPawnTestAccessor::SteerCanceledCount(Pawn), CanceledBefore + 1);
        TestTrue(TEXT("late release preserves autopilot throttle"),
            FMath::Abs(FShipMovementTestAccessor::Throttle(Movement) - 0.65) <= 1e-6);
        TestTrue(TEXT("late release preserves autopilot steer"),
            FMath::Abs(FShipMovementTestAccessor::Steer(Movement) + 0.25) <= 1e-6);
    }
    return true;
}
~~~

세 Pawn RunTest 본문 뒤 파일 tail은 다음 한 줄로 끝난다. Task 4 RED, GREEN과 commit 시점의 계약은 opening 1개, closing 1개, macro 11개다.

~~~cpp
#endif // WITH_DEV_AUTOMATION_TESTS
~~~

- [ ] **Step 2: RED build 확인**

~~~powershell
Invoke-ExpectedRedBuild -Stage "Task4" -ExpectedTests 11 -ExpectedFailurePattern 'AShipPawn|ManualControlMapping|TestManualMappingRemovalCount|DefaultInput|UEnhancedInput'
~~~

Expected: AShipPawn의 component, 실제 input lifecycle seam 또는 DefaultInput 계약 부재로 build가 실패하고 editor는 실행하지 않는다. RED test source의 TransformOwnership 기대값은 1, 1, 1로 전환됐지만 두 ShipPawn child 문장은 아직 제품 source에 없으므로 Task 4 구현 전에는 GREEN이 될 수 없다.

- [ ] **Step 3: Pawn header의 소유권과 lifecycle 선언**

~~~cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ShipPawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UShipMovement;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS()
class SHIPAUTONOMYSIM_API AShipPawn : public APawn
{
    GENERATED_BODY()

public:
    AShipPawn();
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void UnPossessed() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category=Ship)
    TObjectPtr<UBoxComponent> CollisionRoot;
    UPROPERTY(VisibleAnywhere, Category=Ship)
    TObjectPtr<UStaticMeshComponent> VisualMesh;
    UPROPERTY(VisibleAnywhere, Category=Ship)
    TObjectPtr<UShipMovement> ShipMovement;
    UPROPERTY(VisibleAnywhere, Category=Camera)
    TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere, Category=Camera)
    TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(EditAnywhere, Category=Camera)
    double CameraArmLengthCm = 600.0;
    UPROPERTY(EditAnywhere, Category=Camera)
    double CameraPitchDegrees = -20.0;
    UPROPERTY(EditAnywhere, Category=Camera)
    double CameraSocketHeightCm = 100.0;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ThrottleAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SteerAction;
    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> ManualControlMapping;
    UPROPERTY(Transient)
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> RegisteredInputSubsystem;

    bool bManualMappingRegistered = false;
    bool bManualInputActive = false;
    void BuildManualInputObjects();
    void ResetManualInput();
    void DeactivateManualInput();
    void HandleThrottle(const FInputActionValue& Value);
    void HandleSteer(const FInputActionValue& Value);
    void HandleThrottleCompleted();
    void HandleThrottleCanceled();
    void HandleSteerCompleted();
    void HandleSteerCanceled();
    void HandleThrottleReleased();
    void HandleSteerReleased();

#if WITH_DEV_AUTOMATION_TESTS
    int32 TestManualMappingRemovalCount = 0;
    int32 TestThrottleCompletedCount = 0;
    int32 TestThrottleCanceledCount = 0;
    int32 TestSteerCompletedCount = 0;
    int32 TestSteerCanceledCount = 0;
    friend struct FShipPawnTestAccessor;
#endif
};
~~~

- [ ] **Step 4: 200 x 100 x 100 hull과 3인칭 카메라 구현**

~~~cpp
AShipPawn::AShipPawn()
{
    PrimaryActorTick.bCanEverTick = false;
    CollisionRoot = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionRoot"));
    SetRootComponent(CollisionRoot);
    CollisionRoot->InitBoxExtent(FVector(100.0, 50.0, 50.0));
    CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CollisionRoot->SetCollisionObjectType(ECC_Pawn);
    CollisionRoot->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionRoot->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    CollisionRoot->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    CollisionRoot->SetSimulatePhysics(false);
    CollisionRoot->SetEnableGravity(false);

    VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
    VisualMesh->SetupAttachment(CollisionRoot);
    VisualMesh->SetRelativeScale3D(FVector(2.0, 1.0, 1.0));
    VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisualMesh->SetSimulatePhysics(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded())
    {
        VisualMesh->SetStaticMesh(Cube.Object);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Engine cube mesh load failed"));
    }

    ShipMovement = CreateDefaultSubobject<UShipMovement>(TEXT("ShipMovement"));
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(CollisionRoot);
    CameraBoom->bUsePawnControlRotation = false;
    CameraBoom->bInheritYaw = true;
    CameraBoom->bInheritPitch = false;
    CameraBoom->bInheritRoll = false;
    CameraBoom->bDoCollisionTest = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;
}

void AShipPawn::BeginPlay()
{
    Super::BeginPlay();
    CameraBoom->TargetArmLength = CameraArmLengthCm;
    CameraBoom->SetRelativeRotation(FRotator(CameraPitchDegrees, 0.0, 0.0));
    CameraBoom->SocketOffset = FVector(0.0, 0.0, CameraSocketHeightCm);
}
~~~

- [ ] **Step 5: runtime WASD 객체와 binding 구현**

ShipPawn.cpp는 InputAction.h, InputActionValue.h, InputMappingContext.h, EnhancedActionKeyMapping.h, InputModifiers.h, EnhancedInputComponent.h, EnhancedInputSubsystems.h, InputCoreTypes.h를 private include한다.

~~~cpp
void AShipPawn::BuildManualInputObjects()
{
    if (IsValid(ThrottleAction))
    {
        return;
    }
    ThrottleAction = NewObject<UInputAction>(this, TEXT("ThrottleAction"));
    SteerAction = NewObject<UInputAction>(this, TEXT("SteerAction"));
    ManualControlMapping = NewObject<UInputMappingContext>(this, TEXT("ManualControlMapping"));
    for (UInputAction* Action : {ThrottleAction.Get(), SteerAction.Get()})
    {
        Action->ValueType = EInputActionValueType::Axis1D;
        Action->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    }
    ManualControlMapping->MapKey(ThrottleAction, EKeys::W);
    FEnhancedActionKeyMapping& S = ManualControlMapping->MapKey(ThrottleAction, EKeys::S);
    S.Modifiers.Add(NewObject<UInputModifierNegate>(ManualControlMapping));
    ManualControlMapping->MapKey(SteerAction, EKeys::D);
    FEnhancedActionKeyMapping& A = ManualControlMapping->MapKey(SteerAction, EKeys::A);
    A.Modifiers.Add(NewObject<UInputModifierNegate>(ManualControlMapping));
}

void AShipPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    DeactivateManualInput();
    BuildManualInputObjects();
    UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    APlayerController* PC = Cast<APlayerController>(GetController());
    ULocalPlayer* LocalPlayer = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem =
        IsValid(LocalPlayer)
        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer)
        : nullptr;
    if (!IsValid(Enhanced) || !IsValid(Subsystem))
    {
        UE_LOG(LogTemp, Error, TEXT("Manual Enhanced Input is unavailable"));
        return;
    }
    Enhanced->BindAction(ThrottleAction, ETriggerEvent::Triggered,
        this, &AShipPawn::HandleThrottle);
    Enhanced->BindAction(ThrottleAction, ETriggerEvent::Completed,
        this, &AShipPawn::HandleThrottleCompleted);
    Enhanced->BindAction(ThrottleAction, ETriggerEvent::Canceled,
        this, &AShipPawn::HandleThrottleCanceled);
    Enhanced->BindAction(SteerAction, ETriggerEvent::Triggered,
        this, &AShipPawn::HandleSteer);
    Enhanced->BindAction(SteerAction, ETriggerEvent::Completed,
        this, &AShipPawn::HandleSteerCompleted);
    Enhanced->BindAction(SteerAction, ETriggerEvent::Canceled,
        this, &AShipPawn::HandleSteerCanceled);
    Subsystem->AddMappingContext(ManualControlMapping, 0);
    RegisteredInputSubsystem = Subsystem;
    bManualMappingRegistered = true;
    bManualInputActive = true;
}
~~~

- [ ] **Step 6: 공통 reset, 수명 종료와 autopilot guard 구현**

~~~cpp
void AShipPawn::ResetManualInput()
{
    ShipMovement->SetThrottle(0.0f);
    ShipMovement->SetSteer(0.0f);
}

void AShipPawn::DeactivateManualInput()
{
    ResetManualInput();
    bManualInputActive = false;
    if (bManualMappingRegistered && RegisteredInputSubsystem.IsValid() &&
        IsValid(ManualControlMapping))
    {
        RegisteredInputSubsystem->RemoveMappingContext(ManualControlMapping);
#if WITH_DEV_AUTOMATION_TESTS
        ++TestManualMappingRemovalCount;
#endif
    }
    bManualMappingRegistered = false;
    RegisteredInputSubsystem.Reset();
}

void AShipPawn::HandleThrottle(const FInputActionValue& Value)
{
    if (bManualInputActive)
    {
        ShipMovement->SetThrottle(FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f));
    }
}

void AShipPawn::HandleSteer(const FInputActionValue& Value)
{
    if (bManualInputActive)
    {
        ShipMovement->SetSteer(FMath::Clamp(Value.Get<float>(), -1.0f, 1.0f));
    }
}

void AShipPawn::HandleThrottleCompleted()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestThrottleCompletedCount;
#endif
    HandleThrottleReleased();
}

void AShipPawn::HandleThrottleCanceled()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestThrottleCanceledCount;
#endif
    HandleThrottleReleased();
}

void AShipPawn::HandleSteerCompleted()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestSteerCompletedCount;
#endif
    HandleSteerReleased();
}

void AShipPawn::HandleSteerCanceled()
{
#if WITH_DEV_AUTOMATION_TESTS
    ++TestSteerCanceledCount;
#endif
    HandleSteerReleased();
}

void AShipPawn::HandleThrottleReleased()
{
    if (bManualInputActive)
    {
        ShipMovement->SetThrottle(0.0f);
    }
}

void AShipPawn::HandleSteerReleased()
{
    if (bManualInputActive)
    {
        ShipMovement->SetSteer(0.0f);
    }
}

void AShipPawn::UnPossessed()
{
    DeactivateManualInput();
    Super::UnPossessed();
}

void AShipPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DeactivateManualInput();
    Super::EndPlay(EndPlayReason);
}
~~~

향후 autopilot 전환은 먼저 `DeactivateManualInput()`이 `ResetManualInput()`을 직접 호출해 남은 키 값을 지운 뒤 같은 두 setter를 호출한다. 값 handler뿐 아니라 Completed와 Canceled가 공유하는 두 release handler도 `bManualInputActive`가 false이면 아무 값도 쓰지 않는다. 따라서 mode 전환 뒤 배달된 늦은 release가 autopilot setter 값을 지울 수 없다. 3단계에는 공개 autopilot 전환 함수나 UShipNavigator 변경을 추가하지 않는다.

- [ ] **Step 7: DefaultInput.ini 생성**

~~~ini
[/Script/Engine.InputSettings]
DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput
DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent
bShouldFlushPressedKeysOnViewportFocusLost=True
~~~

- [ ] **Step 8: Pawn GREEN과 전체 회귀**

~~~powershell
Invoke-ShipGreenGate -ExpectedTests 11 -Stage "Task4"
~~~

Expected: guard opening 및 closing 각 1개와 macro 11개를 먼저 확인하고, build exit 0 뒤 발견 11, Success 11, failure 및 automation error 0, clean exit marker 1개 이상이다. TransformOwnership의 actual count는 swept actor move 1, ShipPawn mesh scale 1, ShipPawn camera rotation 1과 일치한다. Pawn tests는 즉시 mapping rebuild 뒤 W throttle과 A steer가 비영 값임을 먼저 증명하고, 실제 Triggered, Completed, Canceled, UnPossessed, EndPlay, mapping 제거 횟수 1, controller pressed-key flush 뒤 drag-only step과 늦은 release 뒤 autopilot 입력 보존을 단언한다. OS focus 전환 자체는 PIE에서 보완 검증한다.

- [ ] **Step 9: Task 4 commit**

~~~powershell
git add ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp ShipAutonomySim/Config/DefaultInput.ini
git diff --cached --check
git commit -m "feat: 선박 수동 입력과 카메라 구성" -m "변경 이유: 에셋 없이 PIE에서 선박 거동과 입력 수명주기를 관찰할 수 있어야 합니다." -m "핵심 변경: 기본 hull과 카메라, runtime Enhanced Input WASD, focus loss 및 수명 종료 reset을 구성했습니다." -m "검증 방법: Pawn 포함 자동화 테스트 11개, editor 빌드와 입력 설정 검사를 실행했습니다."
~~~

---

### Task 5: ASimGameMode test spawn과 possession

**Files:**

- Modify: ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp
- Modify: ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp

**Interfaces:**

- Consumes: AShipPawn
- Produces: world origin 및 yaw 0 기본 test spawn, 첫 player controller possession, 중복 방지
- Produces test: ShipAutonomySim.ShipMovement.GameMode.Bootstrap

- [ ] **Step 1: GameMode RED test 추가**

Task 4 파일 마지막의 closing `#endif`를 GameMode macro, accessor와 RunTest 본문 뒤로 옮기고 아래 block 끝에서 다시 파일 마지막 줄로 둔다. Task 4의 LocalPlayer fixture가 `SetGameMode`, `InitializeActorsForPlay`, `SpawnPlayActor`, `UWorld::BeginPlay`를 실행하게 한다. ASimGameMode actor의 BeginPlay는 정상 actor lifecycle에서 정확히 한 번만 실행한다. spawn과 possession의 멱등 부분은 private `EnsureTestShipForFirstPlayer()` helper로 추출하고 test는 BeginPlay가 아니라 이 helper만 두 번 더 호출한다. 이로써 `AActor::DispatchBeginPlay` 상태 전제를 위반하지 않고도 반복성을 검증한다.

~~~cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimGameModeBootstrapTest,
    "ShipAutonomySim.ShipMovement.GameMode.Bootstrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

struct FSimGameModeTestAccessor
{
    static void EnsureTestShipForFirstPlayer(ASimGameMode& GameMode)
    {
        GameMode.EnsureTestShipForFirstPlayer();
    }
    static int32 BeginPlayInvocationCount(const ASimGameMode& GameMode)
    {
        return GameMode.TestBeginPlayInvocationCount;
    }
};

bool FSimGameModeBootstrapTest::RunTest(const FString&)
{
    FScopedShipInputWorld Input;
    AShipPawn& InputShip = Input.PossessShip();
    ASimGameMode* GameMode = Cast<ASimGameMode>(Input.World->GetAuthGameMode());
    TestNotNull(TEXT("project GameMode exists"), GameMode);
    if (GameMode == nullptr)
    {
        return false;
    }
    TestTrue(TEXT("GameMode used normal BeginPlay lifecycle"),
        GameMode->HasActorBegunPlay());
    TestEqual(TEXT("GameMode BeginPlay ran exactly once"),
        FSimGameModeTestAccessor::BeginPlayInvocationCount(*GameMode), 1);
    TestNotNull(TEXT("real LocalPlayer"), Input.Controller->GetLocalPlayer());
    TestNotNull(TEXT("real Enhanced subsystem"), Input.Subsystem);
    AShipPawn* PossessedShip = Cast<AShipPawn>(Input.Controller->GetPawn());
    TestNotNull(TEXT("ship possessed"), PossessedShip);
    TestTrue(TEXT("fixture returned possessed ship"), PossessedShip == &InputShip);
    if (PossessedShip != nullptr)
    {
        TestNotNull(TEXT("possessed ship has Enhanced input component"),
            Cast<UEnhancedInputComponent>(PossessedShip->InputComponent));
        TestTrue(TEXT("manual context registered"), Input.Subsystem->HasMappingContext(
            &FShipPawnTestAccessor::Mapping(*PossessedShip)));
    }
    TArray<AActor*> Ships;
    UGameplayStatics::GetAllActorsOfClass(
        Input.World, AShipPawn::StaticClass(), Ships);
    TestEqual(TEXT("one ship"), Ships.Num(), 1);
    FSimGameModeTestAccessor::EnsureTestShipForFirstPlayer(*GameMode);
    FSimGameModeTestAccessor::EnsureTestShipForFirstPlayer(*GameMode);
    Ships.Reset();
    UGameplayStatics::GetAllActorsOfClass(
        Input.World, AShipPawn::StaticClass(), Ships);
    TestEqual(TEXT("helper remains idempotent"), Ships.Num(), 1);
    TestTrue(TEXT("same ship remains possessed"),
        Input.Controller->GetPawn() == PossessedShip);
    TestEqual(TEXT("helper does not redispatch BeginPlay"),
        FSimGameModeTestAccessor::BeginPlayInvocationCount(*GameMode), 1);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
~~~

이 `#endif`가 ShipMovementTests.cpp의 유일한 closing guard다. 파일 첫 줄의 opening guard보다 앞이나 이 줄보다 뒤에 include, macro, fixture 또는 RunTest 구현을 두지 않는다. Task 5 RED, GREEN과 commit 시점에는 opening 1개, closing 1개, macro 12개여야 한다.

- [ ] **Step 2: RED build 확인**

~~~powershell
Invoke-ExpectedRedBuild -Stage "Task5" -ExpectedTests 12 -ExpectedFailurePattern 'EnsureTestShipForFirstPlayer|ShipPawnClass|TestShipSpawnTransform|FSimGameModeBootstrapTest'
~~~

Expected: idempotent helper, ShipPawnClass, TestShipSpawnTransform 또는 spawn 구현 부재로 build가 실패하고 editor는 실행하지 않는다.

- [ ] **Step 3: GameMode header와 source 구현**

~~~cpp
// SimGameMode.h
class AShipPawn;

UCLASS()
class SHIPAUTONOMYSIM_API ASimGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ASimGameMode();
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(EditAnywhere, Category=Stage3Test)
    TSubclassOf<AShipPawn> ShipPawnClass;
    UPROPERTY(EditAnywhere, Category=Stage3Test)
    FTransform TestShipSpawnTransform = FTransform::Identity;
    void EnsureTestShipForFirstPlayer();
#if WITH_DEV_AUTOMATION_TESTS
    int32 TestBeginPlayInvocationCount = 0;
    friend struct FSimGameModeTestAccessor;
#endif
};
~~~

~~~cpp
// SimGameMode.cpp
#include "SimGameMode.h"
#include "ShipPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ASimGameMode::ASimGameMode()
{
    DefaultPawnClass = nullptr;
    ShipPawnClass = AShipPawn::StaticClass();
    TestShipSpawnTransform = FTransform(FRotator::ZeroRotator, FVector::ZeroVector);
}

void ASimGameMode::BeginPlay()
{
    Super::BeginPlay();
#if WITH_DEV_AUTOMATION_TESTS
    ++TestBeginPlayInvocationCount;
#endif
    EnsureTestShipForFirstPlayer();
}

void ASimGameMode::EnsureTestShipForFirstPlayer()
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!IsValid(PC))
    {
        UE_LOG(LogTemp, Error, TEXT("Stage 3 ship spawn requires a player controller"));
        return;
    }
    if (IsValid(Cast<AShipPawn>(PC->GetPawn())))
    {
        return;
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    AShipPawn* Ship = GetWorld()->SpawnActor<AShipPawn>(
        ShipPawnClass, TestShipSpawnTransform, Params);
    if (!IsValid(Ship))
    {
        UE_LOG(LogTemp, Error, TEXT("Stage 3 ship spawn failed"));
        return;
    }
    PC->Possess(Ship);
}
~~~

- [ ] **Step 4: GameMode GREEN과 12개 전체 회귀**

~~~powershell
Invoke-ShipGreenGate -ExpectedTests 12 -Stage "Task5"
~~~

Expected: guard opening 및 closing 각 1개와 macro 12개를 먼저 확인하고, build exit 0 뒤 발견 12, Success 12, failure 및 automation error 0, clean exit marker 1개 이상이다. Bootstrap test는 정상 BeginPlay 한 번, helper 두 번, ship 한 대와 같은 possession을 단언한다. ShipNavigator, CourseBuilder, ShipCapture 파일은 diff에 없어야 한다.

- [ ] **Step 5: Task 5 commit**

~~~powershell
git add ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp
git diff --cached --check
git commit -m "feat: 선박 PIE 부트스트랩 구성" -m "변경 이유: MainLevel PIE 시작 즉시 테스트 선박 한 대를 조종할 수 있어야 합니다." -m "핵심 변경: 기본 pawn 자동 생성을 끄고 중복 없는 ship spawn과 첫 player controller possession을 구성했습니다." -m "검증 방법: GameMode 포함 자동화 테스트 12개와 editor 빌드를 실행했습니다."
~~~

---

## Final Verification

- [ ] **Step 1: clean gate와 검증 전 snapshot 기록**

~~~powershell
$RepoRoot = git rev-parse --show-toplevel
$Branch = git branch --show-current
$HeadBefore = git rev-parse HEAD
$StatusBefore = @(git status --porcelain=v1 --untracked-files=all)
if ($Branch -ne "feat/ship-movement-model" -or $StatusBefore.Count -ne 0) { throw "No-Go: branch or clean gate mismatch" }
git merge-base --is-ancestor 76461c18681998d8d8e954b1e0fddf79d874ffeb HEAD
if ($LASTEXITCODE -ne 0) { throw "No-Go: main base is not an ancestor" }
$ConfigRoot = "$RepoRoot/ShipAutonomySim/Config"
$ConfigBefore = Get-ChildItem -LiteralPath $ConfigRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{
        Path = $_.FullName.Substring($ConfigRoot.Length + 1).Replace("\","/")
        Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        Length = $_.Length
        LastWriteTimeUtc = $_.LastWriteTimeUtc.Ticks
    }
}
$MapPath = "$RepoRoot/ShipAutonomySim/Content/Maps/MainLevel.umap"
$MapItemBefore = Get-Item -LiteralPath $MapPath
$MapBefore = [pscustomobject]@{
    Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $MapPath).Hash
    Length = $MapItemBefore.Length
    LastWriteTimeUtc = $MapItemBefore.LastWriteTimeUtc.Ticks
}
~~~

- [ ] **Step 2: UE 5.5.4 full editor build**

~~~powershell
$EngineRoot = Join-Path $env:ProgramFiles "Epic Games/UE_5.5"
$Project = "$RepoRoot/ShipAutonomySim/ShipAutonomySim.uproject"
$ProjectRoot = [IO.Path]::GetFullPath("$RepoRoot/ShipAutonomySim")
$OutputRoots = @(
    [IO.Path]::GetFullPath("$ProjectRoot/Binaries"),
    [IO.Path]::GetFullPath("$ProjectRoot/Intermediate")
)
foreach ($OutputRoot in $OutputRoots) {
    $ExpectedPrefix = $ProjectRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $Leaf = [IO.Path]::GetFileName($OutputRoot)
    if (!$OutputRoot.StartsWith($ExpectedPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $Leaf -notin @("Binaries", "Intermediate")) {
        throw "No-Go: UBT output scope escaped the project: $OutputRoot"
    }
    $RelativeOutput = $OutputRoot.Substring($RepoRoot.Length + 1).Replace("\", "/")
    git check-ignore -q -- $RelativeOutput
    if ($LASTEXITCODE -ne 0) {
        throw "No-Go: UBT output is not ignored: $RelativeOutput"
    }
}

$TrackedBeforeFullBuild = (git status --porcelain=v1 --untracked-files=no | Out-String)
$CleanLog = "$RepoRoot/ShipAutonomySim/Saved/Logs/ShipMovement-FinalClean.log"
& "$EngineRoot/Engine/Build/BatchFiles/Build.bat" ShipAutonomySimEditor Win64 Development "-Project=$Project" -WaitMutex -Clean 2>&1 |
    Tee-Object -FilePath $CleanLog
$CleanExit = $LASTEXITCODE
if ($CleanExit -ne 0) { throw "No-Go: project-scoped UBT clean failed with exit $CleanExit" }
if ($TrackedBeforeFullBuild -cne (git status --porcelain=v1 --untracked-files=no | Out-String)) {
    throw "No-Go: tracked state changed during UBT clean; do not reset, restore, clean, or delete"
}

$FullBuildLog = "$RepoRoot/ShipAutonomySim/Saved/Logs/ShipMovement-FinalFullBuild.log"
& "$EngineRoot/Engine/Build/BatchFiles/Build.bat" ShipAutonomySimEditor Win64 Development "-Project=$Project" -WaitMutex 2>&1 |
    Tee-Object -FilePath $FullBuildLog
$FullBuildExit = $LASTEXITCODE
if ($FullBuildExit -ne 0) { throw "No-Go: full editor build failed with exit $FullBuildExit" }
$FullBuildText = Get-Content -LiteralPath $FullBuildLog -Raw
$CompileActions = [regex]::Matches($FullBuildText, '(?m)^\s*\[\d+/\d+\]\s+Compile\b').Count
$LinkActions = [regex]::Matches($FullBuildText, '(?m)^\s*\[\d+/\d+\]\s+Link\b').Count
$UpToDateMarkers = [regex]::Matches($FullBuildText, 'Target is up to date').Count
if ($CompileActions -lt 1 -or $LinkActions -lt 1 -or $UpToDateMarkers -ne 0) {
    throw "No-Go: full compile proof missing compile=$CompileActions link=$LinkActions upToDate=$UpToDateMarkers"
}
if ($TrackedBeforeFullBuild -cne (git status --porcelain=v1 --untracked-files=no | Out-String)) {
    throw "No-Go: tracked state changed during full build; do not repair it"
}
[pscustomobject]@{
    CleanExit = $CleanExit
    FullBuildExit = $FullBuildExit
    CompileActions = $CompileActions
    LinkActions = $LinkActions
}
~~~

Expected: 명시한 project의 ignored `Binaries`와 `Intermediate`만 UBT `-Clean` 대상임을 먼저 증명한다. clean exit 0 뒤 full build exit 0, compile action 1개 이상, link action 1개 이상, `Target is up to date` 0개를 기록한다. 광범위한 filesystem 삭제 명령은 사용하지 않는다. 사용 가능한 MSVC가 UE 선호 버전과 다르다는 warning은 build 성공과 분리해 기록한다.

- [ ] **Step 3: 전체 Automation Test 12개**

~~~powershell
if ($FullBuildExit -ne 0 -or $CompileActions -lt 1 -or $LinkActions -lt 1) {
    throw "No-Go: verified full build did not complete; stale editor binary must not run"
}
$AutomationLog = "$RepoRoot/ShipAutonomySim/Saved/Logs/ShipMovement-FinalAutomation.log"
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $Project -Unattended -NoSplash -NoSound -NullRHI -NoP4 -NoAssetRegistryCache -nowrite -NoAnalytics -NoEpicPortal -stdout -FullStdOutLogOutput "-abslog=$AutomationLog" '-ExecCmds=Automation RunTests ShipAutonomySim.ShipMovement;Automation Quit'
if ($LASTEXITCODE -ne 0) { throw "No-Go: automation process failed" }
$AutomationText = Get-Content -LiteralPath $AutomationLog -Raw
$FoundMatches = [regex]::Matches($AutomationText, 'Found\s+(\d+)\s+automation tests')
$FoundTests = if ($FoundMatches.Count -eq 0) { -1 } else { [int]$FoundMatches[$FoundMatches.Count - 1].Groups[1].Value }
$SuccessTests = [regex]::Matches($AutomationText, 'Test Completed.*Result=\{Success\}').Count
$FailedTests = [regex]::Matches($AutomationText, 'Test Completed.*Result=\{(?:Fail|Failed|Error|NotRun)\}').Count
$AutomationErrors = [regex]::Matches($AutomationText, '(?m)^(?:.*(?:LogAutomationController|LogAutomationCommandLine):\s+Error:|.*Fatal error:)').Count
$AutomationCleanExit = [regex]::Matches($AutomationText, 'LogExit: (?:Exiting\.|Editor shut down)').Count
if ($FoundTests -ne 12 -or $SuccessTests -ne 12 -or $FailedTests -ne 0 -or
    $AutomationErrors -ne 0 -or $AutomationCleanExit -lt 1) {
    throw "No-Go: automation found=$FoundTests success=$SuccessTests failed=$FailedTests errors=$AutomationErrors cleanExit=$AutomationCleanExit"
}
~~~

Expected: full build 증거가 먼저 있어야 editor를 시작한다. 발견 12, 정확한 Success 12, failed/error 0, clean exit marker 1개 이상이어야 한다. 명령 exit 0만으로 통과 처리하지 않는다.

- [ ] **Step 4: no-write MainLevel 명령줄 검증**

~~~powershell
if ($FullBuildExit -ne 0 -or $CompileActions -lt 1 -or $LinkActions -lt 1) {
    throw "No-Go: verified full build did not complete; MainLevel was not opened"
}
$MapLog = "$RepoRoot/ShipAutonomySim/Saved/Logs/ShipMovement-MainLevel-nowrite.log"
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $Project /Game/Maps/MainLevel -Unattended -NoSplash -NoSound -NullRHI -NoP4 -NoAssetRegistryCache -nowrite -NoAnalytics -NoEpicPortal -stdout -FullStdOutLogOutput "-abslog=$MapLog" "-ExecCmds=QUIT_EDITOR"
if ($LASTEXITCODE -ne 0) { throw "No-Go: MainLevel validation process failed" }
$MapText = Get-Content -LiteralPath $MapLog -Raw
$AllMapChecks = [regex]::Matches($MapText, 'Map check complete:[^\r\n]*').Count
$PassingMapChecks = [regex]::Matches($MapText, 'Map check complete: 0 Error\(s\), 0 Warning\(s\)').Count
$LoadErrors = [regex]::Matches($MapText, '(?im)^.*LoadErrors.*(?:Error|Warning)').Count
$FatalErrors = [regex]::Matches($MapText, '(?im)Fatal error:').Count
$SeverityErrors = [regex]::Matches($MapText, '(?m)^.*:\s+Error:').Count
$MapCleanExit = [regex]::Matches($MapText, 'LogExit: (?:Exiting\.|Editor shut down)').Count
if ($AllMapChecks -ne 1 -or $PassingMapChecks -ne 1 -or $LoadErrors -ne 0 -or
    $FatalErrors -ne 0 -or $SeverityErrors -ne 0 -or $MapCleanExit -lt 1) {
    throw "No-Go: mapChecks=$AllMapChecks passing=$PassingMapChecks loadErrors=$LoadErrors fatal=$FatalErrors errors=$SeverityErrors cleanExit=$MapCleanExit"
}
~~~

Expected: exit 0, MapCheck 0 Error와 0 Warning, LoadErrors 및 Fatal과 severity Error 0, clean LogExit. QUIT이나 저장 가능한 editor 실행으로 대체하지 않는다.

- [ ] **Step 5: Config, map과 Git 전후 대조**

~~~powershell
$ConfigAfter = Get-ChildItem -LiteralPath $ConfigRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{
        Path = $_.FullName.Substring($ConfigRoot.Length + 1).Replace("\","/")
        Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        Length = $_.Length
        LastWriteTimeUtc = $_.LastWriteTimeUtc.Ticks
    }
}
$MapItemAfter = Get-Item -LiteralPath $MapPath
$MapAfter = [pscustomobject]@{
    Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $MapPath).Hash
    Length = $MapItemAfter.Length
    LastWriteTimeUtc = $MapItemAfter.LastWriteTimeUtc.Ticks
}
$StatusAfter = @(git status --porcelain=v1 --untracked-files=all)
$ConfigDiff = @(Compare-Object ($ConfigBefore | ConvertTo-Json -Compress) ($ConfigAfter | ConvertTo-Json -Compress))
if ($ConfigDiff.Count -ne 0 -or $MapBefore.Hash -ne $MapAfter.Hash -or
    $MapBefore.Length -ne $MapAfter.Length -or
    $MapBefore.LastWriteTimeUtc -ne $MapAfter.LastWriteTimeUtc -or
    $StatusAfter.Count -ne 0) {
    throw "No-Go: unexpected tracked, config, or map change; do not reset, restore, clean, or delete"
}
if ((git rev-parse HEAD) -ne $HeadBefore) { throw "No-Go: HEAD changed during verification" }
git diff --check
if ($LASTEXITCODE -ne 0) { throw "No-Go: git diff --check failed" }
~~~

- [ ] **Step 6: PIE 사람 검증**

PIE를 시작하고 Output Log와 화면 debug를 보면서 다음 항목을 순서대로 기록한다.

- [ ] 선박 한 대가 자동 생성되고 첫 로컬 플레이어가 possess한다.
- [ ] 선박이 200 x 100 x 100 cm 기본 cube이며 physics simulation 없이 수면에 안정적으로 놓인다.
- [ ] 카메라에서 선체 전체와 진행 방향이 보이고 기존 blocking geometry가 있으면 카메라가 관통하지 않는다.
- [ ] W는 전진, S는 후진, D는 우현, A는 좌현으로 동작한다.
- [ ] W와 S 또는 A와 D를 동시에 누르면 해당 축 입력이 0이다.
- [ ] W 또는 D를 누른 채 viewport focus를 옮겼다가 돌아오면 throttle과 steer가 0이며 남은 추력과 조향이 없다.
- [ ] PIE eject 또는 다른 Pawn possession 뒤 입력이 0이고 다시 possess해도 이전 키 값이 재생되지 않는다.
- [ ] 정지 상태에서 A 또는 D만 눌러도 회전하지 않는다.
- [ ] 전진과 후진 전환 때 순간이동과 speed 부호 고착이 없다.
- [ ] 정지에서 W를 유지하면 약 4초에 180 cm/s, 이후 약 200 cm/s에 수렴한다.
- [ ] 약 200 cm/s에서 W를 놓으면 약 400 cm 뒤 5 cm/s에서 0으로 정지한다.
- [ ] 최고속도와 최대 steer 원 궤적 반경이 약 250 cm다.
- [ ] t.MaxFPS 15, 30, 60, 120 각각에서 자동화 FPS 표의 절대값과 120 FPS 대비 허용오차를 만족한다.
- [ ] 파도 위 Z, pitch, roll이 부드럽고 횡경사에서도 world XY yaw가 흔들리지 않는다.
- [ ] 파도 Ocean은 ValidWaves, 무파도 유효 Ocean은 ValidNoWaves이며 fallback 없이 수면을 따른다.
- [ ] Ocean query 실패 시 Excluded, QueryInvalid 또는 ComponentInvalid가 표시되고 transform은 유한하다.
- [ ] frame hitch에도 한 tick substep 8 이하, dropped DeltaTime 표시, 다음 tick catch-up 급가속 없음이 확인된다.
- [ ] 기존 blocking geometry가 있으면 관통 없이 speed 0이며 후진으로 빠져나온다. 없으면 자동화 transient world 결과를 사용하고 벽을 추가하지 않는다.
- [ ] 화면에 입력, speed, acceleration, yaw rate, substep, water, hit와 parameter 상태가 정상 frame과 Water fallback/error frame 모두 frame당 정확히 한 번 갱신된다.
- [ ] Output Log에 매 프레임 warning, ensure, access violation이 없다.

- [ ] **Step 7: 최종 범위와 이력 확인**

~~~powershell
$AllowedFiles = @(
    "docs/superpowers/specs/2026-08-08-ship-movement-model-design.md",
    "docs/superpowers/plans/2026-08-08-ship-movement-model.md",
    "ShipAutonomySim/Config/DefaultInput.ini",
    "ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h",
    "ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp",
    "ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp",
    "ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp",
    "ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp",
    "ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp",
    "ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h",
    "ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h",
    "ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h"
)
$ChangedFiles = @(git diff 76461c18681998d8d8e954b1e0fddf79d874ffeb..HEAD --name-only)
$UnexpectedFiles = @($ChangedFiles | Where-Object { $_ -notin $AllowedFiles })
if ($UnexpectedFiles.Count -ne 0) {
    throw "No-Go: out-of-scope files: $($UnexpectedFiles -join ', ')"
}

$TestSourcePath = "$RepoRoot/ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp"
$TestSource = Get-Content -LiteralPath $TestSourcePath -Raw
$GuardOpenCount = [regex]::Matches($TestSource, '(?m)^#if WITH_DEV_AUTOMATION_TESTS\s*$').Count
$GuardCloseCount = [regex]::Matches($TestSource, '(?m)^#endif // WITH_DEV_AUTOMATION_TESTS\s*$').Count
$MacroCount = [regex]::Matches($TestSource, '(?m)^IMPLEMENT_SIMPLE_AUTOMATION_TEST\(').Count
if (!$TestSource.TrimStart().StartsWith("#if WITH_DEV_AUTOMATION_TESTS") -or
    !$TestSource.TrimEnd().EndsWith("#endif // WITH_DEV_AUTOMATION_TESTS") -or
    $GuardOpenCount -ne 1 -or $GuardCloseCount -ne 1 -or $MacroCount -ne 12) {
    throw "No-Go: test translation unit guard or macro count mismatch open=$GuardOpenCount close=$GuardCloseCount tests=$MacroCount"
}

git log --format="%h %s%n%b" 76461c18681998d8d8e954b1e0fddf79d874ffeb..HEAD
$FinalStatus = @(git status --porcelain=v1 --untracked-files=all)
if ($FinalStatus.Count -ne 0) { throw "No-Go: final worktree is not clean" }
git status --short --branch
~~~

Expected: 제품 변경은 File Structure의 생성 및 수정 목록만 포함하고 Navigator, CourseBuilder, ShipCapture, SETUP.md, DefaultEngine.ini, uproject, map과 asset은 없다. 모든 구현 커밋은 허용 접두사와 한글 명사형 제목, 세 본문 제목을 가진다. 최종 worktree는 clean이다.

## Plan Author Self-Review

- [x] 설계의 목표, 수식, 안전 경계, Water 다섯 상태, input lifecycle, PIE 및 FPS 합격표를 Task와 검증에 대응시킴
- [x] Task 간 interface 이름과 타입을 Public and Private Interfaces에 고정함
- [x] 5개 Task와 고유 Automation Test 12개를 유지하고 Task 1부터 각 commit 시점에 test translation unit의 opening 및 closing guard 각 1개와 누적 macro 4, 6, 8, 11, 12개를 강제함
- [x] GameMode BeginPlay는 정상 world lifecycle에서 한 번만 실행하고 idempotent helper만 반복 호출하도록 분리함
- [x] LocalPlayer, 실제 Enhanced subsystem과 component, InputKey, PlayerTick, FlushPressedKeys, UnPossessed와 EndPlay 경로를 test code로 고정함
- [x] `bForceImmediately=true`인 RequestRebuildControlMappings를 첫 InputKey 전에 호출하고 W/A가 비영 값을 만든 뒤에만 flush 결과를 검사함
- [x] Completed와 Canceled release handler 모두 manual-active guard를 거치며 늦은 event 뒤 autopilot setter 값 보존을 단언함
- [x] runtime source 전체 transform mutator denylist를 유지하고 Task 3의 ownership count 1, 0, 0을 Task 4 RED에서 1, 1, 1로 전환해 미래 child setup을 앞선 GREEN이 요구하지 않도록 고정함
- [x] 정상, Water fallback과 오류 tick 모두 scope-exit에서 debug를 frame당 정확히 한 번 호출함
- [x] 모든 GREEN gate가 build exit 뒤 발견 수, Success 수, failure/error 0과 clean exit를 강제함
- [x] final build는 project 내부 ignored UBT 산출물만 -Clean한 뒤 compile 및 link action을 강제함
- [x] 제품 변경 경로와 변경 금지 경로를 분리함
- [x] 4단계 Navigator, 코스와 벽 및 5단계 Capture가 구현 Task에 없음을 확인함
- [x] 외부 의존성, 에셋, 새 모듈이 없음을 확인함
- [x] 예상 밖 tracked change를 복구하지 않는 No-Go와 no-write MainLevel 명령을 포함함
- [x] 미정 표현, 타입 및 시그니처, 파일 경로, code fence, Task 선후관계와 3단계 범위를 다시 검사하고 다른 미래 Task 산출물을 앞선 GREEN이 요구하지 않음을 확인함
