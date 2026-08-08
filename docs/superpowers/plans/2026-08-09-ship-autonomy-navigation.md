# Stage 4 선박 자율주행 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stage 3 선박 이동의 transform 단일 소유권을 유지하면서 런타임 항로 생성, 순수 유도 계산, 입력 전환, 자율주행 제어, 종료 판정, 실제 MainLevel 11 case 검증을 Stage 4 경계 안에 추가한다.

**Architecture:** `ACourseBuilder`가 월드의 course actor와 정확한 3점 path를 생성하고, 순수 `ShipNavigationSimulation` 계층이 option, progress, lookahead, heading, throttle, stopping distance, hull gap, terminal priority를 계산한다. `UShipNavigator`는 순수 결과를 `UShipMovement::SetThrottle`과 `SetSteer`에만 전달하며, `UShipMovement`만 swept transform을 적용한다. `ASimGameMode`가 option 검증, spawn, tick 순서, run latch, terminal state와 logging을 소유하고 game-context Automation이 매 case fresh world를 관찰한다.

**Tech Stack:** Unreal Engine 5.5.4, C++20 Unreal 모듈, `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `Water`, Unreal Automation Framework, PowerShell, Git

## Global Constraints

- 구현 branch는 `feat/ship-autonomy-navigation`이며 branch 이름을 새로 만들거나 바꾸지 않는다.
- 이 계획은 작성 시점의 구현, build, Unreal Editor, Automation 실행 결과를 주장하지 않는다. 모든 RED, GREEN, 회귀 결과는 구현자가 해당 단계에서 새로 얻어야 한다.
- Stage 3의 12개 Automation test 이름과 통과 계약을 보존한다.
- `UShipMovement`만 actor transform을 쓰며 `UShipNavigator`, `ACourseBuilder`, `ASimGameMode`는 운항 중 선박 transform API, `TeleportTo`, teleport flag를 호출하지 않는다.
- `UShipNavigator`는 `UShipMovement::SetThrottle(float)`과 `SetSteer(float)`만 제어 출력으로 사용한다. reverse thrust와 lateral slip을 추가하지 않는다.
- MainLevel Play의 `ASimGameMode::BeginPlay`는 spawn과 possession 직후 같은 call stack에서 `EnterAutonomy`를 호출하며 첫 gameplay tick 전에 manual mapping을 제거한다.
- Stage 4 정상 run에서 non-zero `SetThrottle`과 `SetSteer` command를 쓰는 주체는 Navigator뿐이다. `EnterAutonomy`, terminal, runtime error 경로는 safety zero만 쓸 수 있다.
- keyboard, 늦게 도착한 input `Completed`와 `Canceled` event는 autonomy 전환 뒤 Navigator command를 덮지 못한다. W, S, A, D를 포함한 직접 조작은 Stage 4 정상 run에 효과가 없다.
- manual/autonomy toggle key, console command, UI, `ExitAutonomy` API를 만들지 않는다.
- Stage 3 manual input 구현을 삭제하지 않고 기존 회귀를 보존한다. 다만 Stage 4 final run path에서는 mapping이 활성 상태로 남지 않는다.
- map, config, asset, capture, web viewer, CSV 결과 파일을 수정하거나 생성하지 않는다.
- PCG, Niagara, 환경 장식, 화면 capture, web viewer 연동은 범위 밖이다.
- 각 Task는 실패 test, RED, 최소 구현, GREEN, 회귀, Stage 범위 확인, commit 순서를 지킨다.
- 각 checkbox는 약 2분에서 5분 작업 단위다.
- test 명령의 `Automation` prefix는 한 번만 쓰고 `SoftQuit`을 마지막 command로 둔다.
- Editor의 `AutomationOpenMap`에는 URL option이 붙은 asset string을 전달하지 않는다.

## 시작 게이트

구현 세션 시작 전 아래 네 조건을 한 번에 확인한다. 하나라도 다르면 file, index, ref를 바꾸지 않고 No-Go로 종료한다.

```powershell
$ErrorActionPreference = 'Stop'
$ExpectedBranch = 'feat/ship-autonomy-navigation'
$ExpectedHead = '54273859e55b1f302c0c3f1626be7de6a0cc6f9d'
$ActualBranch = git branch --show-current
$ActualHead = git rev-parse HEAD
$Porcelain = @(git status --porcelain=v1)
$EditorCount = @(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count
if ($ActualBranch -ne $ExpectedBranch -or $ActualHead -ne $ExpectedHead -or $Porcelain.Count -ne 0 -or $EditorCount -ne 0) {
    throw "No-Go branch=$ActualBranch head=$ActualHead changes=$($Porcelain.Count) editors=$EditorCount"
}
```

예상 출력 조건은 branch `feat/ship-autonomy-navigation`, HEAD `54273859e55b1f302c0c3f1626be7de6a0cc6f9d`, 변경 0, UnrealEditor 계열 process 0이다. 최초 Task commit 뒤에는 고정 HEAD 비교 대신 직전 Task commit SHA를 기록하고 branch, clean, process 조건을 계속 확인한다.

## 변경 파일 구조 고정

아래 목록 밖의 파일은 변경하지 않는다. `ShipAutonomySim.Build.cs`는 현재 의존성으로 충분하므로 확인만 하고 수정하지 않는다.

```text
Create
ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h
ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h
ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp
ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp

Modify
README.md
ShipAutonomySim/AGENTS.md
ShipAutonomySim/SETUP.md
ShipAutonomySim/Source/ShipAutonomySim/Public/CourseBuilder.h
ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp
ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigator.h
ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp
ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h
ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp
ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h
ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp
ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h
ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp
ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp

Test only, no modification expected
ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs
ShipAutonomySim/Content/Maps/MainLevel.umap
ShipAutonomySim/Config/DefaultEngine.ini
ShipAutonomySim/Config/DefaultGame.ini
ShipAutonomySim/Config/DefaultInput.ini
```

새 include는 기존 module dependency에 모두 속한다.

| Include 또는 API | 제공 module | Build.cs 판단 |
|---|---|---|
| `CoreMinimal.h`, `FRandomStream`, math와 containers | `Core` | 기존 public dependency 재사용 |
| actor, component, `UWorld`, `UGameplayStatics`, `ATargetPoint`, `AStaticMeshActor`, `UStaticMeshComponent`, Automation | `CoreUObject`, `Engine` | 기존 public dependency 재사용 |
| manual input 제거 경계 | `InputCore`, `EnhancedInput` | 기존 private dependency 재사용 |
| water surface query | `Water` | 기존 private dependency 재사용 |

## 고정 C++ 경계

### Public navigation types

`ShipNavigationTypes.h`가 run, setup, runtime error type의 단일 소유자다. terminal enum에는 네 값만 둔다.

```cpp
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
```

### Movement와 Pawn boundary

```cpp
// ShipMovement.h
double GetSignedSpeedCmPerSecond() const;
bool ConsumeBlockingHit(FHitResult& OutHit);

// ShipPawn.h
bool EnterAutonomy(
    const TArray<FVector>& WorldPath,
    AStaticMeshActor* ActualWall,
    ASimGameMode* RunOwner);
```

`GetSignedSpeedCmPerSecond`는 저장된 signed speed의 read-only 복사만 반환한다. `ConsumeBlockingHit`는 마지막 swept blocking `FHitResult`의 actor와 component identity를 보존하고 소비 뒤에만 pending flag를 지운다. `EnterAutonomy`는 manual latch를 false로 만들고 mapping을 제거한 뒤 두 입력을 0으로 만들고 Navigator를 configure하고 enable하는 유일한 전환 경계다.

`EnterAutonomy`는 `bManualInputActive=false`를 먼저 latch하고 manual mapping을 제거한 뒤 throttle과 steer의 남은 값을 0으로 만든다. 모든 manual axis, `Triggered`, `Completed`, `Canceled` handler는 이 latch가 false면 return한다. 따라서 이미 input queue에 들어온 release event도 Navigator가 이후 쓴 command를 0으로 덮지 못한다. autonomy를 다시 manual로 되돌리는 public 경계는 추가하지 않는다.

### CourseBuilder boundary

```cpp
struct FShipCourseBuildResult
{
    TArray<FVector> WorldPath;
    ATargetPoint* StartTarget = nullptr;
    ATargetPoint* EndTarget = nullptr;
    AStaticMeshActor* WallActor = nullptr;
    double SlideCm = 0.0;
    double WaterSurfaceZCm = 0.0;
};

void SetForcedSlideCm(double InSlideCm);
void ClearForcedSlide();
bool BuildRuntimeCourse(
    FShipCourseBuildResult& OutResult,
    EShipSetupFailure& OutFailure);

ATargetPoint* GetStartTarget() const;
ATargetPoint* GetEndTarget() const;
AStaticMeshActor* GetWallActor() const;
const TArray<FVector>& GetWorldPath() const;
double GetResolvedSlideCm() const;
```

`SetForcedSlideCm`는 검증된 `[-500, 500]` 값만 받는다. absent option만 `ClearForcedSlide` 경로로 들어가며 이때 `FRandomStream RandomStream(RandomSeed)`의 `FRandRange(-500.0, 500.0)`을 사용한다. invalid present option은 builder를 호출하지 않는다.

### Navigator boundary

```cpp
bool Configure(
    const TArray<FVector>& InWorldPath,
    UShipMovement* InMovement,
    AStaticMeshActor* InActualWall,
    ASimGameMode* InRunOwner);
void SetNavigationEnabled(bool bEnabled);
bool IsNavigationEnabled() const;
```

`Configure` 성공 시 `InMovement->AddTickPrerequisiteComponent(this)`를 호출해 Navigator tick이 Movement tick보다 먼저 끝나게 한다. Navigator는 `SetThrottle`, `SetSteer`, read-only speed 외의 Movement 내부 상태와 actor transform mutator를 사용하지 않는다.

### GameMode boundary

```cpp
virtual void InitGame(
    const FString& MapName,
    const FString& Options,
    FString& ErrorMessage) override;

void ReportRuntimeCalculationError(EShipRuntimeCalculationError Error);

EShipRunResult GetRunResult() const;
EShipSetupFailure GetSetupFailure() const;
bool HasRuntimeCalculationError() const;
EShipRuntimeCalculationError GetRuntimeCalculationError() const;
int32 GetRuntimeCalculationErrorCount() const;
double GetResolvedSlideCm() const;
double GetElapsedRunSeconds() const;
AShipPawn* GetRunShip() const;
ACourseBuilder* GetCourseBuilder() const;
AActor* GetCollisionActor() const;
UPrimitiveComponent* GetCollisionComponent() const;
```

`ASimGameMode::AddTickPrerequisiteComponent(ShipMovement)`로 Movement 뒤에 GameMode terminal 평가를 둔다. 최종 tick 순서는 Navigator, Movement, GameMode다.

## UE 5.5.4 API와 lifecycle 근거

기억이 아니라 설치된 `C:\Program Files\Epic Games\UE_5.5\Engine`의 5.5.4 source에서 확인한 근거다.

| 계약 | 실제 signature 또는 동작 | 설치 경로 |
|---|---|---|
| option lifecycle | `virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage);`이며 component 초기화 전 호출 | `Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h:58` |
| option 분류 | `HasOption(FString, const FString&)`와 `ParseOption(FString, const FString&)`를 함께 써야 absent와 present-empty 구분 가능 | `Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h:1522`, `:1531`; `Private/GameplayStatics.cpp:3439` |
| fresh travel | `OpenLevel(const UObject*, FName, bool bAbsolute = true, FString Options = FString())`; 내부에서 level name 뒤에 `?Options`를 붙여 client travel | `Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h:341`; `Private/GameplayStatics.cpp:978` |
| spawn | `UWorld::SpawnActor` overload와 `SpawnActorDeferred<T>(UClass*, const FTransform&, AActor*, APawn*, ESpawnActorCollisionHandlingMethod, ESpawnActorScaleMethod)` | `Engine/Source/Runtime/Engine/Classes/Engine/World.h:3355`, `:3441` |
| deferred finish | `void FinishSpawning(const FTransform&, bool, const FComponentInstanceDataCache*, ESpawnActorScaleMethod)` | `Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h:3024` |
| mesh load와 설정 | `LoadObject<T>(UObject*, const TCHAR*, const TCHAR*, uint32, UPackageMap*, const FLinkerInstancingContext*)`; `virtual bool SetStaticMesh(UStaticMesh*)` | `Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h:1989`; `Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h:414` |
| collision | `SetCollisionEnabled`, `SetCollisionObjectType`, `SetCollisionResponseToAllChannels`, `SetCollisionResponseToChannel` | `Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h:1892`, `:1913`, `:2814`, `:2822` |
| random | `FRandomStream(int32)`, `Initialize(int32)`, `GetInitialSeed()`, `FRandRange(FVector::FReal, FVector::FReal)` | `Engine/Source/Runtime/Core/Public/Math/RandomStream.h:40`, `:63`, `:209` |
| water | `UWaterSubsystem::GetWaterSubsystem(const UWorld*)`, `TWeakObjectPtr<UWaterBodyComponent> GetOceanBodyComponent()`, `UWaterBodyComponent::QueryWaterInfoClosestToWorldLocation` | `Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterSubsystem.h:99`, `:109`; `WaterBodyComponent.h:306` |
| debug | `DrawDebugLine`, `DrawDebugPoint`, `DrawDebugBox` | `Engine/Source/Runtime/Engine/Public/DrawDebugHelpers.h:22`, `:24`, `:30` |
| tick ordering | `UActorComponent::AddTickPrerequisiteActor(AActor*)`, `AddTickPrerequisiteComponent(UActorComponent*)` | `Engine/Source/Runtime/Engine/Classes/Components/ActorComponent.h:1135`, `:1139` |
| latent command | `IAutomationLatentCommand::Update()`와 `ADD_LATENT_AUTOMATION_COMMAND` | `Engine/Source/Runtime/Core/Public/Misc/AutomationTest.h` |
| Automation map boundary | `bool AutomationOpenMap(const FString& MapName, bool bForceReload=false)`가 Editor에서 full string을 delegate로 넘기고 `AutomationLoadMap`이 `FEditorFileUtils::LoadMap(*MapName, ...)` 호출 | `Engine/Source/Runtime/Engine/Public/Tests/AutomationCommon.h:213`; `Private/Tests/AutomationCommon.cpp:759`; `Editor/UnrealEd/Private/EditorEngine.cpp:6084`, `:6145` |

선택한 호출의 exact declarations는 다음과 같다.

```cpp
static FString UGameplayStatics::ParseOption(FString Options, const FString& Key);
static bool UGameplayStatics::HasOption(FString Options, const FString& InKey);
static void UGameplayStatics::OpenLevel(
    const UObject* WorldContextObject,
    FName LevelName,
    bool bAbsolute = true,
    FString Options = FString(TEXT("")));

static UWaterSubsystem* UWaterSubsystem::GetWaterSubsystem(const UWorld* InWorld);
TWeakObjectPtr<UWaterBodyComponent> UWaterSubsystem::GetOceanBodyComponent();
virtual FWaterBodyQueryResult UWaterBodyComponent::QueryWaterInfoClosestToWorldLocation(
    const FVector& InWorldLocation,
    EWaterBodyQueryFlags InQueryFlags,
    const TOptional<float>& InSplineInputKey = TOptional<float>()) const;

virtual void UActorComponent::AddTickPrerequisiteComponent(UActorComponent* PrerequisiteComponent);
virtual void AActor::AddTickPrerequisiteComponent(UActorComponent* PrerequisiteComponent);
virtual bool IAutomationLatentCommand::Update() = 0;
```

```cpp
template<class T>
T* UWorld::SpawnActorDeferred(
    UClass* Class,
    const FTransform& Transform,
    AActor* Owner = nullptr,
    APawn* Instigator = nullptr,
    ESpawnActorCollisionHandlingMethod CollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::Undefined,
    ESpawnActorScaleMethod TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot);

void AActor::FinishSpawning(
    const FTransform& Transform,
    bool bIsDefaultTransform = false,
    const FComponentInstanceDataCache* InstanceDataCache = nullptr,
    ESpawnActorScaleMethod TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale);

template<class T>
T* LoadObject(
    UObject* Outer,
    const TCHAR* Name,
    const TCHAR* Filename = nullptr,
    uint32 LoadFlags = LOAD_None,
    UPackageMap* Sandbox = nullptr,
    const FLinkerInstancingContext* InstancingContext = nullptr);

virtual bool UStaticMeshComponent::SetStaticMesh(UStaticMesh* NewMesh);
virtual void UPrimitiveComponent::SetCollisionEnabled(ECollisionEnabled::Type NewType);
virtual void UPrimitiveComponent::SetCollisionObjectType(ECollisionChannel Channel);
virtual void UPrimitiveComponent::SetCollisionResponseToAllChannels(ECollisionResponse NewResponse);
virtual void UPrimitiveComponent::SetCollisionResponseToChannel(
    ECollisionChannel Channel,
    ECollisionResponse NewResponse);
```

```cpp
void DrawDebugLine(
    const UWorld* InWorld,
    const FVector& LineStart,
    const FVector& LineEnd,
    const FColor& Color,
    bool bPersistentLines = false,
    float LifeTime = -1.0f,
    uint8 DepthPriority = 0,
    float Thickness = 0.0f);
void DrawDebugPoint(
    const UWorld* InWorld,
    const FVector& Position,
    float Size,
    const FColor& PointColor,
    bool bPersistentLines = false,
    float LifeTime = -1.0f,
    uint8 DepthPriority = 0);
void DrawDebugBox(
    const UWorld* InWorld,
    const FVector& Center,
    const FVector& Extent,
    const FQuat& Rotation,
    const FColor& Color,
    bool bPersistentLines = false,
    float LifeTime = -1.0f,
    uint8 DepthPriority = 0,
    float Thickness = 0.0f);
```

Editor의 `AutomationOpenMap` 경로는 받은 문자열을 `UEditorEngine::AutomationLoadMap`과 `FEditorFileUtils::LoadMap`에 전달한다. 따라서 option이 포함된 `/Game/Maps/MainLevel?Stage4Slide=-500`를 Editor map asset 이름으로 넘기지 않는다. actual-world test는 `-game` 최초 launch URL에 첫 option을 넣고, 이후 case는 `UGameplayStatics::OpenLevel`로 travel한다.

## 고유 Automation test 목록

기존 Stage 3 이름 12개는 그대로 유지한다.

1. `ShipAutonomySim.ShipMovement.Motion.Dynamics`
2. `ShipAutonomySim.ShipMovement.Motion.Targets`
3. `ShipAutonomySim.ShipMovement.Motion.ParameterValidation`
4. `ShipAutonomySim.ShipMovement.Motion.FrameRatesAndHitch`
5. `ShipAutonomySim.ShipMovement.Water.Classification`
6. `ShipAutonomySim.ShipMovement.Water.SurfaceBasis`
7. `ShipAutonomySim.ShipMovement.Runtime.FallbackAndBlockingHit`
8. `ShipAutonomySim.ShipMovement.Runtime.TransformOwnership`
9. `ShipAutonomySim.ShipMovement.Pawn.Construction`
10. `ShipAutonomySim.ShipMovement.Pawn.InputLifecycle`
11. `ShipAutonomySim.ShipMovement.Pawn.FocusLossAndAutopilotGuard`
12. `ShipAutonomySim.ShipMovement.GameMode.Bootstrap`

Stage 4가 추가할 이름은 다음 20개다.

1. `ShipAutonomySim.ShipNavigation.Unit.Options.Classification`
2. `ShipAutonomySim.ShipNavigation.Unit.Course.Geometry`
3. `ShipAutonomySim.ShipNavigation.Unit.Course.FrameTransform`
4. `ShipAutonomySim.ShipNavigation.Unit.Guidance.ProgressMonotonicity`
5. `ShipAutonomySim.ShipNavigation.Unit.Guidance.SegmentTransition`
6. `ShipAutonomySim.ShipNavigation.Unit.Guidance.Lookahead`
7. `ShipAutonomySim.ShipNavigation.Unit.Guidance.SteeringAndThrottle`
8. `ShipAutonomySim.ShipNavigation.Unit.Guidance.DynamicStoppingDistance`
9. `ShipAutonomySim.ShipNavigation.Unit.Geometry.ConvexHullGap`
10. `ShipAutonomySim.ShipNavigation.Unit.Terminal.Priority`
11. `ShipAutonomySim.ShipNavigation.Unit.Terminal.RuntimeCalculationErrorLatch`
12. `ShipAutonomySim.ShipNavigation.Unit.Movement.BlockingHitIdentity`
13. `ShipAutonomySim.ShipNavigation.Unit.Pawn.AutoStartRemovesManualMapping`
14. `ShipAutonomySim.ShipNavigation.Unit.Pawn.StaleManualEventsIgnored`
15. `ShipAutonomySim.ShipNavigation.Unit.Pawn.DirectManualInputIgnoredDuringAutonomy`
16. `ShipAutonomySim.ShipNavigation.Unit.Course.RuntimeSetupFailure`
17. `ShipAutonomySim.ShipNavigation.Unit.Navigator.ControlCoastAndError`
18. `ShipAutonomySim.ShipNavigation.Unit.GameMode.OptionBootstrap`
19. `ShipAutonomySim.ShipNavigation.Unit.GameMode.TerminalRuntimeError`
20. `ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep`

최종 발견 수는 Editor context 31개, game Client context 1개, 두 shard의 고유 합계 32개다.

---

### Task 1: Stage 4 이전 상태를 부정하는 문서 경계 정리

#### Files
- Modify: `ShipAutonomySim/AGENTS.md`의 project scope와 forbidden work 문단
- Modify: `ShipAutonomySim/SETUP.md`의 현재 구현 상태와 MainLevel 준비 문단
- Modify: `README.md`의 repository 구성과 ShipAutonomySim 상태 문단
- Test: 위 세 Markdown 파일의 Stage 3, Stage 4 책임 진술

#### Consumes

현재 Stage 3 구현 파일, `docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md`, 이 계획의 변경 파일 구조

#### Produces

코드보다 오래된 skeleton, MainLevel 부재, 이동 미구현 진술을 제거한 최소 문서 patch. 구현 완료를 미리 주장하지 않고 Stage 4 구현 예정 경계를 명시한다.

- [ ] 2분: 세 문서에서 `skeleton`, `MainLevel`, `movement`, `navigation`, `spawn`, `구현` 관련 줄만 `rg -n`으로 다시 식별한다.
- [ ] 3분: RED 문서 검사를 작성하거나 일회성 PowerShell assertion으로 현재 문서가 MainLevel 부재와 이동 미구현을 진술해 실패하는 것을 확인한다. 예상 실패 표식은 `STALE_STAGE_BOUNDARY` 세부 항목이며 test count는 문서 assertion 3개 중 1개 이상 실패다.
- [ ] 4분: `ShipAutonomySim/AGENTS.md`에서 Stage 3 이동은 현재 책임, Stage 4 자율주행은 이번 구현 범위, PCG와 capture와 viewer는 비범위라는 세 문장만 교체한다.
- [ ] 4분: `ShipAutonomySim/SETUP.md`에서 MainLevel 생성 안내를 현재 MainLevel 사용 안내로 바꾸고 Stage 3 수동 이동과 Stage 4 자동 운항의 책임을 짧게 구분한다.
- [ ] 3분: 루트 `README.md`에 static viewer와 별도의 `ShipAutonomySim` Unreal 과제 경로 및 현재 Stage 경계를 한 문단으로 추가한다.
- [ ] 2분: 같은 문서 assertion을 GREEN으로 다시 실행한다. 예상 결과는 3개 assertion 통과와 `STALE_STAGE_BOUNDARY` 0회다.
- [ ] 3분: `git diff -- README.md ShipAutonomySim/AGENTS.md ShipAutonomySim/SETUP.md`로 문서 전체 재작성, 구현 완료 주장, 범위 확대가 없는지 확인한다.
- [ ] 2분: `git diff --check`와 `git status --short`를 실행하고 변경 경로가 세 문서뿐인지 확인한다.
- [ ] 3분: 아래 제목과 세 구획 본문으로 commit한다.

```powershell
git add -- README.md ShipAutonomySim/AGENTS.md ShipAutonomySim/SETUP.md
git commit -m "docs: Stage 4 문서 경계 정합성 정리" `
  -m "변경 이유`n현재 구현과 어긋난 skeleton 및 MainLevel 안내 제거" `
  -m "핵심 변경`nStage 3 이동 책임과 Stage 4 자율주행 범위의 최소 명시" `
  -m "검증 방법`n문서 assertion, diff 범위 검사, git diff --check"
```

### Task 2: 순수 option 분류와 course geometry 경계 추가

#### Files
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h`
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h`
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp`
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Options.Classification`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Course.Geometry`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Course.FrameTransform`

#### Consumes

`UGameplayStatics::HasOption(FString, const FString&)`의 bool 결과, `UGameplayStatics::ParseOption(FString, const FString&)`의 raw value, `FTransform` course frame, water reference Z, slide cm

```cpp
FStage4SlideOptionResult ClassifySlideOption(
    bool bHasOption,
    const FString& RawValue);
FShipCourseDefinition BuildCourseDefinition(
    const FTransform& CourseFrame,
    double WaterSurfaceZCm,
    double SlideCm);
```

#### Produces

option state와 setup failure mapping, start와 wall과 waypoint와 end world coordinate, 정확한 3점 path

```cpp
enum class EStage4SlideOptionState : uint8
{
    Absent,
    Valid,
    Empty,
    Malformed,
    NonFinite,
    OutOfRange
};

struct FStage4SlideOptionResult
{
    EStage4SlideOptionState State = EStage4SlideOptionState::Absent;
    double SlideCm = 0.0;
    EShipSetupFailure Failure = EShipSetupFailure::None;
};

struct FShipCourseDefinition
{
    FVector StartWorld = FVector::ZeroVector;
    FVector WallWorld = FVector::ZeroVector;
    FVector WaypointWorld = FVector::ZeroVector;
    FVector EndWorld = FVector::ZeroVector;
    FVector WallScale = FVector(1.0, 10.0, 5.0);
    TArray<FVector> WorldPath;
};
```

- [ ] 4분: 세 test macro를 `EditorContext | ProductFilter`로 추가하고 absent, empty, junk, conversion failure, `NaN`, `Inf`, range 밖, 경계값, valid 값을 table-driven input으로 작성한다.
- [ ] 3분: option RED를 실행한다. Stage 3 12개와 새 unit 3개 중 compile 또는 새 test 실패가 예상되며 실패 이름은 `Options.Classification`, `Course.Geometry`, `Course.FrameTransform`다. 구현 전 예상 full count는 15개다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject' `
  -Unattended -NoSplash -NullRHI -NoAudio -NoPause -NoP4 -nowrite -TestExit='Automation Test Queue Empty' `
  -ExecCmds='Automation RunTests ShipAutonomySim.ShipNavigation.Unit;SoftQuit;' `
  -Log='C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\Stage4-Task2-RED.log'
```

- [ ] 4분: `ShipNavigationTypes.h`에 고정한 네 terminal 값, setup failure, runtime error enum을 정확히 추가한다.
- [ ] 4분: `ShipNavigationSimulation.h`에 option 결과와 course parameter와 course definition type 및 위 두 함수 declaration을 추가한다.
- [ ] 5분: strict decimal parser를 구현한다. whitespace trim 뒤 빈 문자열을 `Empty`, `NaN`과 `Inf` 계열을 `NonFinite`, decimal 또는 exponent grammar 밖을 `Malformed`, `LexTryParseString` 실패를 `Malformed`, `FMath::IsFinite` false를 `NonFinite`, `[-500, 500]` 밖을 `OutOfRange`로 반환한다.

```cpp
FStage4SlideOptionResult ClassifySlideOption(bool bHasOption, const FString& RawValue)
{
    if (!bHasOption)
    {
        return {EStage4SlideOptionState::Absent, 0.0, EShipSetupFailure::None};
    }

    FString Value = RawValue;
    Value.TrimStartAndEndInline();
    if (Value.IsEmpty())
    {
        return {EStage4SlideOptionState::Empty, 0.0, EShipSetupFailure::SlideOptionEmpty};
    }

    static const FRegexPattern DecimalPattern(TEXT("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    FRegexMatcher Matcher(DecimalPattern, Value);
    if (!Matcher.FindNext())
    {
        const FString Lower = Value.ToLower();
        const bool bNamedNonFinite = Lower.Contains(TEXT("nan")) || Lower.Contains(TEXT("inf"));
        return bNamedNonFinite
            ? FStage4SlideOptionResult{EStage4SlideOptionState::NonFinite, 0.0, EShipSetupFailure::SlideOptionNonFinite}
            : FStage4SlideOptionResult{EStage4SlideOptionState::Malformed, 0.0, EShipSetupFailure::SlideOptionMalformed};
    }

    double Parsed = 0.0;
    if (!LexTryParseString(Parsed, *Value))
    {
        return {EStage4SlideOptionState::Malformed, 0.0, EShipSetupFailure::SlideOptionMalformed};
    }
    if (!FMath::IsFinite(Parsed))
    {
        return {EStage4SlideOptionState::NonFinite, 0.0, EShipSetupFailure::SlideOptionNonFinite};
    }
    if (Parsed < -500.0 || Parsed > 500.0)
    {
        return {EStage4SlideOptionState::OutOfRange, Parsed, EShipSetupFailure::SlideOptionOutOfRange};
    }
    return {EStage4SlideOptionState::Valid, Parsed, EShipSetupFailure::None};
}
```

- [ ] 5분: course local coordinate를 start `(0,0)`, end `(2000,0)`, wall `(1000, slide)`, waypoint를 `slide >= 0 ? (1000, slide - 750) : (1000, slide + 750)`로 계산한다.
- [ ] 4분: actor transform에서 world XY translation과 yaw만 course frame으로 만들고 water query의 reference Z를 start와 waypoint와 end Z로 사용한다. wall center Z만 reference Z보다 150 cm 높이고 wall scale은 `(1,10,5)`로 고정한다.

```cpp
const FVector Origin = CourseFrame.GetLocation();
const double Yaw = CourseFrame.Rotator().Yaw;
const FTransform FlatFrame(FRotator(0.0, Yaw, 0.0), FVector(Origin.X, Origin.Y, 0.0));
auto ToWorld = [&FlatFrame, WaterSurfaceZCm](double X, double Y)
{
    FVector World = FlatFrame.TransformPosition(FVector(X, Y, 0.0));
    World.Z = WaterSurfaceZCm;
    return World;
};
```

- [ ] 3분: option unit test에 `HasOption=false` random eligibility와 `HasOption=true` invalid no-fallback를 별도 assertion으로 넣는다.
- [ ] 3분: geometry test에서 slide `-500`, `0`, `500`의 waypoint 부호, 100 cm cube 기준 wall 크기 `100 x 1000 x 500`, center가 water reference surface보다 100 cm 잠기는 `WallWorld.Z = WaterSurfaceZCm + 150.0` 계약을 검증한다.
- [ ] 3분: frame test에서 translation과 yaw가 모든 XY에 적용되고 actor pitch, roll, scale이 course 좌표에 들어가지 않는지 검증한다.
- [ ] 3분: targeted unit GREEN을 실행한다. 예상 결과는 새 unit 3개 통과, 실패와 error와 ensure 0, `TEST COMPLETE. EXIT CODE: 0`이다.
- [ ] 3분: Stage 3 full prefix 회귀를 실행한다. 예상 결과는 기존 12개와 새 3개를 합친 Editor test 15개 통과다.
- [ ] 2분: `ShipAutonomySim.Build.cs` diff가 없고 course helper가 actor spawn, input, transform mutation을 포함하지 않는지 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 항로 옵션과 기하 계산 기반 추가" `
  -m "변경 이유`n런타임 actor와 분리된 Stage 4 option 및 course 계산 필요" `
  -m "핵심 변경`n엄격한 option 분류와 좌표 및 waypoint 부호 계약 구현" `
  -m "검증 방법`noption 및 course unit test와 Stage 3 회귀"
```

### Task 3: progress, lookahead, heading, throttle, dynamic stop 순수 계산 추가

#### Files
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h`의 public path progress value type
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Guidance.ProgressMonotonicity`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Guidance.SegmentTransition`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Guidance.Lookahead`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Guidance.SteeringAndThrottle`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Guidance.DynamicStoppingDistance`

#### Consumes

정확한 3점 `TArray<FVector>`, 이전 `FShipPathProgress`, ship world XY, ship forward XY, signed speed, Stage 3 `FShipMotionParameters`

```cpp
bool AdvancePathProgress(
    const TArray<FVector>& WorldPath,
    const FVector& ShipWorldLocation,
    const FShipPathProgress& Previous,
    FShipPathProgress& OutProgress);
bool FindLookaheadTarget(
    const TArray<FVector>& WorldPath,
    const FShipPathProgress& Progress,
    double LookaheadDistanceCm,
    FVector& OutTarget);
bool ComputeGuidanceCommands(
    const FVector& ShipForward,
    const FVector& ShipLocation,
    const FVector& LookaheadTarget,
    double& OutHeadingErrorDegrees,
    float& OutSteer,
    float& OutThrottle);
bool ComputeDynamicStoppingDistance(
    const FShipMotionParameters& Parameters,
    double InitialForwardSpeedCmPerSecond,
    double& OutStoppingDistanceCm);
```

#### Produces

단조 증가 progress, tick당 최대 한 segment 전이, 300 cm lookahead target, heading error와 steer, piecewise throttle, forward Euler stopping distance 또는 명시적 실패

```cpp
struct FShipPathProgress
{
    int32 ActiveSegmentIndex = 0;
    double MonotonicDistanceCm = 0.0;
};
```

`FShipPathProgress`는 `UShipNavigator` public header가 private include를 참조하지 않고 값을 보관할 수 있도록 `ShipNavigationTypes.h`에 둔다. 계산 함수 declaration과 implementation은 private simulation 파일에 둔다.

- [ ] 4분: active segment 투영이 이전 progress보다 감소하지 않는 test와 ship이 future segment에 더 가까워도 jump하지 않는 test를 먼저 추가한다.
- [ ] 3분: segment endpoint를 넘은 한 tick에 active index가 정확히 1만 증가하고 다음 segment 투영은 다음 tick에 수행되는 RED case를 추가한다.
- [ ] 4분: lookahead가 현재 progress부터 polyline을 따라 300 cm 전진하고 final endpoint에서 clamp되는 RED case를 추가한다.
- [ ] 4분: +X heading에서 +Y 쪽 target이 positive yaw와 positive steer를 만드는지, heading error `30`도에서 steer `1`, `-30`도에서 `-1`, throttle은 `20`도 이하 `1`, `60`도 이상 `0.35`, 중간 선형 보간을 검증한다.
- [ ] 4분: Stage 3 C1과 C2 parameter에서 `1/120` second forward Euler와 stop speed `5`를 사용하는 dynamic stop test를 작성하고 non-finite, 음수, 비감속 step이 실패하는 case를 추가한다.
- [ ] 3분: RED를 `ShipAutonomySim.ShipNavigation.Unit.Guidance` prefix로 실행한다. 예상 새 실패 이름은 위 5개이며 구현 전 Editor full count는 20개다.
- [ ] 5분: XY active segment projection을 구현하고 projection fraction과 누적 distance를 clamp한 뒤 `max(previous, candidate)`를 적용한다.

```cpp
const FVector& A3 = WorldPath[Previous.ActiveSegmentIndex];
const FVector& B3 = WorldPath[Previous.ActiveSegmentIndex + 1];
const FVector2D A(A3.X, A3.Y);
const FVector2D B(B3.X, B3.Y);
const FVector2D P(ShipWorldLocation.X, ShipWorldLocation.Y);
const FVector2D AB = B - A;
const double LengthSquared = AB.SizeSquared();
if (!FMath::IsFinite(LengthSquared) || LengthSquared <= UE_DOUBLE_SMALL_NUMBER)
{
    return false;
}
const double Fraction = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSquared, 0.0, 1.0);
const double SegmentLength = FMath::Sqrt(LengthSquared);
double CompletedDistance = 0.0;
for (int32 SegmentIndex = 0; SegmentIndex < Previous.ActiveSegmentIndex; ++SegmentIndex)
{
    const FVector2D SegmentStart(WorldPath[SegmentIndex].X, WorldPath[SegmentIndex].Y);
    const FVector2D SegmentEnd(WorldPath[SegmentIndex + 1].X, WorldPath[SegmentIndex + 1].Y);
    CompletedDistance += FVector2D::Distance(SegmentStart, SegmentEnd);
}
const double CandidateProgress = CompletedDistance + Fraction * SegmentLength;
OutProgress = Previous;
OutProgress.MonotonicDistanceCm = FMath::Max(Previous.MonotonicDistanceCm, CandidateProgress);
```

- [ ] 4분: `dot(ShipXY - SegmentEnd, SegmentDirection) >= 0`이면 current segment 끝 누적 길이를 progress에 반영하고 active index를 한 번만 올린다. 같은 호출에서 새 segment를 다시 투영하지 않는다.
- [ ] 5분: current progress에서 남은 segment 길이를 순서대로 소비해 300 cm lookahead를 만들고 endpoint 이후 final path point를 반환한다.
- [ ] 4분: `Atan2(cross, dot)`로 signed heading error를 계산하고 `Steer=Clamp(error/30,-1,1)`을 구현한다.
- [ ] 3분: absolute heading error에 대해 throttle piecewise curve를 구현한다.

```cpp
const double AbsError = FMath::Abs(OutHeadingErrorDegrees);
if (AbsError <= 20.0)
{
    OutThrottle = 1.0f;
}
else if (AbsError >= 60.0)
{
    OutThrottle = 0.35f;
}
else
{
    OutThrottle = static_cast<float>(FMath::Lerp(1.0, 0.35, (AbsError - 20.0) / 40.0));
}
```

- [ ] 5분: stopping simulation을 Stage 3 `AdvanceShipMotion`과 같은 C1, C2 parameter에 연결하고 `DeltaSeconds=1.0/120.0`, throttle 0, steer 0으로 speed가 `<=5`가 될 때까지 distance를 누적한다.

```cpp
if (!FMath::IsFinite(InitialForwardSpeedCmPerSecond) || InitialForwardSpeedCmPerSecond < 0.0)
{
    return false;
}
FShipMotionState State{InitialForwardSpeedCmPerSecond, 0.0};
const FShipMotionInput CoastInput = MakeShipMotionInput(0.0, 0.0);
OutStoppingDistanceCm = 0.0;
for (int32 StepIndex = 0; StepIndex < 14400; ++StepIndex)
{
    if (State.SignedSpeedCmPerSecond <= Parameters.StopSpeedThreshold)
    {
        return FMath::IsFinite(OutStoppingDistanceCm);
    }
    const FShipMotionStep Step = AdvanceShipMotion(State, CoastInput, Parameters, 1.0 / 120.0);
    if (!Step.bValid
        || !FMath::IsFinite(Step.TravelCm)
        || Step.TravelCm < 0.0
        || Step.NextState.SignedSpeedCmPerSecond >= State.SignedSpeedCmPerSecond)
    {
        return false;
    }
    OutStoppingDistanceCm += Step.TravelCm;
    State = Step.NextState;
}
return false;
```
- [ ] 4분: 최대 120초 step bound, 모든 입력과 출력 finite 검사, distance 단조 증가, speed 감소 검사를 넣고 하나라도 깨지면 false를 반환한다.
- [ ] 3분: targeted GREEN에서 새 Guidance 5개 통과, 전체 unit 8개 통과, 실패 0과 `TEST COMPLETE. EXIT CODE: 0`을 확인한다.
- [ ] 3분: Editor full prefix 회귀에서 Stage 3 12개와 Stage 4 unit 8개, 합계 20개를 확인한다.
- [ ] 2분: progress helper가 future segment 검색을 하지 않고 stopping helper가 reverse speed를 생성하지 않는지 diff로 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 자율주행 유도 계산 기반 추가" `
  -m "변경 이유`nactor lifecycle과 분리된 재현 가능한 경로 추종 계산 필요" `
  -m "핵심 변경`n단조 progress, lookahead, 조향, throttle, dynamic stop 구현" `
  -m "검증 방법`nGuidance unit test와 전체 Editor Automation 회귀"
```

### Task 4: 8-corner XY convex hull gap과 terminal priority 추가

#### Files
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h`의 public runtime error state value type
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Geometry.ConvexHullGap`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Terminal.Priority`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Terminal.RuntimeCalculationErrorLatch`

#### Consumes

ship과 wall의 local `FBox`, 각 component의 full `FTransform`, collision과 success 조건과 timeout과 runtime error booleans, 새 runtime error enum

```cpp
void TransformBoxCornersToXY(
    const FBox& LocalBox,
    const FTransform& WorldTransform,
    TArray<FVector2D>& OutWorldCorners);
bool BuildConvexHullXY(
    const TArray<FVector2D>& Points,
    TArray<FVector2D>& OutHull);
bool ComputeConvexHullGapCm(
    const FBox& FirstLocalBox,
    const FTransform& FirstWorldTransform,
    const FBox& SecondLocalBox,
    const FTransform& SecondWorldTransform,
    double& OutGapCm);
EShipRunResult SelectTerminalResult(const FShipTerminalInputs& Inputs);
bool LatchRuntimeCalculationError(
    EShipRuntimeCalculationError Error,
    FShipRuntimeErrorState& InOutState);
```

#### Produces

full world transform이 적용된 두 XY hull 사이의 non-negative gap, 겹침 시 0, `Collision > Success > Timeout > Running` 결과, 최초 error와 report count를 보존하는 latch

```cpp
struct FShipTerminalInputs
{
    bool bCollision = false;
    bool bSuccessConditions = false;
    bool bTimeout = false;
    bool bRuntimeCalculationError = false;
};

struct FShipRuntimeErrorState
{
    bool bLatched = false;
    EShipRuntimeCalculationError FirstError = EShipRuntimeCalculationError::None;
    int32 ReportCount = 0;
};
```

`FShipRuntimeErrorState`는 `ASimGameMode` public header가 private simulation header에 의존하지 않도록 `ShipNavigationTypes.h`에 둔다. `FShipTerminalInputs`는 GameMode cpp만 소비하므로 private simulation header에 둔다.

- [ ] 4분: axis-aligned separated boxes, rotated ship, scaled wall, touching edge, overlapping hull을 검증하는 convex hull gap RED table을 추가한다.
- [ ] 3분: local box 중심이나 axis-aligned world bounds만 쓰면 틀리도록 ship과 wall의 pitch, roll, yaw, non-uniform scale을 모두 포함하는 case를 넣는다.
- [ ] 3분: collision과 success와 timeout의 모든 중복 조합 8개를 검증하고 우선순위가 `Collision`, `Success`, `Timeout`, `Running` 순서임을 고정한다.
- [ ] 3분: runtime error가 success를 영구 차단하지만 collision과 timeout enum은 유지되는 latch test를 추가한다. 같은 error 반복과 다른 error 후속 보고에서 first error가 바뀌지 않고 count만 증가해야 한다.
- [ ] 3분: targeted RED를 실행한다. 예상 새 실패는 `Geometry.ConvexHullGap`, `Terminal.Priority`, `Terminal.RuntimeCalculationErrorLatch` 3개이며 Editor full 예상 발견 수는 23개다.
- [ ] 4분: local box의 Min과 Max 조합 8개를 각각 `WorldTransform.TransformPosition`한 뒤 XY로 투영한다.

```cpp
void TransformBoxCornersToXY(const FBox& LocalBox, const FTransform& WorldTransform, TArray<FVector2D>& OutWorldCorners)
{
    OutWorldCorners.Reset(8);
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
```

- [ ] 5분: points를 X와 Y로 정렬하고 중복 제거한 뒤 cross product로 lower와 upper chain을 만드는 Andrew monotone chain을 구현한다. hull point가 3개 미만이면 false다.
- [ ] 5분: 두 hull edge의 segment 교차와 한 hull point의 다른 convex polygon 포함을 먼저 검사하고 true면 gap 0을 반환한다.
- [ ] 5분: 겹치지 않으면 모든 edge pair의 segment distance 최솟값을 계산한다. 결과가 finite이며 `>=0`일 때만 true를 반환한다.
- [ ] 3분: terminal selector에서 runtime error가 있으면 success candidate만 false로 만들고 고정 우선순위를 적용한다.

```cpp
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
```

- [ ] 3분: runtime error가 `None`이면 latch를 바꾸지 않고 false, 첫 non-None이면 first error와 count 1 및 true, 후속 non-None이면 count 증가 및 false를 반환하게 구현한다.
- [ ] 3분: targeted GREEN에서 새 3개와 전체 Stage 4 unit 11개가 통과하고 failure, error, ensure가 0인지 확인한다.
- [ ] 3분: Editor full 회귀에서 Stage 3 12개와 Stage 4 unit 11개, 합계 23개를 확인한다.
- [ ] 2분: gap helper가 `FBox::TransformBy` 결과나 center distance를 사용하지 않고 정확히 local 8 corners를 full transform하는지 diff로 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 충돌 간격과 종료 판정 계산 추가" `
  -m "변경 이유`n회전 및 scale을 보존한 안전 거리와 일관된 종료 우선순위 필요" `
  -m "핵심 변경`n8-corner XY hull gap, terminal selector, runtime error latch 구현" `
  -m "검증 방법`nGeometry 및 Terminal unit test와 Editor 회귀"
```

### Task 5: Movement read-only hit identity와 Pawn EnterAutonomy 경계 추가

#### Files
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h`의 public query와 pending hit state
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp`의 swept hit 보존과 consume 구현
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h`의 Navigator component와 `EnterAutonomy`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp`의 component 생성과 안전한 manual input 해제
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp`의 Stage 4 경계와 transform ownership assertion
- Test: `ShipAutonomySim.ShipNavigation.Unit.Movement.BlockingHitIdentity`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Pawn.AutoStartRemovesManualMapping`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Pawn.StaleManualEventsIgnored`
- Test: `ShipAutonomySim.ShipNavigation.Unit.Pawn.DirectManualInputIgnoredDuringAutonomy`
- Test: 기존 `ShipAutonomySim.ShipMovement.Runtime.TransformOwnership`

#### Consumes

Stage 3 `SignedSpeedCmPerSecond`, swept movement의 blocking `FHitResult`, 기존 `DeactivateManualInput()`, 3점 world path, actual wall actor, run owner

```cpp
double UShipMovement::GetSignedSpeedCmPerSecond() const;
bool UShipMovement::ConsumeBlockingHit(FHitResult& OutHit);
bool AShipPawn::EnterAutonomy(
    const TArray<FVector>& WorldPath,
    AStaticMeshActor* ActualWall,
    ASimGameMode* RunOwner);
```

#### Produces

transform authority를 추가하지 않는 signed speed copy, actual blocking actor와 component identity를 한 번 소비할 수 있는 signal, manual mapping과 두 input을 안전하게 제거하고 Navigator를 enable하는 단일 transition, autonomy 뒤 모든 stale 및 direct manual event 무효화

- [ ] 4분: Movement test accessor로 signed speed를 설정하고 getter가 같은 값을 반환하되 외부에서 수정할 reference를 주지 않는 compile-time와 runtime assertion을 추가한다.
- [ ] 4분: blocking hit actor와 component를 채운 `FHitResult`를 pending state에 넣고 첫 consume은 true와 identity 보존, 두 번째 consume은 false가 되는 RED test를 추가한다.
- [ ] 4분: `Pawn.AutoStartRemovesManualMapping`에 active manual mapping과 non-zero throttle과 steer 상태를 준비한 뒤 `EnterAutonomy`가 같은 call 안에서 manual latch false, mapping 제거, 입력 0, Navigator enable을 만드는 RED assertion을 추가한다.
- [ ] 4분: `Pawn.StaleManualEventsIgnored`에서 Navigator가 non-zero command를 쓴 뒤 throttle과 steer의 늦은 `Completed` 및 `Canceled` handler를 test accessor로 직접 호출하고 command가 바뀌지 않는 RED assertion을 추가한다.
- [ ] 4분: `Pawn.DirectManualInputIgnoredDuringAutonomy`에서 autonomy 뒤 W, S, A, D에 해당하는 forward와 turn handler를 직접 호출하고 Navigator command와 actor transform이 바뀌지 않는 RED assertion을 추가한다.
- [ ] 3분: `EnterAutonomy` configure failure에서도 입력이 0이고 manual mapping이 다시 생기지 않는 safe failure case를 `AutoStartRemovesManualMapping`에 추가한다.
- [ ] 3분: 기존 transform ownership test를 확장해 `ShipNavigator.cpp`, `CourseBuilder.cpp`, `SimGameMode.cpp`, `ShipPawn.cpp`에서 actor transform mutator가 0회이고 Stage 3 swept `SetActorLocationAndRotation`만 1회인지 검사한다.
- [ ] 3분: targeted RED를 실행한다. 예상 새 실패는 `Movement.BlockingHitIdentity`, `Pawn.AutoStartRemovesManualMapping`, `Pawn.StaleManualEventsIgnored`, `Pawn.DirectManualInputIgnoredDuringAutonomy`이며 기존 transform ownership도 새 source가 생기기 전 deny-list assertion 때문에 RED가 될 수 있다. Editor full 예상 발견 수는 27개다.
- [ ] 3분: `ShipMovement.h`에 `Engine/HitResult.h`를 include하고 public getter와 consume declaration, private `FHitResult PendingBlockingHit`와 `bool bHasPendingBlockingHit`를 추가한다.
- [ ] 3분: getter와 consume을 값 복사 semantics로 구현한다.

```cpp
double UShipMovement::GetSignedSpeedCmPerSecond() const
{
    return SignedSpeedCmPerSecond;
}

bool UShipMovement::ConsumeBlockingHit(FHitResult& OutHit)
{
    if (!bHasPendingBlockingHit)
    {
        OutHit = FHitResult();
        return false;
    }
    OutHit = PendingBlockingHit;
    PendingBlockingHit = FHitResult();
    bHasPendingBlockingHit = false;
    return true;
}
```

- [ ] 3분: swept blocking hit 분기에서 `PendingBlockingHit = Hit`와 flag true를 기록한다. tick 시작에서 pending state를 지우지 않고 consume할 때만 지운다.
- [ ] 3분: `ShipPawn` constructor에서 `UShipNavigator` default subobject를 만들고 `UPROPERTY(VisibleAnywhere)` pointer를 보관한다.
- [ ] 4분: `EnterAutonomy` 시작에 `bManualInputActive=false`를 먼저 latch하고 `DeactivateManualInput()`으로 mapping을 제거한 뒤 `SetThrottle(0)`, `SetSteer(0)`을 쓴다. 전환 중 도착한 event가 zero write 뒤 다시 command를 바꾸지 못하게 한다.
- [ ] 4분: null dependency나 Navigator configure 실패면 false를 반환하고 입력 0을 다시 보장한다. 성공일 때만 `SetNavigationEnabled(true)` 후 true를 반환한다.

```cpp
bool AShipPawn::EnterAutonomy(const TArray<FVector>& WorldPath, AStaticMeshActor* ActualWall, ASimGameMode* RunOwner)
{
    DeactivateManualInput();
    if (Movement == nullptr)
    {
        return false;
    }
    Movement->SetThrottle(0.0f);
    Movement->SetSteer(0.0f);
    if (Navigator == nullptr || ActualWall == nullptr || RunOwner == nullptr)
    {
        Movement->SetThrottle(0.0f);
        Movement->SetSteer(0.0f);
        return false;
    }
    if (!Navigator->Configure(WorldPath, Movement, ActualWall, RunOwner))
    {
        Movement->SetThrottle(0.0f);
        Movement->SetSteer(0.0f);
        return false;
    }
    Navigator->SetNavigationEnabled(true);
    return true;
}
```

- [ ] 2분: release callback의 `bManualInputActive` guard와 focus-loss 입력 해제 경로를 보존해 autonomy 입력을 manual release가 덮지 않는지 확인한다.
- [ ] 4분: forward와 turn의 `Triggered`, `Completed`, `Canceled` callback 모두 첫 줄에서 `bManualInputActive`를 검사하게 한다. Stage 3 manual mode에서는 기존 동작을 유지하고 autonomy에서는 return한다.
- [ ] 3분: targeted GREEN에서 새 4개와 기존 transform ownership가 통과하고 Stage 4 unit count가 15인지 확인한다.
- [ ] 3분: Editor full 회귀에서 기존 12개와 Stage 4 unit 15개, 합계 27개를 확인한다.
- [ ] 2분: `rg -n "SetActor(Location|Rotation|Transform)|AddActor(World|Local)|TeleportTo|ETeleportType"`로 Movement 외 운항 transform mutator와 teleport가 0인지 확인한다. 기존 Movement의 swept call에 있는 `ETeleportType::None` 한 곳만 허용한다.
- [ ] 2분: Stage 3 manual input 함수와 기존 input tests가 삭제되지 않았고 toggle key와 console command와 UI와 `ExitAutonomy`가 추가되지 않았는지 diff와 `rg -n`으로 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp
git commit -m "feat: 자율주행 전환 경계 추가" `
  -m "변경 이유`nStage 3 transform 소유권을 보존한 관측 및 입력 전환 경계 필요" `
  -m "핵심 변경`nsigned speed와 hit identity signal 및 EnterAutonomy 구현" `
  -m "검증 방법`nMovement 및 Pawn unit test와 transform ownership 회귀"
```

### Task 6: ACourseBuilder runtime course actor 생성 추가

#### Files
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/CourseBuilder.h`의 build result, forced slide, actor reference API
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp`의 water query, random, runtime spawn, collision 설정
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`의 runtime setup failure test
- Test: `ShipAutonomySim.ShipNavigation.Unit.Course.RuntimeSetupFailure`

#### Consumes

builder actor의 world XY와 yaw, Ocean water surface query, optional forced slide, `RandomSeed`, `/Engine/BasicShapes/Cube.Cube`, `UWorld::SpawnActor`, `SpawnActorDeferred`, `FinishSpawning`

```cpp
void ACourseBuilder::SetForcedSlideCm(double InSlideCm);
void ACourseBuilder::ClearForcedSlide();
bool ACourseBuilder::BuildRuntimeCourse(
    FShipCourseBuildResult& OutResult,
    EShipSetupFailure& OutFailure);
```

#### Produces

runtime `ATargetPoint` start와 end 각 1개, runtime `AStaticMeshActor` wall 1개, exact 3-point world path, `UPROPERTY(Transient)` actor references, resolved slide와 water reference Z

- [ ] 4분: Water subsystem 또는 Ocean body가 없는 transient game world에서 `BuildRuntimeCourse`가 false와 `WaterSurfaceUnavailable`을 반환하고 actor reference가 모두 null인 RED test를 추가한다.
- [ ] 3분: repeated build가 기존 actor를 중복 생성하지 않고 false와 `CourseSpawnFailed`를 반환하는 contract assertion을 추가한다.
- [ ] 3분: RED를 Course runtime test prefix로 실행한다. 예상 새 실패 이름은 `Course.RuntimeSetupFailure`이며 Editor full 예상 발견 수는 28개다.
- [ ] 4분: header에 `FShipCourseBuildResult`, forced slide API, read-only getters와 다음 private state를 추가한다.

```cpp
UPROPERTY(EditDefaultsOnly, Category = "Course")
int32 RandomSeed = 20260809;

TOptional<double> ForcedSlideCm;

UPROPERTY(Transient)
TObjectPtr<ATargetPoint> StartTarget;

UPROPERTY(Transient)
TObjectPtr<ATargetPoint> EndTarget;

UPROPERTY(Transient)
TObjectPtr<AStaticMeshActor> WallActor;

TArray<FVector> WorldPath;
double ResolvedSlideCm = 0.0;
```

- [ ] 4분: `GetWorld()`과 `UWaterSubsystem::GetWaterSubsystem(GetWorld())`과 `GetOceanBodyComponent()`를 순서대로 null 검사한다.
- [ ] 4분: builder location에서 `EWaterBodyQueryFlags::ComputeLocation`만 요청한다. waves flag를 넣지 않고 반환된 water surface location Z가 finite인지 확인한다.

```cpp
UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());
UWaterBodyComponent* OceanBody = WaterSubsystem != nullptr
    ? WaterSubsystem->GetOceanBodyComponent().Get()
    : nullptr;
if (OceanBody == nullptr)
{
    OutFailure = EShipSetupFailure::WaterSurfaceUnavailable;
    return false;
}
const FWaterBodyQueryResult WaterInfo = OceanBody->QueryWaterInfoClosestToWorldLocation(
    GetActorLocation(),
    EWaterBodyQueryFlags::ComputeLocation);
const double WaterSurfaceZCm = WaterInfo.GetWaterSurfaceLocation().Z;
if (!FMath::IsFinite(WaterSurfaceZCm))
{
    OutFailure = EShipSetupFailure::WaterSurfaceUnavailable;
    return false;
}
```

- [ ] 3분: forced slide가 있으면 그대로 쓰고 없을 때만 `FRandomStream RandomStream(RandomSeed)`와 `FRandRange(-500.0, 500.0)`으로 slide를 한 번 생성한다. test accessor로 initial seed가 `RandomSeed`와 같은지 확인한다.
- [ ] 3분: Task 2 helper로 course definition을 만들고 wall center Z가 water surface Z보다 150 cm 높도록 설정한다. cube scale `(1,10,5)`로 bottom이 surface 아래 100 cm가 된다.
- [ ] 3분: `LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))`가 null이면 `MeshLoadFailed`로 종료한다.
- [ ] 5분: start와 end는 `SpawnActor<ATargetPoint>`로 생성하고 wall은 full spawn transform에 flat yaw와 cube scale을 넣어 `SpawnActorDeferred<AStaticMeshActor>`로 생성한다. runtime placement 뒤 `SetActorTransform`을 호출하지 않는다.
- [ ] 4분: wall `UStaticMeshComponent`에 mesh를 설정하고 mobility Static, `QueryOnly`, object `WorldStatic`, all channels Ignore, `ECC_Pawn`만 Block으로 설정한 뒤 `FinishSpawning`한다.

```cpp
UStaticMeshComponent* WallMesh = WallActor->GetStaticMeshComponent();
if (WallMesh == nullptr || !WallMesh->SetStaticMesh(CubeMesh))
{
    OutFailure = EShipSetupFailure::MeshLoadFailed;
    return false;
}
WallMesh->SetMobility(EComponentMobility::Static);
WallMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
WallMesh->SetCollisionObjectType(ECC_WorldStatic);
WallMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
WallMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
WallActor->FinishSpawning(WallTransform);
```

- [ ] 4분: spawn 중간 실패 시 이 builder가 만든 actor만 `Destroy()`하고 세 UPROPERTY reference와 `WorldPath`를 비운 뒤 정확한 setup failure를 반환한다.
- [ ] 3분: 성공 뒤 `OutResult`와 getters에 같은 두 target, wall, 3점 path, slide, surface Z가 들어가는지 test accessor assertion을 추가한다.
- [ ] 3분: targeted GREEN에서 runtime setup failure test와 전체 Stage 4 unit 16개가 통과하는지 확인한다.
- [ ] 3분: Editor full 회귀에서 기존 12개와 Stage 4 unit 16개, 합계 28개를 확인한다.
- [ ] 2분: `git diff -- ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs`가 비어 있고 map, config, asset diff가 없는지 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/CourseBuilder.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 런타임 항로 생성 추가" `
  -m "변경 이유`n동일 world에서 재현 가능한 target과 wall 및 path 생성 필요" `
  -m "핵심 변경`nwater 기준 course frame, forced 및 random slide, actor spawn 구현" `
  -m "검증 방법`nCourse runtime unit test와 전체 Editor 회귀"
```

### Task 7: UShipNavigator 경로 추종과 coast 및 runtime error 추가

#### Files
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigator.h`의 configure와 enable API 및 tick state
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp`의 progress, guidance, coast, error, debug tick
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h`의 runtime error ingress declaration과 latch state
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp`의 최초 runtime error 안전 정지
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`의 Navigator test
- Test: `ShipAutonomySim.ShipNavigation.Unit.Navigator.ControlCoastAndError`

#### Consumes

exact 3-point path, Movement signed speed, owner actor location과 forward vector, 순수 progress와 lookahead와 guidance와 stopping helper, actual wall reference, GameMode runtime error ingress

```cpp
bool UShipNavigator::Configure(
    const TArray<FVector>& InWorldPath,
    UShipMovement* InMovement,
    AStaticMeshActor* InActualWall,
    ASimGameMode* InRunOwner);
void UShipNavigator::SetNavigationEnabled(bool bEnabled);
bool UShipNavigator::IsNavigationEnabled() const;
void ASimGameMode::ReportRuntimeCalculationError(EShipRuntimeCalculationError Error);
```

#### Produces

monotonic path state, 300 cm live target, `SetSteer`와 `SetThrottle` output, final dynamic coast latch, persistent runtime error report, green path와 yellow waypoint와 cyan live target과 actual wall red debug draw

- [ ] 4분: valid 3-point configure 뒤 Navigator가 enable되고 Movement tick prerequisite가 등록되는 RED assertion을 작성한다.
- [ ] 4분: path를 따라 heading error와 throttle curve가 Movement input에 전달되며 actor transform이 바뀌지 않는 RED case를 작성한다.
- [ ] 4분: final remaining distance가 `dynamic stopping distance + 25 cm` 이하가 되면 throttle 0으로 latch되고 ship이 다시 경계 밖으로 보여도 throttle이 재개되지 않는 RED case를 작성한다.
- [ ] 4분: invalid path, non-finite location, stopping distance 실패마다 해당 `EShipRuntimeCalculationError`가 한 번 보고되고 Navigator disable과 두 입력 0이 유지되는 RED case를 작성한다.
- [ ] 3분: targeted RED를 실행한다. 예상 새 실패 이름은 `Navigator.ControlCoastAndError`이며 Editor full 예상 발견 수는 29개다.
- [ ] 4분: constructor에서 `PrimaryComponentTick.bCanEverTick=true`, 기본 enable false를 설정하고 configure 시 dependency와 exact path size와 finite points 및 non-zero segment를 검사한다.
- [ ] 3분: configure 성공 시 path와 weak actor references와 초기 progress를 저장하고 `Movement->AddTickPrerequisiteComponent(this)`를 호출한다.

```cpp
bool UShipNavigator::Configure(
    const TArray<FVector>& InWorldPath,
    UShipMovement* InMovement,
    AStaticMeshActor* InActualWall,
    ASimGameMode* InRunOwner)
{
    if (InWorldPath.Num() != 3 || InMovement == nullptr || InActualWall == nullptr || InRunOwner == nullptr)
    {
        return false;
    }
    for (const FVector& Point : InWorldPath)
    {
        if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y) || !FMath::IsFinite(Point.Z))
        {
            return false;
        }
    }
    WorldPath = InWorldPath;
    Movement = InMovement;
    ActualWall = InActualWall;
    RunOwner = InRunOwner;
    Progress = FShipPathProgress();
    bCoastLatched = false;
    Movement->AddTickPrerequisiteComponent(this);
    return true;
}
```

- [ ] 3분: `SetNavigationEnabled(false)`가 tick disable 전에 throttle과 steer를 0으로 쓰고, true는 successful configure 뒤에만 허용되게 구현한다.
- [ ] 5분: tick에서 active progress를 한 번 갱신하고 300 cm target, heading error, steer, base throttle을 순서대로 계산한다.
- [ ] 5분: `Progress.ActiveSegmentIndex == WorldPath.Num() - 2`인 final segment에서만 endpoint까지 remaining과 current positive forward speed의 dynamic stopping distance를 계산한다. `remaining <= stopping + 25`일 때 coast를 latch하고 throttle 0을 쓴다.
- [ ] 4분: coast 중 steer는 live target을 계속 따라가되 reverse throttle을 쓰지 않는다. throttle range assertion은 `[0,1]`이다.

```cpp
if (Progress.ActiveSegmentIndex == WorldPath.Num() - 2)
{
    const double ForwardSpeed = FMath::Max(0.0, Movement->GetSignedSpeedCmPerSecond());
    double StoppingDistanceCm = 0.0;
    if (!ComputeDynamicStoppingDistance(MotionParameters, ForwardSpeed, StoppingDistanceCm))
    {
        FailRuntimeCalculation(EShipRuntimeCalculationError::InvalidStoppingDistance);
        return;
    }
    if (bCoastLatched || RemainingDistanceCm <= StoppingDistanceCm + 25.0)
    {
        bCoastLatched = true;
        Throttle = 0.0f;
    }
}
Movement->SetSteer(Steer);
Movement->SetThrottle(Throttle);
```

`MotionParameters`는 Movement 내부를 노출해 얻지 않는다. Navigator cpp에서 `const FShipMotionParameters MotionParameters = FShipMotionParameters::Defaults();`를 사용해 설계에 고정된 C1, C2, stop speed 5를 재사용한다.

- [ ] 4분: 각 helper false와 non-finite output을 고유 runtime error enum으로 변환하는 `FailRuntimeCalculation`을 추가한다. 이 함수는 GameMode report 후 Navigator disable과 두 input 0을 수행한다.
- [ ] 3분: `ASimGameMode::ReportRuntimeCalculationError`의 최소 ingress에서 Task 4 latch helper를 호출하고 첫 report일 때만 `Stage4RuntimeCalculationError` marker를 Error level로 기록한다.
- [ ] 3분: GameMode ingress가 첫 report와 반복 report 모두 현재 Movement 입력을 0으로 만들고 Navigator가 이미 disable됐어도 상태를 되살리지 않게 한다.
- [ ] 4분: `DrawDebugLine`으로 전체 path green, waypoint `DrawDebugPoint` yellow, current live target cyan, wall actor의 실제 component bounds를 `DrawDebugBox` red로 그린다. hypothetical wall transform을 다시 계산하지 않는다.
- [ ] 3분: targeted GREEN에서 Navigator test 1개와 전체 Stage 4 unit 17개가 통과하는지 확인한다.
- [ ] 3분: Editor full 회귀에서 기존 12개와 Stage 4 unit 17개, 합계 29개를 확인한다.
- [ ] 2분: Navigator source가 `SetThrottle`, `SetSteer`, read-only speed 외 Movement mutator와 모든 actor transform mutator를 호출하지 않는지 `rg -n`으로 검사한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigator.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 선박 경로 추종 제어 추가" `
  -m "변경 이유`n순수 유도 결과를 Stage 3 입력 경계에 안전하게 연결할 component 필요" `
  -m "핵심 변경`nNavigator tick, coast latch, runtime error 안전 정지, debug 구현" `
  -m "검증 방법`nNavigator unit test와 transform 소유권 및 Editor 회귀"
```

### Task 8: ASimGameMode option, orchestration, terminal run latch 추가

#### Files
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h`의 lifecycle override, run state, read-only getters
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp`의 InitGame, BeginPlay, Tick, terminal과 logging
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp`의 GameMode tests
- Test: `ShipAutonomySim.ShipNavigation.Unit.GameMode.OptionBootstrap`
- Test: `ShipAutonomySim.ShipNavigation.Unit.GameMode.TerminalRuntimeError`

#### Consumes

`InitGame` map options, option helper result, deferred CourseBuilder, Stage 3 ship pawn class와 controller, `EnterAutonomy`, Movement hit와 signed speed, pure terminal selector, runtime error latch

```cpp
void ASimGameMode::InitGame(
    const FString& MapName,
    const FString& Options,
    FString& ErrorMessage);
void ASimGameMode::BeginPlay();
void ASimGameMode::Tick(float DeltaSeconds);
void ASimGameMode::ReportRuntimeCalculationError(EShipRuntimeCalculationError Error);
```

#### Produces

absent-only random slide, invalid-present setup failure, one CourseBuilder와 one ship spawn, possession과 autonomy transition, Navigator to Movement to GameMode tick ordering, persistent four-state terminal result, collision identity, timeout 45초, first-only logs와 test getters

- [ ] 4분: GameMode option test에 absent, present-empty, junk, conversion failure `1e`, `NaN`, `Inf`, `-501`, `501`, valid `-500`, `0`, `500`을 추가한다.
- [ ] 3분: absent만 random mode가 true이고 모든 invalid present value는 setup failure 및 random mode false라는 RED assertion을 넣는다.
- [ ] 4분: terminal test에서 같은 tick의 collision, goal, timeout이 모두 true일 때 Collision, goal과 timeout은 Success, timeout 단독은 Timeout이 되는 RED case를 추가한다.
- [ ] 4분: runtime error 두 번 보고 뒤 first error 보존, count 2, Navigator disabled, 두 입력 0, Success 영구 금지, first-only log counter 1을 검증한다.
- [ ] 4분: `GameMode.OptionBootstrap`의 BeginPlay subcase에서 possession 직후 첫 Tick 전에 `EnterAutonomy` call count 1, mapping inactive, 두 input 0, Navigator enabled를 요구하는 RED assertion을 추가한다.
- [ ] 3분: targeted RED를 실행한다. 예상 새 실패는 `GameMode.OptionBootstrap`, `GameMode.TerminalRuntimeError`이며 Editor full 예상 발견 수는 31개다.
- [ ] 3분: `InitGame` 첫 줄에서 `Super::InitGame(MapName, Options, ErrorMessage)`를 호출한다.
- [ ] 2분: GameMode constructor에서 `PrimaryActorTick.bCanEverTick=true`를 고정하고 `Tick` 첫 줄에서 `Super::Tick(DeltaSeconds)`를 호출한다.
- [ ] 4분: `HasOption`과 `ParseOption`을 함께 호출하고 Task 2 helper 결과를 저장한다. absent는 `bUseRandomSlide=true`, valid는 forced value, invalid present는 exact setup failure로 두고 builder spawn을 금지한다.

```cpp
void ASimGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);
    const bool bHasSlide = UGameplayStatics::HasOption(Options, TEXT("Stage4Slide"));
    const FString RawSlide = UGameplayStatics::ParseOption(Options, TEXT("Stage4Slide"));
    const FStage4SlideOptionResult Parsed = ClassifySlideOption(bHasSlide, RawSlide);
    bUseRandomSlide = Parsed.State == EStage4SlideOptionState::Absent;
    if (Parsed.State == EStage4SlideOptionState::Valid)
    {
        ForcedSlideCm = Parsed.SlideCm;
    }
    else if (Parsed.State != EStage4SlideOptionState::Absent)
    {
        SetupFailure = Parsed.Failure;
    }
}
```

- [ ] 3분: `BeginPlay`에서 setup failure가 있으면 `Stage4SetupFailure` marker를 최초 1회 기록하고 어떤 runtime actor도 spawn하지 않고 반환한다.
- [ ] 5분: `SpawnActorDeferred<ACourseBuilder>`로 builder를 만들고 forced이면 `SetForcedSlideCm`, absent이면 `ClearForcedSlide` 후 `FinishSpawning`한다.
- [ ] 4분: `BuildRuntimeCourse`가 실패하면 returned setup failure를 latch하고 first-only log 후 종료한다.
- [ ] 5분: course start transform에서 `AShipPawn`을 정확히 한 번 spawn하고 player controller를 찾아 possess한다. 기존 Stage 3 pawn spawn이 중복 실행되지 않도록 old bootstrap을 이 orchestration으로 교체한다.
- [ ] 4분: possession 직후 같은 `BeginPlay` call stack에서 `EnterAutonomy(WorldPath, WallActor, this)`를 호출한다. 첫 GameMode Tick이나 user input frame까지 지연하지 않으며 false면 `AutonomyActivationFailed`를 latch하고 두 input 0을 보장한다.
- [ ] 3분: Stage 4 final run path에는 mapping add, toggle key, console command, UI binding을 추가하지 않고 BeginPlay가 반환될 때 mapping inactive와 Navigator enabled를 assertion으로 확인한다.
- [ ] 3분: successful activation 뒤 `AddTickPrerequisiteComponent(ShipMovement)`를 GameMode에 호출한다. Navigator configure의 Movement prerequisite와 합쳐 순서를 Navigator, Movement, GameMode로 고정한다.
- [ ] 4분: GameMode tick에서 setup 또는 terminal 뒤 input 0을 계속 쓰고 run 중에만 elapsed를 `DeltaSeconds`로 누적한다.
- [ ] 4분: Movement의 `ConsumeBlockingHit`를 한 번 호출해 모든 blocking geometry를 collision candidate로 삼고 actor와 component identity를 저장한다.
- [ ] 4분: final endpoint XY distance `<=100 cm`와 `abs(signed speed)<=5 cm/s`를 success 조건으로 계산한다. runtime error latch가 있으면 success false다.
- [ ] 3분: elapsed `>=45.0`을 timeout candidate로 만들고 pure selector로 세 candidate를 같은 tick에 평가한다.

```cpp
const FShipTerminalInputs Inputs{
    bBlockingHit,
    !RuntimeErrorState.bLatched && GoalDistanceCm <= 100.0 && FMath::Abs(SignedSpeedCmPerSecond) <= 5.0,
    ElapsedRunSeconds >= 45.0,
    RuntimeErrorState.bLatched
};
const EShipRunResult Candidate = SelectTerminalResult(Inputs);
if (RunResult == EShipRunResult::Running && Candidate != EShipRunResult::Running)
{
    RunResult = Candidate;
    DisableNavigatorAndZeroInputs();
    LogTerminalOnce(Candidate);
}
```

- [ ] 4분: terminal result는 한 번 Running을 벗어나면 절대 바꾸지 않고 `Stage4Terminal Success`, `Timeout`, `Collision` marker를 각 run 최대 1회 기록한다.
- [ ] 4분: `ReportRuntimeCalculationError`는 pure latch count를 매번 증가시키되 최초 report에서만 Error log와 Navigator disable을 수행하고 이후 매 tick 두 input 0을 다시 보장한다.
- [ ] 4분: header에 run result, setup failure, runtime error, count, slide, elapsed, run ship, builder, collision actor와 component의 read-only getter를 고정 signature대로 구현한다.
- [ ] 3분: targeted GREEN에서 GameMode 2개와 전체 Stage 4 unit 19개가 통과하는지 확인한다.
- [ ] 3분: Editor full 회귀에서 Stage 3 12개와 Stage 4 unit 19개, 합계 31개를 확인한다.
- [ ] 2분: invalid option path가 `ClearForcedSlide`, `FRandomStream`, CourseBuilder spawn에 도달하지 않는지 test와 diff로 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h `
  ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp `
  ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp
git commit -m "feat: 자율주행 실행 오케스트레이션 추가" `
  -m "변경 이유`noption부터 terminal까지 한 owner가 관리하는 run lifecycle 필요" `
  -m "핵심 변경`nInitGame 검증, spawn과 possession, tick 순서, terminal 및 error latch 구현" `
  -m "검증 방법`nGameMode option 및 terminal unit test와 Editor 회귀"
```

### Task 9: MainLevel game-context 11 case fresh-world Automation 추가

#### Files
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp`
- Test: `ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep`
- Test only, no modification: `ShipAutonomySim/Content/Maps/MainLevel.umap`

#### Consumes

launch URL의 첫 `Stage4Slide=-500`, 이후 `UGameplayStatics::OpenLevel`, game `FWorldContext`, `ASimGameMode` read-only state, ship과 actual wall mesh local bounds와 component transforms, pure convex hull gap

```cpp
class FRunShipNavigationSweepCommand final : public IAutomationLatentCommand
{
public:
    explicit FRunShipNavigationSweepCommand(FAutomationTestBase* InTest);
    virtual bool Update() override;
};
```

#### Produces

`-500`부터 `+500`까지 100 cm 간격 11 case의 fresh world 실행, ship `UBoxComponent`와 wall `UStaticMeshComponent`의 local box를 쓰는 gap 관측, in-memory `TArray<FStage4CaseResult>`, success와 collision과 timeout과 setup failure와 runtime error counts, four-column log table. CSV와 결과 파일은 생성하지 않는다.

```cpp
struct FStage4CaseResult
{
    double SlideCm = 0.0;
    bool bSuccess = false;
    double ElapsedSeconds = 0.0;
    double MinimumWallDistanceCm = TNumericLimits<double>::Max();
};
```

- [ ] 3분: test macro를 `ClientContext | ProductFilter`로 만들고 expected slides를 `-500, -400, -300, -200, -100, 0, 100, 200, 300, 400, 500` 순서로 고정한다.
- [ ] 4분: 11 success, collision 0, timeout 0, setup failure 0, runtime calculation error 0, 모든 minimum wall distance `>0`을 요구하는 최종 assertion을 먼저 작성한다.
- [ ] 3분: 아직 정의하지 않은 `FRunShipNavigationSweepCommand`를 macro에서 enqueue한 상태로 Editor target을 incremental build해 RED를 확인한다. 예상 exit는 non-zero, compiler 표식은 `FRunShipNavigationSweepCommand` undeclared, 의도한 test 이름은 `ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep` 하나다.
- [ ] 4분: `FRunShipNavigationSweepCommand`에 `WaitingForWorld`, `ObservingCase`, `Finished` state와 current case index, previous world unique ID, weak current world, wall-clock deadlines를 추가한다.
- [ ] 4분: 최초 case는 process launch로 이미 열린 world를 기다리고 case 1부터는 previous world identity를 저장한 뒤 `OpenLevel`로 travel하도록 구현한다.

```cpp
UGameplayStatics::OpenLevel(
    CurrentWorld.Get(),
    FName(TEXT("/Game/Maps/MainLevel")),
    true,
    FString::Printf(TEXT("Stage4Slide=%.0f"), ExpectedSlides[NextCaseIndex]));
```

- [ ] 5분: `GEngine->GetWorldContexts()`에서 `EWorldType::Game`, previous unique ID와 다른 world, `MainLevel` map name, `HasBegunPlay()`, valid `ASimGameMode`를 모두 만족할 때만 새 case world로 수락한다.

```cpp
UWorld* FindFreshMainLevelWorld(int32 PreviousWorldId)
{
    if (GEngine == nullptr)
    {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (Context.WorldType == EWorldType::Game
            && World != nullptr
            && World->GetUniqueID() != PreviousWorldId
            && World->HasBegunPlay()
            && World->GetMapName().Contains(TEXT("MainLevel")))
        {
            return World;
        }
    }
    return nullptr;
}
```

- [ ] 4분: accepted world의 GameMode setup이 완료될 때까지 builder와 run ship이 모두 non-null이거나 setup failure가 non-None이 될 때까지 기다린다. resolved slide가 expected와 다르면 `Stage4FreshWorldSlideMismatch` fail reason을 1 증가시킨다.
- [ ] 3분: travel 시작부터 15초 안에 identity가 다른 valid world가 없으면 `Stage4FreshWorldLoadTimeout`을 기록하고 전체 test를 실패 종료한다.
- [ ] 3분: 각 case 관찰 시작에 60초 wall-clock deadline을 두고 전체 command 생성 시 720초 deadline을 둔다. external runner는 900초에서 process를 종료한다.
- [ ] 5분: 매 latent update에서 ship `UBoxComponent::GetUnscaledBoxExtent()`로 local `FBox(-Extent, Extent)`를 만들고 wall은 actual collision mesh의 `UStaticMesh::GetBoundingBox()`를 얻는다. 각 collision component의 full `GetComponentTransform()`을 Task 4 helper에 넘겨 gap을 갱신한다.

```cpp
UBoxComponent* ShipCollision = RunShip->FindComponentByClass<UBoxComponent>();
if (ShipCollision == nullptr || WallMesh == nullptr || WallMesh->GetStaticMesh() == nullptr)
{
    RecordFailure(TEXT("Stage4CollisionBoundsUnavailable"));
    return true;
}
const FVector ShipExtent = ShipCollision->GetUnscaledBoxExtent();
const FBox ShipLocalBox(-ShipExtent, ShipExtent);
const FBox WallLocalBox = WallMesh->GetStaticMesh()->GetBoundingBox();
double GapCm = 0.0;
if (!ComputeConvexHullGapCm(
        ShipLocalBox,
        ShipCollision->GetComponentTransform(),
        WallLocalBox,
        WallMesh->GetComponentTransform(),
        GapCm))
{
    RecordFailure(TEXT("Stage4HullGapCalculationError"));
    return true;
}
CurrentResult.MinimumWallDistanceCm = FMath::Min(CurrentResult.MinimumWallDistanceCm, GapCm);
```

- [ ] 4분: collision actor가 actual wall이거나 collision component가 actual wall collision mesh와 동일할 때만 measured gap을 0으로 강제한다.

```cpp
const bool bActualWallHit = GameMode->GetCollisionActor() == WallActor
    || GameMode->GetCollisionComponent() == WallMesh;
if (bActualWallHit)
{
    CurrentResult.MinimumWallDistanceCm = 0.0;
}
```

- [ ] 3분: 다른 blocking geometry hit는 Collision count를 올리되 wall gap을 0으로 덮지 않는 branch와 assertion을 추가한다.
- [ ] 4분: setup failure 또는 runtime calculation error가 관찰되면 각각 고유 fail reason과 count를 올리고 현재 case를 실패로 기록한다. runtime error count는 GameMode getter와 별도로 Automation count를 유지한다.
- [ ] 4분: terminal이 Success, Timeout, Collision 중 하나가 되면 slide, success, elapsed, minimum gap을 in-memory result에 저장하고 다음 case를 travel한다.
- [ ] 3분: GameMode가 45초 timeout을 만들지 못하고 case wall-clock 60초가 되면 `Stage4CaseWatchdogTimeout` fail reason을 기록한다.
- [ ] 4분: 마지막 case 뒤 aggregate counts를 계산하고 11 case를 정확히 한 번씩 받았는지 검사한다.
- [ ] 3분: 최종 per-case log는 `slide | success | elapsed | min wall distance` 네 열만 기록한다. fail reason과 count는 별도 Automation error line으로 기록하고 CSV, JSON, text file writer를 추가하지 않는다.

```cpp
UE_LOG(LogTemp, Display, TEXT("slide | success | elapsed | min wall distance"));
for (const FStage4CaseResult& Result : Results)
{
    UE_LOG(
        LogTemp,
        Display,
        TEXT("%.0f | %s | %.3f | %.3f"),
        Result.SlideCm,
        Result.bSuccess ? TEXT("true") : TEXT("false"),
        Result.ElapsedSeconds,
        Result.MinimumWallDistanceCm);
}
```

- [ ] 4분: implementation build 후 targeted actual-world GREEN을 아래 900초 wrapper로 실행한다. 예상 test count는 1, 11 success, 나머지 네 count 0, 각 min gap `>0`, failure와 error와 ensure 0, `TEST COMPLETE. EXIT CODE: 0`이다.

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject'
$Log = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\Stage4-ActualWorld-Targeted.log'
$Arguments = @(
    $Project,
    '/Game/Maps/MainLevel?Stage4Slide=-500',
    '-game',
    '-Unattended',
    '-NoSplash',
    '-NullRHI',
    '-NoAudio',
    '-NoPause',
    '-NoP4',
    '-nowrite',
    '-TestExit="Automation Test Queue Empty"',
    '-ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep;SoftQuit;"',
    "-Log=$Log"
)
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru -WindowStyle Hidden
if (-not $Process.WaitForExit(900000)) {
    $Process.Kill()
    throw 'Actual-world Automation exceeded 900 seconds'
}
if ($Process.ExitCode -ne 0) {
    throw "Actual-world Automation failed with exit code $($Process.ExitCode)"
}
```

- [ ] 3분: log에서 test started 1, test completed 1, unknown test 0, setup/runtime failure marker 0, 11 result rows와 exit code 0을 확인한다.
- [ ] 3분: Editor-context full 회귀에서 기존 12개와 Stage 4 unit 19개, 합계 31개를 확인한다. ClientContext actual test가 Editor shard에 잘못 발견되지 않아야 한다.
- [ ] 3분: game-context full 회귀에서 actual test 1개만 발견되고 11 case acceptance를 다시 만족하는지 확인한다.
- [ ] 2분: `rg -n "CSV|FFileHelper|SaveString|AutomationOpenMap"`으로 writer와 forbidden map load 경로가 새 test에 없는지 확인한다.
- [ ] 3분: 아래 commit을 만든다.

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp
git commit -m "test: 실제 월드 자율주행 스윕 추가" `
  -m "변경 이유`nMainLevel에서 전체 slide 구간의 통합 운항 결과 검증 필요" `
  -m "핵심 변경`n11 case fresh-world travel과 in-memory metric 및 failure count 추가" `
  -m "검증 방법`ngame-context targeted 및 full Automation과 Editor 회귀"
```

## RED와 GREEN 공통 실행 계약

Automation source를 바꾼 각 Task는 실행 전에 아래 Editor target incremental build를 한다. 실패 test가 새 symbol을 먼저 참조하는 RED 단계에서는 compile error도 유효한 RED이며, 이 경우 compiler error와 non-zero exit를 기록한 뒤 test를 통과시키는 최소 source를 작성한다. compile을 통과한 RED에서는 해당 test의 `Result={Fail}` 또는 Automation process non-zero exit를 기록한다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat' `
  ShipAutonomySimEditor Win64 Development `
  -Project='C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject' `
  -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) {
    throw "Editor target build failed with exit code $LASTEXITCODE"
}
```

Task 2부터 Task 8의 targeted GREEN과 Editor full 회귀는 아래 exact helper와 call matrix를 해당 Task 시점에 사용한다. 각 process timeout은 600초다.

```powershell
function Invoke-ShipEditorAutomation {
    param(
        [Parameter(Mandatory=$true)][string]$Filter,
        [Parameter(Mandatory=$true)][string]$LogName,
        [Parameter(Mandatory=$true)][int]$ExpectedCount
    )
    $Editor = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $Project = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject'
    $Log = "C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\$LogName"
    $Arguments = @(
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
        "-Log=$Log"
    )
    $Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru -WindowStyle Hidden
    if (-not $Process.WaitForExit(600000)) {
        $Process.Kill()
        throw "Automation exceeded 600 seconds: $Filter"
    }
    if ($Process.ExitCode -ne 0) {
        throw "Automation failed filter=$Filter exit=$($Process.ExitCode)"
    }
    $Started = @(Select-String -LiteralPath $Log -SimpleMatch 'Test Started').Count
    if ($Started -ne $ExpectedCount) {
        throw "Automation count mismatch filter=$Filter expected=$ExpectedCount actual=$Started"
    }
    if (Select-String -LiteralPath $Log -Pattern 'Result=\{Fail\}|Unknown test|Ensure condition failed|Fatal error') {
        throw "Automation failure marker: $Log"
    }
    if (-not (Select-String -LiteralPath $Log -SimpleMatch 'TEST COMPLETE. EXIT CODE: 0' -Quiet)) {
        throw "Automation exit marker missing: $Log"
    }
}
```

```powershell
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task2-Unit-GREEN.log' 3
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task2-Full-GREEN.log' 15
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task3-Unit-GREEN.log' 8
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task3-Full-GREEN.log' 20
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task4-Unit-GREEN.log' 11
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task4-Full-GREEN.log' 23
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task5-Unit-GREEN.log' 15
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task5-Full-GREEN.log' 27
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task6-Unit-GREEN.log' 16
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task6-Full-GREEN.log' 28
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task7-Unit-GREEN.log' 17
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task7-Full-GREEN.log' 29
Invoke-ShipEditorAutomation 'ShipAutonomySim.ShipNavigation.Unit' 'Stage4-Task8-Unit-GREEN.log' 19
Invoke-ShipEditorAutomation 'ShipAutonomySim' 'Stage4-Task8-Full-GREEN.log' 31
```

GREEN과 회귀의 공통 exit condition은 다음과 같다.

- process exit code 0
- requested test count가 Task의 예상 count와 일치
- `Result={Fail}`, `Error:`, `Ensure condition failed`, `Unknown test` 0회
- `TEST COMPLETE. EXIT CODE: 0` 1회
- `SoftQuit` 뒤 정상 engine shutdown

Task 2부터 Task 8까지의 RED 예상 실패 이름은 각 Task에 열거한 새 test 이름이다. 새 declaration 때문에 compile 단계에서 멈추면 해당 test binary는 실행되지 않으므로 Automation count를 주장하지 않고 compiler symbol과 exit만 기록한다. GREEN 이후에만 test 발견 수를 판정한다.

## Final Verification

아래 순서를 바꾸지 않는다. 이 절의 명령은 계획 작성 중 실행하지 않으며 Stage 4 구현 commit이 모두 끝난 뒤 구현자가 실행한다.

### 1. Targeted pure tests

예상 결과는 Stage 4 unit 19개 통과, failure와 error와 ensure와 unknown test 0, exit code 0이다.

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject'
$Log = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\Stage4-Unit-Final.log'
& $Editor $Project `
  -Unattended -NoSplash -NullRHI -NoAudio -NoPause -NoP4 -nowrite `
  -TestExit='Automation Test Queue Empty' `
  -ExecCmds='Automation RunTests ShipAutonomySim.ShipNavigation.Unit;SoftQuit;' `
  "-Log=$Log"
if ($LASTEXITCODE -ne 0) {
    throw "Targeted unit Automation failed with exit code $LASTEXITCODE"
}
```

### 2. Targeted actual-world test

최초 map load는 command line URL로 `-500`을 전달한다. 이후 10 case는 latent command가 `OpenLevel`을 호출한다. 예상 결과는 actual test 1개, fresh world 11개, success 11, collision 0, timeout 0, setup failure 0, runtime calculation error 0, 각 minimum wall distance `>0`, exit code 0이다.

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject'
$Log = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\Stage4-ActualWorld-Final.log'
$Arguments = @(
    $Project,
    '/Game/Maps/MainLevel?Stage4Slide=-500',
    '-game',
    '-Unattended',
    '-NoSplash',
    '-NullRHI',
    '-NoAudio',
    '-NoPause',
    '-NoP4',
    '-nowrite',
    '-TestExit="Automation Test Queue Empty"',
    '-ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep;SoftQuit;"',
    "-Log=$Log"
)
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru -WindowStyle Hidden
if (-not $Process.WaitForExit(900000)) {
    $Process.Kill()
    throw 'Targeted actual-world Automation exceeded 900 seconds'
}
if ($Process.ExitCode -ne 0) {
    throw "Targeted actual-world Automation failed with exit code $($Process.ExitCode)"
}
```

### 3. Full ShipAutonomySim Automation

Application context가 다른 test를 한 process에 억지로 합치지 않고 두 shard로 실행한다. Editor shard는 Stage 3 12개와 Stage 4 unit 19개로 31개다. game shard는 actual-world 1개며 내부에서 11 case를 수행한다. 두 shard의 고유 합계는 32개다.

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject'
$EditorLog = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\Stage4-Full-Editor.log'
& $Editor $Project `
  -Unattended -NoSplash -NullRHI -NoAudio -NoPause -NoP4 -nowrite `
  -TestExit='Automation Test Queue Empty' `
  -ExecCmds='Automation RunTests ShipAutonomySim;SoftQuit;' `
  "-Log=$EditorLog"
if ($LASTEXITCODE -ne 0) {
    throw "Full Editor Automation failed with exit code $LASTEXITCODE"
}
```

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Project = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject'
$GameLog = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\Saved\Logs\Stage4-Full-Game.log'
$Arguments = @(
    $Project,
    '/Game/Maps/MainLevel?Stage4Slide=-500',
    '-game',
    '-Unattended',
    '-NoSplash',
    '-NullRHI',
    '-NoAudio',
    '-NoPause',
    '-NoP4',
    '-nowrite',
    '-TestExit="Automation Test Queue Empty"',
    '-ExecCmds="Automation RunTests ShipAutonomySim;SoftQuit;"',
    "-Log=$GameLog"
)
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru -WindowStyle Hidden
if (-not $Process.WaitForExit(900000)) {
    $Process.Kill()
    throw 'Full game Automation exceeded 900 seconds'
}
if ($Process.ExitCode -ne 0) {
    throw "Full game Automation failed with exit code $($Process.ExitCode)"
}
```

다음 log gate를 두 파일에 적용한다.

```powershell
$Logs = @($EditorLog, $GameLog)
foreach ($Path in $Logs) {
    if (-not (Select-String -LiteralPath $Path -SimpleMatch 'TEST COMPLETE. EXIT CODE: 0' -Quiet)) {
        throw "Missing successful Automation exit marker: $Path"
    }
    $Forbidden = Select-String -LiteralPath $Path -Pattern 'Result=\{Fail\}|Unknown test|Ensure condition failed|Fatal error'
    if ($Forbidden) {
        throw "Automation failure marker in $Path"
    }
}
```

### 4. UE 5.5.4 Editor build

Targeted와 full Automation이 사용한 incremental binary 뒤에 Editor target을 한 번 더 build해 최종 source tree의 compile 상태를 확인한다. 예상 exit code는 0이다.

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat' `
  ShipAutonomySimEditor Win64 Development `
  -Project='C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer\ShipAutonomySim\ShipAutonomySim.uproject' `
  -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) {
    throw "Final ShipAutonomySimEditor build failed with exit code $LASTEXITCODE"
}
```

### 5. MainLevel no-write load

map과 config hash를 먼저 저장한 뒤 `-nowrite`와 `QUIT_EDITOR`로 MainLevel을 game mode로 한 번 load한다. `AutomationOpenMap`, `QUIT`, `SoftQuit`을 이 단계에 사용하지 않는다.

```powershell
$Repo = 'C:\Users\siwon\Documents\Codex\2026-08-07\krafton-web-viewer\work\image-sequence-viewer'
$Project = Join-Path $Repo 'ShipAutonomySim\ShipAutonomySim.uproject'
$Map = Join-Path $Repo 'ShipAutonomySim\Content\Maps\MainLevel.umap'
$Configs = @(
    (Join-Path $Repo 'ShipAutonomySim\Config\DefaultEngine.ini'),
    (Join-Path $Repo 'ShipAutonomySim\Config\DefaultGame.ini'),
    (Join-Path $Repo 'ShipAutonomySim\Config\DefaultInput.ini')
)
$BeforeHashes = @{}
foreach ($Path in @($Map) + $Configs) {
    $BeforeHashes[$Path] = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}
$BeforeGit = @(git -C $Repo status --porcelain=v1)
$NoWriteLog = Join-Path $Repo 'ShipAutonomySim\Saved\Logs\Stage4-MainLevel-NoWrite.log'
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  $Project /Game/Maps/MainLevel -game -Unattended -NoSplash -NullRHI -NoAudio -NoPause -NoP4 -nowrite `
  -ExecCmds='QUIT_EDITOR' "-Log=$NoWriteLog"
if ($LASTEXITCODE -ne 0) {
    throw "MainLevel no-write load failed with exit code $LASTEXITCODE"
}
if (-not (Select-String -LiteralPath $NoWriteLog -SimpleMatch 'Load map complete' -Quiet)) {
    throw 'MainLevel load completion marker missing'
}
if (Select-String -LiteralPath $NoWriteLog -Pattern 'LoadErrors|Fatal error|MapCheck: Error') {
    throw 'MainLevel no-write log contains a failure marker'
}
```

### 6. Diff, map, config, Git unchanged gate

```powershell
foreach ($Path in @($Map) + $Configs) {
    $AfterHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    if ($AfterHash -ne $BeforeHashes[$Path]) {
        throw "No-Go protected file changed: $Path"
    }
}
$AfterGit = @(git -C $Repo status --porcelain=v1)
if (($AfterGit -join "`n") -ne ($BeforeGit -join "`n")) {
    throw 'No-Go Git worktree changed during final runtime validation'
}
git -C $Repo diff --check
if ($LASTEXITCODE -ne 0) {
    throw 'git diff --check failed'
}
git -C $Repo diff --name-only -- ShipAutonomySim/Content ShipAutonomySim/Config
```

마지막 명령의 출력은 비어 있어야 한다. 구현 diff는 고정 변경 파일 목록에만 있어야 하며 `ShipAutonomySim.Build.cs`, `.umap`, `.ini`, viewer, capture 경로가 나오면 No-Go다.

### 7. 사람 PIE 체크리스트

- [ ] 2분: 자동 검증이 모두 끝난 뒤 Unreal Editor에서 MainLevel을 열고 save prompt가 없는지 확인한다.
- [ ] 3분: PIE에서 green 3-point path, yellow waypoint, cyan live target, red actual wall bounds가 실제 actor와 일치하는지 본다.
- [ ] 3분: 수동 입력 직후 autonomy로 전환했을 때 mapping이 제거되고 throttle과 steer가 순간적으로 0이 된 뒤 Navigator output만 들어오는지 본다.
- [ ] 3분: wall 근처에서 선박이 waypoint 부호에 맞는 쪽으로 우회하고 reverse 없이 final coast로 들어가는지 본다.
- [ ] 3분: goal 반경에서 속도 `<=5 cm/s`가 된 뒤 Success가 한 번만 표시되고 input이 계속 0인지 본다.
- [ ] 2분: PIE와 Editor를 종료하며 map과 config 저장을 선택하지 않는다.
- [ ] 2분: 앞서 저장한 map과 config SHA-256 및 `git status --porcelain=v1`을 다시 비교해 사람 검증도 변경을 남기지 않았는지 확인한다.

## Design Coverage

| 설계 요구 | 구현 Task | 주 검증 |
|---|---:|---|
| stale Stage 경계의 최소 문서 정리 | 1 | 문서 assertion과 diff scope |
| terminal과 setup 및 runtime error type 분리 | 2, 4, 8 | `Terminal.Priority`, `Terminal.RuntimeCalculationErrorLatch` |
| absent-only random과 present invalid setup failure | 2, 8 | `Options.Classification`, `GameMode.OptionBootstrap` |
| course 좌표, waypoint 부호, flat frame | 2 | `Course.Geometry`, `Course.FrameTransform` |
| progress 단조성, future segment jump 방지 | 3 | `ProgressMonotonicity`, `SegmentTransition` |
| 300 cm lookahead와 heading 및 throttle curve | 3, 7 | `Lookahead`, `SteeringAndThrottle`, `Navigator.ControlCoastAndError` |
| C1 및 C2 forward Euler dynamic stopping과 coast latch | 3, 7 | `DynamicStoppingDistance`, `Navigator.ControlCoastAndError` |
| local 8 corner full transform XY convex hull gap | 4, 9 | `ConvexHullGap`, actual-world minimum gap |
| terminal priority와 runtime error success 금지 | 4, 8 | `Terminal.Priority`, `GameMode.TerminalRuntimeError` |
| Movement read-only speed와 blocking identity | 5 | `Movement.BlockingHitIdentity` |
| actual wall hit만 gap 0, 다른 blocking은 Collision | 5, 8, 9 | hit identity unit test와 actual-world branch assertion |
| 즉시 auto start와 manual mapping 제거 | 5, 8 | `Pawn.AutoStartRemovesManualMapping`, `GameMode.OptionBootstrap` |
| stale input event와 W, S, A, D 직접 입력 무효 | 5 | `Pawn.StaleManualEventsIgnored`, `Pawn.DirectManualInputIgnoredDuringAutonomy` |
| runtime targets 2개, wall actor, exact path와 refs | 6 | `Course.RuntimeSetupFailure`, actual-world setup count |
| water reference, cube scale, collision, seed와 forced slide | 6 | course unit test와 11 forced cases |
| Navigator가 두 input만 사용하고 direct transform 및 teleport 미사용 | 5, 7 | transform ownership 회귀와 source deny scan |
| InitGame, BeginPlay, tick order, timeout, collision, logs | 8 | 두 GameMode unit tests와 actual-world test |
| runtime error first-only log와 permanent safe stop | 7, 8, 9 | Navigator와 GameMode unit tests, separate actual count |
| MainLevel 11 fresh worlds와 in-memory results | 9 | `ActualWorld.NavigationSweep` |
| Stage 3 Automation 12개 보존 | 2에서 9 | 각 Task full Editor 회귀 |
| targeted, full, build, no-write, unchanged, PIE 순서 | Final Verification | 7단계 gate |
| PCG, Niagara, capture, viewer, CSV, reverse, slip, 장식 제외 | Global Constraints, 7, 9 | source와 changed-path scan |

## File Responsibility Audit

| 파일 | 단일 책임 |
|---|---|
| `ShipNavigationTypes.h` | shared run과 failure enum |
| `ShipNavigationSimulation.h/.cpp` | world와 actor에 독립적인 Stage 4 계산 |
| `CourseBuilder.h/.cpp` | runtime course actor 생성과 reference 소유 |
| `ShipNavigator.h/.cpp` | path state와 Movement input command |
| `ShipMovement.h/.cpp` | Stage 3 swept transform와 read-only observation |
| `ShipPawn.h/.cpp` | manual input lifecycle과 autonomy transition |
| `SimGameMode.h/.cpp` | setup과 orchestration과 run terminal latch |
| `ShipNavigationTests.cpp` | Stage 4 Editor-context unit tests 15개 |
| `ShipMovementTests.cpp` | 기존 Stage 3 12개와 Stage 4 Movement 및 Pawn 경계 4개 |
| `ShipNavigationWorldTests.cpp` | game-context fresh-world test 1개 |

## 계획 자체 점검

구현 시작 전에 계획 문서만 대상으로 아래 검사를 실행한다.

- [ ] Task heading이 1부터 9까지 연속이고 각 Task에 Files, Consumes, Produces, RED, 최소 구현, GREEN, 회귀, 범위 확인, commit이 있는지 확인한다.
- [ ] Create와 Modify 경로가 변경 파일 구조 목록 안에 있고 각 파일 책임이 한 Task dependency chain에 맞는지 확인한다.
- [ ] public signature가 고정 C++ 경계와 Task 본문에서 철자와 const 및 pointer type까지 같은지 확인한다.
- [ ] terminal enum이 `Running`, `Success`, `Timeout`, `Collision` 네 값뿐인지 확인한다.
- [ ] test 이름 32개가 모두 고유하고 Editor 31개와 game 1개의 context와 count가 일치하는지 확인한다.
- [ ] Task 순서가 pure type, pure 계산, observation과 input transition, actor spawn, Navigator, GameMode, actual world 순서인지 확인한다.
- [ ] include가 `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `Water` 안에 있고 Build.cs 변경 계획이 없는지 확인한다.
- [ ] 모든 Windows path와 `ExecCmds` quoting, Automation prefix 1회, 마지막 `SoftQuit`, no-write `QUIT_EDITOR`를 확인한다.
- [ ] 구현 완료, build 성공, Automation 통과를 현재 사실로 서술한 문장이 없는지 확인한다.
- [ ] 미완성 표기, 가운데점 문자, required header 외 star 강조, 임의 branch 명명, 외부 service 전달 문구가 없는지 확인한다.
- [ ] `git diff --check`가 통과하고 `git status --short`에 이 계획 파일만 나오는지 확인한다.

```powershell
$Plan = 'docs/superpowers/plans/2026-08-09-ship-autonomy-navigation.md'
$Tasks = @(Select-String -LiteralPath $Plan -Pattern '^### Task [1-9]:' -Encoding UTF8)
if ($Tasks.Count -ne 9) {
    throw "Expected 9 Tasks, found $($Tasks.Count)"
}
$ForbiddenTokens = @(
    ('T' + 'BD'),
    ('T' + 'ODO'),
    ('적절히' + ' 처리'),
    ('위와' + ' 동일'),
    ('나중' + '에')
)
$PlanText = Get-Content -Raw -Encoding UTF8 -LiteralPath $Plan
foreach ($Token in $ForbiddenTokens) {
    if ($PlanText.Contains($Token)) {
        throw "Incomplete marker found in plan: $Token"
    }
}
$MiddleDot = [char]0x00B7
if ($PlanText.Contains($MiddleDot)) {
    throw 'Forbidden middle-dot character found in plan'
}
$TestNames = Select-String -LiteralPath $Plan -Pattern '^[0-9]+\. `ShipAutonomySim\.(ShipMovement|ShipNavigation)\.[A-Za-z0-9.]+`$' -Encoding UTF8 |
    ForEach-Object { $_.Line -replace '^[0-9]+\. `|`$', '' } |
    Sort-Object -Unique
if ($TestNames.Count -ne 32) {
    throw "Expected 32 unique test names, found $($TestNames.Count)"
}
git diff --check
if ($LASTEXITCODE -ne 0) {
    throw 'git diff --check failed'
}
$Changed = @(git status --short)
if ($Changed.Count -ne 1 -or $Changed[0] -ne '?? docs/superpowers/plans/2026-08-09-ship-autonomy-navigation.md') {
    throw "Unexpected plan-authoring changes: $($Changed -join ', ')"
}
```

## 구현 시 확인할 동적 불확실성

- MainLevel의 Ocean water query가 course origin에서 finite surface Z를 반환하고 reference surface가 시각적 수면과 일치하는지 actual-world setup gate로 확인한다.
- Stage 3 C1과 C2 parameter에서 11개 slide가 45초 안에 coast와 success에 도달하는지 측정한다. 상수 변경이 필요하면 pure curve와 stopping test를 먼저 갱신하고 설계 범위를 벗어난 reverse 또는 lateral model을 추가하지 않는다.
- local runner에서 ClientContext test 발견과 non-seamless `OpenLevel` travel이 world identity 변경 및 15초 load deadline을 만족하는지 log와 unique ID로 확인한다.
- hull gap이 매우 작은 양수일 때 floating-point tolerance로 0이 되지 않는지 local 8-corner test와 actual minimum 값으로 확인한다. actual wall identity hit만 정확한 0으로 취급한다.
- component tick prerequisite가 packaged game context에서도 Navigator, Movement, GameMode 순서를 유지하는지 첫 actual case의 input과 hit timing log로 확인한다.

## 구현 완료 보고 형식

구현자가 모든 gate를 통과한 뒤에만 아래 네 열 표를 actual-world log에서 옮긴다. 표 밖의 prose에 success count 11, collision과 timeout과 setup과 runtime error count 0, build와 Automation 명령 exit를 함께 보고한다.

| slide | success | elapsed | min wall distance |
|---:|:---:|---:|---:|

보고에는 Task별 commit SHA, 최종 branch와 clean status, protected map과 config hash 일치, 구현 중 확인된 동적 불확실성을 포함한다. push, merge, PR은 별도 승인 전 수행하지 않는다.
