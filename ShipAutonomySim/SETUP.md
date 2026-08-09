# ShipAutonomySim 에디터 설정

이 프로젝트는 UE 5.5용 C++ Blank 프로젝트이며 Starter Content를 포함하지 않는다. `/Game/Maps/MainLevel`과 Water 배치는 이미 준비되어 있으므로 기존 레벨을 그대로 사용한다.

## 에디터 작업 순서

1. Unreal Engine 5.5.4로 `ShipAutonomySim.uproject`를 연다. 모듈 빌드를 묻는 창이 나오면 빌드를 진행한다.
2. `/Game/Maps/MainLevel`이 열렸는지 확인하고 새 레벨을 만들거나 다른 이름으로 저장하지 않는다.
3. `Edit > Plugins`에서 Water가 활성화되어 있는지만 확인한다. 다른 플러그인은 추가로 활성화하지 않는다.
4. World Outliner에서 기존 Water Body Ocean 한 개와 Water Zone 한 개를 확인하고 중복 배치하지 않는다.
5. Water Zone의 Zone Extent가 Ocean과 코스 영역을 모두 덮는지 확인한다.
6. `Edit > Project Settings > Maps & Modes`에서 Editor Startup Map과 Game Default Map이 `MainLevel`, Default GameMode가 `SimGameMode`인지 확인한다.

## 배치 경계

- 레벨에 직접 배치하는 프로젝트 전용 actor는 Water Body Ocean과 필요한 Water Zone뿐이다.
- ShipPawn, CourseBuilder, 벽, 시작점, 끝점은 배치하지 않는다.
- 선박 이동 경계는 `AShipPawn`과 `UShipMovement`가 담당하고 Stage 4 자동 운항은 `ACourseBuilder`, `UShipNavigator`, `ASimGameMode`가 담당한다.
- 선박, 벽, 시작점, 끝점은 Stage 4 런타임 경로에서 생성하며 레벨에 직접 배치하지 않는다.
- Stage 5 캡처는 `AShipPawn`의 공통 mount, 컬러와 깊이 SceneCapture, `UShipCapture`가 담당하며 별도 Blueprint나 레벨 배치가 필요하지 않다.

## 자동 실행과 캡처 결과

1. `/Game/Maps/MainLevel`에서 Play를 누른다. 키보드나 마우스 입력 없이 코스, 선박, 자율주행과 캡처가 차례로 시작된다.
2. Output Log에서 run당 `Stage5CaptureStarted`가 한 번 나타나는지 확인한다. 캡처 중에는 shared index와 실제 `time_ms`를 가진 `Stage5CapturePair`가 기록된다.
3. 주행이 Success, Collision 또는 Timeout에 도달하면 `Stage4Terminal` 뒤 `Stage5CaptureFinalized`가 한 번 나타나는지 확인한다.
4. 결과는 `Saved/ShipCaptures/YYYYMMDDTHHMMSSmmmZ_GUIDDIGITS`에 저장된다. 각 run에는 같은 index의 `color_*.png`, `depth_*.png`, terminal 뒤 게시된 `manifest.json`, 이 파일들을 한 번에 묶은 `sequence.siv`가 있다.

Success terminal의 manifest `result`는 `success`이고 Collision과 Timeout은 `fail`이다. 캡처가 시작된 뒤 terminal 전에 PIE를 중지하면 EndPlay가 run을 한 번만 마감하며 `result`는 `fail`이다. 캡처 시작 전 setup failure에는 빈 dataset이나 manifest를 만들지 않는다.

오류가 발생하면 최초 캡처 오류는 `Stage5CaptureFailure`, 최초 주행 계산 오류는 `Stage4RuntimeCalculationError`로 한 번만 기록된다. 이후 terminal과 manifest 결과를 확인하고 같은 오류가 반복 기록되지 않았는지 함께 확인한다.

## 시각 확인

- Content Browser에 `/Game/Maps/MainLevel`이 보인다.
- World Outliner에 Water Body Ocean 한 개와 Water Zone 한 개가 보인다.
- 뷰포트의 View Mode를 `Unlit`으로 바꿨을 때 Ocean 표면 영역을 확인할 수 있다.
- Water Zone의 경계가 Ocean 및 향후 코스 영역을 포함한다.
- World Outliner에 ShipPawn, CourseBuilder, 벽, 시작점, 끝점이 없다.
- Maps & Modes의 세 설정이 `DefaultEngine.ini`와 일치한다.
- Play 뒤 선박과 코스가 자동 생성되고 별도 입력 없이 자율주행과 캡처가 시작된다.
- 선택한 run의 첫, 중간, 마지막 컬러와 깊이 pair가 같은 방향과 장면을 공유한다.
- 컬러 연속 프레임의 노출이 고정되어 있고, 깊이 PNG는 가까운 물체가 밝고 2500cm 밖과 하늘이 어둡다.
- `manifest.json`의 frame 수, 6자리 연속 index, pair 파일 수와 `time_ms`가 실제 파일과 일치하고 `sequence.siv`가 함께 생성된다.
- 웹 재생은 저장소 루트 `README.md`의 선택 run 복사 절차로 확인하고, 확인용 복사본만 정리한다.
