# 수상 자율주행 시뮬레이션 구현 보고서

## 1. 결과 요약

Unreal Engine 5.5.4 C++로 바다 위 선박의 관성 이동, 런타임 코스 생성, 벽 우회 자율주행, 전방 컬러와 깊이 캡처를 구현했다. 레벨을 열고 Play를 누르면 선박, 시작점, 끝점과 벽이 생성되고 별도 입력 없이 주행과 저장이 시작된다.

웹 뷰어는 프레임워크와 런타임 패키지 없이 TypeScript와 브라우저 ES 모듈만 사용한다. `manifest.json`과 개별 PNG를 읽는 기본 경로에 더해, 선택 가산점용 단일 파일 `sequence.siv`도 읽는다.

최종 실제 월드 자동 검증에서는 벽 위치 11개와 캡처 활성 3개를 합친 14개 조건이 모두 성공했다. 충돌, 시간 초과, 설정 오류, 계산 오류와 캡처 오류는 0건이었다.

## 2. 런타임 구조

| 클래스 | 책임 |
| --- | --- |
| `ASimGameMode` | 옵션 해석, 코스와 선박 생성, 자율주행과 캡처 시작, 종료 판정 |
| `ACourseBuilder` | Water 기준 높이 조회, 시작점과 끝점, 벽과 3점 경로 생성 |
| `AShipPawn` | 충돌체, 시각 mesh, Movement, Navigator, Capture와 관찰 카메라 소유 |
| `UShipMovement` | 속도와 yaw 적분, Water 정렬, sweep을 사용한 유일한 transform 변경 |
| `UShipNavigator` | 진행도, 전방 주시점, 조향과 추진 명령 계산 |
| `UShipCapture` | 100ms 주기 판정, 컬러와 깊이 PNG, manifest와 SIV 게시 |

Navigator는 위치를 직접 바꾸지 않는다. `SetThrottle`과 `SetSteer`만 호출하며, 실제 이동은 `UShipMovement` 한 곳에서 `bSweep=true`, `ETeleportType::None`으로 처리한다. 따라서 자율주행이 이동 모델을 우회하거나 순간이동하는 경로가 없다.

## 3. 선박 이동 모델

전후진 signed speed `v`는 선형 저항과 이차 저항을 함께 사용한다.

```text
dv/dt = A T - C1 v - C2 v |v|
```

`T`는 -1에서 1 사이 throttle이다. 조향은 속도에 비례해 커지며 속도 0에서는 yaw rate도 0이므로 제자리 회전하지 않는다.

```text
yawRate = MaxYawRate S clamp(|v| / TurnRefSpeed, 0, 1)
```

| 파라미터 | 값 |
| --- | ---: |
| 선형 저항 `C1` | 0.447501534 1/s |
| 이차 저항 `C2` | 0.000400390770 1/cm |
| 최대 추진 가속도 `A` | 105.5159376 cm/s² |
| 정지 임계속도 | 5 cm/s |
| 최대 yaw rate | 45.83662361 deg/s |
| 선회 기준 속도 | 200 cm/s |
| 최대 내부 스텝 | 1/120 s |
| tick당 최대 substep | 8회 |

순수 수치 테스트 결과는 다음과 같다.

| 목표 | 검증 결과 |
| --- | ---: |
| 최고속도 평형 | 200 cm/s |
| 정지에서 180 cm/s 도달 | 약 3.9917 s |
| 200 cm/s에서 타력 정지 | 약 399.9615 cm |
| 최고속도 최소 선회반경 | 약 250 cm |

throttle이 0이고 계산된 속도가 5cm/s 이하가 되면 0으로 고정한다. 이는 위치를 바꾸는 순간이동이 아니라 저속에서 저항식이 0에 점근해 영원히 남는 미세 속도를 끝내는 수치 안정화 규칙이다.

## 4. 벽 우회와 목적지 정지

코스 좌표에서 시작점은 `(0, 0)cm`, 끝점은 `(2000, 0)cm`, 벽 중심은 `(1000, s)cm`다. 벽은 100 x 1000 x 500cm이고 `s`는 매 실행 -500cm에서 500cm 사이에서 달라진다. 이 범위에서는 시작점과 끝점을 잇는 직선이 항상 벽에 막힌다.

벽의 중심선에 가까운 끝을 선택하고 바깥쪽 250cm에 waypoint를 만든다.

```text
s >= 0: waypointY = s - 750
s < 0:  waypointY = s + 750
```

경로는 `start, waypoint, end` 세 점이다. 현재 위치를 active segment에 투영한 진행도는 감소하지 않으며, 진행도보다 300cm 앞선 지점을 바라본다. heading 오차가 작을 때는 throttle 1, 60도 이상일 때는 0.35를 사용하고 그 사이는 선형 보간한다.

마지막 구간에서는 현재 속도를 같은 저항식으로 미리 적분해 정지거리를 계산한다. 남은 경로가 정지거리와 25cm 여유보다 짧아지면 coast를 한 번 latch하고 throttle 0을 유지한다. 성공은 목표에서 100cm 이내이며 속도가 5cm/s 이하일 때만 성립한다.

## 5. 컬러와 깊이 캡처

컬러와 깊이는 선박 전방의 같은 `CaptureMount`를 공유한다. 기본 위치는 선박 local `(110, 0, 50)cm`, FOV는 90도, 해상도는 512 x 512다.

- 컬러: `SCS_FinalColorLDR`, `PF_B8G8R8A8`, 고정 노출
- 깊이: `SCS_SceneDepth`, `PF_R32_FLOAT`, `RCM_MinMax` readback
- 출력: 같은 6자리 index의 `color_*.png`, `depth_*.png`
- 메타데이터: `manifest.json`
- 단일 파일: `sequence.siv`

frame 0은 캡처 시작 즉시 저장한다. 이후 `FPlatformTime::Seconds()`로 실제 경과를 재고 100ms 이상일 때 pair 하나를 저장한다. 긴 hitch에서 밀린 여러 프레임을 한 tick에 몰아 만들지 않으며 manifest에는 실제 `time_ms`를 남긴다.

두 PNG는 메모리 생성과 임시 파일 쓰기가 모두 성공한 뒤 최종 이름으로 옮긴다. 완전한 pair만 manifest에 들어가므로 한쪽만 기록된 프레임을 정상 데이터로 게시하지 않는다.

## 6. 깊이 정규화 근거

초기 5000cm 범위의 178프레임, 46,661,632픽셀을 분석했다. 0이 아닌 유효 픽셀 중 92.656%가 10m 이내, 96.857%가 20m 이내, 97.818%가 25m 이내였다. 코스 길이 20m에 5m 여유를 둔 2500cm를 최종 far로 선택했다.

```text
normalized = clamp(sceneDepthCm / 2500, 0, 1)
g8 = round((1 - normalized) x 255)
```

5000cm의 8비트 간격은 약 19.6cm이고 2500cm에서는 약 9.8cm다. 코스 주변 유효 픽셀의 97.8%를 유지하면서 가까운 장애물의 거리 구분을 두 배 촘촘하게 만든 선택이다.

가까운 값을 255로 저장한 이유는 회피 대상이 되는 가까운 물체를 밝게 강조하고, 웹 컬러맵에서도 큰 값을 따뜻한 색으로 바로 대응시키기 위해서다. 2500cm 이상과 invalid 값은 0이다.

최종 2500cm 설정으로 새로 저장한 216프레임에서는 전체 56,623,104픽셀 중 53.807%가 0이 아니었다. 유효 픽셀의 94.343%가 10m 이내, 98.968%가 20m 이내였다. 0에는 하늘과 far 초과 값이 함께 포함되므로 이 결과를 원거리 실제 깊이 분포로 과장하지 않는다.

## 7. 단일 바이너리 SIV

PNG와 manifest는 필수 제출 경로로 유지하고, 성공한 실행을 finalize할 때 파생 파일 `sequence.siv`를 추가한다.

```text
8 bytes  magic SIVPACK1
4 bytes  little-endian JSON header length
N bytes  UTF-8 JSON header
rest     concatenated PNG payloads
```

header에는 형식 버전, 원본 manifest JSON, 각 asset의 경로, media type, offset과 length가 들어간다. 브라우저는 magic, 버전, 연속 offset, 길이, 중복 경로, PNG signature와 manifest 참조를 검증한 뒤 Blob URL로 기존 플레이어에 연결한다.

PNG는 이미 lossless 압축된 형식이다. 실제 178프레임 세트에서 PNG를 다시 zlib으로 감쌌을 때 절감률은 파일별 0.282%, 전체 0.322%뿐이었다. 이 작은 절감을 위해 UE와 브라우저 양쪽에 추가 압축 상태와 실패 경로를 넣지 않고, 압축된 PNG byte를 한 파일에 패킹했다.

새 216프레임 세트의 결과는 다음과 같다.

| 항목 | 측정값 |
| --- | ---: |
| PNG asset | 432개 |
| PNG 합계 | 54,343,028 bytes |
| `sequence.siv` | 54,400,944 bytes |
| header와 index overhead | 57,916 bytes, 0.1066% |

동일 TypeScript parser를 Node.js에서 432개 asset, warm-up 후 3 pass로 세 번 측정했다. compressed byte 복사만 포함하고 PNG decode와 canvas render는 포함하지 않는다.

| 접근 방식 | 이미지당 중앙값 |
| --- | ---: |
| 순차 | 0.04695 ms |
| 고정 seed 무작위 | 0.04793 ms |

SIV는 파일 수와 HTTP 요청 수를 줄이고 index 기반 임의 접근을 제공하는 선택 경로다. 현재 구현은 파일 전체를 메모리에 올리므로 매우 긴 시퀀스에서는 range request나 streaming index가 후속 개선 대상이다.

## 8. 실제 월드 검증

### 캡처 비활성 11개

| 벽 slide (cm) | 성공 | 경과 (s) | 최소 벽 거리 (cm) |
| ---: | --- | ---: | ---: |
| -500 | 예 | 17.887 | 161.771 |
| -400 | 예 | 18.101 | 149.968 |
| -300 | 예 | 18.481 | 138.403 |
| -200 | 예 | 18.982 | 127.538 |
| -100 | 예 | 19.558 | 117.370 |
| 0 | 예 | 20.280 | 113.619 |
| 100 | 예 | 19.599 | 120.589 |
| 200 | 예 | 18.992 | 128.538 |
| 300 | 예 | 18.533 | 139.072 |
| 400 | 예 | 18.143 | 151.294 |
| 500 | 예 | 17.908 | 162.290 |

기존 -500 첫 행의 493.508cm는 궤적 이상이 아니라 자동화 관측 누락이었다. 테스트 명령으로 먼저 열린 월드가 latent command가 붙기 전에 주행을 시작해 최근접 구간을 놓쳤다. 첫 조건도 같은 옵션으로 fresh world를 다시 열어 관측하도록 고친 뒤 세 독립 실행에서 161.850cm, 161.771cm, 161.824cm가 나왔다. 최대 편차는 0.079cm이며 capture-on 162.668cm와도 일치한다. 제품의 항법 수식과 궤적은 바꾸지 않았다.

### 캡처 활성 3개

| 벽 slide (cm) | 성공 | 경과 (s) | 최소 벽 거리 (cm) | 프레임 | 마지막 시각 (ms) |
| ---: | --- | ---: | ---: | ---: | ---: |
| -500 | 예 | 19.562 | 162.668 | 187 | 19342 |
| 0 | 예 | 22.679 | 112.489 | 214 | 22203 |
| 500 | 예 | 19.936 | 163.385 | 188 | 19410 |

### 캡처 비용

| slide (cm) | capture-off (s) | capture-on (s) | 증가 (s) | 증가율 |
| ---: | ---: | ---: | ---: | ---: |
| -500 | 17.887 | 19.562 | 1.675 | 9.36% |
| 0 | 20.280 | 22.679 | 2.399 | 11.83% |
| 500 | 17.908 | 19.936 | 2.028 | 11.32% |

589개 pair transaction의 중앙값은 73.662ms, p95는 81.529ms였다. GPU readback, PNG 압축과 파일 쓰기가 game thread의 동기 경로에 있어 주행 시간이 약 9%에서 12% 늘어난다. 캡처를 켜도 3개 조건 모두 충돌 없이 성공했지만, 성능 비용이 없다고 주장하지 않는다.

비동기 GPU readback과 background writer는 stall을 줄일 수 있다. 다만 GPU resource 수명, queue backpressure, 종료 시 drain과 incomplete pair 복구가 새로 필요하다. 과제 규모에서는 완전한 pair 게시와 오류 원인을 먼저 보장하고 이 비용을 측정 가능한 한계로 남겼다.

## 9. 웹 뷰어

웹 뷰어는 manifest 모드와 SIV 모드를 같은 재생 상태에 연결한다.

- 컬러와 깊이의 같은 index와 실제 `time_ms` 표시
- 재생, 일시정지, 처음, 이전, 다음과 frame slider
- 0.5x, 1x, 2x 속도
- 깊이 원본과 가까울수록 따뜻한 컬러맵 전환
- 데이터 출처, 프레임 수, 간격과 깊이 범위 요약
- SIV 순차와 고정 seed 무작위 접근 benchmark
- 작은 화면에서 한 열로 바뀌는 반응형 배치

과제 저장 대상은 선박 전방 컬러와 깊이이므로 3인칭 영상은 추가하지 않았다. PIE의 spring arm 카메라는 주행을 관찰하기 위한 별도 장치다. 캡처 계약에 3인칭 스트림을 추가하면 저장량과 GPU readback 비용이 늘고 필수 결과의 의미가 흐려진다.

## 10. 빌드와 실행

### Unreal Engine

```powershell
$EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.5'
$Project = (Resolve-Path 'ShipAutonomySim\ShipAutonomySim.uproject').Path
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') `
    ShipAutonomySimEditor Win64 Development `
    "-Project=$Project" -WaitMutex
```

빌드 후 UE 5.5.4에서 `/Game/Maps/MainLevel`을 열고 Play를 누른다. 수동 입력 없이 주행과 캡처가 시작된다. 영구 배치된 Directional Light, Sky Light와 Sky Atmosphere를 사용하므로 `viewmode unlit`을 입력할 필요가 없다.

성공한 캡처는 `ShipAutonomySim/Saved/ShipCaptures/<run>/`에 저장된다. `Saved`는 제출물에서 제외한다.

### 웹

```powershell
npm ci
npm run build
python -m http.server 8000
```

개별 PNG 모드는 선택한 run의 manifest와 PNG를 README 절차대로 루트에 복사한 뒤 `http://localhost:8000`에서 연다. SIV는 복사하지 않고도 같은 서버의 상대 경로를 query에 줄 수 있다.

```text
http://localhost:8000/?bundle=ShipAutonomySim/Saved/ShipCaptures/<run>/sequence.siv
```

## 11. 의도적으로 제외한 범위

- PCG: 벽 하나와 3점 경로에는 graph와 asset 관리가 이득보다 크다.
- Niagara 물보라: 필수 항법과 캡처 검증을 늘리지 않으면서 시각 효과만 추가하므로 제외했다.
- 횡미끄러짐: 현재 모델은 전방 속도와 yaw만 사용한다. 실제 선체 유체역학 모델은 아니다.
- 3인칭 저장 영상: 과제의 전방 컬러와 깊이 pair에 집중했다.
- 비동기 캡처: 측정된 stall을 줄일 수 있지만 제출 직전 오류 경계를 크게 늘려 제외했다.
- 추가 zlib: 이미 압축된 PNG에서 약 0.3%만 줄어 구현 복잡성 대비 실익이 없었다.

시각 완성도는 필수 결과를 가리지 않는 범위에서 영구 환경 조명과 웹 정보 구조를 개선했다. 엔진 수정, 외부 asset, 외부 C++ 라이브러리와 런타임 웹 패키지는 사용하지 않았다.
