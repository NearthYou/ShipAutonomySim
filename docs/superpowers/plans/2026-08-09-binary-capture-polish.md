# Binary Capture Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** UE 캡처를 self-contained `sequence.siv`로 추가 게시하고, 웹에서 한 파일을 검증 및 재생하며, 깊이 정밀도와 Stage 5 측정 신뢰성, 제출용 화면과 보고서를 함께 개선한다.

**Architecture:** 기존 PNG와 manifest lifecycle은 과제 필수 호환 경로로 보존하고 finalize 뒤 indexed SIV bundle을 파생 산출물로 만든다. 웹은 manifest와 bundle을 하나의 typed `SequenceSource` 경계로 정규화해 기존 player를 재사용한다. 제품 주행은 바꾸지 않고 실제 월드 test의 최초 관찰만 fresh reload로 통일한다.

**Tech Stack:** Unreal Engine 5.5.4 C++, ImageWrapper PNG, Json, strict TypeScript 7.0.2, 브라우저 ES modules와 Blob URL, Node test runner, Python Pillow 분석, Chromium.

## Global Constraints

- Unreal Engine 5.5.4와 Visual Studio 2022를 사용한다.
- 엔진 내장 module과 asset만 사용하고 외부 C++ library, framework, bundler, runtime package를 추가하지 않는다.
- 단일 transform writer는 `UShipMovement`로 유지하고 Navigator나 capture가 actor transform을 쓰지 않는다.
- Water, `MainLevel.umap`, Config, uproject를 수정하지 않는다.
- 기존 PNG와 `manifest.json` 출력은 보존한다.
- bundle은 캡처가 끝난 finalize 경로에서만 만들고 주행 중 frame timing에 추가하지 않는다.
- 제품 코드와 저장소 이력에 사용자명, 절대 경로, 외부 비공개 식별자를 넣지 않는다.
- 각 제품 변경은 실패하는 실제 행동 test를 먼저 확인하고 최소 GREEN 뒤 commit한다.
- commit 제목은 허용 접두사와 한글 명사형, 본문은 `변경 이유`, `핵심 변경`, `검증 방법` 순서를 사용한다.
- push, main merge, PR은 이 계획 범위에 포함하지 않는다.

---

### Task 1: 첫 실제 월드 측정 시작점 보정

**Files:**
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp`

**Interfaces:**
- Consumes: `FindFreshMainLevelWorld`, `UGameplayStatics::OpenLevel`, 기존 14 case state machine
- Produces: 최초 case도 같은 option으로 fresh reload한 뒤 관찰하는 `bInitialFreshLoadRequested` state와 모든 capture-off gap의 100cm 이상 250cm 이하 합격 계약

- [ ] **Step 1: 범위를 검증하는 실패 단언 추가**

최종 actual-world 집계에서 capture-off 11개 각각에 다음 독립 범위를 적용한다.

```cpp
const bool bPlausibleWallGap =
    Result.MinimumWallDistanceCm >= 100.0 &&
    Result.MinimumWallDistanceCm <= 250.0;
```

기존 구현의 첫 `-500cm`는 약 493cm이므로 이 단언만 실패해야 한다.

- [ ] **Step 2: actual-world test를 실행해 RED 확인**

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat' ShipAutonomySimEditor Win64 Development -Project="$PWD\ShipAutonomySim\ShipAutonomySim.uproject" -WaitMutex -NoHotReloadFromIDE
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe' "$PWD\ShipAutonomySim\ShipAutonomySim.uproject" '/Game/Maps/MainLevel?Stage4Slide=-500?Stage5Capture=0' -game -unattended -nosplash -NoSound -NoP4 -RenderOffscreen -ExecCmds="Automation RunTests ShipAutonomySim.ShipNavigation.ActualWorld.NavigationSweep;SoftQuit;"
```

Expected: build exit 0, Automation은 `-500cm` plausible gap 단언 한 건 때문에 non-zero다.

- [ ] **Step 3: 최초 world를 관찰하지 않고 같은 option으로 한 번 reload**

state에 `bool bInitialFreshLoadRequested = false;`를 추가한다. `UpdateWaitingForWorld`가 첫 유효 MainLevel을 찾았을 때 현재 world identity를 previous로 저장하고 `OpenLevel`을 current case option으로 호출한 뒤 flag를 true로 바꾼다. case index는 증가시키지 않는다. 이후 world만 기존 `BeginCurrentResult`와 gap sampling으로 넘긴다.

- [ ] **Step 4: actual-world GREEN 확인**

Step 2 명령을 다시 실행한다.

Expected: 14 case success, collision, timeout, setup, runtime, capture failure 0, capture-off 11개 gap 모두 100cm 이상 250cm 이하다.

- [ ] **Step 5: commit**

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp
git commit -m "test: 최초 월드 관찰 시점 보정" -m "변경 이유`n첫 자동화 case가 벽 최근접 구간을 놓쳐 잘못된 최소 거리를 보고했기 때문.`n`n핵심 변경`n첫 case도 같은 option의 fresh world에서 관찰하고 wall gap 현실 범위를 검증함.`n`n검증 방법`nNavigation ActualWorld 14 case를 실행함."
```

### Task 2: SIV pure bundle format 구현

**Files:**
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.h`
- Create: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`

**Interfaces:**
- Consumes: UTF-8 manifest JSON과 `{Path, MediaType, Bytes}` asset 배열
- Produces: `bool BuildShipCaptureBundle(const FString&, const TArray<FShipCaptureBundleAsset>&, TArray64<uint8>&)`

- [ ] **Step 1: exact bundle fixture test를 RED로 추가**

두 PNG signature fixture를 넣고 결과의 첫 8 bytes가 `SIVPACK1`, 다음 uint32가 header byte length, header의 두 offset이 `0`과 첫 payload length, 마지막 bytes가 두 fixture의 정확한 연결인지 단언한다. empty manifest, empty asset, duplicate path, empty bytes, PNG signature 오류는 false와 빈 output을 단언한다.

- [ ] **Step 2: ShipCapture test만 실행해 RED 확인**

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat' ShipAutonomySimEditor Win64 Development -Project="$PWD\ShipAutonomySim\ShipAutonomySim.uproject" -WaitMutex -NoHotReloadFromIDE
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\ShipAutonomySim\ShipAutonomySim.uproject" -unattended -nop4 -nosplash -NullRHI -ExecCmds="Automation RunTests ShipAutonomySim.ShipCapture;SoftQuit;"
```

Expected: 새 builder symbol 부재 또는 새 test failure로 non-zero다.

- [ ] **Step 3: pure builder 최소 구현**

`FShipCaptureBundleAsset`은 `FString Path`, `FString MediaType`, `TArray64<uint8> Bytes`를 가진다. builder는 모든 입력을 먼저 검증하고 JSON header를 UTF-8로 직렬화한다. `TArray64<uint8>`에 magic, little-endian header length, header, payload 순서로 append하며 어느 단계든 실패하면 output을 reset한다.

- [ ] **Step 4: ShipCapture GREEN 확인**

Step 2 명령을 다시 실행한다.

Expected: 기존 ShipCapture test와 새 format test 모두 success다.

- [ ] **Step 5: commit**

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.h ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCaptureBundle.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp
git commit -m "feat: 단일 캡처 바이너리 형식 추가" -m "변경 이유`nPNG 시퀀스를 한 파일에서 순차 및 무작위 접근할 수 있게 하기 위함.`n`n핵심 변경`nSIV magic, JSON index와 PNG payload를 만드는 pure builder를 추가함.`n`n검증 방법`nShipCapture Automation의 exact bytes와 거부 입력 test를 실행함."
```

### Task 3: 캡처 finalize에 bundle 게시 연결

**Files:**
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp`
- Modify: `ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp`
- Modify: `ShipAutonomySim/SETUP.md`

**Interfaces:**
- Consumes: committed `Frames`, published PNG paths, serialized manifest JSON
- Produces: `sequence.siv`, `Stage5CaptureFinalized ... bundle=published|not_published`, exact file inventory

- [ ] **Step 1: finalize와 실제 월드 file inventory test를 RED로 확장**

성공 finalize는 `sequence.siv`가 존재하고 magic이 맞으며 manifest와 frame path 수가 일치해야 한다. 기존 actual-world expected files에는 `sequence.siv` 한 개를 추가한다. failure finalize는 incomplete `.tmp`를 남기지 않아야 한다.

- [ ] **Step 2: ShipCapture test를 실행해 RED 확인**

Task 2의 ShipCapture 명령을 실행한다.

Expected: 성공 run에 `sequence.siv`가 없어 실패한다.

- [ ] **Step 3: manifest serialize와 publish를 분리하고 bundle writer 연결**

`SerializeManifest(bool, FString&)`, `WriteManifest(const FString&)`, `WriteBinaryBundle(const FString&)` 경계를 만든다. bundle writer는 각 frame의 color와 depth PNG를 읽어 pure builder에 전달하고 `.sequence.siv.tmp`를 write한 뒤 `sequence.siv`로 rename한다. bundle publication은 manifest publication 뒤에 실행하며 실패 시 기존 PNG와 manifest는 보존하고 `bundle=not_published`를 기록한다.

- [ ] **Step 4: depth far 기본값을 2500cm로 변경하고 행동 test 갱신**

`DepthFarCm = 2500.0`으로 바꾼다. normalization test는 near 0, midpoint 1250이 128, far 2500이 0, far 초과와 invalid가 0임을 실제 함수로 확인한다. manifest test도 `depth_far_cm=2500`을 단언한다.

- [ ] **Step 5: ShipCapture와 actual-world GREEN 확인**

Task 2와 Task 1의 두 Automation 명령을 실행한다.

Expected: ShipCapture 전체 success, actual-world 14 case success, capture-on 세 run 모두 PNG, manifest, SIV inventory valid다.

- [ ] **Step 6: commit**

```powershell
git add -- ShipAutonomySim/Source/ShipAutonomySim/Public/ShipCapture.h ShipAutonomySim/Source/ShipAutonomySim/Private/ShipCapture.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipCaptureTests.cpp ShipAutonomySim/Source/ShipAutonomySim/Private/Tests/ShipNavigationWorldTests.cpp ShipAutonomySim/SETUP.md
git commit -m "feat: 캡처 바이너리 게시 연결" -m "변경 이유`n정상 run마다 웹에서 직접 읽을 수 있는 단일 파일을 제공하기 위함.`n`n핵심 변경`nfinalize bundle publication과 2500cm 깊이 범위를 적용함.`n`n검증 방법`nShipCapture와 actual-world Automation에서 파일 구조와 주행 회귀를 확인함."
```

### Task 4: strict TypeScript bundle parser와 benchmark 구현

**Files:**
- Create: `src/bundle.ts`
- Create: `tests/bundle.test.ts`
- Modify: `src/manifest.ts`
- Modify: `tests/types.contract.ts`

**Interfaces:**
- Produces: `loadBundle(url): Promise<BundleSequenceSource>`, `parseBundle(buffer, sourceUrl)`, `benchmarkBundleAccess(source, options)`
- `BundleSequenceSource`는 `manifest`, `assets`, `objectUrls`, `revoke()`와 byte benchmark input을 제공한다.

- [ ] **Step 1: parser와 benchmark 실패 test 작성**

hand-built ArrayBuffer fixture로 valid two-frame bundle을 parse하고 source path가 Blob URL로 정규화되는지 확인한다. bad magic, truncated header, unsupported version, duplicate path, payload overflow, missing manifest asset, bad PNG signature를 각각 거부한다. benchmark는 literal order `[0,1,2,3]`과 fixed seed random order를 fake clock으로 실행해 exact average와 sample count를 단언한다.

- [ ] **Step 2: npm test RED 확인**

```powershell
npm test
```

Expected: `src/bundle.ts` 부재 또는 새 test failure다.

- [ ] **Step 3: parser와 source 정규화 최소 구현**

`DataView`로 uint32 little-endian을 읽고 `TextDecoder`로 header를 decode한다. 모든 unknown JSON을 runtime guard한 뒤 기존 `validateManifest`를 사용한다. 각 asset은 original bundle buffer의 exact range로 Blob을 만들고 object URL을 manifest frame에 연결한다.

- [ ] **Step 4: deterministic benchmark 구현**

전체 asset 순서를 sequential과 xorshift32 기반 Fisher-Yates random으로 만들고 warm-up 1회 뒤 3 pass에서 `ArrayBuffer.slice`로 compressed image bytes를 복사한다. 각 copy의 첫 byte와 마지막 byte를 checksum에 더하고 `performance.now()` delta를 asset 수로 나눈다.

- [ ] **Step 5: npm test GREEN 확인**

```powershell
npm test
```

Expected: tsc success와 모든 Node test success다.

- [ ] **Step 6: commit**

```powershell
git add -- src/bundle.ts src/manifest.ts tests/bundle.test.ts tests/types.contract.ts
git commit -m "feat: 웹 바이너리 해석 경계 추가" -m "변경 이유`n브라우저가 SIV 한 파일을 검증하고 image frame으로 복원해야 하기 때문.`n`n핵심 변경`nstrict parser, Blob URL source와 deterministic 접근 benchmark를 추가함.`n`n검증 방법`nnpm test에서 정상 형식과 손상 경계를 확인함."
```

### Task 5: bundle mode와 시각 완성도 연결

**Files:**
- Modify: `src/app.ts`
- Modify: `index.html`
- Modify: `styles.css`
- Modify: `tests/app.test.ts`
- Modify: `README.md`

**Interfaces:**
- Consumes: `?bundle=sequence.siv`, `BundleSequenceSource`, 기존 `SequencePlayer`
- Produces: source summary, depth range legend, benchmark button과 결과, 기존 manifest fallback

- [ ] **Step 1: app source 선택과 UI 상태 test를 RED로 작성**

query에 bundle이 있으면 `loadBundle`만 호출하고 없으면 `loadManifest`만 호출하는지 확인한다. bundle mode의 source badge와 benchmark result formatter를 literal input으로 단언하고 manifest mode에서는 benchmark control이 disabled인지 확인한다.

- [ ] **Step 2: npm test RED 확인**

```powershell
npm test
```

Expected: source loader와 새 element 계약 부재로 실패한다.

- [ ] **Step 3: app source 경계 연결**

`ViewerController.start()`는 URL query를 읽어 bundle 또는 manifest 한 경로만 선택한다. bundle의 object URL은 page unload에서 revoke한다. source type, frame count, interval과 depth range를 summary element에 넣고 benchmark click에서 결과를 표시한다.

- [ ] **Step 4: HTML과 CSS 개선**

상단 제목, source badge, 네 summary item, depth scale, benchmark card를 semantic section으로 추가한다. 기존 1440px two-column과 390px single-column 동작, focus-visible, reduced-motion, error card를 보존한다.

- [ ] **Step 5: README 실행 절차 추가**

기존 manifest mode와 함께 다음 bundle URL을 기록한다.

```text
http://localhost:8000/?bundle=sequence.siv
```

bundle 하나를 viewer root에 복사하는 방법, benchmark가 compressed byte extraction이며 decode를 제외한다는 정의를 기록한다.

- [ ] **Step 6: npm test GREEN 확인 후 commit**

```powershell
npm test
git add -- src/app.ts index.html styles.css tests/app.test.ts README.md
git commit -m "feat: 바이너리 뷰어 정보 구조 개선" -m "변경 이유`n평가자가 데이터 출처와 깊이 및 접근 성능을 화면에서 바로 확인하게 하기 위함.`n`n핵심 변경`nbundle mode, summary, depth scale와 benchmark panel을 연결함.`n`n검증 방법`nnpm test로 source 선택과 UI 상태를 확인함."
```

### Task 6: 실데이터 재측정과 제출 보고서 작성

**Files:**
- Create: `REPORT.md`
- Modify: `REPORT_DRAFT.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: final actual-world logs, performance A-B-B-A log, 2500cm capture PNG, browser bundle benchmark
- Produces: 읽기 쉬운 제출 본문과 근거 appendix 연결

- [ ] **Step 1: `-500cm` 실제 월드 sweep을 세 번 실행**

Task 1의 actual-world 명령을 fresh process로 세 번 실행하고 각 capture-off `-500cm` minimum gap을 기록한다. 세 값이 100cm 이상 250cm 이하이고 최대와 최소 차이가 5cm 이하인지 확인한다.

- [ ] **Step 2: 2500cm 정상 run 생성과 histogram 계산**

Lit D3D12 game run 한 건을 성공 terminal까지 실행해 보존하고 depth PNG 전체의 0 비율, valid grayscale percentile, 2500cm clipping 비율을 Pillow로 계산한다. manifest의 far가 2500이고 SIV frame 수와 PNG 수가 맞는지 확인한다.

- [ ] **Step 3: Chromium bundle benchmark와 UI 확인**

viewer root에서 `sequence.siv`를 제공하고 `?bundle=sequence.siv`를 연다. benchmark를 실행해 sequential과 random ms/image, sample count를 기록한다. 1440x1000과 390x844에서 frame render, play, seek, speed, depth mode, source badge와 console error 0을 확인한다.

- [ ] **Step 4: `REPORT.md` 작성**

설계 문서의 10개 본문 순서로 작성한다. capture-off와 capture-on elapsed 차이, transaction median과 p95, synchronous limitation을 표에서 본문으로 끌어올린다. 새 `-500cm` 반복값, 깊이 histogram, SIV 크기와 접근 benchmark를 넣는다. 입증하지 못한 값은 추측하지 않는다.

- [ ] **Step 5: `REPORT_DRAFT.md` 상태 갱신**

2c 미구현 표기를 구현 완료로 바꾸고 새 evidence의 commit SHA와 log 이름을 추가한다. 기존 오래된 수치는 지우지 않고 역사적 baseline으로 표시한다.

- [ ] **Step 6: 문서와 web commit**

```powershell
git add -- REPORT.md REPORT_DRAFT.md README.md
git commit -m "docs: 제출 보고서 완성" -m "변경 이유`n구현과 성능 판단을 채점자가 빠르게 검토할 수 있는 본문이 필요하기 때문.`n`n핵심 변경`n단일 바이너리, 깊이 분포, 주행 재현성과 캡처 비용을 실측값으로 정리함.`n`n검증 방법`n실제 월드 반복 실행, PNG histogram과 Chromium benchmark 결과를 대조함."
```

### Task 7: 최종 전체 검증과 handoff

**Files:**
- Verify only: tracked repository 전체

**Interfaces:**
- Produces: clean feature branch, fresh build와 test evidence, 사람이 재현할 수 있는 수동 확인 순서

- [ ] **Step 1: UE 5.5.4 build와 Automation 실행**

```powershell
& 'C:\Program Files\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat' ShipAutonomySimEditor Win64 Development -Project="$PWD\ShipAutonomySim\ShipAutonomySim.uproject" -WaitMutex -NoHotReloadFromIDE
```

ShipMovement, ShipNavigation unit, ShipCapture, actual-world, performance A-B-B-A를 fresh logs로 실행한다. 각 process exit 0, `TEST COMPLETE. EXIT CODE: 0`, failure, error, unknown, ensure 0을 확인한다.

- [ ] **Step 2: MainLevel no-write 확인**

`/Game/Maps/MainLevel -game -nowrite -ExecCmds=QUIT_EDITOR`를 실행하고 LoadErrors, Fatal, MapCheck Error 0과 clean exit를 확인한다. map과 Config SHA, tracked file 집합이 시작 기준과 같아야 한다.

- [ ] **Step 3: web 전체 검증**

```powershell
npm ci
npm test
& 'C:\Users\siwon\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m unittest discover -s tests -p 'test_*.py'
& 'C:\Users\siwon\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m compileall -q scripts tests
npm ls --omit=dev --depth=0
git diff --check
```

generated JavaScript 전체에 `node --check`를 적용하고 runtime package 0을 확인한다.

- [ ] **Step 4: 최종 상태 확인**

`feat/binary-capture-polish`, clean worktree, UnrealEditor process 0, 금지 산출물 tracked 0을 확인한다. push, merge, PR은 하지 않는다.

- [ ] **Step 5: 사용자 수동 확인 절차 제공**

UE Play에서 자동 주행과 capture finalize를 확인한 뒤 run 폴더의 PNG, manifest, SIV를 확인한다. viewer root에 `sequence.siv`를 복사하고 bundle URL에서 재생, depth mode, benchmark를 확인하는 순서를 정확히 제공한다.
