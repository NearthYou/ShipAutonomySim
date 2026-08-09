# ShipAutonomySim 작업 규칙

## 고정 환경

- 프로젝트는 Unreal Engine 5.5 계열 C++ 프로젝트로 유지한다.
- Starter Content, Fab 또는 Marketplace 플러그인과 에셋 팩, 외부 C++ 라이브러리를 추가하지 않는다.
- 프로젝트에서 직접 활성화하는 엔진 플러그인은 Water뿐이다.
- Unreal API와 모듈명은 사용하는 엔진 버전의 로컬 헤더, `Build.cs`, `uplugin`에서 확인한다.
- 제품 파일에 개발자 PC의 절대 경로를 넣지 않는다.

## 현재 단계 경계

- Stage 3 선박 이동은 현재 `UShipMovement`와 `AShipPawn`의 책임으로 구현되어 있다.
- Stage 4 코스 생성과 자율주행은 `ACourseBuilder`, `UShipNavigator`, `ASimGameMode`의 책임으로 구현되어 있다.
- Stage 5 이미지 캡처는 `AShipPawn`이 소유한 공통 mount, 두 SceneCapture와 `UShipCapture`의 책임이다.
- PCG와 웹 뷰어 제품 소스 변경은 Stage 5 범위에 포함하지 않는다. 연동 확인은 생성 dataset을 선택해 복사하는 방식으로만 수행한다.
- 사람이 레벨에 배치하는 프로젝트 전용 actor는 Water Body Ocean과 필요한 Water Zone뿐이다.
- 선박, 벽, 시작점, 끝점의 런타임 생성과 캡처 시작과 마감 orchestration은 `ASimGameMode`에 둔다.
- 기존 웹 뷰어 파일은 Unreal 작업 범위에 포함하지 않는다.

## 코드와 의존성

- UObject 파생 포인터 멤버에는 `UPROPERTY()`를 붙인다.
- `.generated.h`는 해당 헤더의 include 목록 마지막에 둔다.
- 새 모듈을 사용하는 변경은 `ShipAutonomySim.Build.cs`에도 반영한다.
- 공개 헤더에 노출되지 않는 모듈은 private 의존성으로 둔다.
- 튜닝 값을 추가해야 하는 이후 단계에서는 `UPROPERTY(EditAnywhere, Category=...)`로 노출한다.
- 빌드 생성물은 Git에 추가하지 않는다.

## Stage 5 캡처 규칙

- 컬러와 깊이는 같은 mount, transform, forward, 90도 FOV와 512×512 해상도를 공유한다. 자동 frame capture를 켜지 않고 명시적 `CaptureScene`만 사용한다.
- 컬러는 `SCS_FinalColorLDR`, 고정 수동 노출과 BGRA8 PNG를 사용한다. 깊이는 `SCS_SceneDepth`, `PF_R32_FLOAT`, `RCM_MinMax`, `ReadLinearColorPixels` 뒤 0cm에서 5000cm를 역선형 G8 PNG로 저장하며 near는 255, far와 invalid는 0이다.
- 시계는 `FPlatformTime::Seconds()`를 사용하고 frame 0은 0ms, 이후 목표 간격은 100ms다. hitch 뒤 catch-up pair를 만들지 않는다.
- color와 depth는 shared index transaction으로 임시 파일을 모두 쓴 뒤 pair를 게시한다. run은 project-relative `Saved/ShipCaptures/YYYYMMDDTHHMMSSmmmZ_GUIDDIGITS`에만 저장한다.
- terminal 또는 EndPlay finalize는 exactly once다. setup과 runtime capture failure는 최초 오류만 latch하고 임의의 새 terminal 결과를 만들지 않는다.
- 성능 영향이 커도 async readback, background writer, 해상도 축소나 PNG 외 형식은 별도 승인 없이 적용하지 않는다.

## API와 보호 경계

- Unreal API 시그니처와 enum은 설치된 UE 5.5.4의 로컬 헤더와 소스에서 확인한다. 특히 SceneCapture target과 source, `CaptureScene`, render-target readback, ImageWrapper `CompressImage`, JSON writer, platform clock과 file move API를 추측하지 않는다.
- UObject가 소유하는 mount, capture component, render target 포인터는 `UPROPERTY()`로 추적한다. editor 조정값은 `UPROPERTY(EditAnywhere, Category=Capture)`와 제품 기본값을 유지한다.
- `RenderCore`, `RHI`, `ImageCore`, `ImageWrapper`, `Json`처럼 공개 헤더에 노출되지 않는 모듈은 `ShipAutonomySim.Build.cs`의 private dependency로 유지한다.
- runtime log에는 project-relative run path만 기록한다. 개발자 PC의 절대 경로, 사용자명이나 외부 식별자를 소스, 문서, manifest와 commit에 넣지 않는다.
- `Content/Maps/MainLevel.umap`, `Config` 전체, `ShipAutonomySim.uproject`, 루트 웹 뷰어 제품 파일은 Stage 5 구현에서 수정하지 않는다.
- `Binaries`, `Intermediate`, `Saved` 산출물은 stage하거나 commit하지 않는다. Automation cleanup은 현재 테스트가 만든 정확한 `Saved/ShipCaptures/Automation` 하위 run만 대상으로 하고 기존 항목은 보존한다.
