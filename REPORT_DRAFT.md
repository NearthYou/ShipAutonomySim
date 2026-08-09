# 보고서 근거 자료 (보관용)

최종 제출 문서는 `REPORT.md`, 실행 안내는 `README.md`와 `ShipAutonomySim/SETUP.md`다. 이 파일은 2026-08-09에 수집한 코드, Git 이력, 자동 검증과 수동 확인의 근거를 보존한 자료이며 제출용 본문이 아니다. 아래의 단계명과 로그명은 당시 증거 식별자이므로 현재 사용자 안내 용어로 해석하지 않는다.

## 근거 상태 표기

| 표기 | 의미 |
| --- | --- |
| 현재 코드 | 제품 코드 기준 SHA `f481f5f0985970ce56b40a4432e657e0c5e6eb9d`에서 직접 확인한 구현 |
| 기존 측정 | 기존 로컬 Automation 로그에서 확인하고 관련 소스의 변경 여부를 Git 이력과 대조한 측정값. 정확한 실행 SHA가 별도로 입증된 묶음에만 SHA를 적으며, 그 외 로그의 SHA는 추정하지 않음. `Saved` 아래 로그는 제출 대상이 아님 |
| Stage 6 확인 | 이 문서를 만들면서 같은 체크아웃에서 새로 실행한 명령의 결과 |
| 전달받은 수동 확인 | 사람이 확인했다고 전달한 범위. Stage 6에서 다시 수행한 것으로 쓰지 않음 |
| 미확인 | 현재 근거로 완료를 주장할 수 없는 항목 |

역사적 설계 문서와 구현 계획은 당시의 결정 근거다. 문서 안의 미래형 문장, 체크박스, 당시 기준선은 현재 구현 완료 근거로 사용하지 않는다. 현재 상태 판정은 제품 코드와 현재 Git 이력을 우선하며, 기존 Automation 결과는 로그에서 확인한 관측값과 관련 소스 변경 여부를 대조한 범위에서만 사용한다.

## 1. 클래스별 역할과 런타임 데이터 흐름

### Unreal 클래스 책임

| 클래스 또는 모듈 | 현재 역할 | 근거 |
| --- | --- | --- |
| `ASimGameMode` | URL option을 해석하고 코스, 선박, 자율주행, 캡처 시작을 조율한다. Movement 뒤에 terminal 조건을 평가하고 캡처를 한 번 finalize한다. | [SimGameMode.h:18-37](ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h#L18-L37), [SimGameMode.cpp:252-299](ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp#L252-L299), [SimGameMode.cpp:328-435](ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp#L328-L435) |
| `ACourseBuilder` | Water의 wave 제외 기준 높이를 조회하고 시작점, 끝점, 실제 벽, 정확히 3점인 경로를 런타임에 만든다. 일반 Play에서는 난수 slide, Automation에서는 강제 slide를 사용한다. | [CourseBuilder.cpp:86-196](ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp#L86-L196), [CourseBuilder.cpp:206-278](ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp#L206-L278) |
| `AShipPawn` | 200 x 100 x 100cm 충돌 상자, 단순 시각 mesh, Movement, Navigator, Capture rig, PIE 관찰용 CameraBoom을 소유한다. 자율주행 진입 때 수동 입력을 잠그고 Navigator를 연결한다. | [ShipPawn.h:45-83](ShipAutonomySim/Source/ShipAutonomySim/Public/ShipPawn.h#L45-L83), [ShipPawn.cpp:31-91](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp#L31-L91), [ShipPawn.cpp:188-237](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp#L188-L237) |
| `UShipNavigator` | actor transform을 쓰지 않고, 현재 위치와 heading에서 progress, lookahead, steer, throttle, coast를 계산해 `UShipMovement`의 두 입력 setter만 호출한다. | [ShipNavigator.h:17-25](ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigator.h#L17-L25), [ShipNavigator.cpp:145-284](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp#L145-L284) |
| `UShipMovement` | signed forward speed와 yaw를 고정 스텝으로 적분하고, Water 높이와 normal을 조회한 다음 sweep을 켠 단일 actor transform 쓰기 경로를 실행한다. | [ShipMovementSimulation.cpp:59-105](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp#L59-L105), [ShipMovement.cpp:242-308](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp#L242-L308), [ShipMovement.cpp:363-387](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp#L363-L387) |
| `UShipCapture` | 실제 시계로 due를 판정하고 공통 optical rig의 컬러와 깊이를 차례로 캡처한다. 완전한 PNG pair와 manifest를 필수 결과로 게시한 뒤 파생 `sequence.siv`를 추가한다. 실패 원인과 finalize를 latch한다. | [ShipCapture.h](ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h), [ShipCapture.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp), [ShipCaptureBundle.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.cpp) |

### 한 실행의 호출 순서

1. `ASimGameMode::InitGame`이 `Stage4Slide`의 존재 여부와 값을 분리해 검증한다.
2. `ASimGameMode::BeginPlay`가 `ACourseBuilder`를 deferred spawn하고, 코스를 만든다.
3. `ACourseBuilder`가 Water 기준 높이, 시작점, 끝점, 실제 벽과 3점 경로를 반환한다.
4. GameMode가 시작점에 `AShipPawn`을 spawn하고 player controller가 possess한다.
5. `AShipPawn::EnterAutonomy`가 수동 입력을 제거하고 `UShipNavigator`를 활성화한다.
6. GameMode가 유효한 wall slide를 `UShipCapture::StartCapture`에 전달한다. frame 0 pair가 성공해야 capture tick이 활성화된다.
7. 매 tick은 Navigator 판단, Movement 적분과 swept transform, Capture due 처리, GameMode terminal 평가 순으로 진행된다. Movement가 Navigator를, Capture가 Movement를, GameMode가 Capture를 prerequisite로 둔다. [ShipNavigator.cpp:68-76](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp#L68-L76), [SimGameMode.cpp:240-274](ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp#L240-L274)
8. Collision, Success, Timeout 중 하나가 latch되면 Navigator 입력을 0으로 만들고 성공 여부를 캡처 finalization에 한 번 전달한다.

### 웹 뷰어 데이터 흐름

`src/app.ts`는 기본 모드에서 `manifest.json`을 읽고, `?bundle=`이 있으면 `src/bundle.ts`로 SIV 전체 구조와 내부 manifest를 검증한다. 두 경로는 같은 preload와 player에 연결된다. 프레임이 바뀌면 두 canvas가 같은 index를 표시하고 `src/depth.ts`가 깊이 원본을 컬러맵으로 바꿀 수 있다. [app.ts](src/app.ts), [bundle.ts](src/bundle.ts), [manifest.ts](src/manifest.ts), [preload.ts](src/preload.ts)

## 2. 이동 모델 수식, 최종 파라미터와 거동 수치

### 수식과 적분 순서

전후진을 포함한 signed forward speed `v`는 다음 식을 사용한다.

```text
dv/dt = A T - C1 v - C2 v |v|
```

`T`는 `[-1, 1]`로 제한된 throttle이다. 한 내부 스텝에서는 현재 속도로 가속도, 이동거리 `v h`, yaw rate를 계산한 다음 forward Euler로 다음 속도와 yaw를 만든다. throttle이 0이고 다음 속도의 절댓값이 5cm/s 이하이면 속도를 정확히 0으로 만든다. [ShipMovementSimulation.cpp:50-105](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp#L50-L105)

yaw rate는 다음 식이다.

```text
yawRate = MaxYawRate S clamp(|v| / TurnRefSpeed, 0, 1)
```

`S`는 `[-1, 1]`로 제한된 steer다. 속도 0에서는 yaw rate도 0이므로 제자리 회전하지 않는다.

### 현재 기본 파라미터

| 값 | 기본값 | 단위 |
| --- | ---: | --- |
| `LinearDragCoeff` | 0.447501534 | 1/s |
| `QuadraticDragCoeff` | 0.000400390770 | 1/cm |
| `MaxThrustAccel` | 105.5159376 | cm/s² |
| `StopSpeedThreshold` | 5 | cm/s |
| `MaxYawRate` | 45.83662361 | deg/s |
| `TurnRefSpeed` | 200 | cm/s |
| `MaxSimulationStepSeconds` | 1/120 | s |
| `MaxSubstepsPerTick` | 8 | 회/tick |

출처: [ShipMovement.h:29-53](ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h#L29-L53)

### 목표값, 수치 검증값, 미확인값 분리

| 항목 | 목표 또는 해석값 | 현재 자동 검증값 | 구분과 남은 확인 |
| --- | ---: | ---: | --- |
| 전진 최고속도 | 약 200cm/s | `v=200`, `T=1`에서 계산 가속도 절댓값 1e-6 이하 | 안정 평형 검증이다. 유한 시간 actual-world 최고속도 실측으로 쓰지 않음 |
| 90% 가속 시간 | 정지에서 180cm/s까지 약 4.0s | 1/120s 적분에서 최초 도달 약 3.9917s, 허용오차 0.01s | 순수 수치 테스트 값 |
| 타력 정지 거리 | 200cm/s에서 throttle 0, 5cm/s까지 연속식 약 400cm | 1/120s 적분에서 약 399.9615cm, 이후 speed 0 | 순수 수치 테스트 값 |
| 최고속도 최소 선회반경 | 약 250cm | 200cm/s에서 yaw rate 45.83662361deg/s, 약 0.8rad/s이므로 계산 반경 250cm | 속도와 yaw rate 단언에서 계산한 값. actual-world 원 궤적 반경을 새로 측정하지 않음 |

직접 근거는 [ShipMovementTests.cpp:128-209](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp#L128-L209)와 [ShipMovementTests.cpp:301-358](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp#L301-L358)이다. 기존 최종 Movement Automation은 12개를 찾고 12개를 수행해 exit 0이었다. 근거 로그는 `ShipAutonomySim/Saved/Logs/Stage5-Final2-ShipMovement.log:1199-1356`이며 `Saved` 제출물은 아니다.

횡속도 상태와 횡미끄러짐은 없다. 매 스텝 이동 벡터는 현재 수평 yaw의 전방축에만 놓인다. 따라서 이 수치는 longitudinal speed와 yaw 모델의 결과이며 유체역학 선체 모델의 실측값이 아니다.

## 3. 벽 슬라이드와 우회 방향 판정 근거

course 좌표에서 시작점은 `(0, 0)cm`, 끝점은 `(2000, 0)cm`, 벽 중심은 `(1000, s)cm`다. 벽 크기는 X/Y/Z가 `100/1000/500cm`이고 Y 점유 범위는 `[s-500, s+500]`이다. 허용 slide `s`가 `[-500, 500]cm`이므로 Y=0 직선은 항상 벽에 막힌다. [ShipNavigationSimulation.h:25-32](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.h#L25-L32), [ShipNavigationSimulation.cpp:247-277](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L247-L277), [CourseBuilder.cpp:241-270](ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp#L241-L270)

우회 waypoint의 Y는 다음과 같다.

```text
s >= 0: waypointY = s - 750
s < 0:  waypointY = s + 750
```

`s >= 0`이면 벽의 음의 Y 끝 `s-500`이 중심선에서 더 가깝고, 그 끝에서 250cm 더 밖으로 나간 점이 `s-750`이다. `s < 0`이면 양의 Y 끝 `s+500`이 더 가까워 `s+750`을 쓴다. `s=0`은 양쪽이 동률이며 코드의 `>=` 분기로 음의 Y를 택한다. 자동 테스트의 대표값은 `s=-500 -> waypointY=250`, `s=0 -> -750`, `s=500 -> -250`이다. [ShipNavigationTests.cpp:347-397](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp#L347-L397)

벽과의 최소거리는 actor origin이나 world AABB가 아니다. ship box와 wall mesh local bounds의 8개 corner를 각각 full world transform으로 XY에 투영해 convex hull을 만들고, 겹치면 0, 분리되면 hull 사이 최소 point-to-segment 거리를 사용한다. [ShipNavigationSimulation.cpp:570-733](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L570-L733), [ShipNavigationTests.cpp:1345-1464](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp#L1345-L1464)

## 4. 3점 경로, progress, lookahead, heading, throttle, coast와 성공 판정

### 경로와 progress

- 경로는 `start, waypoint, end`의 정확히 3점 polyline이다.
- active segment 하나에만 현재 선박 XY를 투영한다.
- candidate progress는 해당 segment 안으로 clamp한 투영거리와 앞 segment 누적거리의 합이다.
- 저장 progress는 `max(previous, candidate)`여서 감소하지 않는다.
- segment 끝점 평면을 통과하면 한 tick에 한 segment만 전환하며, 새 segment 투영은 다음 tick에 반영한다.

구현과 반례 테스트: [ShipNavigationSimulation.cpp:303-370](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L303-L370), [ShipNavigationTests.cpp:1136-1192](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp#L1136-L1192)

### lookahead와 입력 명령

기본 lookahead는 polyline 누적거리 기준 300cm이며, `min(progress + 300cm, totalLength)`의 지점을 보간한다. segment 경계를 넘어 다음 선분에 놓일 수 있다. [ShipNavigator.h:43-59](ShipAutonomySim/Source/ShipAutonomySim/Public/ShipNavigator.h#L43-L59), [ShipNavigationSimulation.cpp:372-434](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L372-L434)

heading error는 현재 forward XY와 lookahead 방향 XY의 `atan2(cross, dot)` 각도다. 명령은 다음과 같다.

```text
steer = clamp(headingError / 30deg, -1, 1)

|headingError| <= 20deg: throttle = 1
|headingError| >= 60deg: throttle = 0.35
20deg < |headingError| < 60deg: 1에서 0.35까지 선형 보간
```

근거: [ShipNavigationSimulation.cpp:436-514](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L436-L514), [ShipNavigationTests.cpp:1214-1295](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp#L1214-L1295)

### 동적 정지거리와 irreversible coast

최종 segment에서 현재 양의 speed를 입력으로 3단계와 같은 drag, forward Euler, 1/120s 스텝을 반복해 5cm/s까지의 정지거리를 계산한다. `final remaining <= dynamic stopping distance + 25cm`가 처음 참이 되면 coast를 latch한다. 이후 terminal까지 throttle은 0이고 다시 추진하거나 reverse braking하지 않는다. steer 계산은 계속될 수 있다. [ShipNavigationSimulation.cpp:517-568](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L517-L568), [ShipNavigator.cpp:251-277](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp#L251-L277), [ShipNavigationTests.cpp:628-714](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationTests.cpp#L628-L714)

### terminal 판정

Success는 goal XY 거리 100cm 이하, signed speed 절댓값 5cm/s 이하, runtime calculation error 미발생을 동시에 요구한다. timeout은 Running 진입 후 45s다. 같은 평가에서 조건이 겹치면 Collision, Success, Timeout 순이다. runtime calculation error는 run 동안 latch되어 후속 값이 정상이어도 Success를 막는다. [SimGameMode.h:60-78](ShipAutonomySim/Source/ShipAutonomySim/Public/SimGameMode.h#L60-L78), [SimGameMode.cpp:328-382](ShipAutonomySim/Source/ShipAutonomySim/Private/SimGameMode.cpp#L328-L382), [ShipNavigationSimulation.cpp:735-768](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L735-L768)

## 5. 컬러와 SceneDepth 캡처, 실제 시계, 정규화와 파일 계약

### 공통 optical transform과 별도 관찰 카메라

`CaptureMount`는 collision root 아래에 있고 color와 depth `USceneCaptureComponent2D`는 같은 mount 아래 identity relative transform으로 붙는다. mount 기본 위치는 선박 local `(110, 0, 50)cm`, 회전은 0, 두 capture의 FOV는 90deg, 해상도는 512 x 512다. 매 pair 전에 attach parent, relative/world transform, FOV, source, target format과 크기를 다시 비교한다. [ShipPawn.cpp:60-71](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp#L60-L71), [ShipCapture.h:121-155](ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h#L121-L155), [ShipCapture.cpp:85-161](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L85-L161), [ShipCapture.cpp:507-537](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L507-L537)

`CameraBoom`과 그 아래 player camera는 PIE에서 선박을 보는 별도 3인칭 관찰 장치다. 과제 저장 영상의 전방 SceneCapture와 같은 스트림이 아니다. [ShipPawn.cpp:72-91](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp#L72-L91)

### 컬러와 깊이

- 컬러는 `SCS_FinalColorLDR`, `PF_B8G8R8A8`, 수동 노출, exposure bias 0을 사용한다.
- 깊이는 `SCS_SceneDepth`, `PF_R32_FLOAT`, linear gamma target을 사용하고 R 채널을 `ReadLinearColorPixels`의 `RCM_MinMax`로 읽는다.
- 같은 logical pair에서 color `CaptureScene`, depth `CaptureScene`을 순서대로 호출한다. 단일 GPU pass에서 동시에 캡처하는 구조는 아니다.

근거: [ShipCapture.cpp:108-161](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L108-L161), [ShipCapture.cpp:594-657](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L594-L657), commit `b0b09a51b83e07f2458c71bec4509ff4855e0193`.

### 100ms 실제 시계

frame 0은 capture 시작 즉시 만들며 `time_ms=0`이다. 이후 `FPlatformTime::Seconds()`의 실제 경과를 누적해 100ms 이상일 때 pair 한 개만 만든다. due 뒤 누적값은 0으로 초기화하므로 긴 hitch에서도 같은 tick에 밀린 frame을 여러 개 만들지 않는다. `time_ms`는 `index x 100`이 아니라 첫 capture 이후 실제 시각을 반올림한 값이며 반드시 이전 값보다 커야 한다. 따라서 100ms는 목표 간격이고 모든 저장 timestamp가 정확히 100ms 차이라는 뜻이 아니다. [ShipCaptureSimulation.cpp:10-98](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp#L10-L98), [ShipCaptureTests.cpp:273-396](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp#L273-L396)

### 0에서 2500cm 역선형 정규화

기존 5000cm run의 178프레임, 46,661,632픽셀을 분석했다. 0이 아닌 유효 픽셀 중 92.656%가 1000cm 이내, 96.857%가 2000cm 이내, 97.818%가 2500cm 이내였다. 코스 길이 2000cm에 500cm 여유를 두면서 G8 양자화 간격을 약 19.6cm에서 약 9.8cm로 줄이기 위해 2500cm를 최종 far로 사용한다. [binary capture polish design](docs/superpowers/specs/2026-08-09-binary-capture-polish-design.md)

```text
normalized = clamp(sceneDepthCm / 2500, 0, 1)
g8 = round((1 - normalized) x 255)
```

0cm 이하와 near는 255, 1250cm는 약 128, 2500cm 이상은 0이다. NaN 또는 infinity 같은 invalid sample도 0이다. 가까운 값을 밝게 저장하면 웹 컬러맵에서 회피 대상이 되는 가까운 물체를 따뜻한 색에 바로 대응시킬 수 있다. 새 2500cm run 216프레임에서는 전체 픽셀의 53.807%가 0이 아니었고, 그중 94.343%가 1000cm 이내, 98.968%가 2000cm 이내였다. [ShipCaptureSimulation.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp), [ShipCaptureTests.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp)

### pair transaction과 manifest

두 PNG를 메모리에서 모두 만든 뒤 각각 숨김 temp 파일에 쓴다. 두 temp가 모두 유효할 때 color와 depth final 이름으로 차례로 rename하고, 두 final 파일이 모두 존재할 때만 frame record를 append한다. 중간 실패에서는 temp와 final pair 경로를 정리하므로 incomplete pair는 manifest에 기록하지 않는다. 두 rename 사이의 아주 짧은 구간까지 파일시스템 단일 원자 연산인 것은 아니다. [ShipCapture.cpp:297-446](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L297-L446)

manifest는 `frame_count`, `interval_ms`, `depth_near_cm`, `depth_far_cm`, `capture_resolution`, `wall_slide_cm`, `result`, `frames`의 정확한 8개 최상위 필드를 만든다. manifest도 temp write 뒤 final 이름으로 rename한다. 그 다음 manifest와 모든 PNG를 `SIVPACK1`, little-endian header 길이, JSON index, 연속 PNG payload로 구성한 `sequence.siv`에 추가 게시한다. bundle 실패가 필수 PNG와 manifest를 삭제하지는 않는다. [ShipCapture.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp), [ShipCaptureBundle.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.cpp)

과제 저장 데이터는 선박 전방 color/depth pair다. 웹 manifest에는 index당 color와 depth 경로 하나씩만 있고 3인칭 stream 필드가 없다. 따라서 웹 뷰어에 3인칭 영상을 추가하지 않은 현재 상태는 전방 pair와 공통 manifest 계약의 범위다. PIE 관찰용 CameraBoom이 존재한다는 사실과 혼동하지 않는다.

## 6. 반복 검증 결과

### Stage 4 capture-off 11개

아래 actual-world 11개와 capture-on 3개는 제품 코드 SHA `f481f5f0985970ce56b40a4432e657e0c5e6eb9d`에서 2026-08-09에 새로 실행한 결과다. 각 case는 fresh MainLevel, 실제 Water, 실제 Movement와 sweep을 사용한다. 최소 벽 거리는 앞에서 설명한 full-transform XY convex hull gap이다.

| slide (cm) | success | elapsed (s) | min wall distance (cm) |
| ---: | --- | ---: | ---: |
| -500 | yes | 17.887 | 161.771 |
| -400 | yes | 18.101 | 149.968 |
| -300 | yes | 18.481 | 138.403 |
| -200 | yes | 18.982 | 127.538 |
| -100 | yes | 19.558 | 117.370 |
| 0 | yes | 20.280 | 113.619 |
| 100 | yes | 19.599 | 120.589 |
| 200 | yes | 18.992 | 128.538 |
| 300 | yes | 18.533 | 139.072 |
| 400 | yes | 18.143 | 151.294 |
| 500 | yes | 17.908 | 162.290 |

측정 근거: `ShipAutonomySim/Saved/Logs/Stage8-BinaryPolish-FinalActualWorld.log:2549-2559`와 `Stage8-BinaryPolish-FinalActualWorld-Rerun2.log:2547-2568`. 기존 -500의 약 493cm는 첫 게임 월드가 Automation 관측 시작 전에 진행되어 최근접 구간을 놓친 측정 오류였다. 첫 조건도 같은 option으로 fresh reload하도록 고친 뒤 세 독립 실행에서 161.850cm, 161.771cm, 161.824cm가 나와 최대 편차는 0.079cm였다. capture-on 162.668cm와도 일치하며 제품 항법 수식은 바꾸지 않았다.

### Stage 5 capture-on 3개

이 세 행은 capture 비용이 포함된 별도 acceptance case다. Stage 4의 11개 capture-off 표에 섞지 않는다.

| slide (cm) | success | elapsed (s) | min wall distance (cm) | frames | last time (ms) |
| ---: | --- | ---: | ---: | ---: | ---: |
| -500 | yes | 19.562 | 162.668 | 187 | 19342 |
| 0 | yes | 22.679 | 112.489 | 214 | 22203 |
| 500 | yes | 19.936 | 163.385 | 188 | 19410 |

측정 근거: 같은 로그 `:2560-2570`. 전체 actual-world 집계는 cases 14, success 14, failure 0, error 0, unknown 0, ensure 0이었고 test exit code는 0이었다. 세 capture-on run 모두 manifest와 `sequence.siv`를 게시했다.

capture-on은 같은 slide의 capture-off보다 -500에서 1.675초, 0에서 2.399초, 500에서 2.028초 느렸다. 증가율은 각각 9.36%, 11.83%, 11.32%다. 589개 pair transaction의 중앙값은 73.662ms, p95는 81.529ms였다. GPU readback, PNG 압축과 파일 쓰기를 game thread에서 동기 처리하는 비용이며 이를 숨기지 않고 한계로 보고한다.

### 기존 Automation 범위와 Stage 6 실행 범위

| 묶음 | 근거 결과 | Stage 6에서 재실행 여부 |
| --- | --- | --- |
| ShipMovement | 12/12, exit 0 | 아니오. 기존 로그와 현재 test source를 읽음 |
| ShipNavigation editor | 19/19, exit 0 | 아니오. `ShipAutonomySim/Saved/Logs/Stage5-Final2-ShipNavigation-Editor.log:1199-1551` 확인 |
| ShipCapture | 10/10, exit 0 | 예. SIV exact bytes와 실패 경계 포함 |
| actual-world | 14/14 success, exit 0 | 예. 위 최신 결과 표 |
| 웹 Node | 66/66 | 예 |
| Python | 4/4 | 예 |

`ShipMovement`와 `ShipNavigation editor` 기존 로그의 정확한 실행 SHA는 이 자료에서 별도로 입증하지 못했으므로 추정하지 않는다. 이번 ShipCapture와 actual-world 결과는 제품 코드 SHA `f481f5f0985970ce56b40a4432e657e0c5e6eb9d`에서 직접 실행했다.

## 7. UE 5.5.4와 Visual Studio 2022 빌드 절차

### 채점자가 Binaries와 Intermediate 없이 빌드하는 순서

1. Unreal Engine 5.5.4와 Visual Studio 2022의 Desktop/Game development with C++ 및 해당 Windows SDK를 준비한다.
2. 저장소 루트의 PowerShell에서 다음을 실행한다.

```powershell
$EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.5'
$Project = (Resolve-Path 'ShipAutonomySim\ShipAutonomySim.uproject').Path
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') `
    ShipAutonomySimEditor Win64 Development `
    "-Project=$Project" -WaitMutex
```

3. exit code 0을 확인한다. 이 명령이 `Binaries`와 `Intermediate`를 생성한다. 두 디렉터리를 제출물에 다시 넣지 않는다.
4. `ShipAutonomySim/ShipAutonomySim.uproject`를 UE 5.5.4로 연다. project association은 5.5이고 Water plugin이 활성화되어 있다. [ShipAutonomySim.uproject:1-18](ShipAutonomySim/ShipAutonomySim.uproject#L1-L18)
5. 기본 editor/game map은 `/Game/Maps/MainLevel`, 기본 GameMode는 `SimGameMode`다. [DefaultEngine.ini:1-4](ShipAutonomySim/Config/DefaultEngine.ini#L1-L4)
6. build 후 Editor에서 MainLevel을 열고 Play를 누르는 수동 확인은 Stage 7 clean-copy 확인과 함께 수행한다.

### Stage 6에서 실제 확인한 것

- 설치 엔진의 `Build.version`은 5.5.4였다.
- 위와 같은 Editor target Build.bat 명령은 exit 0이었다.
- 출력은 `Target is up to date`였으므로 현재 checkout의 compile 상태는 확인했지만, Binaries와 Intermediate가 전혀 없는 새 복사본에서 처음부터 재생성했다는 근거는 아니다.
- build와 웹 테스트 전후 MainLevel, 두 Config, uproject SHA-256은 아래와 같았고 변하지 않았다.

| 보호 파일 | Stage 6 전후 SHA-256 |
| --- | --- |
| `ShipAutonomySim/Content/Maps/MainLevel.umap` | `B4CBB30170061DB313F02A246F18D7152F9E962AA0CEDE6A69FED381EF3EC354` |
| `ShipAutonomySim/Config/DefaultEngine.ini` | `A7749D939304B943ED428A11C8CF7DD0C62676F4D0B16F00DABEB932A2A38A28` |
| `ShipAutonomySim/Config/DefaultInput.ini` | `3205AE751AB6EBBCF8EE6CD92459B902AFE0AB76F39E6236E01EC356908FE8C4` |
| `ShipAutonomySim/ShipAutonomySim.uproject` | `54EB75A89319381EC92FAAF9F809D921854DEFC98A97866DCA07773D1EF54F46` |

### Stage 7로 이관할 확인

- 다른 디렉터리에 제출 대상만 복사하고 `Binaries`, `Intermediate`, `Saved`가 없는 상태에서 Build.bat를 새로 실행한다.
- 생성물 없이 source만으로 build되는지, hardcoded absolute path와 제출 제외 파일이 없는지 검사한다.
- 새 복사본을 UE 5.5.4로 열어 MainLevel load, 기본 GameMode, Water plugin, compiler warning 목록을 확인한다.
- 사람이 최종 clean-copy에서 Play까지 누르는 확인은 Stage 7 항목이며 Stage 6 완료로 쓰지 않는다.

## 8. 웹 뷰어 준비, 실제 run 복사, 실행과 정리

### 준비와 빌드

저장소 루트에서 실행한다.

```powershell
npm ci
npm run build
```

`npm run build`는 `tsc -p tsconfig.json`이고, 브라우저는 `dist/src/app.js`를 실행한다. runtime dependency는 없고 TypeScript와 Node type은 dev dependency다. [package.json:1-13](package.json#L1-L13), [index.html:8-10](index.html#L8-L10)

### 실제 Unreal run 복사와 서버 실행

선택한 성공 run의 디렉터리명만 `<run-directory>`에 넣는다. 기존 루트 파일을 덮어쓰지 않고, 이번에 복사한 정확한 경로만 정리한다.

```powershell
$Run = 'ShipAutonomySim\Saved\ShipCaptures\<run-directory>'
$SourceFiles = @(
    Get-Item -LiteralPath (Join-Path $Run 'manifest.json')
    Get-Item -LiteralPath (Join-Path $Run 'sequence.siv')
    Get-ChildItem -LiteralPath $Run -File -Filter 'color_*.png'
    Get-ChildItem -LiteralPath $Run -File -Filter 'depth_*.png'
)
$ColorCount = @($SourceFiles | Where-Object Name -Like 'color_*.png').Count
$DepthCount = @($SourceFiles | Where-Object Name -Like 'depth_*.png').Count
if ($ColorCount -eq 0 -or $DepthCount -eq 0) {
    throw '선택한 run에 color 또는 depth frame이 없습니다.'
}
$Destination = (Resolve-Path '.').Path
$Collisions = @($SourceFiles | Where-Object {
    Test-Path -LiteralPath (Join-Path $Destination $_.Name)
})
if ($Collisions.Count -ne 0) {
    throw '루트에 같은 이름의 파일이 있어 복사를 중단합니다.'
}
$CopiedPaths = @($SourceFiles | ForEach-Object {
    $Target = Join-Path $Destination $_.Name
    Copy-Item -LiteralPath $_.FullName -Destination $Target
    $Target
})
python -m http.server 8000
```

브라우저에서 `http://localhost:8000`을 열면 manifest 경로를 확인할 수 있고, `http://localhost:8000/?bundle=sequence.siv`에서는 단일 바이너리 경로와 접근 benchmark를 확인할 수 있다. 서버 종료는 같은 터미널에서 `Ctrl+C`다. 그 뒤 같은 PowerShell 세션에서 다음처럼 이번 복사본만 지운다.

```powershell
$CopiedPaths | ForEach-Object { Remove-Item -LiteralPath $_ }
git status --short
```

상세 원본 절차: [README.md:125-175](README.md#L125-L175)

### 조작

- 재생과 일시정지, 처음으로, 이전, 다음
- frame slider 탐색
- 0.5x, 1x, 2x 속도
- 깊이 grayscale과 colormap 전환
- color/depth 동기 index와 manifest의 실제 `time_ms` 표시
- manifest와 SIV 출처 표시, 프레임 수와 깊이 범위 요약
- SIV 순차와 고정 seed 무작위 compressed-byte 접근 benchmark

근거: [README.md:72-82](README.md#L72-L82), [app.ts:257-302](src/app.ts#L257-L302)

### Stage 6 실제 검사와 수동 확인 구분

- Stage 6에서 외부 연결 없이 `npm ci --offline`, `npm run build`, `npm test`를 실행했다.
- 최신 `npm test`는 TypeScript build 뒤 Node test 66개 중 66개 통과, fail 0이었다.
- `python -m unittest discover -s tests -p "test_*.py" -v`는 4개 중 4개 통과였다.
- Stage 6에서는 HTTP 서버와 브라우저를 새로 열지 않았다.
- 전달받은 수동 확인 범위는 실제 color/depth 재생, 자동주행, 출력 파일, 웹 조작이다. 이 사실은 Stage 6에서 사람이 다시 확인했다고 바꿔 쓰지 않는다.

## 9. 구현하지 않은 것과 알려진 한계

| 항목 | 현재 사실 또는 한계 | 근거와 상태 |
| --- | --- | --- |
| 선택 가산점 단일 binary | 필수 PNG와 manifest를 유지하면서 같은 run을 `sequence.siv` 한 파일에 추가 게시한다. PNG는 이미 압축되어 있어 실제 재압축 절감률이 약 0.3%뿐이므로 추가 zlib은 넣지 않았다. 새 216프레임 SIV는 54,400,944 bytes이고 PNG 합계 대비 header와 index overhead는 0.1066%다. | [ShipCaptureBundle.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.cpp), [bundle.ts](src/bundle.ts), [binary capture polish design](docs/superpowers/specs/2026-08-09-binary-capture-polish-design.md) |
| PCG | 단일 벽 코스는 C++ `ACourseBuilder`가 직접 생성하며 PCG asset 또는 module dependency가 없다. | [CourseBuilder.cpp:86-278](ShipAutonomySim/Source/ShipAutonomySim/Private/CourseBuilder.cpp#L86-L278), [ShipAutonomySim.Build.cs:17-28](ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs#L17-L28) |
| Niagara | Niagara module, system 또는 effect를 사용하지 않았다. | [ShipAutonomySim.Build.cs:17-28](ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs#L17-L28), [ship navigation design:714-716](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L714-L716) |
| 캡처 frame-time | color/depth GPU readback, PNG 압축, temp write와 rename을 game thread 동기 경로에서 수행한다. 최신 589개 pair transaction의 중앙값은 73.662ms, p95는 81.529ms였다. capture-on 경과 시간은 같은 slide의 capture-off보다 9.36%에서 11.83% 늘었다. | [ShipCapture.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp), `ShipAutonomySim/Saved/Logs/Stage8-BinaryPolish-FinalActualWorld.log` |
| 두 capture의 동시성 | 같은 mount와 logical pair를 공유하지만 color와 depth `CaptureScene()`은 연속 호출이다. 단일 pass의 완전 동시 GPU capture는 아니다. | [ShipCapture.cpp:594-600](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L594-L600) |
| 선박 표현 | 충돌은 200 x 100 x 100cm box이고 시각 표현은 engine 기본 cube scale 2,1,1인 단순 mesh다. | [ShipPawn.cpp:31-43](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp#L31-L43), [ShipMovementTests.cpp:1550-1582](ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp#L1550-L1582) |
| 횡미끄러짐 | signed forward speed와 yaw만 모델링하고 lateral velocity, slip angle, 선체 관성은 없다. | [ShipMovementSimulation.h:29-42](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.h#L29-L42), [ship movement design:20-29](docs/superpowers/specs/2026-08-08-ship-movement-model-design.md#L20-L29) |
| 북서쪽 Water hole | commit `adac6870f6a819c981f9010ecacc2f043cef4f5d`에서 ShapeDilation 4096cm 뒤에도 dilated hole이 남도록 북서쪽 4200cm, 4점 linear polygon을 저장했다. 후속 `efecf64e6e26110c76f47b10a66aa3a273948a0b`는 기존 Water geometry 보존을 명시한다. current HEAD에 두 commit이 포함된다. 이는 map 특화 유지보수 요소다. Stage 6은 spline vertex를 새로 추출하지 않았고, 최신 actual-world 14건의 성공만 재사용했다. | 두 commit SHA와 현재 Git ancestry |
| 실제 100ms 간격 | scheduler 목표는 100ms지만 저장 timestamp는 실제 시계다. capture-on 표도 last time과 frame count가 정확히 `100 x (frames-1)`로 고정되지 않는다. | [ShipCaptureSimulation.cpp:54-98](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureSimulation.cpp#L54-L98), 6절 기존 측정 |
| clean-copy | 현재 checkout의 Editor target build는 통과했다. 제출본에서는 Binaries, Intermediate와 Saved를 제외하고 소스에서 다시 빌드해야 한다. | 7절 빌드 절차 |
| 수동 시각 품질 | color/depth 재생과 조작은 전달받은 수동 확인이다. Stage 6에서 viewport, PIE, 브라우저를 새로 실행하지 않았다. | 전달받은 범위와 Stage 6 실행 기록 |
| 최종 보고서 | 채점자가 읽는 본문은 루트 `REPORT.md`로 분리했다. 이 파일은 세부 근거와 검증 경계를 보존한다. | [REPORT.md](REPORT.md) |

`index.html`의 favicon data URL 한 줄은 브라우저의 암묵적 `/favicon.ico` 404를 없애기 위한 것이다. 제품 캡처, 저장 파일, manifest 또는 재생 결과를 바꾸는 기능이 아니다. [index.html:8-10](index.html#L8-L10), commit `6b2d756c3d28496ce0d144c1e0dcd2a3f194f257`.

## 10. 단계별 검토 대안과 미채택 이유

이 표는 설계 문서, 현재 코드 또는 Git 이력으로 확인되는 범위만 적는다. 근거가 없는 과거 시도는 완료 사실로 만들지 않는다.

| 대안 | 현재 채택하지 않은 이유 또는 확인 상태 | 근거 |
| --- | --- | --- |
| 웹 프레임워크와 번들러 | 정적 두 canvas, manifest, preload와 재생 상태를 브라우저 ES module로 나눌 수 있어 runtime dependency와 bundle 단계를 추가하지 않았다. TypeScript는 `tsc` 출력만 만든다. | [TypeScript viewer design:1-37](docs/superpowers/specs/2026-08-07-typescript-web-viewer-design.md#L1-L37), [package.json:1-13](package.json#L1-L13) |
| 단일 JavaScript | 초기 파일 수는 적지만 상태와 화면 처리가 결합되어 테스트와 변경 추적이 어려우므로 작은 ES module로 나눴다. | [image sequence viewer design:29-33](docs/superpowers/specs/2026-08-07-image-sequence-viewer-design.md#L29-L33) |
| 웹 Worker와 `createImageBitmap` | 대규모 시퀀스에는 유용하지만 이번 과제 규모에서는 복잡성 대비 이득이 작아 제외했다. | [image sequence viewer design:29-33](docs/superpowers/specs/2026-08-07-image-sequence-viewer-design.md#L29-L33) |
| 순수 이차저항 | 0에 점근하고, 별도 threshold를 넣어도 4초 가속과 400cm coast 목표를 동시에 맞추지 못해 선형과 이차 저항 혼합을 채택했다. | [ship movement design:231-255](docs/superpowers/specs/2026-08-08-ship-movement-model-design.md#L231-L255), [ShipMovementSimulation.cpp:76-92](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp#L76-L92) |
| 저속 구간의 상수 저항 | 속도 부호에 따라 일정한 저항을 빼면 0 근처 가속도가 불연속이고 한 스텝 안에서 부호를 반복해 넘는 떨림이 생기기 쉽다. 정지 임계값과 연속 선형 저항이 같은 목적을 더 명확히 달성하므로 제외했다. | [ship movement design:750-752](docs/superpowers/specs/2026-08-08-ship-movement-model-design.md#L750-L752) |
| 횡미끄러짐 모델 | 선회 중 slip angle을 더 현실적으로 표현할 수 있지만 상태, 계수와 검증 항목이 크게 늘어난다. 이번 단계의 정량 목표는 전방 signed speed와 yaw로 충족되며 횡방향 미끄러짐은 승인 범위 밖이므로 제외했다. | [ship movement design:754-756](docs/superpowers/specs/2026-08-08-ship-movement-model-design.md#L754-L756) |
| NavMesh와 A* | 장애물이 한 개이고 짧은 쪽 끝이 식으로 정해져 Water 위 navigation 설정과 일반 탐색기 실패 경계를 늘리지 않는 3점 경로를 사용했다. | [ship navigation design:682-691](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L682-L691), [ShipNavigationSimulation.cpp:247-277](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigationSimulation.cpp#L247-L277) |
| waypoint 정지 후 회전 | 3단계 선박은 speed 0에서 회전하지 않고 관성이 크다. waypoint를 정확히 찍고 정지한 뒤 도는 방식은 모델과 맞지 않고 overshoot를 키우므로 전방 주시를 채택했다. | [ship navigation design:690-692](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L690-L692) |
| 전체 polyline 최근접 투영 | 모든 segment를 비교하면 벽 근처에서 미래 segment로 progress가 점프하거나 overshoot 뒤 이전 segment로 되돌아갈 수 있어 active segment 전용 투영과 끝점 평면 전환을 채택했다. | [ship navigation design:694-696](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L694-L696) |
| 고정 정지거리 | 현재 speed와 무관한 고정 거리는 저속에서 너무 일찍, 고속에서 너무 늦게 coast를 시작하므로 현재 speed와 3단계 drag를 적분한 dynamic distance를 채택했다. | [ship navigation design:698-700](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L698-L700) |
| 위치 직접 대입 또는 teleport | 과제의 이동 모델을 우회하고 충돌 sweep을 건너뛰므로 Navigator는 입력만 쓰고 Movement 한 곳만 swept transform을 쓴다. | [ShipNavigator.cpp:226-284](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp#L226-L284), [ShipMovement.cpp:363-387](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp#L363-L387) |
| reverse braking | 목표 근처 앞뒤 진동과 별도 제어 상태를 추가하지 않기 위해 final coast는 한번 latch된 뒤 throttle 0을 유지한다. | [ship navigation design:702-705](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L702-L705), [ShipNavigator.cpp:251-277](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipNavigator.cpp#L251-L277) |
| CSV 또는 Saved 결과 | Automation 결과 파일은 cleanup, Git 추적과 재현 경계를 늘리므로 11개 결과는 test memory와 로그에만 두고 검증 보고서에 옮긴다. | [ship navigation design:706-708](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L706-L708) |
| 별도 test map 또는 가짜 Water | 과제 합격은 실제 MainLevel과 실제 Water에서의 완주다. 순수 계산은 unit test로 분리하되 통합 검증은 실제 world를 사용한다. | [ship navigation design:710-712](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L710-L712) |
| PCG | 단일 벽과 3점 코스에 graph, asset, 별도 seed 책임을 추가하지 않았다. | [ship navigation design:682-685](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L682-L685) |
| Niagara | 물보라는 항법과 캡처 acceptance에 필요한 데이터 계약이 아니어서 현재 module과 asset 범위에 넣지 않았다. | [ship navigation design:714-716](docs/superpowers/specs/2026-08-08-ship-autonomy-navigation-design.md#L714-L716), [ShipAutonomySim.Build.cs:17-28](ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs#L17-L28) |
| 단일 capture packing | render 호출 수를 줄일 수 있지만 최종 LDR 컬러, 고정 노출과 SceneDepth 정밀도를 한 출력 계약에 섞는다. 컬러와 깊이 SceneCapture 두 개 요구에 직접 맞지 않고 custom material과 변환 검증도 늘어나므로 선택하지 않았다. | [ship capture design:65-69](docs/superpowers/specs/2026-08-09-ship-image-capture-design.md#L65-L69) |
| async GPU readback와 background writer | stall 감소 가능성은 있지만 GPU resource 수명, 동기화, queue와 failure 경계가 늘어난다. 현재는 공개 동기 API와 complete-pair transaction을 채택하고 성능 위험을 드러낸다. | [ship capture design:57-75](docs/superpowers/specs/2026-08-09-ship-image-capture-design.md#L57-L75), [ShipCapture.cpp:594-688](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L594-L688) |
| terminal 강제 capture | terminal tick에 scheduled capture가 due가 아니면 별도 frame을 강제로 추가하지 않는다. 강제 capture는 직전 frame과 100ms보다 가까운 duplicate를 만들 수 있어 제외했다. | [ship capture design:298-306](docs/superpowers/specs/2026-08-09-ship-image-capture-design.md#L298-L306) |
| PNG payload 추가 zlib | PNG 자체가 이미 lossless 압축되어 있다. 실제 178프레임 세트에서 파일별 재압축은 0.282%, 전체 재압축은 0.322%만 줄었다. 이득보다 UE와 브라우저 양쪽의 decompression 상태와 오류 경계 비용이 커서, 압축된 PNG를 index와 함께 `sequence.siv` 한 파일에 패킹했다. | [binary capture polish design](docs/superpowers/specs/2026-08-09-binary-capture-polish-design.md), [ShipCaptureBundle.cpp](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.cpp) |
| engine Water 수정 | engine plugin source나 runtime bypass를 바꾸지 않고 MainLevel의 Ocean spline과 생성 mesh를 map-only로 수정했다. | commit `adac6870f6a819c981f9010ecacc2f043cef4f5d`, [ShipAutonomySim.Build.cs:17-28](ShipAutonomySim/Source/ShipAutonomySim/ShipAutonomySim.Build.cs#L17-L28) |
| 1m hole | 현재 tracked 문서와 Git commit에는 1m hole을 실제 적용하고 검증했다는 근거가 없다. 따라서 시도 완료나 실패 원인을 주장하지 않는다. 확인 가능한 최종 선택은 ShapeDilation 4096cm보다 큰 4200cm hole이다. | commit `adac6870f6a819c981f9010ecacc2f043cef4f5d` |
| 웹 3인칭 영상 추가 | 과제 저장 경로는 선박 전방 color와 SceneDepth pair이고, 현재 manifest는 index당 color/depth 하나씩만 가진다. 별도 3인칭 field, capture, preload와 canvas 계약을 늘리지 않았다. PIE 관찰 CameraBoom은 별도다. | [ShipCapture.cpp:724-761](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp#L724-L761), [ShipPawn.cpp:60-91](ShipAutonomySim/Source/ShipAutonomySim/Private/ShipPawn.cpp#L60-L91), [manifest.ts:3-24](src/manifest.ts#L3-L24) |

## 출처 및 검증 경계 요약

- 제품 코드 및 최신 actual-world 실행 기준 SHA: `f481f5f0985970ce56b40a4432e657e0c5e6eb9d`
- Water 4200cm hole 이력: `adac6870f6a819c981f9010ecacc2f043cef4f5d`
- Water geometry 보존 후속 이력: `efecf64e6e26110c76f47b10a66aa3a273948a0b`
- TypeScript 전환 기반 이력: `4c557467d38784b6d9b84a92b78f10f657ccba02`
- Stage 3 비유한 위치 방어 이력: `ea119f50aac4db72d7a17c544ae7ace99465410c`
- `Saved` 아래 Automation 로그는 현재 로컬 근거이며 제출 범위와 Git commit에 포함하지 않는다.
- 최신 보강은 UE build, ShipCapture 10/10, actual-world 14/14, Node 66/66, Python 4/4, 보호 파일 hash와 Git 상태를 확인한다.
- 최종 사람이 수행할 PIE와 브라우저 시각 확인 절차는 README와 `REPORT.md`에 분리한다.
