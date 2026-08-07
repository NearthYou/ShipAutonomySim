# ShipAutonomySim 에디터 설정

이 프로젝트는 UE 5.5용 C++ Blank 프로젝트이며 Starter Content를 포함하지 않는다. 현재 단계에는 `/Game/Maps/MainLevel` 에셋이 없으므로 아래 순서로 한 번만 생성한다.

## 에디터 작업 순서

1. Unreal Engine 5.5.4로 `ShipAutonomySim.uproject`를 연다. 모듈 빌드를 묻는 창이 나오면 빌드를 진행한다.
2. 시작 맵을 찾지 못했다는 메시지가 나오면 현재 단계에서는 정상이다. 임시 빈 레벨로 계속 연다.
3. Content Browser에서 `Content/Maps` 폴더를 만든다.
4. `File > New Level > Empty Level`을 선택한다.
5. `File > Save Current Level As`에서 레벨을 `Content/Maps/MainLevel`로 저장한다. 최종 패키지 경로는 `/Game/Maps/MainLevel`이어야 한다.
6. `Edit > Plugins`에서 Water가 활성화되어 있는지만 확인한다. 다른 플러그인은 추가로 활성화하지 않는다. 재시작을 요구하면 저장 후 에디터를 재시작한다.
7. Place Actors 패널에서 `Water Body Ocean`을 검색해 한 개 배치한다.
8. World Outliner에서 `WaterZone`이 한 개 자동 생성됐는지 확인한다. 없을 때만 Place Actors 패널에서 `Water Zone`을 한 개 배치한다. 중복 Water Zone은 만들지 않는다.
9. Water Zone을 선택하고 Zone Extent가 현재 Ocean과 향후 코스 영역을 모두 덮는지 확인한다. 이번 단계에서는 수치를 고정하지 않는다.
10. `File > Save All`을 실행한다.
11. `Edit > Project Settings > Maps & Modes`에서 Editor Startup Map과 Game Default Map이 `MainLevel`, Default GameMode가 `SimGameMode`인지 확인한다.

## 배치 경계

- 레벨에 직접 배치하는 프로젝트 전용 actor는 Water Body Ocean과 필요한 Water Zone뿐이다.
- ShipPawn, CourseBuilder, 벽, 시작점, 끝점은 배치하지 않는다.
- 선박, 벽, 시작점, 끝점의 생성 책임은 이후 단계의 `ASimGameMode::BeginPlay`에 둔다.
- 현재 단계에서는 스폰, 이동, 항법, 캡처 로직이나 관련 Blueprint를 만들지 않는다.

## 시각 확인

- Content Browser에 `/Game/Maps/MainLevel`이 보인다.
- World Outliner에 Water Body Ocean 한 개와 Water Zone 한 개가 보인다.
- 뷰포트의 View Mode를 `Unlit`으로 바꿨을 때 Ocean 표면 영역을 확인할 수 있다.
- Water Zone의 경계가 Ocean 및 향후 코스 영역을 포함한다.
- World Outliner에 ShipPawn, CourseBuilder, 벽, 시작점, 끝점이 없다.
- Maps & Modes의 세 설정이 `DefaultEngine.ini`와 일치한다.
