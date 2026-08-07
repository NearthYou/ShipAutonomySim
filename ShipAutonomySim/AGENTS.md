# ShipAutonomySim 작업 규칙

## 고정 환경

- 프로젝트는 Unreal Engine 5.5 계열 C++ 프로젝트로 유지한다.
- Starter Content, Fab 또는 Marketplace 플러그인과 에셋 팩, 외부 C++ 라이브러리를 추가하지 않는다.
- 프로젝트에서 직접 활성화하는 엔진 플러그인은 Water뿐이다.
- Unreal API와 모듈명은 사용하는 엔진 버전의 로컬 헤더, `Build.cs`, `uplugin`에서 확인한다.
- 제품 파일에 개발자 PC의 절대 경로를 넣지 않는다.

## 현재 단계 경계

- 현재 승인 범위는 컴파일 가능한 프로젝트와 클래스 골격까지다.
- 스폰, 선박 이동, 항법, 캡처, 코스 생성 동작은 이후 단계의 명시적 요청 전까지 구현하지 않는다.
- 사람이 레벨에 배치하는 프로젝트 전용 actor는 Water Body Ocean과 필요한 Water Zone뿐이다.
- 선박, 벽, 시작점, 끝점의 런타임 생성 책임은 이후 단계의 `ASimGameMode::BeginPlay`에 둔다.
- 기존 웹 뷰어 파일은 Unreal 작업 범위에 포함하지 않는다.

## 코드와 의존성

- UObject 파생 포인터 멤버에는 `UPROPERTY()`를 붙인다.
- `.generated.h`는 해당 헤더의 include 목록 마지막에 둔다.
- 새 모듈을 사용하는 변경은 `ShipAutonomySim.Build.cs`에도 반영한다.
- 공개 헤더에 노출되지 않는 모듈은 private 의존성으로 둔다.
- 튜닝 값을 추가해야 하는 이후 단계에서는 `UPROPERTY(EditAnywhere, Category=...)`로 노출한다.
- 빌드 생성물은 Git에 추가하지 않는다.
