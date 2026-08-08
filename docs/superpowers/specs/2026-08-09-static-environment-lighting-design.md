# 영구 환경 조명 설계

## 문서 목적과 결정 상태

이 문서는 `/Game/Maps/MainLevel`에 영구 환경 조명을 추가하기 위한 확정 설계다. 구현 단계에서 저장할 actor, 기존 런타임 책임과의 경계, 향후 Stage 5 캡처 계약과의 분리, 실패 및 검증 기준을 정의한다.

현재 완료된 범위는 Stage 4 코스 생성과 자율주행이다. 이 문서는 조명 구현, MainLevel 저장, 빌드, 에디터 검증 또는 Stage 5 캡처 구현이 완료되었다는 증거가 아니다. MainLevel 바이너리 변경은 별도 구현 단계에서만 수행한다.

## 배경과 문제 원인

현재 MainLevel은 Water 환경과 Stage 4 런타임 actor를 사용하지만, Lit view가 장면을 읽을 수 있도록 보장하는 영구 환경 조명 구성이 없다. 그 결과 코스와 선박 기능이 정상이어도 Lit view에서는 장면 확인이 어렵고, 사람이 View Mode를 Unlit으로 바꾸는 조작에 의존하게 된다. 원인은 항법, 이동, 충돌 또는 Water query가 아니라 레벨에 저장된 환경 조명의 부재다.

Selected Viewport PIE 수동 확인 네 번은 Stage 4 기능과 조명 문제를 분리하는 근거다.

| run | random slide | terminal | elapsed | Water state | terminal hit |
| --- | ---: | --- | ---: | --- | --- |
| 1 | -51.075 | Success | 19.892초 | ValidWaves | actor=None, component=None |
| 2 | 169.393 | Success | 19.111초 | ValidWaves | actor=None, component=None |
| 3 | 344.643 | Success | 18.259초 | ValidWaves | actor=None, component=None |
| 4 | 380.334 | Success | 18.130초 | ValidWaves | actor=None, component=None |

네 run 모두 setup failure, runtime calculation error, Collision, Timeout 없이 끝났다. 처음 세 run에는 `Set new viewmode: Unlit` 로그가 있어 Lit 가시성 문제와 수동 Unlit 의존을 재현했다. bounded substep drop warning은 있었지만 네 run이 모두 Success였으므로 이 조명 설계에서는 Stage 4 기능 실패로 분류하지 않는다. 수동 확인에 사용한 원본 로그 파일은 설계 근거일 뿐 저장소 산출물이 아니며 복사하거나 커밋하지 않는다.

## 목표

- MainLevel에 `DirectionalLight`, `SkyLight`, `SkyAtmosphere` 세 actor를 영구 저장한다.
- 맑고 중립적인 낮 장면을 만들고 시간 변화, 애니메이션 또는 런타임 조명 제어를 두지 않는다.
- UE 5.5.4의 Lit view에서 Play 전과 Play 중 장면을 확인할 수 있게 한다.
- 장면 확인에 `viewmode unlit` 콘솔 입력이나 View Mode 수동 전환이 필요 없게 한다.
- 기존 Water actor와 Stage 4 런타임 생성 및 자율주행 책임을 보존한다.
- 컬러 캡처에는 조명 결과가 반영되고 깊이 캡처에는 조명이 영향을 주지 않는 Stage 5 경계를 고정한다.

## 비목표

- `PostProcessVolume`, Exponential Height Fog를 포함한 안개, 구름, 렌즈 효과를 추가하지 않는다.
- 외부 cubemap, Starter Content, Fab, Marketplace 또는 다른 외부 에셋을 추가하지 않는다.
- 세 영구 환경 actor 외에 새 환경 기능, Blueprint, Sequencer 또는 시간대 시스템을 추가하지 않는다.
- 조명 수치의 별도 성능 목표나 새로운 게임플레이 튜닝 계약을 만들지 않는다.
- 선박 이동 모델, Navigator 판단, 코스 기하, 충돌 응답 또는 Water query를 변경하지 않는다.
- `UShipCapture`와 SceneCapture, 파일 writer, manifest 또는 웹 뷰어를 구현하지 않는다.
- Stage 5 컬러 노출, 깊이 인코딩, PNG 저장 또는 100ms 스케줄러를 현재 조명 작업에서 구현하지 않는다.

## 과제 범위 적합성과 배치 규칙 수정

세 actor는 UE 5.5.4 엔진 내장 환경 actor이며 외부 에셋이나 새 플러그인이 아니다. 환경을 사람이 한 번 구성해 MainLevel에 저장하는 과제 범위에 맞고, Play마다 달라지는 코스와 선박은 기존 Stage 4 런타임 책임으로 남는다.

기존의 맵에는 Water 관련 actor만 사람이 배치한다는 문구는 조명 actor의 영구 저장을 설명하지 못한다. 이 설계는 그 문구를 다음 배치 규칙으로 정확히 대체한다. 이번 문서 커밋에서는 `AGENTS.md`와 `SETUP.md` 자체를 수정하지 않는다.

> MainLevel에는 기존 Water Body Ocean과 필요한 Water Zone을 보존하고, 영구 환경 조명 actor로 DirectionalLight, SkyLight, SkyAtmosphere만 저장한다. CourseBuilder와 선박, 벽, 시작점, 도착점은 ASimGameMode와 ACourseBuilder가 런타임에 생성하며 레벨에 직접 배치하지 않는다.

이 구분에서 Water Body Ocean과 Water Zone은 기존 영구 Water 기반이고, 세 조명 actor는 새 영구 환경 범위다. CourseBuilder, 선박, 벽, 시작점, 도착점은 Stage 4 런타임 범위다. 조명 actor를 런타임 코스 actor로 취급하거나 코스 actor를 MainLevel에 미리 저장하지 않는다.

## 영구 actor 구성과 책임

| actor | 책임 | 책임 밖 |
| --- | --- | --- |
| `DirectionalLight` | 맑은 낮의 주 방향광을 제공해 선박, 벽, Water 표면과 코스 영역의 형태를 Lit view에서 읽을 수 있게 한다. | 시간 변화, 태양 애니메이션, 항법 방향 결정, 물리 또는 충돌 변경 |
| `SkyLight` | 하늘에서 오는 간접 환경광을 제공해 직접광이 닿지 않는 면도 Lit view에서 식별할 수 있게 한다. | 외부 cubemap, 컬러 캡처 노출 고정, 깊이값 보정 |
| `SkyAtmosphere` | 맑고 중립적인 낮의 하늘 배경과 엔진 내장 대기 산란을 제공한다. | 별도 안개, 구름, 날씨, 렌즈 효과 또는 시간대 시스템 |

세 actor에는 공통으로 다음 원칙을 적용한다.

- MainLevel이 영구 소유하며 `ASimGameMode`, `ACourseBuilder` 또는 다른 C++ 코드가 생성하거나 제거하지 않는다.
- 시간에 따라 움직이거나 밝기와 색을 애니메이션하지 않는다.
- engine built-in 기능만 사용하고 외부 texture, material, cubemap을 참조하지 않는다.
- 선박, 벽, 시작점, 도착점의 spawn 순서와 transform 소유권을 바꾸지 않는다.
- collision primitive, collision channel, Water Body, Water Zone, Water query flag와 query 결과를 바꾸지 않는다.

## Stage 4와 Stage 5 책임 경계

현재 Stage 4는 `ASimGameMode`와 `ACourseBuilder`가 CourseBuilder, 선박, 벽, 시작점, 도착점을 런타임에 준비하고 `UShipNavigator`와 `UShipMovement`가 주행을 수행하는 범위다. 현재 `UShipCapture`는 캡처 동작을 구현하지 않은 골격이며, 이 조명 설계는 Stage 4가 캡처까지 완료했다고 간주하지 않는다.

Stage 5는 컬러와 깊이 SceneCapture 및 파일 출력을 구현하는 후속 범위다. Stage 5에서 컬러 SceneCapture의 노출을 명시적으로 고정한다. 현재 환경 조명 구현은 viewport 가시성을 제공하지만, 컬러 데이터셋의 고정 노출까지 대신하지 않는다.

## 컬러, 깊이, PNG 압축과 100ms 계약 분리

| 계약 | 조명과의 관계 | 변경 책임 |
| --- | --- | --- |
| 컬러 캡처 | 컬러 픽셀이 `DirectionalLight`, `SkyLight`, `SkyAtmosphere`의 영향을 받는 것이 의도다. | Stage 5에서 컬러 SceneCapture와 고정 노출을 구현한다. 현재 조명 작업은 캡처 코드를 만들지 않는다. |
| 깊이 캡처 | 별도 SceneDepth의 실수 거리값을 사용하므로 조명 색, 밝기와 그림자에 독립한다. | Stage 5에서 0..5000cm로 clip한 뒤 8비트 grayscale PNG로 저장한다. 하늘 또는 무한 깊이는 clipping 결과의 최대값으로 처리한다. |
| PNG 압축 | 파일 인코딩과 크기를 다루며 조명이나 거리 측정 의미를 바꾸지 않는다. 손실성 화질 조정으로 컬러나 깊이 의미를 보정하지 않는다. | Stage 5 파일 출력 책임이다. PNG 형식, 해상도와 프레임 이름 계약은 이 조명 작업에서 변경하지 않는다. |
| 캡처 간격 | 조명 actor의 tick이나 시간 변화와 무관하다. | Stage 5의 100ms 실제 시간 간격 계약을 그대로 유지하며 이 조명 작업에서 스케줄링을 변경하지 않는다. |

깊이 결과가 어둡거나 밝아 보인다는 이유로 환경 조명을 조정해서는 안 된다. 깊이는 SceneDepth 거리 계약으로 검증하고, 컬러는 Stage 5 고정 노출이 적용된 별도 캡처 경로로 검증한다.

## 실패 처리와 롤백 원칙

- Lit view 가시성을 얻기 위해 Unlit 전환, PostProcessVolume, 안개, 구름, 렌즈 효과 또는 외부 cubemap이 필요하면 설계 불일치로 처리한다.
- 세 actor 중 하나라도 런타임 생성에 의존하거나 시간에 따라 변하면 설계 불일치로 처리한다.
- 구현 단계에서 Water query 상태, Stage 4 terminal 결과, 충돌 또는 항법 동작이 달라지면 조명과 무관한 회귀로 치부하지 않고 No-Go로 처리한다.
- `WaterInfo` mesh, Ocean spline, WaterZone과 기존 map actor의 삭제, 재생성, 이동 또는 설정 변경이 발견되면 MainLevel 저장을 승인하지 않는다.
- MainLevel 이외의 Config, Source, 문서 또는 다른 asset이 변경되면 구현 범위를 벗어난 것으로 처리한다. 에디터가 만든 Config 변경도 함께 커밋하지 않는다.
- 구현 전 MainLevel의 식별 가능한 hash와 파일 상태를 기록하고, 저장 후에는 의도한 세 actor 추가 외 기존 map 상태가 보존됐는지 확인한다.
- 검증 실패 시 롤백 단위는 조명 actor를 추가한 MainLevel 변경 하나다. 기존 Water와 코스 actor를 재구성하지 않고, 검증된 구현 전 MainLevel을 기준으로 조명 변경만 되돌린다.
- Stage 5 컬러 노출이나 깊이 파일 문제가 발견돼도 현재 조명 값을 임시 보정 수단으로 사용하지 않는다. 해당 문제는 Stage 5 캡처 책임에서 해결한다.

## 구현 단계 검증 기준

### 시작 및 범위 gate

- 구현을 시작하기 전에 승인된 branch, HEAD와 clean 상태를 확인한다.
- 저장 전후 `ShipAutonomySim/Content/Maps/MainLevel.umap`, Config 파일 집합과 Git 상태를 기록한다.
- 구현 커밋의 제품 변경 파일은 `ShipAutonomySim/Content/Maps/MainLevel.umap` 하나여야 한다.

### UE 5.5.4 Lit 가시성

- UE 5.5.4에서 MainLevel을 Lit view로 열었을 때 Play 전부터 하늘, Water 표면과 코스 영역을 식별할 수 있어야 한다.
- Selected Viewport PIE를 시작한 뒤 선박, 벽, 시작점과 도착점이 Lit view에서 식별 가능해야 한다.
- Play 전후에 `viewmode unlit` 콘솔 입력이나 View Mode 수동 전환이 없어야 한다.
- 검증 로그에 조명 확인을 위해 발생한 `Set new viewmode: Unlit`가 없어야 한다.
- 시간 변화나 조명 애니메이션 없이 Play 전과 Play 중 환경 방향과 인상이 유지돼야 한다.

### Water와 Stage 4 회귀

- 기존 Water Body Ocean, Water Zone, WaterInfo mesh와 Ocean spline을 보존한다.
- 기존 Stage 4 unit 및 actual-world 검증 계약을 조명 구현 전과 같은 조건으로 통과해야 한다.
- Selected Viewport PIE에서 Water state가 기존 정상 분기인 `ValidWaves`를 유지해야 한다.
- Stage 4가 Success에 도달하고 setup failure, runtime calculation error, Collision, Timeout이 새로 발생하지 않아야 한다.
- bounded substep drop warning은 기존 경고만으로 실패 처리하지 않는다. terminal 실패, 비정상 위치 또는 다른 회귀와 함께 나타나는지는 별도로 확인한다.

### MainLevel no-write와 map 검증

- 저장된 MainLevel을 `-nowrite`로 다시 로드해 검증 실행 자체가 map이나 Config를 바꾸지 않게 한다.
- 설치된 UE 5.5.4에서 MapCheck를 실행할 수 있으면 error 0, warning 0을 확인한다.
- MapCheck commandlet을 사용할 수 없으면 `/Game/Maps/MainLevel` no-write load 또는 `-game` load를 동등 검증으로 사용한다. process exit 0, map load 완료, LoadErrors와 Fatal 없음, 정상 종료를 확인한다.
- no-write 검증 전후 MainLevel hash, Config 파일 집합과 Git 상태가 같아야 한다.

### 최종 map, Config와 Git 범위

- World Outliner에서 새 영구 환경 actor가 `DirectionalLight`, `SkyLight`, `SkyAtmosphere` 각 한 개뿐인지 확인한다.
- `PostProcessVolume`, 안개, 구름, 렌즈 효과 actor와 외부 asset 참조가 추가되지 않았는지 확인한다.
- Config와 Source에는 차이가 없어야 한다.
- 구현 커밋에는 MainLevel 한 파일만 stage하고, staged name-status와 commit name-status를 다시 확인한다.
- 구현 커밋 후 작업 트리는 clean이어야 한다.

## 사람이 확인할 최소 시각 체크리스트

- Play 전 View Mode가 Lit인 상태에서 맑고 중립적인 낮의 하늘과 Water 표면이 보인다.
- Play 시작을 위해 View Mode 변경이나 콘솔 입력을 하지 않는다.
- Play 중 선박, 실제 벽, 시작점, 도착점과 이동 경로 주변을 Lit view에서 구분할 수 있다.
- Play 전과 Play 중 하늘과 조명 방향이 시간에 따라 변하지 않는다.
- Water 표면, Ocean spline과 WaterZone의 기존 배치가 시각적으로 유지된다.
- 추가된 환경 요소는 DirectionalLight, SkyLight, SkyAtmosphere뿐이다.
- Stage 4 terminal은 Success로 끝나고 조명 때문에 새 setup, runtime, Collision 또는 Timeout 실패가 생기지 않는다.
- 컬러 캡처 노출과 깊이 PNG 외관은 이 체크에서 합격 판정하지 않고 Stage 5 검증으로 남긴다.
