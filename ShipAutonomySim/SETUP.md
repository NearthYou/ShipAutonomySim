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
- Stage 3 수동 이동은 `AShipPawn`과 `UShipMovement`가 담당하고 Stage 4 자동 운항은 이번 구현에서 `ACourseBuilder`, `UShipNavigator`, `ASimGameMode`가 담당한다.
- 선박, 벽, 시작점, 끝점은 Stage 4 런타임 경로에서 생성하며 레벨에 직접 배치하지 않는다.
- 이미지 캡처와 관련 Blueprint는 이번 Stage 4 범위에서 만들지 않는다.

## 시각 확인

- Content Browser에 `/Game/Maps/MainLevel`이 보인다.
- World Outliner에 Water Body Ocean 한 개와 Water Zone 한 개가 보인다.
- 뷰포트의 View Mode를 `Unlit`으로 바꿨을 때 Ocean 표면 영역을 확인할 수 있다.
- Water Zone의 경계가 Ocean 및 향후 코스 영역을 포함한다.
- World Outliner에 ShipPawn, CourseBuilder, 벽, 시작점, 끝점이 없다.
- Maps & Modes의 세 설정이 `DefaultEngine.ini`와 일치한다.
