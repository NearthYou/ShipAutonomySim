# ShipAutonomySim 실행 안내

필요 환경은 Unreal Engine 5.5.4와 Visual Studio 2022다. Starter Content와 외부 asset은 사용하지 않는다. 제출본에는 빌드 산출물이 없으므로 먼저 C++ 모듈을 빌드한다.

## 1. 빌드

저장소 루트의 PowerShell에서 실행한다.

```powershell
$EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.5'
$Project = (Resolve-Path 'ShipAutonomySim\ShipAutonomySim.uproject').Path
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') `
    ShipAutonomySimEditor Win64 Development `
    "-Project=$Project" -WaitMutex
```

exit code 0을 확인한 뒤 `ShipAutonomySim.uproject`를 연다.

## 2. 에디터 확인

1. `/Game/Maps/MainLevel`이 열렸는지 확인한다.
2. Water plugin이 활성화되어 있는지 확인한다.
3. World Outliner에서 Water Body Ocean, Water Zone, Directional Light, Sky Light, Sky Atmosphere가 각각 한 개씩 있는지 확인한다.
4. `Maps & Modes`에서 시작 맵과 기본 맵이 `MainLevel`, 기본 GameMode가 `SimGameMode`인지 확인한다.
5. 뷰포트는 기본 `Lit` 상태로 둔다. `viewmode unlit` 명령은 필요하지 않다.

추가 actor를 배치하거나 레벨을 다시 저장할 필요가 없다. 선박, 벽, 시작점과 끝점은 Play 시 코드로 생성된다. 레벨에 저장된 조명 3개는 화면과 컬러 캡처를 위한 환경 actor이며 주행 로직에는 관여하지 않는다.

## 3. Play

1. Play를 한 번 누른다.
2. 별도 키 입력 없이 선박이 출발해 벽을 우회하는지 확인한다.
3. 약 18초에서 23초 뒤 선박이 목적지 근처에서 타력으로 멈추는지 확인한다.
4. 순간이동이나 위치 튐, 벽 충돌이 없어야 한다.
5. `W`, `A`, `S`, `D`를 눌러도 자동 경로가 바뀌지 않아야 한다.

성공 여부는 Output Log의 `Stage4Terminal` 한 줄에서 확인한다. 정상 실행은 `Success`다. 이 이름은 로그 검색용이며 별도의 작업 단계가 아니다.

## 4. 저장 결과

결과는 실행마다 다음 폴더에 저장된다.

```text
Saved/ShipCaptures/<run>/
    color_000000.png, depth_000000.png, ...
    manifest.json
    sequence.siv
```

확인 항목은 다음과 같다.

- 컬러와 깊이 파일 수가 같고 6자리 번호가 연속인가
- 첫, 중간, 마지막 컬러와 깊이가 같은 방향과 장면을 보는가
- 가까운 물체가 깊이 이미지에서 밝고 2500cm 이상 또는 하늘이 어두운가
- `manifest.json`의 프레임 수와 파일 수가 같은가
- `sequence.siv`가 함께 생성됐는가

정상 종료 전 PIE를 중지하거나 충돌 및 시간 초과가 발생하면 manifest의 `result`는 `fail`이다. 시작 준비가 실패하면 빈 데이터 세트를 만들지 않는다.

## 5. 웹 재생

저장소 루트에서 웹을 빌드하고 서버를 실행한다.

```powershell
npm ci
npm run build
python -m http.server 8000
```

단일 파일은 복사 없이 아래 주소로 연다. `<run>`만 실제 폴더명으로 바꾼다.

```text
http://localhost:8000/?bundle=ShipAutonomySim/Saved/ShipCaptures/<run>/sequence.siv
```

개별 PNG 방식과 조작법은 저장소 루트의 `README.md`를 참고한다.
