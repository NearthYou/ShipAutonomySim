# 3단계 선박 이동 모델 설계

## 문서 목적과 결정 상태

이 문서는 3단계에서 구현할 선박 이동 모델의 확정 설계다. 구현 순서나 작업 분할을 다루지 않으며, 이동 수식, 클래스 책임, 입력과 수면 정렬, 충돌 처리, 검증 기준을 정의한다.

설계에 적은 Unreal 선언은 로컬 Unreal Engine 5.5.4의 Water 및 Enhanced Input 헤더와 현재 프로젝트 골격을 기준으로 확인했다. 제품 구현은 이 문서와 별도 단계이며, 이 문서 확정만으로 구현 완료를 뜻하지 않는다.

## 목표

- 스로틀과 조향 입력만으로 전진과 후진, 가속, 감속, 정지, 선회를 재현한다.
- 최고속도, 가속 시간, 타력 정지 거리, 최소 선회반경을 수치로 검증할 수 있게 한다.
- 프레임 시간이 달라져도 제한된 내부 적분 스텝으로 결과 편차를 작게 유지한다.
- Water 플러그인의 Ocean 표면 위치와 법선을 따라 선박의 높이와 기울기를 정렬한다.
- 모든 런타임 이동과 회전을 `UShipMovement` 한 모듈에 모으고 호출자에게는 작은 입력 인터페이스만 제공한다.
- 엔진 기본 도형, 3인칭 카메라, WASD, 자동 스폰과 possession으로 PIE에서 즉시 수동 검증할 수 있게 한다.
- 수치 적분을 UObject와 월드에서 분리된 내부 순수 계산 seam으로 두어 자동화 테스트가 같은 수식을 직접 검증하게 한다.

## 비목표

- `UShipNavigator`의 경로 추종, 웨이포인트, 회피, 자동주행 상태기는 구현하지 않는다.
- 코스, 벽, 시작점, 종료점 생성은 구현하지 않는다.
- 컬러 및 깊이 캡처와 데이터 저장은 구현하지 않는다.
- 횡방향 속도, 횡미끄러짐, 선체 관성 모멘트, 롤 복원력, 부력 물리는 모델링하지 않는다.
- Chaos 물리로 선박을 구동하지 않는다.
- 네트워크 복제와 멀티플레이 입력 권한은 다루지 않는다.
- 외부 에셋이나 외부 C++ 라이브러리를 추가하지 않는다.

## 필수 거동과 설계 정량화의 구분

상위 요구에서 직접 필요한 거동은 다음과 같다.

- 선박이 스로틀로 전진과 후진하고 조향으로 좌우 선회한다.
- 정지 상태에서는 조향만으로 제자리 회전하지 않는다.
- 이동 중 Ocean 수면의 높이와 법선을 따라간다.
- 이동은 충돌을 무시한 순간이동이 아니라 sweep으로 blocking 충돌을 처리한다.
- 수동 키보드와 이후 자동주행은 같은 이동 입력 인터페이스를 사용한다.
- PIE 시작 시 테스트 선박이 생성되고 로컬 플레이어가 조종할 수 있다.
- 사람이 거동을 볼 수 있는 3인칭 카메라와 화면 디버그 값이 있다.

이번 설계에서 검증 가능하도록 추가한 정량 목표는 다음과 같다.

| 항목 | 정량 목표 |
| --- | --- |
| 전진 최고속도 | 약 `200 cm/s` |
| 정지에서 90퍼센트 속도 도달 | `180 cm/s`까지 약 `4.0 s` |
| 타력 정지 | `200 cm/s`에서 스로틀 0 후 약 `400 cm` 이동해 `5 cm/s`에 도달한 뒤 0으로 정지 |
| 정지 판정 속도 | `5 cm/s` |
| 최고속도 최소 선회반경 | 약 `250 cm` |
| 내부 적분 최대 스텝 | 기본 `1/120 s` |
| tick당 최대 내부 스텝 | 기본 `8`회, 최대 모사 시간 `1/15 s` |

카메라 위치, 수면 기준 오프셋, 보간 속도, 내부 적분 최대 스텝과 tick당 최대 내부 스텝은 상위 요구에 없던 튜닝 기본값이다. 이 값들은 아래에서 근거와 검증 방법을 함께 정의한다.

## 모듈 책임과 외부 인터페이스

### UShipMovement

`UShipMovement`는 이동 모델을 감추는 깊은 모듈이다. 외부 호출자가 알아야 하는 런타임 입력 인터페이스는 다음 두 함수로 제한한다.

```cpp
void SetThrottle(float Value);
void SetSteer(float Value);
```

두 함수는 입력을 즉시 `[-1, 1]`로 clamp해 저장한다. 키보드 adapter와 이후 자동주행은 이 두 함수만 사용한다. 호출자는 속도 적분, 수면 조회, 회전 재구성, sweep, 정지 임계값을 직접 다루지 않는다.

`UShipMovement`가 소유하는 책임은 다음과 같다.

- signed speed와 수평 yaw 상태
- 선형 및 이차 저항을 포함한 수치 적분
- 속도 비례 yaw 적분
- Ocean 표면 조회와 수면 fallback
- 높이와 법선 보간
- actor transform의 유일한 런타임 변경
- blocking hit에 따른 속도 초기화
- 이동 관련 화면 디버그 표시

### 내부 수치 계산 seam

수치 적분 핵심은 `UShipMovement`의 private 메서드에 숨기지 않고 모듈 private 영역의 작은 C++ 값 타입과 순수 함수로 둔다.

- `FShipMotionParameters`: 계수와 정지 및 선회 기준
- `FShipMotionState`: signed speed와 수평 yaw
- `FShipMotionInput`: clamp된 throttle과 steer
- `FShipMotionStep`: 다음 상태, 이동 거리, yaw rate, 가속도
- `FShipSubstepSchedule`: 모사 시간, 폐기 시간, 내부 스텝 수와 스텝 길이
- `AdvanceShipMotion`: 월드와 UObject에 접근하지 않는 단일 스텝 순수 함수
- `BuildShipSubstepSchedule`: DeltaTime과 두 substep 설정을 bounded schedule로 바꾸는 순수 함수
- `ValidateShipMotionParameters`: 편집값을 검증해 활성 snapshot과 검증 상태를 정하는 순수 함수
- `BuildShipSurfaceBasis`: 수평 yaw와 수면 법선으로 직교기저를 만드는 순수 함수
- `ResolveWaterSurfaceSample`: query 결과와 `HasWaves()`를 유효한 수면 상태 또는 fallback으로 분류하는 순수 함수

이 seam은 `Source/ShipAutonomySim/Private` 안에 두고 `SHIPAUTONOMYSIM_API`로 export하지 않는다. `ShipMovement.cpp`와 같은 모듈의 자동화 테스트만 include한다. 수치 적분, 수면 상태 분류와 회전 기하를 테스트하기 위해 `UShipMovement`의 public 함수나 상태 getter를 늘리지 않는다.

### AShipPawn

`AShipPawn`은 선박의 구성과 사람 입력 adapter를 담당한다.

- 충돌 루트와 엔진 기본 시각 메시 구성
- `UShipMovement` 소유
- 3인칭 카메라 구성
- 런타임 Enhanced Input 객체 소유
- WASD 값을 `SetThrottle`과 `SetSteer`로 전달
- possession 시 입력 mapping context 등록
- 입력 action 종료와 focus loss에서 수동 입력 초기화
- unpossession과 `EndPlay`에서 수동 입력 초기화 및 mapping context 해제

`AShipPawn`은 매 프레임 transform을 직접 변경하지 않는다.

### ASimGameMode

`ASimGameMode`는 3단계 수동 검증을 위해 선박 한 대를 생성하고 첫 로컬 플레이어 컨트롤러가 possess하게 한다. 초기 spawn transform을 정하는 것 외에 이동 중 transform을 변경하지 않는다.

이 책임은 3단계 테스트 부트스트랩에 한정한다. 이후 시나리오 생성 단계가 시작되면 선박 생성 위치와 possession 정책은 그 단계의 부트스트랩으로 교체하되 `UShipMovement` 입력 인터페이스는 유지한다.

### UShipNavigator와 이후 호출자

`UShipNavigator`는 이번 단계에서 수정하거나 활성화하지 않는다. 이후 자동주행이 추가되더라도 actor transform이나 내부 speed를 직접 바꾸지 않고 `SetThrottle`과 `SetSteer`만 호출한다.

## 데이터 흐름

1. `ASimGameMode::BeginPlay`가 테스트용 `AShipPawn`을 생성하고 플레이어 컨트롤러가 possess한다.
2. `AShipPawn`이 런타임 mapping context를 로컬 Enhanced Input subsystem에 등록한다.
3. W와 S는 하나의 1D throttle 값으로, A와 D는 하나의 1D steer 값으로 합성된다.
4. Pawn 입력 handler가 값을 `UShipMovement::SetThrottle` 또는 `SetSteer`로 전달한다.
5. `UShipMovement::TickComponent`가 프레임 DeltaTime을 제한된 내부 스텝으로 나눈다.
6. 각 내부 스텝에서 순수 함수가 signed speed, 수평 이동 거리와 yaw를 계산한다.
7. 계산된 다음 XY에서 Ocean 표면 위치와 법선을 조회하고 Z와 up 방향을 보간한다.
8. 수평 yaw를 보존하도록 최종 회전을 재구성한다.
9. `UShipMovement`가 sweep을 켠 `SetActorLocationAndRotation`으로 위치와 회전을 함께 적용한다.
10. blocking hit가 있으면 signed speed를 0으로 만들고 그 tick의 남은 내부 스텝을 중단한다.

## 좌표, 단위와 부호

UE 좌표계의 `+X`는 선박 전방, `+Y`는 우현, `+Z`는 위쪽으로 사용한다. 길이는 `cm`, 시간은 `s`, 속도는 `cm/s`, 가속도는 `cm/s^2`다.

- `Throttle = +1`: 최대 전진 추력
- `Throttle = -1`: 최대 후진 추력
- `Steer = +1`: 우현 선회
- `Steer = -1`: 좌현 선회
- `Speed > 0`: 현재 수평 yaw의 전방으로 이동
- `Speed < 0`: 같은 전방축의 반대 방향으로 이동

UE에서 yaw `+90 deg`는 전방 `+X`를 `+Y`로 돌린다. 따라서 D를 `Steer = +1`, A를 `Steer = -1`에 대응시킨다.

후진에서도 승인된 식대로 yaw rate는 `abs(Speed)`와 steer 부호로 정한다. 즉 D는 속도 부호와 무관하게 양의 yaw를 만든다. 위치 변화만 signed speed 때문에 전방축 반대로 진행한다.

## 수평 이동 수식

signed speed `v`의 연속시간 모델은 다음과 같다.

```text
dv/dt = A * T - C1 * v - C2 * v * abs(v)
```

| 기호 | 의미 | 단위 |
| --- | --- | --- |
| `t` | 시간 | `s` |
| `v` | 선박 전방축 signed speed | `cm/s` |
| `T` | clamp된 throttle | 무차원, `[-1, 1]` |
| `A` | `MaxThrustAccel` | `cm/s^2` |
| `C1` | `LinearDragCoeff` | `s^-1` |
| `C2` | `QuadraticDragCoeff` | `cm^-1` |

`v * abs(v)`는 속도와 같은 부호이므로 앞의 음수 부호를 거치면 전진과 후진 모두에서 운동 반대 방향의 저항이 된다. 식은 원점에 대해 대칭이므로 throttle `-1`의 후진 평형 속도도 약 `-200 cm/s`다.

사용자 승인으로 고정된 확정 기본값은 다음과 같다.

```text
LinearDragCoeff    = 0.447501534 s^-1
QuadraticDragCoeff = 0.000400390770 cm^-1
MaxThrustAccel     = 105.5159376 cm/s^2
StopSpeedThreshold = 5 cm/s
```

## 계수 역산

전진 구간 `v >= 0`에서 목표 최고속도를 `V = 200 cm/s`, 정지 판정 속도를 `Vs = 5 cm/s`, 최고속도 90퍼센트를 `V90 = 180 cm/s`로 둔다.

아래 역산식에서 `x`와 `D`는 수평 이동 거리 `cm`, `v0`는 타력 시작 속도 `cm/s`, `R`은 미분방정식의 음의 평형근 크기 `cm/s`를 뜻한다.

### 평형 속도 조건

throttle 1에서 `v = V`가 평형이려면 다음이 성립해야 한다.

```text
A = C1 * V + C2 * V^2
```

수치를 대입하면 다음과 같다.

```text
A = 0.447501534 * 200 + 0.000400390770 * 200^2
  = 105.5159376 cm/s^2
```

### 타력 정지 거리 조건

throttle 0이고 `v > 0`일 때 `dx/dt = v`를 사용하면 다음 관계를 얻는다.

```text
dv/dx = -C1 - C2 * v
```

따라서 `V`에서 `Vs`까지 이동 거리는 다음과 같다.

```text
D = (1 / C2) * ln((C1 + C2 * V) / (C1 + C2 * Vs))
```

확정 기본값을 대입하면 `D = 399.9999999 cm`로 400 cm 목표와 일치한다.

### 90퍼센트 도달 시간 조건

throttle 1에서 양의 평형근을 `V`, 음의 평형근의 크기를 `R`이라 두면 다음과 같다.

```text
R = A / (C2 * V) = 1317.661963 cm/s
A - C1 * v - C2 * v^2 = C2 * (V - v) * (v + R)
```

정지에서 속도 `v`까지의 시간은 다음과 같다.

```text
t(v) = ln(V * (v + R) / (R * (V - v))) / (C2 * (V + R))
```

`v = 180 cm/s`를 대입하면 `t = 3.9999999997 s`다. 평형 속도 식, 400 cm 거리 식과 4초 도달 시간 식을 함께 수치 해석한 결과가 위의 `C1`, `C2`, `A`다.

## 순수 이차 저항을 채택하지 않은 이유

순수 이차 저항으로 타력 주행하면 다음과 같다.

```text
dv/dt = -C2 * v^2
v(t) = v0 / (1 + C2 * v0 * t)
v(x) = v0 * exp(-C2 * x)
```

여기서 `v0`는 타력 주행을 시작할 때의 양의 속도이고 `x`는 시작점부터 누적한 수평 이동 거리다.

속도는 시간과 거리 모두에서 0에 점근하므로 유한 시간과 유한 거리에서 실제 0이 되지 않는다. 별도 정지 임계값을 추가해도 4초 가속과 400 cm 감속을 동시에 맞출 수 없다.

- 4초에 90퍼센트 속도를 맞추면 `C2 = 0.001840274362 cm^-1`이고, `200 cm/s`에서 `5 cm/s`까지 약 `2004.53 cm`가 필요하다.
- 400 cm에서 `5 cm/s`에 도달하도록 맞추면 `C2 = 0.009222198635 cm^-1`이고, 90퍼센트 속도 도달 시간이 약 `0.798 s`로 짧아진다.

연속적인 선형 저항과 이차 저항을 함께 사용하면 두 과도 응답과 평형 속도를 동시에 만족한다.

## 정지 정의와 전진 및 후진 전환

`StopSpeedThreshold = 5 cm/s`는 저속 저항을 바꾸는 계수가 아니라 시뮬레이션의 실제 정지 판정이다.

- throttle이 0으로 판정되고 다음 signed speed의 절댓값이 `5 cm/s` 이하이면 speed를 정확히 0으로 만든다.
- throttle이 0이 아니면 임계값을 적용하지 않는다. 정지에서 첫 가속 스텝이 임계값보다 작더라도 지워지지 않으므로 출발할 수 있다.
- 반대 throttle이 들어오면 drag와 추력이 현재 속도를 줄이고 0을 통과해 반대 부호 속도로 전환한다.
- 목표 최고속도 `200 cm/s`에 gameplay hard clamp를 두지 않는다. 승인된 미분방정식의 안정 평형이 약 `+200 cm/s`와 `-200 cm/s`를 만든다.
- 지원 속도 범위 안에서 수치 오차로 평형 속도를 넘으면 저항이 다시 평형 방향으로 감속시킨다. 뒤에서 정의하는 `500 cm/s` 경계는 정상 속도를 포화시키는 규칙이 아니라 잘못된 튜닝과 손상 상태를 검출하는 안전 경계다.

throttle 0 판정은 clamp 후 저장된 값에 `FMath::IsNearlyZero`를 적용한다. 입력 자체는 매 setter 호출에서 유한값인지 확인하며 비유한 값은 0으로 대체하고 한 번만 경고한다.

## explicit 적분과 제한된 내부 스텝

수치 계산은 우변을 현재 상태에서 평가하는 forward Euler로 고정한다. 각 내부 스텝 `h`의 순서는 다음과 같다.

1. 현재 `v_n`, `yaw_n`, clamp된 `T`, `S`를 읽는다.
2. `a_n = A * T - C1 * v_n - C2 * v_n * abs(v_n)`을 계산한다.
3. `yawRate_n`을 현재 `v_n`으로 계산한다.
4. `travel_n = v_n * h`를 계산하고 `yaw_n`에서 만든 world 수평 전방 벡터에 곱해 다음 XY 이동을 만든다.
5. `v_next = v_n + a_n * h`를 계산하고 throttle 0 정지 판정을 적용한다.
6. `yaw_next = NormalizeAxis(yaw_n + yawRate_n * h)`를 계산한다.
7. 다음 상태와 `travel_n`, `a_n`, `yawRate_n`을 값으로 반환한다.

이 순서는 구현과 자동화 테스트가 같은 결과를 사용하도록 고정한다. 위치에 다음 속도를 사용하는 semi-implicit 방식이나 프레임마다 다른 가변 순서는 섞지 않는다.

양의 유한 프레임 `DeltaTime`은 다음처럼 상한을 먼저 적용한 뒤 나눈다.

```text
MaxSimulatedDeltaTime = MaxSimulationStepSeconds * MaxSubstepsPerTick
SimulatedDeltaTime = min(DeltaTime, MaxSimulatedDeltaTime)
DroppedDeltaTime = DeltaTime - SimulatedDeltaTime
N = clamp(ceil(SimulatedDeltaTime / MaxSimulationStepSeconds), 1, MaxSubstepsPerTick)
h = SimulatedDeltaTime / N
```

`DeltaTime`, `MaxSimulationStepSeconds`, `MaxSimulatedDeltaTime`, `SimulatedDeltaTime`, `DroppedDeltaTime`, `h`의 단위는 `s`다. `MaxSubstepsPerTick`과 `N`은 무차원 양의 정수다.

`MaxSimulationStepSeconds`의 확정 기본값은 `1/120 s`, 즉 약 `0.008333333 s`이고 `MaxSubstepsPerTick`의 확정 기본값은 8이다. 따라서 tick 하나가 모사하는 시간은 최대 `8/120 s = 1/15 s`, 약 `0.066666667 s`다. 모든 내부 스텝은 부동소수점 산술 허용오차 안에서 `h <= MaxSimulationStepSeconds`를 만족한다.

이 기본값은 120 Hz를 기준 적분 해상도로 삼는다. 120 FPS에서는 1회, 60 FPS에서는 2회, 30 FPS에서는 4회, 15 FPS에서는 8회 계산한다. 이 정상 프레임 범위는 최대 모사 시간 안에 있으므로 시간을 버리지 않으며 승인된 목표 수치가 변하지 않는다. 확정 기본값의 forward Euler 수치 검산 결과는 다음과 같다.

- `180 cm/s` 최초 도달: 약 `3.9917 s`
- `200 cm/s`에서 `5 cm/s`까지 타력 거리: 약 `399.9615 cm`

큰 hitch에서 `DeltaTime` 전체를 따라잡기 위해 무제한 반복하지 않는다. `DeltaTime`이 `MaxSimulatedDeltaTime`을 넘으면 `DroppedDeltaTime`을 즉시 버리고 다음 tick에 누적하거나 재생하지 않는다. 예를 들어 기본값에서 `DeltaTime = 0.5 s`이면 정확히 8개 내부 스텝으로 `0.066666667 s`만 모사하고 약 `0.433333333 s`를 버린다. 이 상태는 횟수가 제한된 로그와 화면 디버그 값으로 드러낸다.

`MaxSimulationStepSeconds`와 `MaxSubstepsPerTick`은 `UPROPERTY(EditAnywhere, Category=ShipMovement)`로 두고 아래의 편집 안전 경계를 적용한다. DeltaTime이 0 이하이면 해당 tick을 건너뛴다. DeltaTime이나 계산 결과가 비유한 값이면 transform을 적용하지 않고 speed를 0으로 만든 뒤 오류 상태를 표시한다.

## yaw 적분과 선회반경

yaw rate는 다음 식을 사용한다.

```text
yawRate = MaxYawRate * S * clamp(abs(v) / TurnRefSpeed, 0, 1)
```

| 기호 | 의미 | 단위 |
| --- | --- | --- |
| `S` | clamp된 steer | 무차원, `[-1, 1]` |
| `MaxYawRate` | 최고속도 최대 yaw rate | `deg/s` |
| `TurnRefSpeed` | 조향 배율이 1이 되는 기준 속도 | `cm/s` |
| `yawRate` | 수평 yaw 변화율 | `deg/s` |

사용자 승인으로 고정된 yaw 확정 기본값은 다음과 같다.

```text
MaxYawRate  = 45.83662361 deg/s
TurnRefSpeed = 200 cm/s
```

`45.83662361 deg/s`는 약 `0.8 rad/s`다. 최고속도와 최대 steer에서 선회반경은 다음과 같다.

```text
Rturn = speed / angularSpeed
      = 200 / 0.8
      = 250 cm
```

여기서 `speed`는 `cm/s`, `angularSpeed`는 `rad/s`, `Rturn`은 `cm`다.

speed가 0이면 yaw rate도 0이므로 제자리 회전이 없다. 낮은 속도에서는 yaw rate가 속도에 선형 비례한다. 횡속도 상태가 없으므로 매 스텝 이동 벡터는 항상 현재 수평 yaw의 전방축에 놓인다.

## 확정 기본값과 편집 안전 경계

아래 기본값은 사용자 승인을 받은 4초 가속, 400 cm 타력 정지와 250 cm 선회반경의 합격 기준이자 제품의 초기값이다. `EditAnywhere`는 PIE에서 제한적으로 튜닝하기 위한 통로일 뿐 기본값이 미확정이라는 뜻이 아니다. 값을 바꾼 인스턴스는 별도의 튜닝 변형이며 세 정량 목표를 자동으로 보장하지 않는다. 3단계 합격 검증은 아래 기본값으로 되돌려 실행하고, 기본값 변경은 설계 변경으로 다시 승인한다.

| `UPROPERTY` | 확정 기본값 | 단위 | 편집기 범위 |
| --- | --- | --- | --- |
| `LinearDragCoeff` | `0.447501534` | `s^-1` | `ClampMin 0`, `ClampMax 2` |
| `QuadraticDragCoeff` | `0.000400390770` | `cm^-1` | `ClampMin 0`, `ClampMax 0.002` |
| `MaxThrustAccel` | `105.5159376` | `cm/s^2` | `ClampMin 0.001`, `ClampMax 500` |
| `StopSpeedThreshold` | `5` | `cm/s` | `ClampMin 0.1`, `ClampMax 50` |
| `MaxYawRate` | `45.83662361` | `deg/s` | `ClampMin 0`, `ClampMax 180` |
| `TurnRefSpeed` | `200` | `cm/s` | `ClampMin 1`, `ClampMax 1000` |
| `MaxSimulationStepSeconds` | `1/120`, 약 `0.008333333` | `s` | `ClampMin 0.001`, `ClampMax 0.016666667` |
| `MaxSubstepsPerTick` | `8` | 회 | `ClampMin 1`, `ClampMax 32` |

편집기 metadata만 안전 경계로 믿지 않는다. 최초 모사 전, PIE 중 property 변경 뒤 다음 tick 전에 모든 값을 하나의 parameter snapshot으로 검증한다. 값은 유한하고 표의 범위 안이어야 하며 `C1`과 `C2`가 동시에 0이면 안 된다. 전진 throttle 1의 양의 평형 속도 `Veq`는 다음 식으로 계산해 유한하고 `StopSpeedThreshold < Veq <= SupportedSpeedCmPerSecond`인지 확인한다.

```text
if C2 > 0:
    Veq = 2 * A / (C1 + sqrt(C1^2 + 4 * C2 * A))
else:
    Veq = A / C1

SupportedSpeedCmPerSecond = 500 cm/s
```

explicit Euler의 국소 변화율 상한과 무차원 안정성 수는 다음과 같다.

```text
LambdaMax = C1 + 2 * C2 * SupportedSpeedCmPerSecond
EulerStabilityNumber = MaxSimulationStepSeconds * LambdaMax
EulerStabilityNumber <= 0.5
```

`LambdaMax`의 단위는 `s^-1`이고 `EulerStabilityNumber`는 무차원이다. `0.5`는 forward Euler의 절댓값 안정 조건보다 보수적으로, 지원 속도 범위에서 저항 응답이 한 스텝마다 부호를 번갈아 넘지 않게 두는 여유다. 확정 기본값의 평형 속도는 `200 cm/s`, 안정성 수는 약 `0.00706577`이다. 표의 drag와 최대 스텝 상한을 동시에 대입해도 안정성 수는 약 `0.06666667`이므로 편집기 범위 전체가 이 조건 안에 있다. 런타임 검증은 metadata를 우회한 코드 설정이나 손상된 값까지 막는다.

하나라도 검증에 실패하면 일부 값만 섞지 않고 활성 parameter snapshot 전체를 위 확정 기본값으로 대체하고 `TuningFallback`을 횟수 제한 경고와 화면 디버그에 표시한다. 각 내부 스텝에서 계산된 다음 speed도 유한하고 절댓값이 `500 cm/s` 이하여야 한다. 확정 기본값을 사용한 뒤에도 손상된 상태 때문에 이 조건을 만족하지 못하면 speed를 0으로 만들고 해당 스텝의 transform을 적용하지 않는다.

## 수면 조회

### 확인된 UE 5.5.4 선언

Water 의존성은 현재 `ShipAutonomySim.Build.cs`의 private module에 이미 있다. 구현 파일에서 필요한 실제 공개 헤더와 선언은 다음과 같다.

- `WaterSubsystem.h`
- `WaterBodyComponent.h`
- `WaterBodyTypes.h`

```cpp
static UWaterSubsystem* UWaterSubsystem::GetWaterSubsystem(const UWorld* InWorld);
TWeakObjectPtr<UWaterBodyComponent> UWaterSubsystem::GetOceanBodyComponent();
bool UWaterBodyComponent::HasWaves() const;

virtual FWaterBodyQueryResult UWaterBodyComponent::QueryWaterInfoClosestToWorldLocation(
    const FVector& InWorldLocation,
    EWaterBodyQueryFlags InQueryFlags,
    const TOptional<float>& InSplineInputKey = TOptional<float>()) const;
```

`GetOceanBodyComponent`의 선언형은 `UWaterBodyComponent` weak pointer이며, 실제 Ocean actor에서는 `UWaterBodyOceanComponent` 인스턴스를 가리킨다. 구현은 구체 Ocean subclass로 cast할 필요 없이 base component의 query 인터페이스를 사용한다.

### 조회 순서와 플래그

1. `UWaterSubsystem::GetWaterSubsystem(GetWorld())`로 subsystem을 구한다.
2. 캐시한 weak `OceanBodyComponent`가 유효하지 않으면 `GetOceanBodyComponent()`로 갱신한다. 캐시는 `UPROPERTY(Transient)`가 붙은 `TWeakObjectPtr<UWaterBodyComponent>`로 둔다.
3. 해당 내부 스텝의 다음 XY와 현재 Z로 world query 위치를 만든다.
4. 다음 플래그를 OR로 결합해 query한다.

```text
ComputeLocation | ComputeNormal | IncludeWaves
```

5. 위치에는 `FWaterBodyQueryResult::GetWaterSurfaceLocation()`, 법선에는 `GetWaterSurfaceNormal()`을 사용한다. 파도 적용 여부는 query flag가 아니라 유효한 component의 `HasWaves()`와 아래 분기로 판정한다.

### `IncludeWaves` 의미와 상태 분기

`IncludeWaves`는 파도 정보를 결과에 반영해 달라는 요청이며 파도가 실제로 있다는 결과 표시가 아니다. UE 5.5.4의 `CheckAndAjustQueryFlags`는 `IncludeWaves`가 있고 `HasWaves()`가 참일 때 `ComputeLocation`과 `ComputeDepth`를 추가한다. 파도가 없다고 `IncludeWaves`를 제거하지는 않는다. 실제 surface location과 normal에 파도 변위를 적용하는 계산도 결과 flag에 `IncludeWaves`가 있고 component의 `HasWaves()`가 참일 때만 실행된다.

따라서 `ResolveWaterSurfaceSample`은 다음 다섯 상태를 빠짐없이 구분한다.

| 상태 | 판정과 처리 |
| --- | --- |
| `ValidWaves` | subsystem과 component가 유효하고 `HasWaves()`가 참이며, exclusion 밖이고 필수 결과 flag와 유한한 위치 및 법선이 있다. 파도가 반영된 surface 값을 새 sample로 채택한다. |
| `ValidNoWaves` | 같은 유효성 조건에서 `HasWaves()`가 거짓이다. `IncludeWaves`가 반환 flag에 남아 있어도 파도 계산은 실행되지 않으므로 평면 surface location과 normal을 정상 sample로 채택한다. |
| `Excluded` | query 결과가 exclusion volume 내부다. 엔진이 반환한 위치와 법선은 기술적으로 유효한 수면 sample로 취급할 수 없으므로 폐기하고 fallback한다. |
| `QueryInvalid` | `ComputeLocation` 또는 `ComputeNormal` 결과가 없거나, 위치나 법선이 비유한 값이거나, 법선이 영벡터에 가깝거나 회전 안전 경계를 만족하지 못한다. 새 sample을 폐기하고 fallback한다. |
| `ComponentInvalid` | subsystem이 없거나 갱신 뒤에도 Ocean component가 유효하지 않다. query를 호출하지 않고 fallback한다. |

`IncludeWaves`의 존재만 보고 `ValidWaves`로 분류하는 경로는 금지한다. 새 sample은 `ValidWaves` 또는 `ValidNoWaves`에서만 채택한다.

### 실패 fallback

- 이전에 유효한 sample이 있으면 마지막 유효한 목표 Z와 normal을 유지한다.
- 아직 유효한 sample이 한 번도 없으면 현재 actor Z와 `FVector::UpVector`를 기준으로 사용한다.
- 수평 속도와 sweep 이동은 계속 처리해 일시적인 초기화 순서 문제에서 회복할 수 있게 한다.
- 성공에서 실패, 실패에서 성공으로 상태가 바뀔 때만 로그를 남겨 매 프레임 로그 폭주를 막는다.
- 화면 디버그에는 `ValidWaves`, `ValidNoWaves`, `Excluded`, `QueryInvalid`, `ComponentInvalid` 중 현재 상태와 fallback 사용 여부를 함께 표시한다.

## waterline offset과 수면 보간

`WaterlineOffsetCm`은 query한 surface location의 world Z에 더하는 값이다. 확정 기본값은 `0 cm`다. 200 x 100 x 100 cm 대칭 box의 원점이 중앙이므로 기본값 0은 수면이 선체 중앙을 지나게 하는 중립적인 기준이다. 시각 메시를 바꾸지 않고 흘수만 조정할 수 있도록 `UPROPERTY(EditAnywhere, Category=ShipMovement)`로 둔다. PIE에서 calm water의 surface Z와 actor 원점 Z 차이가 `0 cm`인지 확인하고, 시각적 흘수를 바꾸는 경우 이 값만 조정한다.

목표 높이와 법선에 즉시 snap하지 않고 프레임 독립적인 지수 보간을 사용한다.

```text
alpha = 1 - exp(-InterpSpeed * h)
```

`alpha`는 무차원 보간 비율, `InterpSpeed`는 `s^-1`, `h`는 내부 스텝 시간 `s`다.

- `WaterHeightInterpSpeed = 5 s^-1`
- `WaterNormalInterpSpeed = 5 s^-1`

두 값은 확정 기본값이며 step 응답의 약 95퍼센트를 `0.6 s` 안에 따라가게 한다. 짧은 파도 변화의 시각적 떨림을 줄이면서 수면 변화에 1초 이내로 반응하는 검증용 절충값이다. 둘 다 양수 `UPROPERTY(EditAnywhere, Category=ShipMovement)`로 두며 PIE에서 파도 마루 통과 시 선체가 튀거나 수면과 장시간 분리되지 않는지 확인한다.

Z는 현재 actor Z와 `SurfaceZ + WaterlineOffsetCm` 사이를 보간한다. normal은 현재 up vector와 목표 surface normal을 같은 alpha로 선형 보간한 뒤 normalize한다.

## yaw를 보존한 회전 재구성

수면의 pitch와 roll이 actor의 다음 수평 yaw 상태를 오염시키지 않도록 yaw는 별도 scalar state로 유지한다. 단순 평면 투영 `H - dot(H, N) * N`은 경사진 법선의 XY 성분을 빼므로 수평 heading을 바꾼다. 예를 들어 `H = (1, 0, 0)`, `N = normalize(1, 1, 1)`이면 투영 결과의 XY는 `(2/3, -1/3)`이고 원래 0 deg인 yaw가 약 `-26.565 deg`가 된다. 이 방식은 사용하지 않는다.

`BuildShipSurfaceBasis`는 world XY 전방을 그대로 두고 Z 성분만 법선과 직교하도록 다음 기저를 구성한다.

```text
psi = yaw_next in radians
H = (cos(psi), sin(psi), 0)
Z = (0, 0, 1)
N = normalized smoothed surface normal

Fraw = H - Z * (dot(H, N) / dot(Z, N))
F = normalize(Fraw)
R = normalize(cross(N, F))
U = normalize(cross(F, R))
```

`psi`의 단위는 `rad`이고 `H`, `Z`, `N`, `Fraw`, `F`, `R`, `U`는 무차원 방향 벡터다. `dot(Fraw, N) = 0`이며 `Fraw`의 XY가 정확히 `H`이므로 양의 normalize 뒤에도 `atan2(F.Y, F.X) = psi`다. `R = cross(N, F)`는 평면 수면에서 `+X` 전방에 대해 `+Y` 우현이 되고, `U = cross(F, R)`는 오른손 직교기저를 완성한다. 검증된 `F`와 `U`로 `FRotationMatrix::MakeFromXZ(F, U).ToQuat()`를 만든다.

현재 normal은 모든 성분이 유한하고 normalize 가능하며 `dot(Z, N) = N.Z >= MinSurfaceNormalZ`일 때만 사용한다. `MinSurfaceNormalZ`의 확정 내부 상수는 `0.1`이다. 이는 분모가 0에 가까워져 pitch와 roll이 폭증하는 수직에 가까운 sample을 거부하면서 일반적인 파도 경사는 허용하는 안전 경계다. 현재 normal이 거부되면 같은 조건을 만족한 마지막 유효 normal을 사용하고, 그것도 없으면 `Z`를 사용한다. 기저의 어느 벡터라도 비유한 값이거나 normalize할 수 없으면 `F = H`, `R = cross(Z, H)`, `U = Z`인 yaw 전용 기저로 fallback하고 상태를 표시한다. 임의의 수면 접선이나 기울어진 actor yaw를 fallback으로 사용하지 않으므로 모든 분기에서 world XY heading을 보존한다.

## transform 소유권과 sweep 경계

런타임 이동 중 actor transform을 바꾸는 곳은 `UShipMovement` 하나뿐이다. 각 내부 스텝에서 다음 overload를 사용한다.

```cpp
SetActorLocationAndRotation(
    NewLocation,
    NewRotation,
    true,
    &Hit,
    ETeleportType::None);
```

UE 5.5.4 선언에서 sweep은 root component만 검사하며 blocking 물체가 있으면 목적지 전에 멈춘다. 따라서 `AShipPawn`의 충돌 box를 root로 둔다. 시각 메시와 카메라는 child이므로 별도 sweep 주체가 아니다.

- `Hit.bBlockingHit`가 참이면 실제 sweep 결과 위치를 유지하고 signed speed를 0으로 만든다.
- blocking hit가 난 내부 스텝 뒤에는 해당 tick의 남은 내부 스텝을 실행하지 않는다.
- 저장된 throttle과 steer는 지우지 않는다. 다음 tick에 사용자가 후진하거나 조향을 바꿔 빠져나올 수 있어야 한다.
- overlap은 blocking 정지로 취급하지 않는다.
- hit actor 또는 component가 유효하면 화면 디버그에 이름과 impact normal을 표시한다.

`AShipPawn`, `ASimGameMode`, `UShipNavigator`는 이동 중 `SetActorLocation`, `SetActorRotation`, `AddActorWorldOffset` 또는 동등한 직접 transform 변경을 호출하지 않는다. GameMode의 초기 `SpawnActor` transform만 이 경계의 예외다.

## AShipPawn 구성

### 충돌과 시각 표현

`AShipPawn`은 다음 subobject를 constructor에서 만든다. 모든 UObject 파생 포인터 멤버에는 `UPROPERTY()`를 붙인다.

- `UBoxComponent` root: full size `200 x 100 x 100 cm`, box extent `100 x 50 x 50 cm`
- `UStaticMeshComponent` visual: 엔진 기본 `/Engine/BasicShapes/Cube.Cube`를 사용하고 relative scale `2 x 1 x 1`
- `UShipMovement` movement component
- `USpringArmComponent` camera boom
- `UCameraComponent` camera

충돌 root는 Pawn object type의 query collision을 사용해 WorldStatic과 WorldDynamic blocking sweep을 제공한다. 현재 WaterBodyCollision의 Pawn 응답은 overlap이므로 수면 자체가 이동을 막지 않는다. root의 physics simulation과 gravity는 끈다. visual mesh의 collision과 physics도 끄고 root collision만 권위 있게 사용한다.

엔진 기본 cube를 사용하므로 새 에셋을 만들거나 추가하지 않는다. mesh 로딩이 실패해도 collision root와 이동은 유지하고 오류를 한 번 표시한다.

### 3인칭 카메라 기본값

카메라는 선박에 붙은 spring arm 끝에 두며 pawn control rotation은 사용하지 않는다. spring arm은 `bInheritYaw = true`, `bInheritPitch = false`, `bInheritRoll = false`로 두어 yaw만 선박에서 상속하고 파도 위에서도 수평선을 안정적으로 유지한다.

| 튜닝 값 | 기본값 | 근거 |
| --- | --- | --- |
| `CameraArmLengthCm` | `600 cm` | 선체 길이의 3배 거리에서 전체 선체와 진행 방향을 함께 확인 |
| `CameraPitchDegrees` | `-20 deg` | 선체와 주변 수면을 동시에 보는 완만한 하향 시점 |
| `CameraSocketHeightCm` | `100 cm` | 선체 높이 1배만큼 시점을 들어 수면 가림을 줄임 |

세 값은 표의 확정 기본값을 가진 `UPROPERTY(EditAnywhere, Category=Camera)`로 둔다. spring arm collision test는 켠다. PIE에서 200 cm 선체가 화면 밖으로 잘리지 않고 250 cm 반경 선회 방향을 관찰할 수 있는지 확인한다. 레벨에 기존 blocking geometry가 있는 경우에는 카메라 collision도 함께 확인하되 검증을 위해 벽 actor나 asset을 새로 추가하지 않는다.

## 런타임 Enhanced Input

Enhanced Input 구현 파일에서 사용하는 UE 5.5.4 공개 헤더는 `InputAction.h`, `InputActionValue.h`, `InputMappingContext.h`, `EnhancedActionKeyMapping.h`, `InputModifiers.h`, `EnhancedInputComponent.h`, `EnhancedInputSubsystems.h`다. 키 상수에는 `InputCoreTypes.h`를 사용한다. 이 헤더들은 public class 선언에 노출하지 않고 `ShipPawn.cpp`에서 include하며 `ShipPawn.h`는 UObject class를 전방 선언해 현재 private module 의존성을 유지한다.

### 객체 소유권

입력 에셋 파일을 추가하지 않고 `AShipPawn`이 런타임에 다음 UObject를 만든다.

- `UInputAction* ThrottleAction`
- `UInputAction* SteerAction`
- `UInputMappingContext* ManualControlMapping`

세 멤버는 `UPROPERTY(Transient)`가 붙은 `TObjectPtr`로 보관하고 Pawn을 Outer로 사용한다. mapping별 `UInputModifierNegate`는 mapping context의 instanced modifier 배열이 소유한다. 등록에 성공한 `UEnhancedInputLocalPlayerSubsystem`도 `UPROPERTY(Transient)`가 붙은 weak pointer로 기억해 controller 상태와 무관하게 같은 context를 해제한다.

두 action은 `EInputActionValueType::Axis1D`와 `EInputActionAccumulationBehavior::Cumulative`를 사용한다.

- W는 throttle `+1`
- S는 negate modifier를 거쳐 throttle `-1`
- D는 steer `+1`
- A는 negate modifier를 거쳐 steer `-1`

같은 축의 양쪽 키를 동시에 누르면 누적값이 0이 되며 Pawn handler에서 마지막으로 `[-1, 1]` clamp한다.

`SetupPlayerInputComponent`에서 `UEnhancedInputComponent`를 확인하고 `Triggered`, `Completed`, `Canceled`를 bind한다. `Triggered`는 현재 Axis1D 값을 setter로 보내고, `Completed`와 `Canceled`는 해당 setter에 0을 보낸다.

로컬 `APlayerController`의 `ULocalPlayer`에서 `UEnhancedInputLocalPlayerSubsystem`을 구해 mapping context를 우선순위 0으로 등록한다. Pawn의 private 수명주기 경계는 다음 두 동작으로 고정한다.

- `ResetManualInput`은 `SetThrottle(0)`과 `SetSteer(0)`을 호출한다. Movement 내부 상태를 우회해 값을 직접 쓰지 않는다.
- `DeactivateManualInput`은 `ResetManualInput`을 먼저 호출하고, 등록 상태일 때만 저장한 local player subsystem에서 mapping context를 제거한 뒤 등록 flag와 weak pointer를 지운다. 이미 초기화됐거나 context가 없는 경우에도 안전한 멱등 동작이다.

`UnPossessed` override는 첫 동작으로 `DeactivateManualInput`을 호출하고 그 뒤 상위 수명주기를 진행한다. context 제거는 저장한 subsystem을 사용하므로 호출 시점의 controller 연결 여부에 의존하지 않는다. `EndPlay`도 종료 이유와 무관하게 같은 동작을 호출하므로 입력이 남은 채 Pawn이 제거되는 경로가 없다. `Completed`와 `Canceled`는 정상 키 해제와 입력 flush에서 각 축을 즉시 0으로 만들며, `UnPossessed`와 `EndPlay`의 공통 초기화는 이 이벤트가 오지 않는 수명 종료 경로까지 닫는다.

### DefaultInput.ini 필요성

런타임에 action과 mapping을 만들어도 PlayerInput과 InputComponent의 기본 클래스가 Enhanced Input 계열이어야 `SetupPlayerInputComponent`에서 `UEnhancedInputComponent`를 받을 수 있다. 따라서 구현 단계에는 `Config/DefaultInput.ini`가 필요하며 다음 설정을 가져야 한다.

```ini
[/Script/Engine.InputSettings]
DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput
DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent
bShouldFlushPressedKeysOnViewportFocusLost=True
```

이 경로는 UE 5.5.4 기본 C++ 템플릿의 설정과 일치한다. `bShouldFlushPressedKeysOnViewportFocusLost=True`이면 viewport focus를 잃을 때 플레이어 컨트롤러가 pressed key를 flush하고 Enhanced Input의 `Completed` 또는 `Canceled` 경로가 각 축 setter에 0을 보낸다. 현재 `Build.cs`에는 `InputCore`와 `EnhancedInput`이 private dependency로 이미 있으므로 새 모듈은 필요하지 않다.

### 수동 입력과 이후 자동주행의 중재

3단계에서는 수동 모드만 활성화한다. 이후 자동주행 활성화 경계에는 다음 불변조건을 적용한다.

- 수동에서 자동으로 바뀌는 순간 Pawn이 `ResetManualInput`을 호출해 남은 키 값을 지운다.
- 자동주행 활성 중 Pawn의 Enhanced Input handler는 입력 값을 무시한다.
- 자동주행은 별도 속도나 transform 통로를 만들지 않고 같은 두 setter만 호출한다.
- 자동에서 수동으로 돌아올 때 현재 눌림 상태를 임의로 재생하지 않고 다음 Enhanced Input 이벤트부터 반영한다.

자동주행 모드 전환 인터페이스와 Navigator 로직 자체는 이후 단계에서 정의한다. 3단계 제품 인터페이스에 미래용 공개 함수를 미리 추가하지 않는다.

## ASimGameMode의 테스트 부트스트랩

`ASimGameMode::BeginPlay`는 3단계 PIE 검증을 위해 다음 동작만 담당한다.

GameMode constructor에서 `DefaultPawnClass = nullptr`로 두어 엔진 기본 pawn이 먼저 자동 생성되는 경로를 막는다. 선박 생성 권위는 아래의 명시적 3단계 부트스트랩 한 곳에만 둔다.

1. 첫 플레이어 컨트롤러를 구한다.
2. 이미 유효한 `AShipPawn`을 possess하고 있으면 중복 생성하지 않는다.
3. 아니면 `ShipPawnClass`로 선박 한 대를 `AdjustIfPossibleButAlwaysSpawn` 정책으로 생성한다.
4. spawn이 성공하면 플레이어 컨트롤러가 새 선박을 possess한다.

`ShipPawnClass`와 `TestShipSpawnTransform`은 GameMode의 `UPROPERTY(EditAnywhere, Category=Stage3Test)`로 둔다. 확정 class 기본값은 `AShipPawn`, transform 기본값은 world origin과 yaw 0이다. 원점은 별도 코스나 시작점 actor를 요구하지 않는 중립 기본값이며, MainLevel의 배치된 Ocean 위에서 첫 수면 query가 실제 Z를 결정한다. PIE에서 원점이 Ocean 유효 영역인지 확인하고, 레벨 조건이 바뀌면 이 property만 조정한다.

플레이어 컨트롤러가 없거나 spawn이 실패하면 오류를 남기고 재귀 spawn이나 임의 actor 탐색을 하지 않는다. 이 경우 PIE 수동 조작은 실패 상태지만 레벨의 기존 actor와 설정은 변경하지 않는다.

이 자동 spawn과 possession은 3단계 검증 장치다. 이후 단계의 코스 또는 시나리오 부트스트랩이 책임을 넘겨받을 때 제거하거나 비활성화할 수 있어야 하며, Navigator, 벽, 캡처를 지금 GameMode에 넣지 않는다.

## 화면 디버그 값

에디터 및 Development 실행에서 `UShipMovement`는 고정 메시지 키로 다음 값을 한 블록에 갱신한다.

- clamp된 throttle과 steer
- signed speed `cm/s`
- 현재 longitudinal acceleration `cm/s^2`
- yaw rate `deg/s`
- 이번 프레임 내부 substep 수, step seconds, simulated DeltaTime과 dropped DeltaTime
- water의 다섯 상태, fallback 사용 여부, target surface Z, 적용 Z, waterline offset
- surface normal과 적용 up vector
- 마지막 blocking hit 대상과 impact normal
- 입력 모드 `Manual` 또는 이후 `Autopilot`
- parameter 검증 상태 `Defaults`, `Tuned` 또는 `TuningFallback`

`bShowMovementDebug`는 확정 기본값 true인 `UPROPERTY(EditAnywhere, Category=Debug)`로 두고 Shipping에서는 화면 출력을 하지 않는다. 디버그 표시는 관찰 전용이며 계산 상태를 수정하지 않는다.

## 오류와 fallback

| 상황 | 처리 |
| --- | --- |
| owner 또는 root component 없음 | movement tick을 비활성화하고 한 번 오류 기록 |
| 비유한 throttle 또는 steer | 해당 입력을 0으로 대체하고 한 번 경고 |
| 비유한 DeltaTime 또는 motion 결과 | transform 적용 중단, speed 0, 오류 상태 표시 |
| DeltaTime이 최대 모사 시간 초과 | `MaxSubstepsPerTick`회까지만 모사하고 초과 시간 폐기, 누적 금지, 횟수 제한 경고와 debug 표시 |
| 계수, 기준 속도 또는 substep 설정 검증 실패 | parameter snapshot 전체를 확정 기본값으로 복구하고 `TuningFallback` 표시 |
| 확정 기본값에서도 motion 상태가 지원 속도 범위 밖 | speed 0, 해당 transform 적용 중단, 오류 상태 표시 |
| Water subsystem 또는 Ocean component 없음 | `ComponentInvalid`, query 생략, 마지막 유효 수면 또는 현재 Z와 world up 사용 |
| exclusion volume 내부 | `Excluded`, 반환 sample 폐기, 수면 fallback 사용 |
| query flag, location 또는 normal 무효 | `QueryInvalid`, 새 sample 폐기, 수면 fallback 사용 |
| blocking hit | sweep 위치 유지, speed 0, 남은 내부 스텝 중단 |
| Enhanced Input component가 아님 | 이동 모듈은 유지, 수동 입력만 비활성화하고 오류 기록 |
| local player subsystem 없음 | mapping 등록을 생략하고 possession 상태 오류 기록 |
| 기본 cube 로딩 실패 | 충돌과 이동 유지, 시각 메시 오류 기록 |
| 테스트 ship spawn 실패 | possession 시도 중단, 원인 기록 |

fallback은 실패를 숨기지 않는다. 로그는 상태 전환마다 한 번 남기고 화면 디버그에는 현재 fallback을 계속 표시한다.

## 확정 파일별 책임

아래 목록은 설계상 책임 위치를 고정하며 구현 순서를 뜻하지 않는다.

- `Source/ShipAutonomySim/Public/ShipMovement.h`: 두 setter, component lifecycle 선언, 확정 기본값과 안전 범위를 가진 튜닝 `UPROPERTY`, UObject 포인터 전방 선언
- `Source/ShipAutonomySim/Private/ShipMovement.cpp`: parameter snapshot 검증, bounded substep, 수면 query, 보간, 회전 재구성, sweep, hit 및 debug 처리
- `Source/ShipAutonomySim/Private/ShipMovementSimulation.h`: module-private 값 타입과 `AdvanceShipMotion`, `BuildShipSubstepSchedule`, `ValidateShipMotionParameters`, `BuildShipSurfaceBasis`, `ResolveWaterSurfaceSample` 선언
- `Source/ShipAutonomySim/Private/ShipMovementSimulation.cpp`: longitudinal 및 yaw 단일 스텝, bounded schedule, parameter 검증, 수면 상태 분류와 직교기저 순수 계산
- `Source/ShipAutonomySim/Public/ShipPawn.h`: Pawn 구성, 입력 lifecycle override, private `ResetManualInput`과 `DeactivateManualInput`, 런타임 입력 UObject와 등록한 local player subsystem weak pointer 선언
- `Source/ShipAutonomySim/Private/ShipPawn.cpp`: box, cube, movement, camera 구성, 런타임 Enhanced Input adapter, 공통 입력 초기화와 mapping context 해제
- `Source/ShipAutonomySim/Public/SimGameMode.h`: constructor, 테스트 ship class와 spawn transform property
- `Source/ShipAutonomySim/Private/SimGameMode.cpp`: 기본 pawn 자동 생성을 끄고 3단계 ship spawn과 possession 수행
- `Source/ShipAutonomySim/Private/Tests/ShipMovementTests.cpp`: pure motion step, parameter 검증, bounded substep, 수면 분기, 회전 기하와 입력 수명주기 자동화 테스트
- `Config/DefaultInput.ini`: Enhanced PlayerInput, Enhanced InputComponent 기본 클래스와 viewport focus loss pressed-key flush
- `Source/ShipAutonomySim/ShipAutonomySim.Build.cs`: 기존 `InputCore`, `EnhancedInput`, `Water` private dependency 유지, 새 의존성 없음

`ShipNavigator`, `CourseBuilder`, `ShipCapture`, `SETUP.md`, `DefaultEngine.ini`, map과 asset은 이 설계의 제품 변경 대상이 아니다.

## 자동화 테스트

### 순수 수치 테스트

module-private `AdvanceShipMotion`과 parameter 검증 함수를 직접 호출해 다음을 검증한다.

- throttle과 steer가 `[-1, 1]`에 머문다.
- 양수와 음수 속도에서 drag가 항상 속도 반대 방향이다.
- throttle 1의 평형 가속도가 `200 cm/s`에서 0에 가깝다.
- throttle -1의 평형 가속도가 `-200 cm/s`에서 0에 가깝다.
- 확정 기본값과 `1/120 s` forward Euler에서 `180 cm/s` 최초 도달은 약 `3.9917 s`, 정지 거리는 약 `399.9615 cm`다.
- throttle이 0이 아닐 때 `5 cm/s` 이하 속도가 강제로 0이 되지 않는다.
- 반대 throttle에서 signed speed가 0을 통과해 반대 방향으로 전환한다.
- speed 0에서 steer만 입력해도 yaw가 변하지 않는다.
- speed `200 cm/s`, steer 1에서 yaw rate가 `45.83662361 deg/s`다.
- 표의 편집 범위 안에 있는 유효한 변형은 활성 snapshot으로 채택되고 확정 기본값은 `Defaults` 상태다.
- 음수, 비유한 값, 편집 범위 초과, `C1 = C2 = 0`, 유한 평형 속도 부재, `Veq <= StopSpeedThreshold`, `Veq > 500 cm/s`, `EulerStabilityNumber > 0.5`는 snapshot 전체를 확정 기본값으로 fallback시킨다.
- 비유한 입력과 지원 속도 범위를 벗어난 계산 결과가 transform에 전달될 값으로 번지지 않는다.

### 지원 FPS와 hitch 합격 기준

외부 프레임은 `15`, `30`, `60`, `120 FPS` 네 집합을 유지하고 `120 FPS`를 비교 기준으로 삼는다. 각 시나리오는 FPS만 바꾸며 동일한 초기 상태, 입력 시퀀스와 관측 시간을 사용한다. 기본 `MaxSimulationStepSeconds = 1/120 s`와 `MaxSubstepsPerTick = 8`을 사용하고, 정해진 관측 시간을 정확히 채우는 일정한 DeltaTime 시퀀스를 만든다.

| 시나리오 | 초기 상태와 입력 | 관측 시간 | 절대 합격 기준 | 각 FPS와 120 FPS의 최대 차이 |
| --- | --- | --- | --- | --- |
| 가속 speed | `v = 0`, `yaw = 0`, `T = 1`, `S = 0` | `4.0 s` | 최종 speed `180 ± 1 cm/s`, `180 cm/s` 최초 도달 `4.0 ± 0.05 s` | 최종 speed `0.1 cm/s` |
| 타력 distance | `v = 200 cm/s`, `yaw = 0`, `T = 0`, `S = 0` | `8.0 s` | 최종 speed 정확히 0, 누적 거리 `400 ± 5 cm` | 누적 거리 `0.5 cm` |
| yaw | `v = 200 cm/s`, `yaw = 0`, `T = 1`, `S = 1` | `2.0 s` | 누적 yaw `91.67324722 ± 0.1 deg` | 최단 각도 차 `0.1 deg` |

기본값에서 `DeltaTime = 0.5 s`인 hitch 테스트는 내부 스텝 수 8, `SimulatedDeltaTime = 0.066666667 s`, `DroppedDeltaTime = 0.433333333 s`를 각각 부동소수점 허용오차 `1e-6 s` 안에서 확인한다. speed, yaw와 transform 입력은 모두 유한해야 하며, 다음 정상 tick에서 버린 시간을 재생하지 않아야 한다. 설정 가능한 어느 값에서도 한 tick의 내부 호출 수가 `MaxSubstepsPerTick`을 넘지 않는지도 검증한다.

### 수면 분기와 회전 기하 테스트

- `IncludeWaves` 요청, `HasWaves() = true`, 유효 결과는 `ValidWaves`로 분류하고 새 surface 값을 채택한다.
- `IncludeWaves` 요청이 결과에 남아 있어도 `HasWaves() = false`이면 `ValidNoWaves`로 분류하고 유효한 평면 surface 값을 채택한다.
- exclusion 결과는 `Excluded`, 필수 flag 누락과 비유한 또는 안전 경계 밖 normal은 `QueryInvalid`, subsystem 또는 component 부재는 `ComponentInvalid`로 분류하고 각각 정해진 fallback을 사용한다.
- 유효 sample 뒤의 세 실패 상태는 마지막 유효 Z와 normal을 유지하고, 첫 query부터 실패한 경우에는 actor의 현재 Z와 world up을 사용한다.
- `H = (1, 0, 0)`, `N = normalize(1, 1, 1)`인 단순 투영 반례에서 결과 전방의 world XY yaw 오차가 `0.01 deg` 이하여야 한다.
- `yaw = 45 deg`, `N = normalize(0, 1, 1)`인 별도 측면 경사에서도 결과 전방의 world XY yaw 오차가 `0.01 deg` 이하여야 한다.
- 유효한 기저는 각 벡터 길이의 1에 대한 오차와 벡터 쌍의 내적 절댓값이 각각 `1e-5` 이하여야 하며 `dot(cross(F, R), U) >= 0.99999`여야 한다.
- `N.Z < 0.1`, 영벡터와 비유한 normal은 마지막 유효 normal 또는 world up을 거쳐 유한한 기저를 만들고, 최종 yaw 오차가 `0.01 deg` 이하여야 한다.

### actor와 월드 경계 테스트

- root collision이 blocking actor 앞에서 sweep hit를 내고 penetration 없이 speed가 0이 된다.
- Water가 없는 test world에서 현재 Z와 world up fallback으로 수평 이동을 계속한다.
- `UShipMovement` 외 클래스가 이동 중 actor transform을 직접 바꾸지 않는지 코드 검사로 확인한다.
- Input 설정이 `UEnhancedPlayerInput`, `UEnhancedInputComponent`와 focus loss pressed-key flush를 활성화하는지 확인한다.
- W 또는 D를 누른 뒤 `Completed`와 `Canceled`를 각각 발생시키면 대응하는 Movement 입력이 0이 된다.
- 입력을 누른 상태에서 `UnPossessed`와 `EndPlay`를 각각 실행하면 두 Movement 입력이 0이 되고 mapping context가 한 번만 제거된다.
- 플레이어 컨트롤러의 pressed key flush로 focus loss 경로를 재현한 뒤 두 Movement 입력이 0이며, 다음 motion step이 남은 추력이나 조향 없이 drag만 적용한다.

## PIE 사람 검증 체크리스트

- PIE 시작 시 선박 한 대가 자동 생성되고 플레이어가 possess한다.
- 선박이 200 x 100 x 100 cm 기본 cube로 보이며 physics simulation 없이 안정적으로 수면에 놓인다.
- 3인칭 카메라에서 선체 전체와 진행 방향을 볼 수 있다. 기존 blocking geometry가 있으면 카메라가 이를 관통하지 않는지도 확인한다.
- W는 전진, S는 후진, D는 우현, A는 좌현으로 동작한다.
- W와 S 또는 A와 D를 동시에 누르면 해당 축 입력이 0이 된다.
- W 또는 D를 누른 채 viewport focus를 다른 창으로 옮겼다가 돌아오면 debug throttle과 steer가 0이며 남은 추력이나 조향이 없다.
- 조종 중 PIE eject 또는 다른 Pawn possession으로 선박이 `UnPossessed`되면 입력이 즉시 0이 되고, 다시 possess했을 때 이전 키 값이 재생되지 않는다.
- 정지 상태에서 A 또는 D만 눌러도 선박이 회전하지 않는다.
- 전진과 후진 전환 시 순간이동하거나 speed 부호가 고착되지 않는다.
- 정지에서 W를 유지하면 약 4초에 `180 cm/s`에 도달하고 이후 약 `200 cm/s`에 수렴한다.
- 약 `200 cm/s`에서 W를 놓으면 약 400 cm를 진행한 뒤 `5 cm/s`에서 0으로 정지한다.
- 최고속도에서 최대 steer 원 궤적의 반경이 약 250 cm다.
- 15, 30, 60, 120 FPS 제한에서 동일 입력과 관측 시간을 사용한 speed, distance, yaw 기록이 자동화 테스트 표의 절대값과 FPS 간 허용오차를 만족한다.
- 파도 위에서 Z와 pitch 및 roll이 부드럽게 따라가고 횡경사에서도 world XY yaw가 흔들리지 않는다.
- 파도가 있는 Ocean은 `ValidWaves`, 파도가 없는 유효 Ocean은 `ValidNoWaves`로 표시되며 둘 다 fallback 없이 surface를 따른다.
- Ocean query를 사용할 수 없는 상태가 생기면 `Excluded`, `QueryInvalid` 또는 `ComponentInvalid` 원인이 표시되고 선박이 비유한 transform으로 사라지지 않는다.
- frame hitch를 유발해도 한 tick의 substep이 8회를 넘지 않고 dropped DeltaTime이 표시되며 다음 tick에서 catch-up 급가속이 없다.
- 기존 blocking geometry를 사용할 수 있으면 접촉 시 관통하지 않고 speed가 0이 되며 후진 입력으로 빠져나오는지 확인한다. geometry가 없으면 이 항목은 자동화 test world 결과로 대체하고 영구 벽을 추가하지 않는다.
- 화면에 입력, speed, acceleration, yaw rate, substep, water, hit 상태가 갱신된다.
- Output Log에 매 프레임 반복되는 경고, ensure, access violation이 없다.

## 3단계 완료 기준

- `UShipMovement`의 외부 제어가 `SetThrottle`과 `SetSteer`로 수렴한다.
- 확정 기본값의 선형 및 이차 저항 계수와 정지 임계값이 자동화 테스트에서 4초, 400 cm와 200 cm/s 목표를 만족한다.
- 편집 가능한 계수와 substep 설정이 범위, 평형 속도와 Euler 안정성 검증을 통과하거나 snapshot 전체가 확정 기본값으로 안전하게 fallback한다.
- 전진, 후진, 속도 비례 조향, 0속도 회전 금지가 동작한다.
- 기본 8회 최대 내부 스텝과 초과 시간 폐기 정책으로 15, 30, 60, 120 FPS 결과와 hitch 결과가 명시한 허용오차 안에 있다.
- Water의 `ValidWaves`, `ValidNoWaves`, `Excluded`, `QueryInvalid`, `ComponentInvalid` 분기 모두에서 유한한 transform을 유지한다.
- 수면 Z와 normal을 부드럽게 따라가면서 횡경사 반례에서도 world XY yaw를 `0.01 deg` 이내로 보존한다.
- 모든 런타임 이동이 `UShipMovement`의 swept `SetActorLocationAndRotation`을 통과한다.
- blocking hit가 penetration 없이 speed를 초기화한다.
- 엔진 기본 시각 메시, 충돌 root, 3인칭 카메라, 런타임 WASD가 동작한다.
- action 종료, focus loss, unpossession과 `EndPlay` 뒤 throttle과 steer가 0이고 mapping context가 남지 않는다.
- `ASimGameMode`의 테스트 spawn과 possession으로 MainLevel PIE 검증을 시작할 수 있다.
- 자동화 테스트와 적용 가능한 PIE 체크리스트가 통과하고 빌드 및 map load 검증 뒤 작업 트리가 예상 파일만 포함한다.
- Navigator, 코스, 벽, 캡처, 횡미끄러짐이 유입되지 않는다.

## 채택하지 않은 대안

### 순수 이차 저항

실제 0속도에 유한 시간과 거리로 도달하지 않으며, 4초 가속과 400 cm 타력 거리 목표를 동시에 맞추지 못해 제외한다.

### 저속 구간의 상수 저항

속도 부호에 따라 일정 크기의 저항을 빼는 방식은 0 근처에서 가속도가 불연속이고 한 스텝 안에서 부호를 반복해 넘는 떨림을 만들기 쉽다. 정지 임계값과 연속 선형 저항이 같은 목적을 더 명확하게 달성하므로 제외한다.

### 횡미끄러짐 모델

횡속도와 횡저항을 추가하면 선회 중 slip angle과 선체 방향을 더 현실적으로 표현할 수 있지만 상태, 계수와 검증 항목이 크게 늘어난다. 이번 단계의 정량 목표는 전방 signed speed와 yaw만으로 충족되며 횡방향 미끄러짐은 승인 범위 밖이므로 제외한다.
