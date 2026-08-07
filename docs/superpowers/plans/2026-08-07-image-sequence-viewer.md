# 이미지 시퀀스 웹 뷰어 구현 계획

> 작업자 안내: 이 계획은 작업 단위마다 테스트를 먼저 실패시키고 최소 구현으로 통과시킨 뒤 한글 커밋을 작성한다.

목표: 외부 웹 라이브러리 없이 manifest 기반 컬러 및 깊이 이미지 시퀀스를 재생하고 탐색하는 정적 웹 뷰어를 완성한다.

구조: ES 모듈이 manifest, 선로딩, 재생, 깊이 변환 책임을 나누고 `src/app.js`가 DOM과 캔버스를 연결한다. Python 표준 라이브러리 생성기가 테스트용 PNG와 manifest를 만든다.

기술: HTML5, CSS, 브라우저 ES 모듈, Canvas 2D, Node.js 내장 테스트 러너, Python 표준 라이브러리와 `unittest`

## 공통 제약

- 정적 웹 뷰어 범위만 구현하고 별도 애플리케이션 작업을 시작하지 않는다.
- 외부 JavaScript, CSS, Python 라이브러리를 추가하지 않는다.
- 애플리케이션은 `file://`가 아닌 `python -m http.server`로 실행한다.
- main에는 초기 뼈대만 두고 `stage/01-web-viewer`에서 구현한다.
- 브랜치 이름에 codex 문자열을 사용하지 않는다.
- 기능별 커밋의 제목과 본문은 모두 한글로 작성한다.
- 각 커밋 본문에 변경 이유, 핵심 변경, 검증 방법을 기록한다.
- 리뷰 전에는 main에 병합하지 않는다.

## 파일 구성

- `package.json`: ES 모듈과 내장 테스트 명령 정의
- `index.html`: 뷰어 화면과 조작 요소
- `styles.css`: 반응형 레이아웃과 상태 표현
- `src/manifest.js`: manifest 가져오기와 검증
- `src/preload.js`: 이미지 전체 선로딩과 진행률
- `src/player.js`: 재생 상태와 시간 누적
- `src/depth.js`: 깊이 컬러맵 계산
- `src/app.js`: 데이터 로딩, 캔버스, 조작 연결
- `scripts/generate_dummy_data.py`: 표준 라이브러리 PNG 생성기
- `scripts/__init__.py`: 생성기 테스트용 패키지 표시
- `tests/page.test.js`: 정적 화면 계약 검사
- `tests/manifest.test.js`: manifest 정상 및 오류 검사
- `tests/preload.test.js`: 선로딩 진행률과 실패 검사
- `tests/player.test.js`: 이동, 속도, 재생 경계 검사
- `tests/depth.test.js`: 깊이 색상 변환 검사
- `tests/app.test.js`: 표시 문자열과 오류 안내 검사
- `tests/test_generate_dummy_data.py`: 생성 파일과 manifest 검사
- `README.md`: 데이터 생성, 실행, 검사, 조작 설명

## 작업 1: 정적 뷰어 화면

생성 파일: `package.json`, `index.html`, `styles.css`, `tests/page.test.js`

제공 계약:

- `index.html`은 `color-canvas`, `depth-canvas`, `play-toggle`, `restart`, `previous-frame`, `next-frame`, `frame-slider`, `playback-speed`, `depth-mode`, `frame-readout`, `time-readout`, `loading-progress`, `viewer-error` 식별자를 제공한다.
- 모든 조작 요소에는 `data-viewer-control` 속성을 둔다.
- 760픽셀 이하에서는 캔버스 영역을 한 열로 배치한다.

- [ ] 1단계: 실패하는 화면 계약 테스트 작성

```js
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

test("필수 캔버스와 재생 조작을 제공한다", async () => {
  const html = await readFile(new URL("../index.html", import.meta.url), "utf8");
  for (const id of ["color-canvas", "depth-canvas", "play-toggle", "frame-slider", "depth-mode"]) {
    assert.match(html, new RegExp(`id=["']${id}["']`));
  }
});
```

- [ ] 2단계: 테스트가 파일 부재로 실패하는지 확인

실행: `node --test tests/page.test.js`

예상: `index.html`을 찾을 수 없어 실패

- [ ] 3단계: 의미 있는 HTML 요소와 반응형 CSS 구현

```html
<main class="viewer-shell">
  <section class="canvas-grid" aria-label="프레임 비교">
    <article class="canvas-panel">
      <h2>컬러</h2>
      <canvas id="color-canvas"></canvas>
    </article>
    <article class="canvas-panel">
      <h2>깊이</h2>
      <canvas id="depth-canvas"></canvas>
    </article>
  </section>
</main>
```

- [ ] 4단계: 화면 계약과 문법 검사

실행: `node --test tests/page.test.js`

예상: 전체 통과

- [ ] 5단계: 기능 단위 커밋

커밋 제목: `웹 뷰어 화면 뼈대를 구성한다`

## 작업 2: manifest 검증과 이미지 선로딩

생성 파일: `src/manifest.js`, `src/preload.js`, `tests/manifest.test.js`, `tests/preload.test.js`

제공 계약:

- `validateManifest(value, manifestUrl)`은 검증되고 이미지 절대 URL이 포함된 새 객체를 반환한다.
- `loadManifest(url, fetchImpl)`은 HTTP, JSON, 계약 오류를 `ManifestError`로 변환한다.
- `preloadFrames(frames, options)`은 `colorImage`와 `depthImage`가 연결된 프레임 배열을 반환한다.
- `FrameLoadError.failures`는 실패한 프레임, 종류, URL을 보존한다.

- [ ] 1단계: 정상 manifest와 대표 오류 테스트 작성

```js
test("manifest 경로를 기준으로 이미지 URL을 해석한다", () => {
  const result = validateManifest(validManifest(), "https://example.test/data/manifest.json");
  assert.equal(result.frames[0].colorUrl, "https://example.test/data/color_000000.png");
});

test("프레임 수가 배열 길이와 다르면 거부한다", () => {
  assert.throws(() => validateManifest({ ...validManifest(), frame_count: 2 }, BASE_URL), ManifestError);
});
```

- [ ] 2단계: 테스트가 모듈 부재로 실패하는지 확인

실행: `node --test tests/manifest.test.js tests/preload.test.js`

예상: 모듈을 찾을 수 없어 실패

- [ ] 3단계: 최소 manifest 검증 구현

```js
export function validateManifest(value, manifestUrl) {
  requireRecord(value, "manifest 최상위 값");
  requirePositiveInteger(value.frame_count, "frame_count");
  requirePositiveNumber(value.interval_ms, "interval_ms");
  return {
    ...value,
    frames: value.frames.map((frame) => ({
      ...frame,
      colorUrl: new URL(frame.color, manifestUrl).href,
      depthUrl: new URL(frame.depth, manifestUrl).href,
    })),
  };
}
```

- [ ] 4단계: 선로딩 진행률과 다중 실패 테스트 작성 및 확인

```js
test("각 이미지 완료 시 진행률을 알린다", async () => {
  const progress = [];
  const loaded = await preloadFrames(frames, {
    imageLoader: async (url) => ({ url }),
    onProgress: (state) => progress.push(state.completed),
  });
  assert.deepEqual(progress, [1, 2]);
  assert.equal(loaded[0].colorImage.url, frames[0].colorUrl);
});
```

- [ ] 5단계: 전체 결과를 기다리고 실패 파일을 모으는 구현

```js
const settled = await Promise.all(assets.map(async (asset) => {
  try {
    return { ...asset, image: await imageLoader(asset.url) };
  } catch (cause) {
    return { ...asset, cause };
  } finally {
    completed += 1;
    onProgress({ completed, total, percent: Math.round((completed / total) * 100) });
  }
}));
```

- [ ] 6단계: 전체 JavaScript 테스트 실행

실행: `node --test`

예상: 전체 통과

- [ ] 7단계: 기능 단위 커밋

커밋 제목: `manifest 검증과 이미지 선로딩을 추가한다`

## 작업 3: 재생 상태와 시간 제어

생성 파일: `src/player.js`, `tests/player.test.js`

제공 계약:

- `SequencePlayer`는 `frameCount`, `intervalMs`, 화면 갱신 콜백과 교체 가능한 애니메이션 프레임 함수를 받는다.
- `play`, `pause`, `restart`, `previous`, `next`, `seek`, `setSpeed`를 제공한다.
- 허용 속도는 0.5, 1, 2뿐이다.

- [ ] 1단계: 이동 경계와 일시정지 테스트 작성

```js
test("처음과 마지막을 넘어 이동하지 않는다", () => {
  const player = createPlayer({ frameCount: 3 });
  player.previous();
  assert.equal(player.index, 0);
  player.seek(2);
  player.next();
  assert.equal(player.index, 2);
});
```

- [ ] 2단계: 모듈 부재 실패 확인

실행: `node --test tests/player.test.js`

예상: 모듈을 찾을 수 없어 실패

- [ ] 3단계: 탐색과 속도 검증 최소 구현

```js
setSpeed(speed) {
  if (![0.5, 1, 2].includes(speed)) {
    throw new RangeError("재생 속도는 0.5, 1, 2 중 하나여야 합니다.");
  }
  this.speed = speed;
}
```

- [ ] 4단계: 가짜 애니메이션 프레임으로 속도와 끝 동작 테스트

```js
player.play();
scheduler.run(0);
scheduler.run(100);
assert.equal(player.index, 1);
scheduler.run(200);
assert.equal(player.index, 2);
assert.equal(player.isPlaying, false);
```

- [ ] 5단계: 누적 시간 기반 재생 시계 구현

```js
const frameDuration = this.intervalMs / this.speed;
const steps = Math.floor(this.elapsedMs / frameDuration);
if (steps > 0) {
  this.elapsedMs -= steps * frameDuration;
  this.updateIndex(Math.min(this.index + steps, this.frameCount - 1));
}
```

- [ ] 6단계: 전체 JavaScript 테스트 실행

실행: `node --test`

예상: 전체 통과

- [ ] 7단계: 기능 단위 커밋

커밋 제목: `프레임 재생 상태를 구현한다`

## 작업 4: 깊이 컬러맵

생성 파일: `src/depth.js`, `tests/depth.test.js`

제공 계약:

- `mapDepthIntensity(value)`는 0부터 255 사이 밝기를 RGB 배열로 바꾼다.
- `colorizeDepthPixels(pixels)`는 원본을 변경하지 않고 새 `Uint8ClampedArray`를 반환한다.
- 알파 채널은 보존한다.

- [ ] 1단계: 어두운 값, 밝은 값, 알파 보존 테스트 작성

```js
test("밝은 깊이값을 따뜻한 색으로 바꾼다", () => {
  const far = mapDepthIntensity(0);
  const near = mapDepthIntensity(255);
  assert.ok(far[2] > far[0]);
  assert.ok(near[0] > near[2]);
});
```

- [ ] 2단계: 모듈 부재 실패 확인

실행: `node --test tests/depth.test.js`

예상: 모듈을 찾을 수 없어 실패

- [ ] 3단계: 색상 정지점 보간과 픽셀 변환 구현

```js
const STOPS = [
  [0, [12, 35, 84]],
  [0.25, [0, 155, 207]],
  [0.5, [246, 220, 73]],
  [0.75, [245, 123, 32]],
  [1, [210, 35, 42]],
];
```

- [ ] 4단계: 전체 JavaScript 테스트 실행

실행: `node --test`

예상: 전체 통과

- [ ] 5단계: 기능 단위 커밋

커밋 제목: `깊이 프레임 컬러맵을 추가한다`

## 작업 5: 브라우저 애플리케이션 연결

생성 파일: `src/app.js`, `tests/app.test.js`

수정 파일: `index.html`, `styles.css`

제공 계약:

- `formatElapsed(timeMs)`는 `분:초.밀리초` 문자열을 반환한다.
- `describeViewerError(error)`는 manifest와 이미지 오류를 사용자 행동이 포함된 한국어 문장으로 바꾼다.
- 페이지 로드 시 조작을 잠그고 manifest 및 이미지 로딩 완료 뒤 활성화한다.
- 모든 프레임 변경은 두 캔버스와 프레임 및 시간 표시를 함께 갱신한다.

- [ ] 1단계: 시간과 오류 문구 테스트 작성

```js
test("밀리초를 경과 시간으로 표시한다", () => {
  assert.equal(formatElapsed(61_234), "01:01.234");
});

test("프레임 오류에 실패 파일을 포함한다", () => {
  const message = describeViewerError(new FrameLoadError([{ url: "depth_000003.png" }]));
  assert.match(message, /depth_000003\.png/);
});
```

- [ ] 2단계: 모듈 부재 실패 확인

실행: `node --test tests/app.test.js`

예상: 모듈을 찾을 수 없어 실패

- [ ] 3단계: 순수 표시 함수와 초기 로딩 연결

```js
export function formatElapsed(timeMs) {
  const minutes = Math.floor(timeMs / 60_000);
  const seconds = Math.floor((timeMs % 60_000) / 1_000);
  const milliseconds = Math.floor(timeMs % 1_000);
  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${String(milliseconds).padStart(3, "0")}`;
}
```

- [ ] 4단계: 캔버스 렌더링과 모든 조작 연결

```js
player = new SequencePlayer({
  frameCount: frames.length,
  intervalMs: manifest.interval_ms,
  onFrameChange: renderFrame,
  onPlayingChange: updatePlayButton,
});
```

- [ ] 5단계: 깊이 모드 전환 시 현재 프레임 다시 그리기

```js
depthModeButton.addEventListener("click", () => {
  depthMode = depthMode === "grayscale" ? "colormap" : "grayscale";
  renderFrame(player.index);
});
```

- [ ] 6단계: 전체 JavaScript 테스트와 문법 검사

실행: `node --test; node --check src/app.js`

예상: 전체 통과, 문법 오류 없음

- [ ] 7단계: 기능 단위 커밋

커밋 제목: `뷰어 데이터와 재생 조작을 연결한다`

## 작업 6: 표준 라이브러리 더미 데이터 생성기

생성 파일: `scripts/__init__.py`, `scripts/generate_dummy_data.py`, `tests/test_generate_dummy_data.py`

제공 계약:

- `generate_dataset(output, frame_count, width, height, interval_ms)`가 파일을 생성한다.
- PNG는 8비트 RGB 또는 회색조이며 추가 패키지가 필요 없다.
- manifest는 생성된 파일 수, 시간, 깊이 범위를 정확히 기록한다.

- [ ] 1단계: 임시 폴더 생성 결과 테스트 작성

```python
def test_generate_dataset_writes_matching_frames_and_manifest(self):
    with tempfile.TemporaryDirectory() as directory:
        generate_dataset(Path(directory), frame_count=3, width=16, height=10, interval_ms=100)
        manifest = json.loads((Path(directory) / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["frame_count"], 3)
        self.assertEqual(len(list(Path(directory).glob("color_*.png"))), 3)
        self.assertEqual(len(list(Path(directory).glob("depth_*.png"))), 3)
```

- [ ] 2단계: 모듈 부재 실패 확인

실행: `python -m unittest tests.test_generate_dummy_data -v`

예상: 생성기 모듈을 찾을 수 없어 실패

- [ ] 3단계: PNG 청크 작성과 프레임 생성 구현

```python
def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", binascii.crc32(kind + data) & 0xFFFFFFFF)
```

- [ ] 4단계: 생성 데이터 테스트와 기본 30프레임 생성

실행: `python -m unittest discover -s tests -p "test_*.py" -v`

실행: `python scripts/generate_dummy_data.py`

예상: 테스트 통과, 루트에 30쌍의 PNG와 `manifest.json` 생성

- [ ] 5단계: 기능 단위 커밋

커밋 제목: `표준 라이브러리 더미 데이터를 생성한다`

## 작업 7: 실행 문서와 최종 검증

수정 파일: `README.md`

검증 대상: 전체 저장소, 실제 브라우저

- [ ] 1단계: README에 준비 및 실행 명령 작성

```text
python scripts/generate_dummy_data.py
python -m http.server 8000
```

- [ ] 2단계: README에 조작, 테스트, 오류 해결, 데이터 계약 설명 작성

- [ ] 3단계: 전체 자동 검사

실행: `node --test`

실행: `python -m unittest discover -s tests -p "test_*.py" -v`

실행: `python -m json.tool manifest.json`

실행: `git diff --check main...HEAD`

예상: 모두 통과

- [ ] 4단계: HTTP 서버에서 실제 브라우저 검증

확인 항목:

- 로딩 진행률이 증가하고 완료 뒤 조작이 활성화된다.
- 두 캔버스에 같은 인덱스의 컬러와 깊이가 표시된다.
- 재생, 일시정지, 처음으로, 앞뒤 이동이 동작한다.
- 슬라이더가 임의 프레임으로 이동한다.
- 0.5배, 1배, 2배에서 프레임 증가 속도가 달라진다.
- 깊이 원본과 컬러맵이 즉시 전환된다.
- manifest 부재 상태에서 이해 가능한 오류가 보인다.
- 1440픽셀과 390픽셀 너비에서 겹침이나 잘림이 없다.
- 콘솔에 애플리케이션 오류가 없다.

- [ ] 5단계: 문서 커밋

커밋 제목: `실행과 검증 방법을 안내한다`

- [ ] 6단계: 원격 브랜치 푸시 및 분리 상태 확인

실행: `git push -u origin stage/01-web-viewer`

실행: `git log --oneline origin/main..origin/stage/01-web-viewer`

예상: 기능 커밋이 원격 작업 브랜치에만 있고 main은 초기 커밋을 유지
