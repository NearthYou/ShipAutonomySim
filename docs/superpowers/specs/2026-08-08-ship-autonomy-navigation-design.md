# 4단계 코스 배치와 자율주행 설계

## 문서 목적과 결정 상태

이 문서는 4단계 코스 배치와 선박 자율주행의 확정 설계다. 코스 기하, 런타임 책임, 경로 추종, 타력 정지, 결과 판정, 반복 검증 계약을 정의한다. 구현 순서와 커밋 분할은 후속 구현 계획에서 다룬다.

과제 원문, 프로젝트 브리프의 4단계, 현재 2단계 골격 자료, 3단계 설계와 구현 계획, 현재 CourseBuilder, ShipNavigator, ShipPawn, ShipMovement, SimGameMode 및 기존 자동화 테스트를 기준으로 작성했다. Unreal 선언은 로컬 UE 5.5.4 헤더와 소스에서 확인한 범위만 기록한다.

이 문서의 확정은 구현, 빌드, 에디터 실행 또는 자동화 통과를 뜻하지 않는다.

## 목표

- Play 시작만으로 코스 생성, 선박 생성, 자율주행 시작이 이어지게 한다.
- 벽 슬라이드가 어느 허용값이든 더 짧은 쪽 끝을 지나는 3점 경로를 만든다.
- 자율주행이 3단계 UShipMovement의 SetThrottle과 SetSteer만 사용하게 한다.
- 전방 주시와 각도 기반 감속으로 관성이 있는 선박이 경로를 추종하게 한다.
- 현재 속도에서 계산한 동적 정지거리로 최종 구간의 타력 정지를 시작한다.
- Collision, Success, Timeout 중 하나만 최종 결과가 되게 한다.
- 실제 MainLevel, 실제 Water, 실제 3단계 이동과 sweep을 사용해 11개 슬라이드 값을 반복 검증한다.
- 재현에 필요한 seed, 슬라이드, 주행 결과와 검증 관측값을 로그로 남긴다.

## 비목표

- PCG로 단일 벽 코스를 생성하지 않는다.
- A*, NavMesh 또는 일반 장애물 탐색기를 만들지 않는다.
- Niagara 물보라를 4단계 또는 4단계 자동화에 넣지 않는다.
- CSV writer, Saved 결과 파일 또는 결과 파일의 Git 추적을 추가하지 않는다.
- 컬러 및 깊이 캡처와 웹 뷰어를 변경하지 않는다.
- 환경 장식, 복수 장애물, 동적 장애물 또는 추가 코스 유형을 만들지 않는다.
- 3단계의 횡방향 미끄러짐 부재를 재설계하지 않는다.
- reverse thrust, 순간이동, 직접 위치 대입 또는 sweep을 우회하는 이동 통로를 추가하지 않는다.

## 기존 계약과 4단계 경계

현재 3단계 구현은 다음 계약을 이미 가진다.

- UShipMovement가 signed speed, 수면 정렬과 runtime transform 변경을 소유한다.
- runtime transform은 sweep을 켠 SetActorLocationAndRotation으로만 변경된다.
- 외부 제어는 SetThrottle(float)과 SetSteer(float) 두 함수로 수렴한다.
- 선박 충돌 root는 200 x 100 x 100 cm box다.
- 전진 최고속도는 약 200 cm/s, 정지 판정 속도는 5 cm/s다.
- 선형 저항 계수 C1은 0.447501534 s^-1이고 이차 저항 계수 C2는 0.000400390770 cm^-1이다.
- forward Euler 내부 최대 스텝은 1/120 s이고 tick당 최대 8회다.
- 기존 ShipAutonomySim.ShipMovement 자동화 테스트 12개가 입력, 수면, sweep과 transform 소유권을 고정한다.

4단계는 이 계약을 교체하지 않고 그 위에 코스와 판단을 추가한다.

## 고정 수치

### 코스와 벽

| 항목 | 확정값 |
| --- | --- |
| 시작점 course 좌표 | (0, 0) cm |
| 끝점 course 좌표 | (2000, 0) cm |
| 벽 중심 전진 좌표 | 1000 cm |
| 벽 슬라이드 s | -500 cm 이상 500 cm 이하 |
| 벽 두께 | 100 cm |
| 벽 길이 | 1000 cm |
| 벽 높이 | 500 cm |
| 벽 cube scale | (1, 10, 5) |
| 벽 수중 깊이 | 100 cm |
| 벽 중심 Z | wave 제외 reference surface Z + 150 cm |
| 수면 위 벽 높이 | 400 cm |
| s가 0 이상일 때 waypoint | (1000, s - 750) cm |
| s가 0 미만일 때 waypoint | (1000, s + 750) cm |

### 추종과 종료

| 항목 | 확정값 |
| --- | --- |
| lookahead | 300 cm |
| full steer가 되는 heading error | 30 deg |
| full throttle 상한 | abs error 20 deg 이하 |
| minimum throttle 하한 | abs error 60 deg 이상 |
| minimum throttle | 0.35 |
| coast 여유 | 25 cm |
| 성공 goal XY 반경 | 100 cm |
| 성공 speed 상한 | abs speed 5 cm/s |
| timeout | 45 s |

### 반복 검증

| 항목 | 확정값 |
| --- | --- |
| slide 시작 | -500 cm |
| slide 끝 | 500 cm |
| slide 간격 | 100 cm |
| case 수 | 11 |
| 합격 success | 11/11 |
| 허용 collision | 0 |
| 허용 timeout | 0 |
| 허용 setup error | 0 |
| 각 case 최소 wall distance | 0 cm 초과 |

이 수치들은 4단계 기본값이자 합격 조건이다. 적절한 튜닝 값은 UPROPERTY(EditAnywhere, Category=...)로 노출하되, 4단계 자동화는 위 기본값으로 실행한다. 기본값 변경은 별도 설계 변경이다.

## course frame과 좌표 변환

ACourseBuilder actor의 world XY와 yaw만 course frame으로 사용한다.

- course +X는 시작점에서 끝점으로 향하는 전진축이다.
- course +Y는 수평 횡축이다.
- actor roll, pitch, world Z와 scale은 course frame 계산에 사용하지 않는다.
- 기본 actor transform이 identity이면 course origin은 world origin이고 yaw는 0 deg다.

actor yaw로 만든 수평 단위 전방 F와 우측 R을 사용해 course 좌표 (x, y)를 다음처럼 world XY로 바꾼다.

world XY = actor world XY + x F + y R

시작점, waypoint, 끝점의 경로 계산과 모든 진행도 및 거리 판정은 XY에서만 수행한다. target point의 표시 Z는 같은 wave 제외 reference surface를 사용하며 별도 높이 offset을 만들지 않는다. goal 성공도 target actor의 표시 높이가 아니라 끝점의 world XY를 기준으로 한다.

## 코스 생성

### 런타임 actor

ACourseBuilder는 다음 actor를 런타임에 생성하고 소유 참조를 유지한다.

- 시작점 ATargetPoint 한 개
- 끝점 ATargetPoint 한 개
- 벽 AStaticMeshActor 한 개

시작점과 끝점은 각각 변환된 course 좌표 (0, 0), (2000, 0)에 놓인다.

벽은 변환된 course 좌표 (1000, s)에 놓이고 course yaw를 그대로 사용한다. 엔진 기본 mesh /Engine/BasicShapes/Cube.Cube를 사용하며 scale은 (1, 10, 5)다. 기본 cube 한 변이 100 cm이므로 실제 크기는 X/Y/Z 100/1000/500 cm다.

벽 static mesh component의 collision 계약은 다음과 같다.

- CollisionEnabled는 QueryOnly
- object type은 WorldStatic
- 모든 채널 응답을 Ignore로 초기화한 뒤 Pawn 채널만 Block

actor 포인터는 UPROPERTY(Transient)가 붙은 TObjectPtr로 보관한다. 클래스, mesh와 고정 기본값을 조정하는 값은 역할에 맞는 UPROPERTY로 관리한다. world가 actor 수명을 소유하지만 raw UObject 포인터를 장기 멤버로 두지 않는다.

### reference water surface

벽 world XY에서 Ocean component에 ComputeLocation을 요청한다. IncludeWaves는 넣지 않는다. 이 결과를 wave 제외 reference surface로 사용한다.

reference surface가 유효하면 벽 중심 Z는 surface Z + 150 cm다. 따라서 500 cm 높이의 벽은 아래쪽 100 cm가 물에 잠기고 위쪽 400 cm가 수면 위에 남는다.

Water subsystem, Ocean component, query 결과 또는 surface 위치가 유효하지 않으면 코스를 임의 Z에 만들지 않는다. GameMode가 Running에 들어가기 전 setup failure로 분류하고 원인을 로그에 남긴다.

### slide와 waypoint

벽의 횡축 점유 구간은 [s - 500, s + 500]이다. s가 허용 범위 어디에 있어도 y = 0 직선은 벽 구간 안에 있으므로 직진 경로는 항상 막힌다.

- s가 0 이상이면 가까운 끝은 s - 500이고 waypoint는 그 바깥 250 cm인 s - 750이다.
- s가 0 미만이면 가까운 끝은 s + 500이고 waypoint는 그 바깥 250 cm인 s + 750이다.

반환 경로는 start, waypoint, end 순서의 정확히 3점 polyline이다. 250 cm는 벽 끝과 waypoint 중심선 사이의 clearance다. 선박 footprint를 고려한 별도 임의 합격 margin은 두지 않는다.

### 일반 Play와 Automation 주입

UGameplayStatics::HasOption이 false인 일반 Play에서만 ACourseBuilder가 시간 기반 새 seed로 FRandomStream을 초기화하고 s를 [-500, 500]에서 뽑는다. initial seed와 실제 s를 한 번 로그에 남긴다.

ASimGameMode는 InitGame에서 먼저 UGameplayStatics::HasOption으로 URL option Stage4Slide의 존재 여부를 판정한다. HasOption이 true이면 ParseOption 결과가 비어 있지 않은지, 숫자 변환이 성공했는지, 값이 NaN이나 Inf가 아닌지, [-500, 500] 범위 안인지 검증한다. 어느 조건이라도 실패하면 random 값으로 대체하지 않고 setup failure 원인으로 보존한다. Automation actual-world 11 case는 검증을 통과하는 forced value만 전달한다.

GameMode는 CourseBuilder를 deferred spawn하고 강제 s를 주입한 뒤 FinishSpawning을 호출한다. 이렇게 해야 강제값이 CourseBuilder BeginPlay 전에 들어간다. option이 없을 때만 일반 Play random 경로를 사용한다.

## 모듈 책임

### ACourseBuilder

ACourseBuilder는 course frame을 world XY로 변환하고 reference water surface를 조회한다. 시작점, 끝점과 벽을 생성하고 실제 s와 3점 경로를 제공한다.

ACourseBuilder는 선박을 생성하거나 움직이지 않는다. 주행 결과, timeout과 입력 모드를 소유하지 않는다.

### ASimGameMode

ASimGameMode는 전체 run의 조율자이자 terminal result의 유일한 소유자다.

- InitGame에서 Stage4Slide option을 읽고 검증한다.
- BeginPlay에서 CourseBuilder, course actor와 선박을 준비한다.
- 선박을 시작점에 한 번 spawn하고 첫 player controller가 possess하게 한다.
- AShipPawn::EnterAutonomy를 호출해 자율주행을 시작한다.
- Running, Success, Timeout, Collision 중 정확히 한 상태만 소유한다.
- bRuntimeCalculationError 또는 동등한 run 단위 bool latch를 소유한다.
- collision, success, timeout 조건을 한 곳에서 우선순위대로 평가한다.
- terminal 전환, setup failure와 최초 runtime calculation error를 각각 한 번만 로그에 남긴다.

setup failure는 네 terminal result 중 하나로 위장하지 않는다. run이 Running에 진입하기 전의 별도 부트스트랩 실패다.

### AShipPawn

AShipPawn은 UShipMovement와 UShipNavigator를 구성한다.

AShipPawn::EnterAutonomy는 다음 순서를 지킨다.

1. 남은 throttle과 steer를 0으로 만든다.
2. 등록된 수동 Enhanced Input mapping을 제거한다.
3. 늦게 도착한 Completed 또는 Canceled 입력이 자동 입력을 지우지 못하도록 수동 입력을 비활성화한다.
4. 3점 경로와 관찰에 필요한 course 정보를 Navigator에 전달한다.
5. Navigator를 활성화한다.

EnterAutonomy는 actor transform, speed 또는 yaw를 직접 변경하지 않는다.

### UShipNavigator

UShipNavigator는 판단만 담당한다.

- 현재 선박 XY와 수평 heading을 읽는다.
- 현재 active segment의 progress와 lookahead를 갱신한다.
- heading error에서 steer를 계산한다.
- 각도 구간에서 throttle을 계산한다.
- 최종 구간에서 dynamic stopping distance를 계산하고 coast latch를 건다.
- UShipMovement의 SetThrottle과 SetSteer만 호출한다.
- path, waypoint, lookahead와 actual wall debug를 그린다.

UShipNavigator는 SetActorLocation, SetActorRotation, TeleportTo 또는 component transform mutator를 호출하지 않는다. 주행 중 actor 위치를 직접 대입하지 않는다.

### UShipMovement

UShipMovement는 3단계 책임을 유지한다.

- 실제 runtime transform은 sweep으로만 변경한다.
- 현재 signed speed를 읽기 전용으로 노출한다.
- blocking hit 신호와 hit actor 또는 component 식별 정보를 GameMode의 같은 run 판정까지 잃지 않게 노출한다.
- 입력은 기존 SetThrottle과 SetSteer로만 받는다.

새 읽기 경계의 최종 인터페이스 이름과 tick prerequisite에 필요한 선언은 구현 계획 첫 기술 검증에서 현재 로컬 헤더와 기존 test accessor를 다시 확인해 고정한다. 이 설계는 승인되지 않은 별도 speed setter나 transform 통로를 만들지 않는다.

## runtime 데이터 흐름

1. ASimGameMode::InitGame이 HasOption으로 Stage4Slide option 유무를 구분하고, 존재할 때만 ParseOption 결과를 검증해 기록한다.
2. ASimGameMode::BeginPlay가 CourseBuilder를 deferred spawn한다.
3. forced slide가 있으면 CourseBuilder BeginPlay 전에 주입한다.
4. CourseBuilder가 reference surface, 시작점, 끝점, actual wall과 3점 path를 만든다.
5. GameMode가 정확히 3점인 path와 runtime actor 유효성을 확인한다.
6. GameMode가 start에 AShipPawn을 생성하고 첫 player controller가 possess하게 한다.
7. AShipPawn::EnterAutonomy가 수동 입력을 제거하고 Navigator를 활성화한다.
8. Navigator가 progress, lookahead, steer와 throttle을 계산한다.
9. UShipMovement가 입력을 적분하고 swept transform을 적용한다.
10. GameMode가 movement 이후의 blocking hit, goal과 speed, elapsed time을 평가한다.
11. terminal 조건이 있으면 우선순위대로 한 결과를 확정하고 Navigator 입력을 0으로 만든다.

Navigator 판단이 movement보다 먼저, GameMode terminal 평가가 movement보다 뒤에 실행되는 순서를 보장해야 한다. 구체적인 tick prerequisite 선언은 구현 계획에서 로컬 UE 5.5.4 tick 헤더를 다시 확인해 고정한다. blocking hit 신호는 이 순서 안에서 누락되지 않게 유지한다.

새 run이 Running에 들어갈 때 runtime calculation error latch를 false로 초기화한다. 그 run의 어느 계산 단계에서든 위치, progress, lookahead 또는 후속 판단값이 유효하지 않으면 GameMode가 latch를 true로 한 번만 바꾼다. 최초 전환에서 Navigator를 비활성화하고 SetThrottle(0), SetSteer(0)을 보낸 뒤 이후 tick에도 두 입력을 0으로 유지한다. 후속 계산이 정상으로 돌아와도 latch를 지우거나 Navigator를 다시 활성화하지 않는다.

## 3점 polyline 추종

### progress 상태

경로 점을 P0, P1, P2로 두고 segment 0은 P0에서 P1, segment 1은 P1에서 P2다. 각 segment 길이와 누적 길이는 초기화 때 한 번 계산한다. 길이가 0인 segment나 비유한 점은 setup failure다.

Navigator는 다음 상태만 유지한다.

- active segment index
- 이전까지 확정한 누적 progress
- coast latch 여부

매 tick에는 active segment 하나에만 현재 선박 XY를 투영한다.

1. active segment 시작점 A, 끝점 B와 단위 방향 D를 구한다.
2. along = dot(ShipXY - A, D)를 계산한다.
3. active segment 안의 투영 길이는 along을 0과 segment length 사이로 제한한다.
4. candidate progress는 이전 segment 누적 길이와 제한된 투영 길이의 합이다.
5. progress는 max(previous progress, candidate progress)다.

이 max 규칙 때문에 진행도가 되돌아가지 않는다. 전체 polyline에서 가장 가까운 segment를 다시 찾지 않으므로 벽 반대편 미래 segment로 점프하지 않는다.

### segment 전환

현재 segment 끝점 평면 통과는 dot(ShipXY - B, D)가 0 이상인지로 판정한다.

- 끝점 평면을 지나지 않았으면 active segment를 유지한다.
- 끝점 평면을 지났고 다음 segment가 있으면 active segment를 정확히 한 번만 증가시킨다.
- 전환 tick의 progress는 현재 segment 끝 누적 길이 이상으로 고정한다.
- 새 segment 투영은 다음 tick부터 적용한다.
- 한 tick에서 두 segment를 연속 전환하지 않는다.

이 규칙은 관성으로 waypoint를 지나도 진행을 허용하면서 미래 segment 점프와 이전 segment 복귀를 모두 막는다.

### lookahead

total length를 L이라 할 때 lookahead arc length는 다음과 같다.

lookahead distance = min(progress + 300 cm, L)

이 누적 길이에 해당하는 polyline 점을 보간해 live target으로 사용한다. lookahead는 active segment 끝을 넘어 다음 segment에 놓일 수 있지만 progress 투영 자체는 active segment에만 머문다.

### steer

현재 actor forward를 XY에 투영해 normalize하고, 현재 XY에서 lookahead로 향하는 XY 방향을 구한다. 둘 사이 signed heading error를 deg로 계산하고 [-180, 180]으로 정규화한다.

steer = clamp(heading error / 30 deg, -1, 1)

UE 좌표에서 positive yaw와 positive steer가 +X에서 +Y 방향 선회를 만들도록 signed angle 부호를 맞춘다.

### throttle

heading error 절댓값 e에 따라 throttle을 다음처럼 정한다.

- e가 20 deg 이하이면 1
- e가 60 deg 이상이면 0.35
- 20 deg보다 크고 60 deg보다 작으면 1에서 0.35까지 선형 보간

중간 구간 식은 다음과 같다.

throttle = 1 - ((e - 20) / 40) x 0.65

이 감속은 3단계 yaw 식의 최소 선회반경을 줄이지 않는다. yaw rate와 speed가 함께 선형으로 줄어드는 구간에서는 기하학적 선회반경이 유지된다. 감속의 목적은 시간당 전진량과 transient overshoot를 줄이는 것이며 더 작은 반경을 새로 만드는 것이 아니다.

## dynamic stopping distance와 coast latch

최종 segment에서만 dynamic stopping distance를 사용한다.

순수 helper는 현재 양의 speed와 3단계 확정 기본 parameter를 입력으로 받고 다음 식을 3단계와 같은 forward Euler 순서로 적분한다.

dv/dt = -C1 v - C2 v^2

- C1 = 0.447501534 s^-1
- C2 = 0.000400390770 cm^-1
- 내부 step = 1/120 s
- 적분 종료 speed = 5 cm/s

각 step의 이동거리는 3단계와 같이 update 전 speed x step seconds로 누적한다. speed가 5 cm/s 이하이면 stopping distance는 0이다. 비유한 입력, 음수 입력 또는 다음 speed가 감소하지 않는 잘못된 parameter는 유효한 거리로 취급하지 않고 run setup 또는 계산 오류로 드러낸다. 정상 기본값에서는 speed가 단조 감소해 helper가 종료한다.

final remaining은 total length - progress다. final remaining이 dynamic stopping distance + 25 cm 이하가 되는 첫 tick에 coast를 latch한다.

- latch 뒤 throttle은 terminal까지 0이다.
- reverse throttle은 호출하지 않는다.
- steer 판단은 계속할 수 있지만 throttle 0 규칙을 덮어쓸 수 없다.
- coast가 너무 일찍 끝나 goal 반경 밖에 정지해도 다시 추진하거나 후진하지 않는다. 이 경우 Success가 아니며 45 s에 Timeout이 된다.

## 성공과 terminal result

Success 조건은 두 조건을 동시에 만족해야 한다.

- ship과 end의 XY 거리 100 cm 이하
- UShipMovement의 abs signed speed 5 cm/s 이하

runtime calculation error latch가 true이면 두 조건이 나중에 만족돼도 Success를 허용하지 않는다.

GameMode는 한 평가 시점에 여러 조건이 참이면 다음 우선순위를 사용한다.

1. Collision
2. Success
3. Timeout

Running에서 terminal result로 전환은 한 번만 허용한다. terminal 뒤에는 Navigator를 비활성화하고 SetThrottle(0), SetSteer(0)을 보낸다. 이전 result를 덮어쓰거나 중복 결과 로그를 남기지 않는다.

Collision은 UShipMovement의 blocking hit 신호로 판정한다. 벽 이외의 예상치 못한 blocking geometry도 성공으로 숨기지 않고 Collision으로 종료하며 hit 대상을 로그에 포함한다.

Timeout 기본값은 45 s인 UPROPERTY다. elapsed time은 run이 Running에 들어간 시점부터 계산하며 course spawn과 map setup 시간은 포함하지 않는다.

## 오류 처리

| 상황 | 처리 |
| --- | --- |
| Stage4Slide option이 없고 HasOption이 false | 일반 Play random slide 사용 |
| Stage4Slide option이 있지만 ParseOption 결과가 empty | setup failure, random fallback 금지 |
| Stage4Slide 값이 junk이거나 숫자 변환 실패 | setup failure, random fallback 금지 |
| forced slide가 NaN, Inf 또는 [-500, 500] 범위 밖 | setup failure |
| Water subsystem 또는 Ocean component 없음 | setup failure |
| wave 제외 reference surface query 실패 | setup failure |
| target 또는 wall spawn 실패 | setup failure |
| cube mesh load 또는 할당 실패 | setup failure |
| path가 3점이 아니거나 segment 길이가 0 | setup failure |
| player controller 또는 ship spawn 실패 | setup failure |
| Movement 또는 Navigator component 없음 | setup failure |
| Running 중 위치, progress, lookahead 또는 후속 판단값이 유효하지 않음 | runtime calculation error latch, Navigator 비활성화, 입력 0 유지, Success 금지 |
| blocking hit | Collision pending 후 우선순위 평가 |
| goal과 speed 조건 만족 | Success pending |
| elapsed 45 s 도달 | Timeout pending |

setup failure는 test의 success 수에 포함하지 않는다. map load, spawn, mesh와 Water 원인을 서로 구분해 case 로그와 최종 집계 로그에 남긴다.

runtime calculation error는 Running 진입 뒤에만 발생하는 run 실패이며 setup failure가 아니다. 최초 원인만 로그에 남기고 네 상태 enum에 새 result를 추가하지 않는다. 다른 terminal 조건이 없으면 일반 run은 기존 Timeout으로 귀결된다. Collision이 발생하면 기존 우선순위를 유지한다. Automation은 latch를 관측하는 즉시 runtime calculation error를 별도 fail reason과 count로 기록해 case를 실패시킬 수 있으므로 Timeout까지 기다릴 필요가 없다.

## debug와 로그

Development와 Editor 실행에서 다음 색을 고정한다.

- path 두 선분은 green
- waypoint는 yellow
- live lookahead는 cyan
- actual wall은 red

actual wall debug는 고정 course 중심선이 아니라 실제 spawned wall transform과 extents를 사용한다. debug draw는 관찰 전용이며 progress, collision과 결과 판정을 바꾸지 않는다.

일반 Play 로그에는 seed와 실제 s를 남긴다. 모든 run은 시작, terminal 또는 setup failure를 한 번씩 남긴다. terminal 로그에는 result, slide와 elapsed를 포함한다. 매 tick 반복 로그는 만들지 않는다.

## 반복 Automation 설계

### 실행 형태

반복 검증은 Unreal Automation으로 구현한다. 한 actual-world latent test가 다음 slide를 순서대로 실행한다.

- -500
- -400
- -300
- -200
- -100
- 0
- 100
- 200
- 300
- 400
- 500

각 case는 /Game/Maps/MainLevel?Stage4Slide=<value> URL을 사용하고 force reload하여 fresh MainLevel world를 만든다. ASimGameMode::InitGame이 option을 BeginPlay 전에 읽는다.

로컬 UE 5.5.4의 AutomationOpenMap 구현은 game context에서 전체 Open URL을 전달하고, editor handler에서는 MapName을 editor asset load에도 사용한다. 따라서 URL option을 보존하는 actual-world runner 형태는 구현 계획 첫 검증에서 game-context Automation 명령으로 고정한다. editor map loader에 option이 포함된 asset path를 넘기는 방식은 사용하지 않는다.

case 사이에서 유지되는 집계는 world가 소유하지 않는 Automation test 메모리에 둔다. 새 world가 열릴 때 이전 ship, GameMode와 course actor 포인터를 재사용하지 않는다.

### case 관측값

완료된 각 case는 Automation test 메모리에 다음 네 값만 보관한다.

| 열 | 의미 |
| --- | --- |
| slide | forced Stage4Slide cm |
| success | terminal result가 Success인지 |
| elapsed | Running에서 terminal까지 시간 |
| min wall distance | 실제 ship box와 wall cube의 XY footprint 사이 최소거리 cm |

각 case 결과와 11개 최종 집계를 로그에 남긴다. CSV writer, Saved 파일과 결과 asset은 만들지 않는다. 구현 완료 뒤 별도 최종 검증 보고서에만 이 네 열을 표로 옮긴다.

setup failure는 완료된 결과 행으로 위장하지 않는다. 원인을 별도 로그에 남기고 11건 완전 조건을 실패시킨다. 통과 보고서의 표는 완료된 11개 case를 정확히 한 행씩 가진다.

runtime calculation error도 네 열 결과에 새 열이나 terminal result로 섞지 않는다. Automation test 메모리에 별도 fail reason과 count만 보관하고 latch 관측 즉시 case와 전체 합격을 실패시킨다.

### 실제 XY footprint 거리

이 과제의 두 실제 collision body 사이 XY footprint 거리를 직접 반환하는 적용 가능한 UE body 간 거리 API가 없으므로 재현 가능한 2D convex footprint 계산을 사용한다.

- ship footprint는 UBoxComponent local half extent의 8개 corner를 현재 full world transform으로 변환하고 XY에 투영한 점들의 convex hull이다.
- wall footprint는 actual wall collision component의 cube local bounds 8개 corner를 full world transform으로 변환하고 XY에 투영한 점들의 convex hull이다.
- 두 convex hull의 edge 교차 또는 포함으로 overlap이 확인되면 거리는 0이다.
- blocking hit의 actor가 actual wall이거나 component가 그 wall collision component일 때만 wall gap을 0으로 처리한다.
- 다른 blocking geometry hit는 Collision terminal로 처리하지만 현재 geometric wall gap을 0으로 덮지 않는다.
- 두 hull이 분리돼 있으면 양쪽 hull vertex와 반대 hull edge 사이 point-to-segment 거리의 최솟값을 구한다.
- 매 관측 tick의 값을 run minimum과 비교해 최솟값을 유지한다.

이 값은 현재 선박의 roll, pitch, yaw와 wall full transform을 반영한 XY convex gap이다. 실제 collision mesh 사이 3D body distance가 아니라 그 XY convex 거리의 재현 가능한 대용치이며, actor origin 거리나 axis-aligned world bounds 거리가 아니다.

250 cm는 waypoint 중심선 clearance이므로 별도의 임의 margin 합격값을 두지 않는다. 합격은 min wall distance가 엄격히 0보다 큰지로 판정하고 실제 값을 보고한다.

### actual-world 증거

각 case에서 다음을 확인한다.

- 로드된 world가 fresh MainLevel이다.
- ASimGameMode가 forced slide를 사용했다.
- 실제 Water subsystem과 Ocean component가 유효하다.
- spawned ship이 현재 AShipPawn이고 기존 UShipMovement를 가진다.
- Stage 3 movement의 swept transform 경로가 실제로 실행됐다.
- actual wall은 지정 mesh, scale과 collision 계약을 가진다.
- terminal 또는 setup failure가 명확히 관측된다.

기존 WITH_DEV_AUTOMATION_TESTS sweep trace와 transform ownership 검사를 함께 사용해 actual-world ship이 3단계 swept call을 실행했다는 증거를 남긴다. 제품 public API를 test만을 위해 확장하지 않는다.

## 자동화 테스트 범위

### 순수 테스트

다음 네 영역을 UObject와 world에서 분리한 순수 계산으로 검증한다.

- course geometry: slide 경계, 짧은 쪽 선택, 3점 path, course yaw 변환과 Stage4Slide absent, empty, junk, NaN, Inf, 범위 밖 option 계약
- progress: active segment projection, 단조 progress, 끝점 평면 전환, tick당 최대 한 전환과 lookahead
- stopping distance: 5 cm/s 이하 0, 유한성과 양의성, speed 증가에 따른 비감소, 3단계 기본값과 같은 적분
- terminal priority: Collision, Success, Timeout 조합에서 고정 우선순위와 단일 전환, runtime calculation error latch의 영속성과 Success 금지

부동소수점 비교 허용오차와 test 이름은 구현 계획에서 기존 Stage 3 테스트의 정밀도 관례와 로컬 Automation 선언을 확인해 고정한다. 새로운 합격 margin은 만들지 않는다.

### latent actual-world 테스트

latent test는 11개 case마다 fresh MainLevel, 실제 Water, 실제 Stage 3 UShipMovement와 sweep을 사용한다. 합격 조건은 다음과 같다.

- 완료 case 11
- Success 11
- Collision 0
- Timeout 0
- setup error 0
- runtime calculation error 0
- 모든 min wall distance > 0

기존 Stage 3 Automation test 12개는 이름과 검증 의도를 보존하고 Stage 4 test와 함께 회귀 검증한다.

## 수동 PIE 확인

- Play 시작 뒤 추가 입력 없이 course와 ship이 생성되고 자율주행이 시작된다.
- 일반 Play마다 새 seed와 s가 로그에 남는다.
- s가 0 이상이면 negative Y 쪽의 짧은 끝, s가 0 미만이면 positive Y 쪽의 짧은 끝을 지난다.
- green path, yellow waypoint, cyan live lookahead, red actual wall이 실제 위치와 일치한다.
- heading error가 큰 구간에서 throttle이 줄고 steer가 포화된다.
- final segment에서 coast가 한 번 latch되고 이후 throttle이 0으로 유지된다.
- reverse thrust, teleport 또는 직접 위치 대입이 없다.
- goal XY 100 cm 이내이면서 abs speed 5 cm/s 이하일 때 Success가 한 번 기록된다.
- collision과 timeout을 Success로 기록하지 않는다.
- Output Log에 map load, spawn, mesh, Water, ensure 또는 access violation 오류가 없다.

## 확인한 UE 5.5.4 API

아래 경로는 설치 엔진 root 기준 상대 경로다.

### game option과 lifecycle

Engine/Source/Runtime/Engine/Classes/GameFramework/GameModeBase.h

ENGINE_API virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage);

헤더 주석은 InitGame이 PreInitializeComponents를 포함한 다른 actor 초기화보다 먼저 호출된다고 명시한다. Stage4Slide를 BeginPlay 전에 읽는 근거다.

Engine/Source/Runtime/Engine/Classes/Kismet/GameplayStatics.h

static ENGINE_API FString ParseOption( FString Options, const FString& Key );

static ENGINE_API bool HasOption( FString Options, const FString& InKey );

Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp에서 두 함수가 option pair를 순회하는 구현을 확인했다. ParseOption은 key가 없을 때와 key는 있지만 value가 empty일 때 모두 empty 문자열을 반환한다. HasOption만 두 경우를 구분하므로 false일 때만 random slide를 사용하고, true이면 ParseOption 결과를 별도로 검증한다.

### deferred spawn과 runtime actor

Engine/Source/Runtime/Engine/Classes/Engine/World.h

template<class T>
T* SpawnActorDeferred(
    UClass* Class,
    const FTransform& Transform,
    AActor* Owner = nullptr,
    APawn* Instigator = nullptr,
    ESpawnActorCollisionHandlingMethod CollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::Undefined,
    ESpawnActorScaleMethod TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot);

Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h

ENGINE_API void FinishSpawning(
    const FTransform& Transform,
    bool bIsDefaultTransform = false,
    const FComponentInstanceDataCache* InstanceDataCache = nullptr,
    ESpawnActorScaleMethod TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale);

Engine/Source/Runtime/Engine/Classes/Engine/TargetPoint.h

class ATargetPoint : public AActor

Engine/Source/Runtime/Engine/Classes/Engine/StaticMeshActor.h

UStaticMeshComponent* GetStaticMeshComponent() const;

Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h

ENGINE_API virtual bool SetStaticMesh(UStaticMesh* NewMesh);

### collision과 footprint

Engine/Source/Runtime/Engine/Classes/Components/PrimitiveComponent.h

ENGINE_API virtual void SetCollisionEnabled(ECollisionEnabled::Type NewType);

ENGINE_API virtual void SetCollisionObjectType(ECollisionChannel Channel);

ENGINE_API virtual void SetCollisionResponseToChannel(
    ECollisionChannel Channel,
    ECollisionResponse NewResponse);

ENGINE_API virtual void SetCollisionResponseToAllChannels(
    ECollisionResponse NewResponse);

Engine/Source/Runtime/Engine/Classes/Components/BoxComponent.h

ENGINE_API FVector GetUnscaledBoxExtent() const;

Engine/Source/Runtime/Engine/Classes/Components/StaticMeshComponent.h

TObjectPtr<UStaticMesh> GetStaticMesh() const;

Engine/Source/Runtime/Engine/Classes/Engine/StaticMesh.h

ENGINE_API FBoxSphereBounds GetBounds() const;

### Water

Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterSubsystem.h

static UWaterSubsystem* GetWaterSubsystem(const UWorld* InWorld);

TWeakObjectPtr<UWaterBodyComponent> GetOceanBodyComponent();

Engine/Plugins/Experimental/Water/Source/Runtime/Public/WaterBodyComponent.h

bool HasWaves() const;

virtual FWaterBodyQueryResult QueryWaterInfoClosestToWorldLocation(
    const FVector& InWorldLocation,
    EWaterBodyQueryFlags InQueryFlags,
    const TOptional<float>& InSplineInputKey = TOptional<float>()) const;

벽 reference surface query는 ComputeLocation만 요청하고 IncludeWaves를 넣지 않는다. 선박의 runtime 수면 추종은 기존 3단계 ComputeLocation, ComputeNormal, IncludeWaves 계약을 그대로 유지한다.

### random과 Automation

Engine/Source/Runtime/Core/Public/Math/RandomStream.h

FRandomStream(FName InName);

void Initialize(FName InName);

int32 GetInitialSeed() const;

FVector::FReal FRandRange(FVector::FReal InMin, FVector::FReal InMax) const;

로컬 구현은 NAME_None으로 초기화할 때 FPlatformTime::Cycles()를 initial seed로 사용한다.

Engine/Source/Runtime/Engine/Public/Tests/AutomationCommon.h

ENGINE_API bool AutomationOpenMap(
    const FString& MapName,
    bool bForceReload = false);

Engine/Source/Runtime/Engine/Private/Tests/AutomationCommon.cpp에서 game context는 Open <MapName> 전체를 실행하고 force reload를 지원한다. Editor/UnrealEd/Private/EditorEngine.cpp의 editor handler는 map load와 PIE 시작을 별도로 처리하므로 URL option runner는 구현 계획에서 game context로 고정한다.

### debug와 기존 이동

Engine/Source/Runtime/Engine/Public/DrawDebugHelpers.h

ENGINE_API void DrawDebugLine(
    const UWorld* InWorld,
    const FVector& LineStart,
    const FVector& LineEnd,
    const FColor& Color,
    bool bPersistentLines = false,
    float LifeTime = -1.f,
    uint8 DepthPriority = 0,
    float Thickness = 0.f);

ENGINE_API void DrawDebugPoint(
    const UWorld* InWorld,
    const FVector& Position,
    float Size,
    const FColor& PointColor,
    bool bPersistentLines = false,
    float LifeTime = -1.f,
    uint8 DepthPriority = 0);

ENGINE_API void DrawDebugBox(
    const UWorld* InWorld,
    const FVector& Center,
    const FVector& Extent,
    const FQuat& Rotation,
    const FColor& Color,
    bool bPersistentLines = false,
    float LifeTime = -1.f,
    uint8 DepthPriority = 0,
    float Thickness = 0.f);

현재 프로젝트 ShipAutonomySim/Source/ShipAutonomySim/Public/ShipMovement.h

void SetThrottle(float Value);

void SetSteer(float Value);

현재 프로젝트 ShipAutonomySim/Source/ShipAutonomySim/Private/ShipMovement.cpp는 다음 UE actor API를 bSweep true와 ETeleportType::None으로 호출한다.

bool SetActorLocationAndRotation(
    FVector NewLocation,
    const FQuat& NewRotation,
    bool bSweep,
    FHitResult* OutSweepHitResult,
    ETeleportType Teleport);

4단계는 이 호출을 교체하거나 우회하지 않는다.

## 구현 계획의 첫 사전 작업

현재 ShipAutonomySim/AGENTS.md, ShipAutonomySim/SETUP.md와 root README.md에는 골격 단계 또는 웹 뷰어 단독 상태를 설명해 현재 3단계 구현과 4단계 책임을 부정하는 문장이 남아 있다.

이번 설계 문서 작업에서는 세 파일을 수정하지 않는다. 후속 구현 계획의 첫 작업은 제품 코드 전에 다음을 수행해야 한다.

1. 세 문서의 현재 구현 부정 문장을 정확히 식별한다.
2. 3단계 완료 상태와 4단계 책임으로 최소 수정한다.
3. 문서 변경만 별도 검토하고 커밋한다.
4. clean gate를 다시 확인한 뒤 Stage 4 구현을 시작한다.

같은 첫 기술 검증에서 새 project interface의 이름, tick prerequisite 선언, game-context Automation 실행 명령과 부동소수점 test 허용오차를 로컬 UE 5.5.4 헤더 및 현재 코드에 맞춰 고정한다. 이 항목은 새 기능 승인이 아니라 이미 확정된 설계를 정확한 버전 API로 옮기기 위한 사전 검증이다.

## 채택하지 않은 대안

### PCG

단일 벽과 세 점으로 완전히 결정되는 코스에 PCG graph, asset과 seed 관리가 추가된다. 과제 범위를 늘리면서 경로 품질을 높이지 않으므로 제외한다.

### A*와 NavMesh

장애물 한 개의 짧은 쪽 끝이 닫힌 식으로 결정된다. 일반 탐색기를 넣으면 Water 위 navigation 설정과 추가 실패 경계가 생기므로 3점 polyline을 채택한다.

### waypoint 도착 후 제자리 방향 전환

3단계 선박은 speed 0에서 회전하지 않으며 관성이 크다. waypoint를 정확히 찍고 정지한 뒤 도는 방식은 모델과 맞지 않고 overshoot를 키우므로 전방 주시를 채택한다.

### 전체 polyline 최근접 투영

현재 위치에서 모든 segment를 비교하면 벽 근처에서 미래 segment로 progress가 점프하거나 overshoot 뒤 이전 segment로 되돌아갈 수 있다. active segment 전용 투영과 끝점 평면 전환을 채택한다.

### 고정 정지거리

현재 speed와 무관한 고정 거리는 저속에서 너무 일찍, 고속에서 너무 늦게 coast를 시작한다. 현재 speed와 3단계 drag를 적분한 dynamic distance를 채택한다.

### reverse braking

후진 추력으로 정지 오차를 보정하면 최종 구간에서 앞뒤 진동과 별도 제어 상태가 생긴다. 승인 범위는 coast latch와 throttle 0이므로 제외한다.

### CSV와 Saved 결과

Automation 결과 파일은 cleanup, Git 추적과 재현 경계를 늘린다. 11개 결과는 test memory와 로그에만 두고 검증 보고서에 옮긴다.

### 별도 test map 또는 가짜 Water

과제 합격은 실제 MainLevel과 실제 Water에서의 완주다. 순수 계산은 unit test로 분리하되 통합 검증은 실제 world를 사용한다.

### Niagara와 lateral slip

Niagara 물보라는 5단계 capture가 완료된 뒤의 선택 사항이다. lateral slip은 3단계 이동 모델 재설계다. 둘 다 4단계와 Automation에서 제외한다.

## 완료 기준

- CourseBuilder가 runtime start, end, actual wall과 정확히 3점 path를 만든다.
- 일반 Play는 새 seed와 s를 로그에 남기고 Automation은 BeginPlay 전 forced s를 사용한다.
- Navigator는 active segment progress, 300 cm lookahead, 확정 steer와 throttle 규칙을 사용한다.
- UShipMovement만 swept runtime transform을 변경한다.
- final remaining이 dynamic stopping distance + 25 cm 이하일 때 coast가 한 번 latch된다.
- Success는 goal XY 100 cm와 abs speed 5 cm/s를 함께 요구한다.
- terminal 우선순위는 Collision, Success, Timeout이고 결과는 한 번만 확정된다.
- 순수 테스트 네 영역과 actual-world 11 case가 설계 계약을 검증한다.
- 11 case는 Success 11, Collision 0, Timeout 0, setup error 0, runtime calculation error 0, 모든 min wall distance > 0을 만족해야 한다.
- 기존 Stage 3 자동화 테스트 12개를 보존한다.
- CSV, Saved 결과, capture, 웹 뷰어, PCG, Niagara와 lateral slip 변경이 없다.
