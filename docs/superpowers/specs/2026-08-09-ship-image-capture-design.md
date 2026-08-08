# 5단계 선박 이미지 캡처 설계

## 문서 목적과 결정 상태

이 문서는 5단계 `UShipCapture` 이미지 저장 기능의 확정 설계다. 선박 전방의 컬러와 SceneDepth 캡처, 실제 시계 기반 100 ms 스케줄링, PNG 프레임 쌍과 `manifest.json`의 저장 계약, Stage 3/4와의 연결, 실패 처리 및 검증 기준을 정의한다.

과제 원문과 제공 PDF 전체, 현재 저장소의 README와 Unreal 안내 문서, Stage 3 이동 설계와 계획, Stage 4 자율주행 설계와 계획, 영구 환경 조명 설계와 계획, 현재 `UShipCapture`, `AShipPawn`, `ASimGameMode`, `ACourseBuilder`, Build.cs, Unreal 자동화 테스트와 TypeScript manifest 계약을 기준으로 작성했다. Unreal API는 이 PC에 설치된 UE 5.5.4 헤더, 소스와 셰이더에서 확인한 범위만 사용한다.

이 문서의 확정은 구현, 빌드, 에디터 실행, 이미지 생성, 성능 측정 또는 수동 화면 확인이 완료되었다는 뜻이 아니다. 이번 문서 작업에서는 Source, Config, map, uproject와 웹 뷰어를 변경하지 않는다.

## 결정 요약

선박의 공유 capture mount 아래에 컬러와 깊이 `USceneCaptureComponent2D`를 같은 identity transform으로 붙이고, 실제 시계가 100 ms를 채울 때마다 같은 논리 시각에 두 `CaptureScene()`을 호출한 뒤 공개 동기 readback, 8비트 PNG 인코딩과 임시 파일 rename으로 완전한 프레임 쌍만 manifest에 기록한다.

## 현재 기준선과 보존 계약

현재 `main`의 기준선은 다음과 같다.

- `UShipCapture`는 tick이 꺼진 빈 골격이다.
- `AShipPawn`은 box collision root, visual mesh, `UShipMovement`, `UShipNavigator`, spring arm과 player camera를 기본 subobject로 소유한다.
- `UShipMovement`만 runtime 선박 actor transform을 쓰며, sweep을 켠 `SetActorLocationAndRotation(..., true, ..., ETeleportType::None)` 한 경로를 사용한다.
- `UShipNavigator`는 `UShipMovement`보다 먼저 tick하고 `SetThrottle`과 `SetSteer`만 호출한다.
- `ASimGameMode`는 `ACourseBuilder`가 만든 3점 경로와 벽을 받아 선박을 생성하고 자동 주행을 시작한다.
- Stage 4 terminal 우선순위는 `Collision`, `Success`, `Timeout`, `Running` 순서이며 runtime 계산 오류는 Success만 막고 Timeout과 Collision은 허용한다.
- `ASimGameMode`는 `UShipMovement` 뒤에 tick해 blocking hit, 성공과 timeout을 판정한다.
- `ACourseBuilder::GetResolvedSlideCm()`와 `FShipCourseBuildResult::SlideCm`이 이번 실행의 실제 wall slide를 제공한다.
- 현재 웹 manifest validator는 필수 기존 필드만 읽고 알 수 없는 최상위 필드를 거부하지 않는다. `frame_count`는 1 이상이어야 하고 `frames.Num()`과 같아야 하며, index는 0부터 연속이고 `time_ms`는 0 이상 비감소여야 한다.
- MainLevel의 영구 조명은 `DirectionalLight`, `SkyLight`, `SkyAtmosphere` 세 actor로 고정돼 있다. Stage 5는 이 actor나 값을 변경하지 않는다.

Stage 5는 위 계약 위에 관측과 저장만 추가한다. 항법 입력, 이동 적분, Water query, 선박 actor transform, 충돌, terminal 선택과 영구 조명 값은 바꾸지 않는다.

## 목표

- Play만 누르면 기존 코스 생성과 자율주행에 이어 이미지 캡처가 자동 시작되게 한다.
- 선박 전방을 보는 컬러와 깊이 캡처가 위치, 방향, FOV와 해상도를 공유하게 한다.
- 기본 512 x 512 해상도에서 컬러 PNG와 8비트 grayscale 깊이 PNG를 100 ms 실제 경과 간격으로 저장한다.
- 컬러와 깊이를 같은 frame index, 같은 논리 capture instant와 같은 선박 transform에 묶는다.
- SceneDepth의 실수 view-Z 거리를 0 cm에서 5000 cm 사이로 clip하고 웹 뷰어의 가까울수록 밝고 따뜻한 계약에 맞게 8비트로 역선형 정규화한다.
- 실행마다 `Saved/ShipCaptures` 아래에 충돌하지 않는 폴더를 만들고, 완전하게 저장된 frame pair만 manifest에 넣는다.
- runtime calculation error와 capture runtime error는 각각 실패 상태로 latch하되 그 시점에 새 terminal을 만들거나 즉시 finalize하지 않는다. 기존 Stage 4 terminal 또는 EndPlay에 도달했을 때만 실패 manifest를 정확히 한 번 finalize한다.
- 기존 TypeScript 웹 뷰어가 소스 변경 없이 manifest와 프레임을 읽고 재생하게 한다.
- 동기 구현의 영향은 저장 켬과 끔의 동일 조건 A/B로 측정하되, 결과가 나빠도 별도 승인 없이 비동기 writer나 해상도 변경을 적용하지 않는다.

## 비목표

- 비동기 GPU readback, background writer, task graph, worker thread 또는 저장 큐를 구현하지 않는다.
- 512 x 512 기본 해상도를 낮추거나 adaptive resolution을 넣지 않는다.
- PNG를 JPEG, EXR, raw binary, video 또는 다른 압축 형식으로 바꾸지 않는다.
- 하나의 capture에 여러 출력을 packing하는 custom material이나 render pass를 만들지 않는다.
- PCG, Niagara, 보고서 생성, CSV writer, 새 웹 뷰어 기능 또는 웹 뷰어 source 변경을 포함하지 않는다.
- MainLevel, 영구 환경 조명, Water actor, Config 또는 uproject를 수정하지 않는다.
- Stage 3 이동 모델과 단일 transform writer, Stage 4 경로, 감속, terminal 의미와 우선순위를 재설계하지 않는다.
- Saved 결과, 임시 PNG, manifest 또는 측정 로그를 Git에 추가하지 않는다.

## 접근법 비교와 선택

### 접근법 A: 두 SceneCapture와 공개 동기 readback

컬러와 깊이 `USceneCaptureComponent2D`를 공유 mount에 붙인다. due 시점에 두 capture의 `CaptureScene()`을 연속 호출하고, 컬러는 `ReadPixels`, 깊이는 `ReadLinearColorPixels`로 game thread에서 동기 readback한다. 메모리에서 두 PNG를 모두 만든 다음 임시 파일 두 개를 쓰고 rename한다.

장점은 과제 문장을 그대로 구현하며 SceneDepth 의미, 프레임 쌍, 오류 위치와 저장 결과를 직접 검사할 수 있다는 점이다. 공개 UE API만 사용하고 현재 모듈 구조에 작은 변경으로 들어간다. 단점은 GPU flush, PNG 압축과 파일 I/O가 game thread를 막아 frame time에 영향을 줄 수 있다는 점이다.

이 접근법을 선택한다. 과제는 먼저 가장 단순한 동기 구현을 요구하며, 성능 영향은 구현 뒤 측정할 대상으로 명시한다.

### 접근법 B: 한 capture에 컬러와 깊이를 packing

`SCS_SceneColorSceneDepth`의 alpha 또는 custom post-process material을 사용해 한 render target에 두 출력을 packing한 뒤 분리할 수 있다.

render 호출 수를 줄일 가능성은 있지만 최종 LDR 컬러, 고정 노출과 SceneDepth 정밀도를 한 출력 계약에 섞는다. 과제의 컬러용과 깊이용 SceneCapture 두 개 요구에도 직접 맞지 않고 custom material과 변환 검증이 늘어난다. 선택하지 않는다.

### 접근법 C: 직접 RHI 또는 비동기 readback

staging texture나 GPU readback을 직접 관리하면 game-thread stall을 줄일 가능성이 있고 PNG writer도 별도 thread로 옮길 수 있다.

그러나 로컬 UE 5.5.4는 단일 채널 `PF_R32_FLOAT`를 공개 `ReadLinearColorPixels` 경로에서 이미 지원한다. 직접 RHI는 수명, 동기화와 platform 분기를 추가하고 비동기 writer도 이번 승인 범위 밖이므로, 측정 결과의 후속 대안으로만 남기고 선택하지 않는다.

## 전체 책임 흐름

런타임 순서는 다음으로 고정한다.

1. `ASimGameMode`가 현재 순서대로 course, wall과 ship을 준비하고 `EnterAutonomy`를 성공시킨다.
2. `AShipPawn`이 소유한 `UShipCapture`가 rig와 render target 설정을 검증한다.
3. GameMode가 `FShipCourseBuildResult::SlideCm`의 유한성과 `ACourseBuilder::GetResolvedSlideCm()`과의 일치를 검증한다. 실패하면 `StartCapture`를 호출하지 않고 `CaptureInitializationFailed` setup failure로 종료한다.
4. `StartCapture`가 unique run directory를 만들고 첫 frame pair를 즉시 캡처한다. 이 pair가 frame 0이고 `time_ms`는 0이다.
5. 첫 pair가 완전하게 commit된 경우에만 run을 active로 전환한다.
6. tick 순서는 Navigator, Movement, ShipCapture, SimGameMode다.
7. ShipCapture는 실제 시계 누적값이 100 ms에 도달한 tick에 pair를 한 개만 캡처한다.
8. 같은 tick의 GameMode가 terminal을 latch하면 기존 주행을 멈춘 다음 ShipCapture를 한 번 finalize한다.
9. 정상 경로를 벗어난 EndPlay도 active capture를 fail로 한 번 finalize한다.

## 파일 책임과 변경 경계

구현 단계의 예상 파일 책임은 다음과 같다. 이번 설계 커밋에서는 이 파일들을 수정하지 않는다.

| 파일 | 책임 |
| --- | --- |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h` | `UShipCapture`의 최소 public lifecycle, UPROPERTY 설정, UObject 참조와 상태 선언 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp` | rig 설정, 실제 시계 scheduler, SceneCapture 호출, readback, PNG 인코딩, pair commit, manifest finalize와 오류 latch |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.h` | UObject와 파일 I/O가 없는 scheduler, 깊이 정규화, 이름과 frame record 순수 계약 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp` | 순수 함수 구현 |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h` | capture mount, 두 SceneCapture, `UShipCapture`의 UPROPERTY 소유와 `GetCapture()` |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp` | 네 default subobject 생성, 공통 attachment와 rig bind |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h` | capture finalize helper와 test-only A/B gate 상태 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp` | wall slide 전달, tick prerequisite, 자동 start, terminal/setup/EndPlay finalize 호출 |
| `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigationTypes.h` | 기존 값 뒤에 `CaptureInitializationFailed` setup failure를 추가하되 terminal enum은 변경하지 않음 |
| `ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs` | `ImageCore`와 `Json` private dependency 추가, 기존 `ImageWrapper`, `RenderCore`, `RHI` 유지 |
| `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp` | scheduler, depth, rig, 파일, manifest, lifecycle와 실제 world 캡처 테스트 |
| 기존 `ShipMovementTests.cpp` | child component 설정을 단일 actor transform writer 위반으로 오인하지 않도록 정확한 capture mount 한 줄만 whitelist하고 ShipCapture actor mutator 0개를 검사 |
| 기존 Stage 4 테스트 | capture의 기본 자동 시작과 test-only capture-off 조건에서도 기존 terminal 계약이 유지되는지 검사 |
| `ShipAutonomySim/AGENTS.md` | Stage 4 전용 현재 단계 경계를 Stage 5 이미지 캡처 범위로 갱신하고 기존 로컬 엔진 확인, UObject, 의존성, 제품 파일 절대 경로 금지 규칙을 유지 |
| `README.md` | Unreal 범위가 Stage 4라는 문장을 Stage 5 캡처까지 구현된 현재 범위로 바꾸고 `Saved/ShipCaptures` 결과와 웹 뷰어 확인 절차를 최소 추가 |
| `ShipAutonomySim/SETUP.md` | 이미지 캡처가 Stage 4 범위 밖이라는 문장을 제거하고 자동 시작, Saved 실행 폴더, manifest와 컬러/깊이 파일 확인 절차를 최소 추가 |

웹 뷰어 source, 테스트와 더미 데이터 생성기는 변경하지 않는다. 호환성은 생성된 Stage 5 산출물을 현재 validator와 브라우저로 직접 읽어 검증한다.

## `UShipCapture` 인터페이스와 상태

### public 인터페이스

구현 public surface는 다음 역할로 제한한다. 함수명은 구현 계획에서 같은 의미를 유지해야 한다.

```cpp
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

- `BindCaptureRig`는 `AShipPawn` constructor가 default subobject 세 개를 연결하는 setup-only 호출이다. world나 파일을 만지지 않는다.
- `StartCapture`는 GameMode만 호출하며 rig, 설정, run directory와 첫 frame pair를 검증한다.
- `StopAndFinalize`는 여러 경로에서 호출 가능하지만 내부 상태 전이와 `bFinalizeAttempted`로 한 번만 실행된다.
- 외부에는 `CaptureScene`, readback, 파일 경로, frame append 또는 manifest serialize 함수를 노출하지 않는다.

### UPROPERTY 설정

`UShipCapture`가 다음 값을 `EditAnywhere`로 소유한다.

| 속성 | 기본값 | 제약과 의미 |
| --- | ---: | --- |
| `CaptureResolution` | 512 | 1 이상의 정사각 한 변, color/depth 공통 |
| `CaptureFovDegrees` | 90.0 | perspective horizontal FOV, 두 capture 공통 |
| `CaptureRelativeLocationCm` | (110, 0, 50) | 선박 bow보다 10 cm 앞, visual 내부를 피하는 공통 mount 위치 |
| `CaptureRelativeRotation` | (0, 0, 0) | 선박 local +X 전방을 보는 공통 mount 방향 |
| `CaptureIntervalMs` | 100 | 1 이상의 실제 시계 목표 간격, 기본과 제출값은 100 |
| `DepthNearCm` | 0.0 | normalization의 가까운 거리 경계, engine near clip plane이 아님 |
| `DepthFarCm` | 5000.0 | normalization의 먼 거리 경계, `DepthNearCm`보다 커야 함 |

제출 기본 계약은 512, 90 deg, 100 ms, 0 cm, 5000 cm다. 값이 유한하지 않거나 범위를 벗어나면 fallback으로 조용히 바꾸지 않고 `StartCapture`를 실패시킨다.

### UObject 소유와 수명

`AShipPawn`은 다음 default subobject를 `UPROPERTY(VisibleAnywhere)`의 `TObjectPtr`로 소유한다.

- `USceneComponent* CaptureMount`
- `USceneCaptureComponent2D* ColorCapture`
- `USceneCaptureComponent2D* DepthCapture`
- `UShipCapture* ShipCapture`

`UShipCapture`는 bind된 세 component와 runtime에 `NewObject<UTextureRenderTarget2D>(this)`로 만든 color/depth target을 `UPROPERTY(Transient)`의 `TObjectPtr`로 보관한다. GameMode는 ship을 통해 capture를 찾고 장기 raw UObject 포인터를 새로 저장하지 않는다.

frame metadata는 UObject 참조가 없는 값 struct와 `TArray`로 보관한다. run directory, clock, scheduler accumulator, failure와 finalize latch도 값 멤버다.

`UShipCapture` constructor는 `PrimaryComponentTick.bCanEverTick = true`, `PrimaryComponentTick.bStartWithTickEnabled = false`로 설정한다. 첫 pair가 commit된 뒤에만 `SetComponentTickEnabled(true)`를 호출하고, capture failure를 latch하거나 finalize를 시작할 때 즉시 false로 되돌린다. start 전과 finalize 뒤에는 scheduler tick이 실행되지 않는다.

## 공통 optical transform 보장

`CaptureMount`는 `CollisionRoot`에 한 번 붙이고 `CaptureRelativeLocationCm`과 `CaptureRelativeRotation`을 한 번 적용한다. color와 depth capture는 모두 같은 mount 아래 identity relative transform으로 붙인다.

이 구조의 불변식은 다음과 같다.

- 두 capture의 parent는 같은 `CaptureMount`다.
- 두 capture의 relative location은 zero, relative rotation은 zero, relative scale은 one이다.
- FOV는 `CaptureFovDegrees` 한 값을 두 component에 동시에 쓴다.
- texture target 크기는 `CaptureResolution` 한 값으로 두 target을 초기화한다.
- projection type은 perspective로 같다.
- capture pair 직전에 world transform, FOV, target size를 다시 비교한다. 불일치하면 프레임을 만들지 않고 capture failure를 latch한다.

`CaptureMount`의 relative transform 설정은 선박 actor transform 변경이 아니다. 기존 transform ownership 정적 테스트에는 이 한 줄을 정확한 component-only 예외로 추가하고, `ShipCapture.cpp`에서 actor transform mutator가 0개인지 별도로 고정한다. `UShipCapture`는 `AShipPawn`이나 root의 world transform을 쓰지 않는다.

## SceneCapture 설정

두 capture에 공통으로 다음을 적용한다.

- `bCaptureEveryFrame = false`
- `bCaptureOnMovement = false`
- `bAlwaysPersistRenderingState = false`
- perspective projection
- 같은 FOV와 같은 resolution
- automatic capture에 의존하지 않고 due 시점에만 `CaptureScene()` 호출

컬러 capture는 다음 계약을 사용한다.

- `CaptureSource = SCS_FinalColorLDR`
- color render target은 `InitCustomFormat(Resolution, Resolution, PF_B8G8R8A8, false)`
- `PostProcessBlendWeight = 1.0f`
- `PostProcessSettings.bOverride_AutoExposureMethod = true`
- `PostProcessSettings.AutoExposureMethod = AEM_Manual`
- `PostProcessSettings.bOverride_AutoExposureBias = true`
- `PostProcessSettings.AutoExposureBias = 0.0f`
- `PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true`
- `PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false`

이 설정은 capture 내부 노출만 고정한다. MainLevel의 DirectionalLight intensity 10, SkyLight intensity 1, actor transform, mobility, 색과 SkyAtmosphere 값을 변경하지 않는다.

깊이 capture는 다음 계약을 사용한다.

- `CaptureSource = SCS_SceneDepth`
- depth render target은 `InitCustomFormat(Resolution, Resolution, PF_R32_FLOAT, true)`
- 단일 R 채널에 SceneDepth의 32비트 실수값을 받는다.
- post-process 노출이나 환경 조명으로 깊이를 보정하지 않는다.

`PF_R32_FLOAT`는 pixel당 4 bytes이므로 512 x 512 depth target 자체는 1,048,576 bytes, 약 1 MiB다. 공개 `ReadLinearColorPixels`가 R의 32비트 실수값을 `FLinearColor.R`로 그대로 반환하는 로컬 5.5.4 경로를 사용한다. 성능을 이유로 승인 없이 half float, direct RHI 또는 비동기 경로로 바꾸지 않는다.

## 로컬 UE 5.5.4 API 근거

설치된 엔진의 `Engine/Build/Build.version`은 5.5.4, changelist 40574608이다. 사용 근거는 다음과 같다.

| 기능 | 확인한 선언 또는 구현 |
| --- | --- |
| 실제 시계 | `HAL/PlatformTime.h`가 Windows에서 `FWindowsPlatformTime`을 선택하고 `static FORCEINLINE double Seconds()`는 `QueryPerformanceCounter`를 사용한다. 구현은 `Windows/WindowsPlatformTime.h`다. |
| 수동 capture | `USceneCaptureComponent2D::CaptureScene()`은 `SceneCaptureComponent2D.h`에서 `ENGINE_API void CaptureScene();`로 선언되며 즉시 texture target에 render한다고 설명한다. |
| automatic capture 차단 | `SceneCaptureComponent.h`에 `bCaptureEveryFrame`과 `bCaptureOnMovement`가 `UPROPERTY`로 선언돼 있다. |
| capture source | `EngineTypes.h`의 `ESceneCaptureSource`에 `SCS_FinalColorLDR`와 `SCS_SceneDepth`가 있고 후자는 `SceneDepth in R`다. |
| SceneDepth 의미 | `SceneCapturePixelShader.usf`는 SceneDepth mode에서 `float4(CalcSceneDepth(UV), 0, 0, 0)`를 쓴다. `SceneTexturesCommon.ush`는 `CalcSceneDepth`를 device Z에서 변환한 view-Z world distance로 설명한다. UE 기본 world unit이 cm이므로 값 단위는 cm다. 이는 광선 길이가 아니라 camera-forward view-Z 거리다. |
| render target 초기화 | `TextureRenderTarget2D.h`의 `void InitCustomFormat(uint32, uint32, EPixelFormat, bool)`를 사용한다. |
| render target resource | `TextureRenderTarget.h`의 `ENGINE_API FTextureRenderTargetResource* GameThread_GetRenderTargetResource();`로 game thread에서 resource pointer를 얻고 null을 검사한 뒤 공개 readback 함수를 호출한다. |
| color readback | `FRenderTarget::ReadPixels(TArray<FColor>&, FReadSurfaceDataFlags, FIntRect)`가 `UnrealClient.h`에 공개돼 있다. U8 surface 값은 그대로 반환한다고 명시한다. |
| depth readback | `UnrealClient.h`의 `ENGINE_API virtual bool ReadLinearColorPixels(TArray<FLinearColor>&, FReadSurfaceDataFlags, FIntRect)`를 `RCM_MinMax`로 호출한다. 주석은 `RCM_MinMax`가 값을 바꾸지 않고 반환한다고 명시하며, `UnrealClient.cpp` 구현은 `RHICmdList.ReadSurfaceData`를 enqueue한 뒤 `FlushRenderingCommands()`하고 결과가 있으면 true를 반환한다. |
| 32비트 format 선택 | `RHISurfaceDataConversion.h`의 `ConvertRAWSurfaceDataToFLinearColor`에는 `Format == PF_R32_FLOAT` 분기가 있다. 각 source float를 `FLinearColor(SrcPtr[0], 0.f, 0.f, 1.f)`로 복사하고 true를 반환하며 MinMax/UNorm remap을 하지 않는다. 따라서 `RCM_MinMax`와 함께 R의 SceneDepth cm를 공개 동기 경로로 보존한다. |
| PNG encode | `FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName(TEXT("ImageWrapper")))`로 module을 얻고 `virtual bool CompressImage(TArray64<uint8>&, EImageFormat, const FImageView&, int32 Quality = 0)`를 사용한다. `ImageCore.h`의 `FImageView`는 color `BGRA8`와 depth `G8`을 표현하고 `PngImageWrapper.cpp`는 둘을 직접 지원한다. deprecated `CreateImageWrapper`, `SetRaw`, `GetCompressed` 조합은 사용하지 않는다. |
| binary file write | `FFileHelper::SaveArrayToFile(TArrayView64<const uint8>, const TCHAR*, IFileManager*, uint32)`를 사용한다. |
| JSON write | `FJsonObject`, `TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create`와 `FJsonSerializer::Serialize(const TSharedRef<FJsonObject>&, ...)`로 `FString`을 만든 뒤 `FFileHelper::SaveStringToFile(FStringView, const TCHAR*, EEncodingOptions, IFileManager*, uint32)`를 `ForceUTF8WithoutBOM`으로 호출한다. |
| Saved path | `FPaths::ProjectSavedDir()`가 현재 프로젝트 Saved directory를 반환한다. 절대 경로 문자열을 source에 넣지 않는다. |
| directory와 rename | `IFileManager::MakeDirectory(const TCHAR*, bool Tree)`와 `Move(const TCHAR* Dest, const TCHAR* Src, bool Replace, bool EvenIfReadOnly, bool Attributes, bool bDoNotRetryOrError)`를 사용한다. |
| 실행 시각과 unique suffix | `FDateTime::UtcNow()`, `FDateTime::ToString(const TCHAR*)`, `FGuid::NewGuid()`와 `FGuid::ToString(EGuidFormats::Digits)`를 사용한다. |

모듈 의존성은 다음과 같이 고정한다.

| 모듈 | 이유 |
| --- | --- |
| `Core`, `CoreUObject`, `Engine` | clock, path/file, UObject, SceneCapture와 render target |
| `RenderCore`, `RHI` | pixel format과 `FReadSurfaceDataFlags(RCM_MinMax)` |
| `ImageCore` | `FImageView`, `ERawImageFormat::BGRA8`, `ERawImageFormat::G8` |
| `ImageWrapper` | `IImageWrapperModule::CompressImage`와 PNG |
| `Json` | manifest DOM, writer와 serializer |

현재 Build.cs의 `ImageWrapper`, `RenderCore`, `RHI`는 유지하고 private dependency에 `ImageCore`와 `Json`만 추가한다. `JsonUtilities`는 필요하지 않다.

## 실제 시계 scheduler와 `time_ms`

### clock 상태

scheduler는 `DeltaTime`, world time, game time dilation, pause time 또는 Stage 4의 `ElapsedRunSeconds`를 사용하지 않는다. 오직 `FPlatformTime::Seconds()`의 차이를 double로 누적한다.

상태는 다음과 같다.

- `LastClockSeconds`: 직전 실제 시계 관측값
- `AccumulatedRealSeconds`: 마지막 성공 capture 뒤 누적한 실제 시간
- `FirstCaptureSeconds`: frame 0의 논리 capture instant
- `LastCommittedTimeMs`: 마지막 manifest frame의 실제 timestamp

`StartCapture`는 `Now = FPlatformTime::Seconds()`를 한 번 읽고 첫 pair의 공통 timestamp로 사용한다. 첫 pair가 commit되면 `FirstCaptureSeconds = Now`, `LastClockSeconds = Now`, `AccumulatedRealSeconds = 0`, frame 0의 `time_ms = 0`으로 고정한다.

### tick 규칙

각 capture tick은 `Now`를 한 번만 읽는다.

1. `Elapsed = Now - LastClockSeconds`를 계산한다.
2. clock 값과 차이가 유한하고 0 이상인지 검사한다.
3. `AccumulatedRealSeconds += Elapsed`, `LastClockSeconds = Now`로 갱신한다.
4. 누적값이 `CaptureIntervalMs / 1000.0`보다 작으면 종료한다.
5. threshold 이상이면 같은 tick에서 pair를 정확히 한 개만 캡처한다.
6. trigger 시 누적 backlog를 0으로 버린다. `while` 반복, nominal frame 보충과 같은 tick catch-up은 하지 않는다.
7. 다음 `time_ms`는 `RoundToInt64((Now - FirstCaptureSeconds) * 1000.0)`로 계산한다. `index * interval_ms`를 쓰지 않는다.

이 hitch 정책은 누락된 100 ms slot을 뒤늦게 복제하지 않는다. 550 ms hitch 뒤에도 pair는 한 개만 생기며, 다음 pair는 그 trigger 뒤 실제 시간이 다시 100 ms 누적된 후에만 생긴다. 따라서 frame index는 저장된 관측 수이고 `time_ms`는 실제 capture 시작 시각의 상대값이다. frame 간 실제 간격이 100 ms보다 커질 수는 있지만 짧은 catch-up burst나 같은 장면의 duplicate는 만들지 않는다.

clock 역행, NaN, infinity 또는 frame 1 이후 `time_ms <= LastCommittedTimeMs`가 발생하면 capture failure를 latch하고 추가 저장을 중단한다. QPC 기반 Windows 구현은 monotonic source지만 입력 검사는 유지하며, 저장된 timestamp는 frame 0 뒤에 엄격히 증가한다.

## tick 순서와 terminal 계약

현재 Navigator와 Movement의 prerequisite를 보존하고 다음 두 edge를 추가한다.

- `ShipCapture->AddTickPrerequisiteComponent(ShipMovement)`
- `ASimGameMode::AddTickPrerequisiteComponent(ShipCapture)`

기존 `ASimGameMode::AddTickPrerequisiteComponent(ShipMovement)`도 유지한다. 결과 순서는 다음과 같다.

| 순서 | 책임 |
| ---: | --- |
| 1 | `UShipNavigator`가 이번 tick 제어 입력을 계산한다. |
| 2 | `UShipMovement`가 sweep을 사용해 선박 transform을 유일하게 갱신한다. |
| 3 | `UShipCapture`가 due이면 이동 후 transform에서 pair를 저장한다. |
| 4 | `ASimGameMode`가 blocking hit, Success와 Timeout을 기존 우선순위로 판정한다. |

terminal tick에서 scheduled capture가 due이면 terminal 판정보다 먼저 이동 후 상태가 저장된다. due가 아니면 terminal 때문에 별도 frame을 강제로 추가하지 않는다. 강제 terminal capture는 직전 frame과 100 ms보다 가까운 duplicate를 만들 수 있으므로 제외한다.

GameMode는 기존 terminal result를 먼저 latch하고 Navigator와 입력을 멈춘 뒤 `StopAndFinalize`를 호출한다. manifest의 `result`는 simulation terminal이 Success이고 capture 오류가 없을 때만 `success`이며, Collision, Timeout, runtime 오류가 있었거나 capture 오류가 latch됐으면 `fail`이다. capture 저장 실패가 Stage 4의 `EShipRunResult::Success`를 다른 terminal로 바꾸지는 않는다.

Stage 4 timeout은 기존 game elapsed 의미를 유지한다. capture interval과 manifest `time_ms`만 실제 시계 의미를 가진다.

## 한 frame pair의 capture와 readback

pair 하나는 다음 transaction으로 처리한다.

1. frame index와 공통 `CaptureSeconds`를 값으로 고정한다.
2. 두 capture의 world transform, FOV, target resolution을 비교한다.
3. `ColorCapture->CaptureScene()`을 호출한다.
4. world tick이나 actor transform 쓰기 없이 바로 `DepthCapture->CaptureScene()`을 호출한다.
5. 두 target의 `GameThread_GetRenderTargetResource()` 결과가 null이 아닌지 확인하고 color resource에서 `ReadPixels`로 `TArray<FColor>`를 읽는다.
6. depth resource에서 `ReadLinearColorPixels`와 `FReadSurfaceDataFlags(RCM_MinMax)`로 `TArray<FLinearColor>`를 읽는다.
7. 두 배열이 정확히 `Resolution * Resolution`인지 확인한다.
8. color는 `FImageView(FColor*, Width, Height, EGammaSpace::sRGB)`로 PNG 메모리를 만든다.
9. depth R을 순수 normalization 함수로 `TArray<uint8>` G8 buffer로 만든 뒤 `FImageView(..., ERawImageFormat::G8)`로 PNG 메모리를 만든다.
10. 두 PNG 메모리가 모두 성공한 뒤에만 파일 transaction을 시작한다.

두 `CaptureScene()`은 물리적으로 한 GPU instruction에 동시에 실행되지 않는다. 이 설계에서 같은 capture instant는 하나의 game tick, 하나의 선박 transform snapshot, 하나의 clock timestamp를 뜻한다. 두 호출 사이에는 world tick, navigation, movement, terminal 판정 또는 파일 readback이 없으므로 장면 상태가 바뀌지 않는다. frame record도 하나만 만든다.

## 깊이 정규화

입력은 `SCS_SceneDepth`가 R에 기록한 camera-forward view-Z cm 값이다. normalization은 다음 순서다.

1. `DepthNearCm`과 `DepthFarCm`이 유한하고 `Near < Far`인지 start 전에 검사한다.
2. raw depth가 유한하지 않으면 먼저 `DepthFarCm`으로 치환한다. 하늘과 infinity는 여기로 들어온다.
3. 유한 raw depth를 `[DepthNearCm, DepthFarCm]`로 clip한다.
4. 거리 비율 `t = (clipped - near) / (far - near)`를 계산한다.
5. 웹 뷰어가 밝은 값을 가까운 깊이로 해석하므로 `intensity = RoundToInt((1.0 - t) * 255.0)`로 역선형 정규화한다.

따라서 기본값에서 0 cm 이하는 255, 2500 cm는 약 128, 5000 cm 이상과 non-finite/sky는 0이다. 하늘의 raw 무한값은 항상 far 경계로 제한돼 유효 물체의 분포를 재정규화하거나 overflow시키지 않는다.

이 방향은 현재 README와 `src/depth.ts`의 밝은 값은 가까운 깊이, 255는 따뜻한 색이라는 계약을 보존한다. manifest의 near/far는 실제 거리 경계이며, PNG intensity를 다시 cm로 해석할 때는 `depth_cm = far - intensity / 255 * (far - near)`를 사용한다.

## run directory와 파일 이름

output root는 source에 절대 경로를 넣지 않고 다음처럼 만든다.

```text
FPaths::ProjectSavedDir()/ShipCaptures/<UTC timestamp>_<GUID digits>/
```

폴더 이름의 timestamp는 `FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S%sZ"))` 형식이고, 같은 millisecond의 중복을 막기 위해 `FGuid::NewGuid().ToString(EGuidFormats::Digits)`를 suffix로 붙인다. 폴더 충돌 시 덮어쓰지 않고 start failure다.

확정 frame 이름은 다음뿐이다.

```text
color_000000.png
depth_000000.png
color_000001.png
depth_000001.png
```

index는 6자리 zero padding을 사용한다. 999999를 넘기면 이름 폭을 자동 확장하지 않고 capture failure로 종료한다. 기본 100 ms와 45 s timeout에서는 이 한계에 도달하지 않는다.

manifest의 color와 depth에는 absolute path나 Saved prefix를 쓰지 않고 같은 폴더 기준 leaf filename만 쓴다.

## frame pair commit과 부분 실패

filesystem은 파일 두 개를 한 번에 원자적으로 rename할 수 없으므로 원자성은 manifest 공개 단위로 정의한다.

각 index에서 다음 순서를 지킨다.

1. color와 depth PNG를 모두 메모리에 인코딩한다.
2. `.color_000000.png.tmp`와 `.depth_000000.png.tmp`에 각각 `SaveArrayToFile`한다.
3. 두 temp 파일이 존재하고 크기가 0보다 큰지 확인한다.
4. `Move`를 `Replace=false`로 호출해 color와 depth를 최종 이름으로 바꾼다.
5. 두 최종 파일이 모두 존재할 때만 frame record를 `Frames`에 append한다.
6. 두 번째 rename이 실패하면 이번 index에서 만든 첫 최종 파일과 temp만 정확한 경로로 정리하고 record를 append하지 않는다.

정상 실행 중에는 `manifest.json`이 terminal까지 존재하지 않으므로, 소비자가 공식 manifest를 통해 부분 pair를 볼 수 없다. process crash로 orphan temp나 manifest 없는 PNG가 남을 수는 있지만 다음 run은 unique directory를 사용하므로 섞이지 않는다.

불변식은 다음과 같다.

- `frame_count == Frames.Num()`
- `Frames[Index].index == Index`
- 각 record의 color와 depth 최종 파일이 모두 존재한다.
- frame record append 전에는 `NextFrameIndex`를 증가시키지 않는다.
- 실패한 index와 그 이후 index는 manifest에 넣지 않는다.
- 이미 commit된 이전 pair는 capture 오류가 생겨도 삭제하지 않는다.

readback, array size, depth 변환, PNG encode, temp write 또는 rename 중 하나라도 실패하면 이번 pair를 commit하지 않고 capture failure를 latch한다. 이후 scheduler tick을 끄고 terminal 또는 EndPlay의 finalize만 기다린다.

## `manifest.json` 계약

manifest는 terminal 또는 fallback finalize에서 메모리 DOM을 완성한 뒤 한 번 쓴다. 키와 형식은 다음과 같다.

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
      "time_ms": 117
    }
  ]
}
```

`interval_ms`는 목표 scheduler interval이고 각 `time_ms`는 실제 capture 시작 상대시각이다. hitch가 있으면 둘째 값이 정확히 100의 배수가 아닐 수 있다. `wall_slide_cm`은 GameMode가 course build 직후 넘긴 유한 값의 복사본이며 capture가 CourseBuilder를 다시 조회하지 않는다.

manifest에도 `.manifest.json.tmp`를 사용한다. JSON은 `ForceUTF8WithoutBOM`으로 temp에 저장하고, 같은 directory의 final 이름으로 rename까지 성공할 때만 `manifest.json`이 나타난다. 이 정책은 final 경로에 truncated JSON을 직접 쓰지 않는 publication 규칙이지 모든 filesystem에서 rename의 원자성을 보장한다는 주장은 아니다. final manifest write가 실패하면 invalid 또는 truncated `manifest.json`을 남기지 않고 error를 로그로 남긴다. 이미 저장된 frame pair는 진단을 위해 보존한다.

현재 TypeScript validator는 `capture_resolution`, `wall_slide_cm`, `result`를 모르는 추가 필드로 허용하고 기존 필드를 그대로 읽는다. leaf filename은 manifest와 같은 origin의 상대 URL로 해석되므로 구조적으로 그대로 호환된다.

현재 validator는 `frame_count >= 1`을 요구한다. 그러므로 first pair를 commit하지 못한 start failure에는 `frame_count: 0` manifest를 게시하지 않는다. `StartCapture`가 false를 반환하고 GameMode가 `CaptureInitializationFailed` setup failure를 기록하며, run directory에는 유효한 manifest가 없다는 로그를 남긴다. 이는 빈 manifest를 성공한 데이터셋처럼 노출하는 것보다 명시적이다.

## start, stop과 finalize lifecycle

내부 상태는 최소한 `NotStarted`, `Capturing`, `CaptureFailed`, `Finalizing`, `Finalized`를 구분한다.

### start

`StartCapture`는 다음 조건을 모두 만족해야 성공한다.

- 호출 상태가 `NotStarted`다.
- owner, mount, 두 SceneCapture와 두 target이 유효하다.
- 공통 optics와 UPROPERTY 설정이 유효하다.
- wall slide가 유한하다.
- unique Saved directory를 만들었다.
- frame 0 color/depth pair를 완전하게 commit했다.

GameMode는 course build, ship spawn, possession과 `EnterAutonomy`가 성공한 뒤 capture를 마지막 setup 단계로 시작한다. 성공 후에만 `bRunActive = true`로 둔다. Play의 기본 경로에는 capture-off 설정이 없으므로 사용자 조작 없이 자동 시작한다.

### setup failure

capture 시작 전의 slide parse, Water, course, player controller, ship spawn 또는 autonomy failure에서는 capture가 active가 아니므로 finalize 호출은 안전한 no-op이다. 필수 wall slide와 첫 frame이 없는 가짜 manifest는 만들지 않는다.

build result의 slide가 non-finite이거나 CourseBuilder의 resolved slide와 절대 오차 `1e-9 cm` 안에서 같지 않으면 capture context validation failure다. GameMode는 이 경우 `StartCapture`를 호출하지 않고 `CaptureInitializationFailed`를 기록하며 run directory나 빈 manifest를 만들지 않는다.

capture 자체의 rig, directory 또는 첫 pair failure도 `CaptureInitializationFailed`로 기록한다. 이 값은 기존 setup enum 끝에 추가해 기존 numeric 의미를 보존한다. `EShipRunResult`와 terminal 우선순위는 변경하지 않는다.

`RecordSetupFailure`는 방어적으로 `StopAndFinalize(false)`를 호출하되, active가 아니면 파일을 쓰지 않는 idempotent 계약을 사용한다.

### terminal과 runtime failure

- Success는 `StopAndFinalize(true)`를 호출한다.
- Collision과 Timeout은 `StopAndFinalize(false)`를 호출한다.
- runtime calculation error는 GameMode의 기존 runtime error state에 실패를 latch하고 Navigator와 입력을 멈추지만 새 terminal을 만들거나 그 시점에 finalize하지 않는다. 이후 기존 Timeout 또는 Collision, 또는 먼저 발생한 EndPlay에서 `StopAndFinalize(false)`를 한 번 호출한다.
- capture runtime error는 `UShipCapture`의 최초 실패 원인과 index를 latch하고 추가 capture만 멈춘다. 항법과 Stage 4 terminal은 그대로 진행하며 오류 시점에는 finalize하지 않는다. 이후 기존 Stage 4 terminal 또는 EndPlay에서 commit된 이전 pair를 `result: "fail"`로 한 번 finalize한다.

### EndPlay fallback

GameMode와 `UShipCapture::EndPlay` 모두 idempotent finalize 경로를 가진다. terminal 전에 사용자가 PIE를 멈추거나 actor/world가 종료되면 active capture를 fail로 finalize한다. GameMode terminal finalize가 이미 끝났으면 EndPlay는 아무 파일도 다시 쓰지 않는다.

engine crash, OS process kill 또는 전원 손실에서는 EndPlay를 보장할 수 없다. 이 경우 manifest 없는 pair나 temp가 남을 수 있다는 제한을 숨기지 않는다.

## failure와 로그 정책

capture failure는 최초 원인과 발생 index를 한 번 latch하고 추가 capture를 막는다. 같은 오류를 매 tick 반복 로그하지 않는다.

구분할 최소 failure category는 다음과 같다.

- capture context validation failure인 non-finite 또는 mismatched wall slide
- invalid configuration 또는 rig mismatch
- clock invalid 또는 timestamp regression
- directory create 또는 path collision
- color/depth capture target unavailable
- color/depth readback failure 또는 pixel count mismatch
- depth normalization failure
- color/depth PNG encode failure
- temp write failure
- frame rename 또는 cleanup failure
- manifest serialize, write 또는 rename failure

로그에는 category, frame index와 run directory의 프로젝트 상대 위치만 남긴다. 사용자명, 홈 경로, 외부 ID, 제공 문서의 식별자 또는 absolute path를 넣지 않는다. error text를 manifest schema에 임의 필드로 추가하지 않는다.

## GameMode와 CourseBuilder 연결

`ACourseBuilder`는 wall slide를 이미 `FShipCourseBuildResult::SlideCm`와 `GetResolvedSlideCm()`로 제공한다. Stage 5를 위해 CourseBuilder에 writer, capture pointer 또는 result 책임을 넣지 않는다.

GameMode는 build success 직후 다음을 검사한다.

- `BuildResult.SlideCm`이 유한하다.
- `CourseBuilder->GetResolvedSlideCm()`이 유한하다.
- `BuildResult.SlideCm`과 `CourseBuilder->GetResolvedSlideCm()`이 절대 오차 `1e-9 cm` 안에서 같다.

하나라도 실패하면 capture context validation failure로 분류해 `CaptureInitializationFailed`를 기록하고 `StartCapture`를 호출하지 않으며 폴더나 빈 manifest를 만들지 않는다. 모두 통과한 값만 `StartCapture(BuildResult.SlideCm)`에 전달하고 ShipCapture는 값 복사만 보관한다. terminal result는 GameMode가 기존 방식으로 선택하고 bool success/fail 의미만 finalize에 전달한다.

이 경계로 CourseBuilder는 course 생성, GameMode는 실행 조율과 terminal, ShipCapture는 데이터셋 저장 책임을 유지한다.

## 웹 뷰어 호환과 재생 절차

현재 TypeScript 웹 뷰어와의 호환 결과는 다음과 같다.

- 기존 필수 key의 이름과 type이 같다.
- 새 top-level key 세 개는 validator가 무시하므로 parse를 깨지 않는다.
- frame index, filename, 상대 URL과 실제 `time_ms` 계약이 기존 validator를 만족한다.
- color PNG는 브라우저가 읽는 8비트 color PNG다.
- depth PNG는 현재 preloader와 canvas가 읽는 8비트 grayscale PNG다.
- near가 255, far가 0인 encoding은 README의 밝은 값은 가까운 깊이와 `src/depth.ts`의 high intensity warm colormap 의미를 보존한다.

뷰어가 manifest URL을 선택하는 기능은 없고 저장소 root의 `./manifest.json`을 읽는다. 수동 재생은 다음 절차를 사용한다.

1. terminal 뒤 생성된 unique Saved run directory를 고른다.
2. `npm ci`와 `npm run build`로 현재 뷰어를 준비한다.
3. 선택한 run의 `manifest.json`, `color_*.png`, `depth_*.png`를 저장소 root에 복사한다. 이 이름들은 `.gitignore` 대상이다.
4. 저장소 root에서 `python -m http.server 8000`을 실행한다.
5. `http://localhost:8000`을 열어 전체 frame preload, color/depth 동기 재생, frame 이동, 실제 경과 표시와 depth colormap을 확인한다.
6. 확인 뒤 저장소 root에 복사한 생성물만 정확히 제거하고 `git status --short`가 clean인지 확인한다.

구현 단계에서 `README.md`, `ShipAutonomySim/SETUP.md`, `ShipAutonomySim/AGENTS.md`를 파일 책임 표의 범위대로 반드시 갱신한다. manifest loader, query parameter, UI 또는 colormap 코드는 바꾸지 않는다.

## 완료 검증 실행 순서

완료 검증은 다음 순서를 바꾸지 않는다. 앞 단계가 실패하거나 예상 밖 tracked 또는 untracked 파일이 생기면 다음 단계로 진행하지 않고 보고한다.

1. 첫 단계로 저장소 root의 PowerShell에서 정확히 다음 UE 5.5.4 editor target build를 실행한다.

```powershell
& "$env:ProgramFiles\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" ShipAutonomySimEditor Win64 Development "-Project=$((Resolve-Path 'ShipAutonomySim\ShipAutonomySim.uproject').Path)" -WaitMutex
```

   process exit code 0과 compile error 0을 모두 확인한다. compiler 또는 SDK warning은 별도 기록하되 error와 섞어 성공으로 숨기지 않는다. build가 통과한 뒤 editor 실행 전 MainLevel, Config와 uproject hash 기준선을 기록한다.
2. 두 번째 단계로 Automation을 실행한다. 별도 invocation의 command는 각각 `-ExecCmds="Automation RunTests ShipAutonomySim.ShipCapture;SoftQuit;"`, `-ExecCmds="Automation RunTests ShipAutonomySim.ShipMovement;SoftQuit;"`, `-ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation;SoftQuit;"`로 고정한다. 각 command는 `Automation` prefix를 한 번만 쓰고 마지막에 unprefixed `SoftQuit`을 둔다. 등록된 expected test count와 actual-world 회귀 case가 모두 실행됐는지, zero failure/error/unknown/ensure, `TEST COMPLETE. EXIT CODE: 0`과 정상 shutdown을 확인한다.
3. 세 번째 단계로 `/Game/Maps/MainLevel`을 `-nowrite -ExecCmds="QUIT_EDITOR"`로 load한다. exit code 0, world 생성과 cleanup, MapCheck, LoadErrors 0, Fatal/ensure/crash 0과 clean exit를 확인하고 MainLevel, Config, uproject hash와 Git 상태가 기준선과 같은지 비교한다.
4. 세 단계가 모두 통과한 뒤에만 성능 A/B와 사람의 viewport 및 웹 뷰어 확인을 수행한다.

## TDD와 자동 검증

구현은 RED, GREEN 순서를 지키고 각 실패가 목표 계약을 직접 증명하게 한다.

### 순수 unit tests

`ShipCaptureSimulation`에 다음 case를 먼저 RED로 추가한다.

- first frame timestamp가 0이다.
- 99 ms 누적에서는 due가 아니고 100 ms에서 정확히 한 번 due다.
- 550 ms hitch가 한 frame만 만들고 같은 tick catch-up을 하지 않는다.
- hitch 뒤 다음 frame은 새로 100 ms 실제 시간이 쌓인 후에만 due다.
- `time_ms`는 index 곱이 아니라 주입한 clock 차이다.
- clock 역행, NaN과 infinity를 거부한다.
- near, midpoint, far, 범위 밖과 non-finite depth를 정확히 255, 약 128, 0, clip 값과 0으로 바꾼다.
- `near >= far`와 non-finite bounds를 거부한다.
- frame 이름이 정확히 6자리며 999999 다음을 거부한다.
- frame append 전후 `frame_count`, index와 filename 불변식이 유지된다.

순수 함수는 `FPlatformTime`을 직접 부르지 않고 `NowSeconds`를 값으로 받는다. production `UShipCapture`만 실제 API에서 값을 읽어 전달한다. clock interface나 service abstraction은 만들지 않는다.

### component와 lifecycle tests

- `AShipPawn`에 mount, color/depth capture와 `UShipCapture`가 각 한 개 있다.
- 두 capture가 같은 parent, identity relative transform, 같은 world transform, FOV와 resolution을 가진다.
- `bCaptureEveryFrame`과 `bCaptureOnMovement`가 둘 다 false다.
- color/depth capture source와 target format이 각각 `SCS_FinalColorLDR`와 `PF_B8G8R8A8`, `SCS_SceneDepth`와 `PF_R32_FLOAT`인지 검사한다.
- color exposure method, bias와 physical camera flag가 고정값이다.
- Capture waits for Movement, GameMode waits for Capture prerequisite가 존재한다.
- `StartCapture` 중복 호출, finalize 중복 호출과 finalize-before-start가 안전하다.
- non-finite `BuildResult.SlideCm`, non-finite resolved slide와 절대 오차 `1e-9 cm` 밖 mismatch는 모두 `CaptureInitializationFailed`가 되고 `StartCapture`, run directory와 빈 manifest가 생기지 않는다.
- GameMode Success는 success, Collision/Timeout/runtime calculation error/capture runtime error는 fail manifest를 선택하되 Stage 4 terminal을 덮어쓰지 않는다.
- runtime calculation error와 capture runtime error 발생 순간에는 `StopAndFinalize`가 호출되지 않고, 이후 기존 terminal 또는 EndPlay에서만 정확히 한 번 호출된다.
- early setup failure는 폴더나 빈 manifest를 게시하지 않는다.
- EndPlay fallback이 active run을 한 번만 fail finalize한다.

기존 transform ownership 테스트에는 capture mount component 설정 한 줄만 허용하고 actor transform mutator 수는 `ShipMovement.cpp` 1개, `ShipCapture.cpp` 0개로 고정한다.

### image와 manifest structural tests

작은 test world와 낮은 test resolution을 사용하되 production 기본값은 바꾸지 않는다.

- color/depth `CaptureScene`과 sync readback이 각각 정확한 pixel count를 반환한다.
- depth target의 `GetFormat()`이 `PF_R32_FLOAT`이며 `ReadLinearColorPixels(..., RCM_MinMax)` 결과의 R이 알려진 SceneDepth float를 유지하고 변환된 G/B는 0, A는 1인지 검사한다.
- color PNG signature와 dimensions가 맞고 browser-readable 8-bit color다.
- depth PNG를 `ImageWrapper`로 다시 decode했을 때 `ERawImageFormat::G8`, bit depth 8과 dimensions가 맞다.
- known near geometry가 far background보다 큰 grayscale intensity를 가진다.
- non-finite 또는 sky sample 순수 변환은 far intensity 0이다.
- 두 temp 중 하나의 write/rename을 의도적으로 실패시켜 incomplete pair가 `frames`와 `frame_count`에 들어가지 않는지 확인한다.
- final manifest의 key 집합, `capture_resolution` 두 원소, slide, result, frame array와 실제 파일 존재를 검사한다.
- manifest temp write failure에서 truncated `manifest.json`이 나타나지 않는지 확인한다.
- 생성 파일은 `Saved/ShipCaptures/Automation/<unique>` 아래에만 만들고 테스트가 자신이 만든 정확한 directory만 정리한다.

### actual-world integration

MainLevel, 실제 Water, 고정 조명, 실제 course, Navigator와 Movement를 사용해 `?Stage4Slide=-500`, `?Stage4Slide=0`, `?Stage4Slide=500` 세 capture-on 사례를 각각 fresh world에서 실행한다. 세 사례 모두 production 기본 512 x 512와 100 ms를 유지하며 과도한 11개 full capture-on sweep으로 넓히지 않는다.

- Play 시작 뒤 별도 입력 없이 ship, autonomy와 capture가 모두 active가 된다.
- 세 사례가 모두 기존 terminal Success에 도달하고 capture failure count가 0이다.
- setup failure, runtime calculation error, Collision, Timeout, ensure와 crash가 없다.
- 각 manifest가 terminal 뒤에 한 번만 나타나고 `result`가 success이며 `wall_slide_cm`이 해당 `-500`, `0`, `500` cm와 일치한다.
- 각 사례에서 `frame_count >= 1`, color 수, depth 수와 frames 수가 모두 같다.
- 모든 frame 파일명이 6자리 연속 index이며 각 pair dimensions가 512 x 512다.
- `time_ms[0] == 0`, 이후 값은 엄격히 증가하며 hitch에서 같은 timestamp의 burst duplicate가 없다.
- color와 depth rig의 optical equality를 runtime assertion/log로 확인한다.
- 각 사례의 minimum wall gap이 0보다 크고 Stage 4 terminal elapsed와 slide 의미가 기존과 같다.

기존 11-slide Stage 4 sweep은 test-only URL option `?Stage5Capture=0`으로 실행해 항법 자체 회귀를 분리한다. GameMode는 기존 `Stage4Slide`와 같은 `UGameplayStatics::HasOption`과 `ParseOption` 경로를 쓰되 이 option은 `WITH_DEV_AUTOMATION_TESTS`에서만 해석한다. option이 없거나 production build이면 항상 capture-on이므로 product Play 기본 경로에는 capture-off가 없다.

## 성능 A/B 측정

성능 검증은 최적화 적용이 아니라 현재 동기 구현의 비용을 수치로 남기는 단계다.

같은 UE build, MainLevel, fixed slide 0 cm, 512 x 512, FOV, 조명과 command-line 조건에서 네 fresh-world run을 사용한다. 단일 실행 순서의 warm-up과 system load 편향을 줄이기 위해 첫 쌍은 A-B, 둘째 쌍은 B-A 순서로 고정한다.

- A: Stage 5 capture와 저장을 test-only로 끈 baseline
- B: default capture와 저장을 켠 synchronous path

각 run은 warm-up 구간을 제외하고 실제 시계로 연속 game frame 시작 간격을 기록한다. 각 run의 sample 수와 실제 frame-time 분포를 따로 보고하고, A 두 run과 B 두 run의 조건별 집계도 함께 보고한다. capture-on에서는 각 pair의 두 capture, readback, encode와 write 전체 transaction duration을 측정값 그대로 별도 기록한다. 이 측정을 제품 코드 기능으로 만들거나 CSV writer를 추가하지 않는다.

manifest의 인접 `time_ms` 차이로 실제 저장 간격을 계산하고 다음을 보고한다.

- pair 수
- 목표 100 ms
- actual interval minimum, median, p95, maximum
- absolute deviation minimum, median, p95, maximum
- 200 ms 이상 missed-slot interval 수
- duplicate timestamp와 100 ms 미만 catch-up interval 수

위 p95와 maximum은 실제 표본에서 계산해 보고할 통계이며 합격 기준이 아니다. 임의 p95, 최대 편차 또는 frame-time pass/fail 수치를 만들지 않는다. 200 ms 이상 missed-slot이 한 건이라도 나오면 이를 정상 100 ms 간격 달성으로 숨기지 않고 run별 원자료와 함께 PM에 성능 우려로 보고한다. frame time 영향이 커도 원인을 동기 GPU readback, PNG encode와 file write로 구분해 제시만 하며 background writer, async GPU readback, lower resolution 또는 다른 format은 사용자 추가 승인 전 구현하지 않는다.

## Stage 3/4와 map 보호 검증

Stage 5 구현 검증은 다음 회귀를 포함한다.

- 기존 `ShipAutonomySim.ShipMovement` 12개 테스트를 그대로 통과한다.
- 기존 `ShipAutonomySim.ShipNavigation` 19개 editor 테스트를 그대로 통과한다.
- capture-off 조건의 실제 11-slide NavigationSweep가 11 success, collision 0, timeout 0, setup 0, runtime 0과 각 minimum wall distance 0 초과를 유지한다.
- capture-on `-500`, `0`, `500` cm 세 fresh-world 사례가 기본 512 x 512와 100 ms에서 모두 terminal success, capture failure 0, 완전한 파일/manifest 계약과 minimum wall gap 0 초과를 유지한다.
- `npm test`, TypeScript build와 Python 더미 데이터 테스트를 실행해 기존 웹 계약 회귀가 없는지 확인한다.

검증 전후 다음을 비교한다.

- `ShipAutonomySim/Content/Maps/MainLevel.umap` hash
- Config 파일 hash와 파일 집합
- uproject hash
- Git status

실행 순서는 앞의 완료 검증 실행 순서를 따른다. Build.bat build가 exit 0과 compile error 0으로 먼저 통과해야 Automation을 실행하고, Automation이 모두 통과한 뒤에만 UE 5.5.4의 `-nowrite -ExecCmds="QUIT_EDITOR"` MainLevel load를 실행한다. no-write load에서는 exit code, world 생성과 cleanup, MapCheck, LoadErrors, Fatal, ensure, crash와 clean exit를 검사한다.

Saved output은 map/config write가 아니지만 Git 제외 상태를 확인한다. 임의 run 결과를 stage하거나 commit하지 않는다. 예상 밖 tracked 또는 untracked 파일이 생기면 복구, 삭제, reset, restore, clean하지 않고 작업을 중단해 보고한다.

## 사람이 마지막에 확인할 절차

자동 검증 뒤에도 다음 수동 확인은 별도 사람 검증으로 남긴다.

1. UE 5.5.4에서 MainLevel을 Lit로 열고 영구 조명 actor와 값이 구현 전과 같은지 확인한다.
2. Selected Viewport PIE에서 Play만 누르고 키보드 입력 없이 ship이 출발하고 capture log가 한 번 시작되는지 확인한다.
3. run이 Success, Collision 또는 Timeout 중 하나로 끝난 뒤 manifest finalize log와 unique Saved directory를 확인한다.
4. color와 depth 첫, 중간, 마지막 pair를 열어 같은 방향의 장면인지 비교한다.
5. color 연속 frame에서 exposure pumping이 없는지 확인한다.
6. depth grayscale에서 가까운 선박 전방 물체가 밝고 5000 cm 밖과 하늘이 어두운지 확인한다.
7. Saved run을 현재 웹 뷰어 절차로 재생해 두 canvas의 frame index와 시간이 함께 움직이는지 확인한다.
8. depth 원본과 colormap을 전환해 가까운 물체가 따뜻한 색인지 확인한다.
9. terminal 뒤 파일 수가 더 늘지 않고 manifest가 다시 쓰이지 않는지 확인한다.
10. PIE를 terminal 전에 중지한 별도 run에서 `result: "fail"` manifest와 clean shutdown을 확인한다.

이 수동 절차를 실제로 수행하기 전에는 viewport, color 품질, depth 형태와 브라우저 재생을 완료로 보고하지 않는다.

## 완료 조건

Stage 5 구현은 다음을 모두 충족해야 완료다.

- 두 SceneCapture와 공유 mount의 optical equality가 자동 및 runtime 검증으로 고정돼 있다.
- 기본 512 x 512, 100 ms actual clock, near 0 cm, far 5000 cm UPROPERTY 계약이 구현돼 있다.
- automatic capture가 꺼지고 due에서만 두 `CaptureScene()`이 호출된다.
- first `time_ms` 0, hitch 한 pair, no catch-up burst와 actual timestamp 계약이 통과한다.
- color fixed exposure와 `SCS_FinalColorLDR` PNG가 동작한다.
- `SCS_SceneDepth`, `PF_R32_FLOAT`, `RCM_MinMax` 공개 readback과 G8 PNG가 검증된다.
- complete pair만 연속 index로 commit되고 partial failure가 manifest에 들어가지 않는다.
- terminal과 EndPlay finalize가 idempotent이며 exact manifest가 temp write 뒤 같은 directory의 final 이름으로 게시된다.
- non-finite 또는 mismatched wall slide가 `CaptureInitializationFailed`로 차단되고 유효 wall slide와 success/fail result만 올바르게 전달된다.
- Build.bat exit 0과 compile error 0, Automation, no-write load 순서가 통과한다.
- capture-off 11-slide, capture-on `-500`, `0`, `500` cm fresh-world 사례, A-B와 B-A 성능 측정, Stage 3/4 회귀와 map/config hash 검증 결과가 새 evidence로 남는다.
- 현재 TypeScript 웹 뷰어가 소스 변경 없이 실제 run을 load하고 재생한다.
- Source, `README.md`, `ShipAutonomySim/SETUP.md`, `ShipAutonomySim/AGENTS.md`의 필수 최소 갱신 외 map, Config, uproject, 웹 source와 Saved 산출물이 commit에 없다.

## 남는 위험과 후속 승인 경계

- 두 SceneCapture는 같은 logical instant를 공유하지만 GPU에서 완전히 동시에 실행되는 단일 pass는 아니다. world state가 두 호출 사이 바뀌지 않는 계약으로 일관성을 보장한다.
- 공개 sync readback은 render thread flush를 포함하므로 frame time이 커질 수 있다. 이는 A/B로 측정할 위험이며 현재 설계 실패를 숨기지 않는다.
- `PF_R32_FLOAT` depth target 자체는 512 x 512에서 약 1 MiB지만 공개 readback은 `TArray<FLinearColor>`를 만들고 render thread를 flush하므로 CPU 임시 메모리와 stall 비용은 A/B에서 확인해야 한다.
- process crash에서는 EndPlay finalize와 temp cleanup을 보장할 수 없다.
- disk full이 manifest write까지 막으면 complete 이전 pair가 있어도 최종 manifest가 없을 수 있다. truncated manifest를 게시하는 것보다 이 상태를 선택한다.
- 성능 개선이 필요해도 async writer, direct RHI, half float, resolution 축소 또는 format 변경은 새 비교 설계와 사용자 승인이 있어야 한다.

## 민감정보와 외부 제공 금지

- source, diff, 제공 PDF와 prompt 내용은 로컬 작업 범위 밖으로 보내지 않는다.
- 외부 서비스, 교차 모델 제공자 또는 공개 paste에 파일이나 코드를 제공하지 않는다.
- 문서, 로그, manifest, folder name과 Git commit에 민감 식별자, 대외비 ID, Windows 사용자명 또는 absolute local path를 넣지 않는다.
- manifest에는 leaf filename과 과제에 필요한 수치만 기록한다.
