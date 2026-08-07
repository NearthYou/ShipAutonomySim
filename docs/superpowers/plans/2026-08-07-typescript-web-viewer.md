# TypeScript Web Viewer Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 기존 정적 이미지 시퀀스 뷰어를 프레임워크와 번들러 없이 엄격한 TypeScript 코드로 전환하고 현재 기능, 오류 처리, 접근성, 테스트 계약을 보존한다.

**Architecture:** 하나의 `tsconfig.json`이 제품 코드와 Node 테스트를 `dist`에 같은 디렉터리 구조로 컴파일한다. 전환 중에는 `allowJs`로 남아 있는 JavaScript와 변환된 TypeScript를 함께 출력해 매 작업마다 전체 50개 Node 테스트를 유지하고, 마지막 모듈 전환 뒤 TypeScript 전용 설정으로 좁힌다. 외부 manifest는 `unknown`에서 검증해 camel case 내부 모델로 정규화하고, 각 타입은 책임 모듈이 소유한다.

**Tech Stack:** TypeScript 7.0.2, Node.js 24 내장 테스트 러너, `@types/node` 24.13.3, 브라우저 ES 모듈, Python 3 표준 라이브러리

## Global Constraints

- 작업 브랜치는 최신 `main`에서 만든 `feat/typescript-web-viewer`를 사용한다.
- 브랜치 이름에는 금지된 자동화 접두사를 넣지 않는다.
- 프레임워크, 번들러, TypeScript 실행 로더를 추가하지 않는다.
- 최종 런타임 의존성은 0개다.
- 개발 의존성은 `typescript@7.0.2`와 `@types/node@24.13.3`만 사용한다.
- 단일 `tsconfig.json`의 최종 설정은 `target: ES2022`, `module: NodeNext`, `moduleResolution: NodeNext`, `strict: true`다.
- 최종 `tsconfig.json`은 `src/**/*.ts`와 `tests/**/*.ts`만 포함하고 `dist`를 제외한다.
- 로컬 TypeScript import는 컴파일 뒤에도 유효한 `.js` 확장자를 유지한다.
- manifest JSON 형식과 snake case 외부 필드는 변경하지 않는다.
- 검증 전 외부 데이터, 예외 원인, `catch` 변수는 `unknown`으로 취급한다.
- 컬러 프레임, 깊이 프레임, manifest, 선로딩 결과, 재생 속도, 재생 상태, 깊이 표시 상태를 명시적으로 타입화한다.
- `SequencePlayer` 내부에는 `PlaybackState`를 사용하되 기존 boolean 콜백 계약은 유지한다.
- 제품 코드에는 `any`를 사용하지 않는다.
- 생성된 `dist`와 `node_modules`는 Git에서 제외한다.
- 기존 Node 테스트 50개와 Python 테스트 4개의 동작 단언을 보존한다.
- 화면 디자인, manifest 기능, Python 생성기, 오류 문구의 사용자 계약을 바꾸지 않는다.
- 연결된 레이블, 키보드 조작, 포커스 표시, live 및 alert 알림, 동적 접근 가능한 이름과 pressed 상태를 보존한다.
- 커밋 제목은 `feat`, `fix`, `test`, `docs`, `chore`, `perf`, `refactor`, `build` 중 하나와 한글 명사형을 사용한다.
- 모든 커밋 본문에는 `변경 이유`, `핵심 변경`, `검증 방법`을 포함한다.
- 저장소와 커밋 이력에 대외비 식별자를 기록하지 않는다.

## File Structure

### 생성

- `tsconfig.json`: 제품 코드와 Node 테스트의 단일 컴파일 설정
- `package-lock.json`: TypeScript 개발 도구 버전과 무결성 잠금
- `tests/types.contract.ts`: 실행하지 않는 부정 타입 계약

### 수정

- `.gitignore`: `/node_modules/`와 `/dist/` 제외
- `package.json`: `build`, `test` 스크립트와 개발 의존성 두 개
- `index.html`: 브라우저 진입점을 `dist/src/app.js`로 변경
- `README.md`: 설치, 빌드, 실행, 검사, 파일 구성 갱신

### 이름 변경 후 타입화

- `src/depth.js` → `src/depth.ts`: 깊이 RGB 컬러맵 변환
- `src/player.js` → `src/player.ts`: 재생 상태와 프레임 시계
- `src/manifest.js` → `src/manifest.ts`: 외부 입력 검증과 내부 모델 정규화
- `src/preload.js` → `src/preload.ts`: 이미지 로딩, 진행률, 실패 수집
- `src/app.js` → `src/app.ts`: DOM 수집, 상태 표시, 캔버스 반영, 오류 안내
- `tests/depth.test.js` → `tests/depth.test.ts`
- `tests/player.test.js` → `tests/player.test.ts`
- `tests/manifest.test.js` → `tests/manifest.test.ts`
- `tests/preload.test.js` → `tests/preload.test.ts`
- `tests/app.test.js` → `tests/app.test.ts`

### 변경하지 않음

- `styles.css`: 현재 반응형 레이아웃과 포커스 표현 유지
- `scripts/generate_dummy_data.py`: Python 생성기 유지
- `tests/test_generate_dummy_data.py`: Python 테스트 유지

## Migration Invariant

전환 중 모든 커밋에서 `npm test`가 50개 Node 테스트를 실행해야 한다. 이를 위해 Task 1부터 Task 5까지는 아래 임시 설정을 사용한다.

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "NodeNext",
    "moduleResolution": "NodeNext",
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "strict": true,
    "useUnknownInCatchVariables": true,
    "noEmitOnError": true,
    "forceConsistentCasingInFileNames": true,
    "verbatimModuleSyntax": true,
    "rootDir": ".",
    "outDir": "dist",
    "allowJs": true,
    "checkJs": false,
    "types": ["node"]
  },
  "include": [
    "src/**/*.js",
    "src/**/*.ts",
    "tests/**/*.js",
    "tests/**/*.ts"
  ],
  "exclude": ["dist", "node_modules"]
}
```

Task 6에서 모든 JavaScript 이름 변경이 끝난 것을 확인한 뒤 `allowJs`와 JavaScript include를 제거한다. 임시 호환 코드는 Task 3과 Task 4가 끝날 때 각각 제거하며 최종 코드에 남기지 않는다.

## Spec Coverage

| 설계 영역 | 구현 작업 | 검증 |
| --- | --- | --- |
| 단일 `tsc` 빌드와 런타임 의존성 0개 | Task 1, Task 6 | `npm test`, `npm ls --omit=dev` |
| 외부 manifest `unknown` 검증과 내부 정규화 | Task 3 | manifest 테스트 15개, 부정 타입 계약 |
| 컬러 및 깊이 프레임 판별 타입 | Task 4 | 선로딩 테스트 5개, 부정 타입 계약 |
| 재생 속도와 내부 재생 상태 타입 | Task 2 | 재생 테스트 12개, `PlaybackSpeed` 계약 |
| 기존 boolean 콜백과 렌더 실패 중단 | Task 2, Task 5 | player 및 app 회귀 테스트 |
| DOM 요소, 화면 상태, 캔버스 오류 | Task 5 | app 테스트와 오류 원인 단언 |
| TypeScript 전용 최종 구성 | Task 6 | JavaScript 원본 부재, 생성물 문법 검사 |
| 브라우저, 반응형, 접근성 보존 | Task 7 | 정상 및 격리 오류 브라우저 흐름 |
| 독립 리뷰와 main 통합 | Task 8 | Critical 및 Important finding 0개, main 재검증 |

---

### Task 1: TypeScript 도구와 깊이 모듈 전환

**Files:**
- Create: `tsconfig.json`
- Create: `package-lock.json`
- Modify: `.gitignore`
- Modify: `package.json`
- Rename: `src/depth.js` → `src/depth.ts`
- Rename: `tests/depth.test.js` → `tests/depth.test.ts`

**Interfaces:**
- Produces: `mapDepthIntensity(value: unknown): Rgb`
- Produces: `colorizeDepthPixels(pixels: ArrayLike<number>): Uint8ClampedArray`
- Produces: `colorizeDepthPixelsInPlace(pixels: Uint8ClampedArray): Uint8ClampedArray`
- Preserves: 기존 컬러 정지점, 선형 보간, 알파 보존, 제자리 변환 동작

- [ ] **Step 1: 개발 도구와 빌드 스크립트 추가**

Run:

```powershell
npm install --save-dev --save-exact typescript@7.0.2 @types/node@24.13.3
```

`package.json`을 다음 최종 형태로 맞춘다.

```json
{
  "name": "image-sequence-viewer",
  "version": "1.0.0",
  "private": true,
  "type": "module",
  "scripts": {
    "build": "tsc -p tsconfig.json",
    "test": "npm run build && node --test \"dist/tests/*.test.js\""
  },
  "devDependencies": {
    "@types/node": "24.13.3",
    "typescript": "7.0.2"
  }
}
```

`.gitignore` 끝에 다음 두 줄을 추가하고 Migration Invariant의 임시 `tsconfig.json`을 생성한다.

```gitignore
/node_modules/
/dist/
```

- [ ] **Step 2: 깊이 파일을 이름 변경하고 컴파일 실패 확인**

Run:

```powershell
git mv src/depth.js src/depth.ts
git mv tests/depth.test.js tests/depth.test.ts
npm run build
```

Expected: `writeDepthColor`, `target`, `offset`, 공개 함수 매개변수의 암시적 `any` 오류로 실패한다. 다른 JavaScript 파일은 `allowJs`로 `dist`에 복사된다.

- [ ] **Step 3: 깊이 데이터 타입을 최소 범위로 구현**

`src/depth.ts` 상단과 함수 서명을 다음처럼 만든다. 기존 보간 본문과 오류 문구는 그대로 둔다.

```ts
export type Rgb = [red: number, green: number, blue: number];

type ColorStop = readonly [position: number, color: readonly [number, number, number]];
type WritablePixels = Uint8ClampedArray | number[];

const COLOR_STOPS: readonly ColorStop[] = [
  [0, [12, 35, 84]],
  [0.25, [0, 155, 207]],
  [0.5, [246, 220, 73]],
  [0.75, [245, 123, 32]],
  [1, [210, 35, 42]],
];

function writeDepthColor(value: unknown, target: WritablePixels, offset: number): void {
  const normalized = Math.min(Math.max(Number(value), 0), 255) / 255;

  for (let index = 1; index < COLOR_STOPS.length; index += 1) {
    const [upperPosition, upperColor] = COLOR_STOPS[index];
    if (normalized > upperPosition) continue;

    const [lowerPosition, lowerColor] = COLOR_STOPS[index - 1];
    const amount = (normalized - lowerPosition) / (upperPosition - lowerPosition);
    for (let channel = 0; channel < 3; channel += 1) {
      target[offset + channel] = Math.round(
        lowerColor[channel] + (upperColor[channel] - lowerColor[channel]) * amount,
      );
    }
    return;
  }

  const [, lastColor] = COLOR_STOPS[COLOR_STOPS.length - 1];
  for (let channel = 0; channel < 3; channel += 1) {
    target[offset + channel] = lastColor[channel];
  }
}

export function mapDepthIntensity(value: unknown): Rgb {
  const color: Rgb = [0, 0, 0];
  writeDepthColor(value, color, 0);
  return color;
}

export function colorizeDepthPixels(pixels: ArrayLike<number>): Uint8ClampedArray {
  return colorizeDepthPixelsInPlace(new Uint8ClampedArray(pixels));
}

export function colorizeDepthPixelsInPlace(
  pixels: Uint8ClampedArray,
): Uint8ClampedArray {
  if (pixels.length % 4 !== 0) {
    throw new RangeError("깊이 픽셀 데이터는 RGBA 네 채널 단위여야 합니다.");
  }

  for (let offset = 0; offset < pixels.length; offset += 4) {
    const intensity = (pixels[offset] + pixels[offset + 1] + pixels[offset + 2]) / 3;
    writeDepthColor(intensity, pixels, offset);
  }
  return pixels;
}
```

`tests/depth.test.ts`는 기존 단언을 그대로 유지한다. Node import 경로는 `../src/depth.js`를 유지한다.

- [ ] **Step 4: 깊이 테스트와 전체 회귀 검사 실행**

Run:

```powershell
npm run build
node --test "dist/tests/depth.test.js"
npm test
python -m unittest discover -s tests -p "test_*.py" -v
```

Expected: 깊이 테스트 8개, 전체 Node 테스트 50개, Python 테스트 4개 통과.

- [ ] **Step 5: 기반 구성 커밋**

Run:

```powershell
git add .gitignore package.json package-lock.json tsconfig.json src/depth.ts tests/depth.test.ts
git commit -m "build: TypeScript 점진 전환 기반 구성" -m "변경 이유: 모듈을 한 쌍씩 전환하면서도 전체 테스트를 유지할 컴파일 기반이 필요합니다." -m "핵심 변경: 단일 tsc 구성과 개발 의존성을 추가하고 깊이 모듈 및 테스트를 엄격한 타입으로 전환했습니다." -m "검증 방법: 깊이 테스트 8개, 전체 Node 테스트 50개, Python 테스트 4개를 실행했습니다."
```

---

### Task 2: 재생 상태와 시계 타입 전환

**Files:**
- Rename: `src/player.js` → `src/player.ts`
- Rename: `tests/player.test.js` → `tests/player.test.ts`

**Interfaces:**
- Produces: `PlaybackSpeed = 0.5 | 1 | 2`
- Produces: `PlaybackState = "paused" | "playing"`
- Produces: `SequencePlayerOptions`
- Preserves: `onPlayingChange(isPlaying: boolean)`
- Preserves: `onFrameChange(index)`에서 `false`만 실패로 처리
- Consumed later by: `src/app.ts`, `tests/types.contract.ts`

- [ ] **Step 1: 재생 파일을 이름 변경하고 타입 오류 확인**

Run:

```powershell
git mv src/player.js src/player.ts
git mv tests/player.test.js tests/player.test.ts
npm run build
```

Expected: 생성자 구조 분해 매개변수, 스케줄러 콜백, 테스트 배열과 override의 암시적 `any` 오류로 실패.

- [ ] **Step 2: 공개 타입과 내부 상태 구현**

`src/player.ts`에 다음 계약을 정의한다.

```ts
export type PlaybackSpeed = 0.5 | 1 | 2;
export type PlaybackState = "paused" | "playing";
export type FrameChangeHandler = (index: number) => boolean | void;
export type PlayingChangeHandler = (isPlaying: boolean) => void;
export type RequestFrame = (callback: FrameRequestCallback) => number;
export type CancelFrame = (id: number) => void;

export interface SequencePlayerOptions {
  frameCount: number;
  intervalMs: number;
  onFrameChange?: FrameChangeHandler;
  onPlayingChange?: PlayingChangeHandler;
  requestFrame?: RequestFrame;
  cancelFrame?: CancelFrame;
}

function isPlaybackSpeed(value: number): value is PlaybackSpeed {
  return value === 0.5 || value === 1 || value === 2;
}
```

`SequencePlayer`는 다음 상태 원칙을 사용한다.

```ts
export class SequencePlayer {
  readonly frameCount: number;
  readonly intervalMs: number;
  index = 0;
  speed: PlaybackSpeed = 1;
  private state: PlaybackState = "paused";
  private frameRequestId: number | null = null;
  private elapsedMs = 0;
  private lastTimestamp: number | null = null;

  get isPlaying(): boolean {
    return this.state === "playing";
  }

  private setPlaying(isPlaying: boolean): void {
    const nextState: PlaybackState = isPlaying ? "playing" : "paused";
    if (this.state === nextState) return;
    this.state = nextState;
    this.onPlayingChange(isPlaying);
  }
}
```

기존 재생, 일시정지, 탐색, 프레임 누적 계산을 유지한다. `setSpeed`는 `Number(speed)` 뒤 `isPlaybackSpeed`로 좁히고, `updateIndex`는 콜백 결과가 `false`인지 비교한다.

- [ ] **Step 3: 테스트 대역 타입화**

`tests/player.test.ts`에 테스트 전용 스케줄러와 override 타입을 명시한다.

```ts
import type { SequencePlayerOptions } from "../src/player.js";

interface TestScheduler {
  request: (callback: FrameRequestCallback) => number;
  cancel: (id: number) => void;
  run: (timestamp: number) => void;
  readonly pendingCount: number;
}

function createPlayer(overrides: Partial<SequencePlayerOptions> = {}) {
  const scheduler = createScheduler();
  const frameChanges: number[] = [];
  const playingChanges: boolean[] = [];
  const player = new SequencePlayer({
    frameCount: 3,
    intervalMs: 100,
    onFrameChange: (index) => {
      frameChanges.push(index);
    },
    onPlayingChange: (isPlaying) => {
      playingChanges.push(isPlaying);
    },
    requestFrame: scheduler.request,
    cancelFrame: scheduler.cancel,
    ...overrides,
  });
  return { player, scheduler, frameChanges, playingChanges };
}
```

배열 `push` 값을 콜백 결과로 반환하지 않도록 중괄호 본문을 사용한다.

```ts
onFrameChange: (index) => {
  frameChanges.push(index);
},
onPlayingChange: (isPlaying) => {
  playingChanges.push(isPlaying);
},
```

- [ ] **Step 4: 재생 테스트와 전체 회귀 검사 실행**

Run:

```powershell
npm run build
node --test "dist/tests/player.test.js"
npm test
```

Expected: 재생 테스트 12개와 전체 Node 테스트 50개 통과. 상태 변경 단언은 기존 `[true, false]` 값을 유지.

- [ ] **Step 5: 재생 상태 커밋**

Run:

```powershell
git add src/player.ts tests/player.test.ts
git commit -m "refactor: 재생 상태 타입 전환" -m "변경 이유: 재생 속도와 내부 상태를 제한된 타입으로 표현하면서 기존 호출 계약을 보존해야 합니다." -m "핵심 변경: PlaybackSpeed와 PlaybackState를 도입하고 boolean 콜백 및 프레임 실패 계약을 유지했습니다." -m "검증 방법: 재생 테스트 12개와 전체 Node 테스트 50개를 실행했습니다."
```

---

### Task 3: manifest 검증 경계와 내부 모델 정규화

**Files:**
- Rename: `src/manifest.js` → `src/manifest.ts`
- Rename: `tests/manifest.test.js` → `tests/manifest.test.ts`
- Modify temporarily: `src/app.js`

**Interfaces:**
- Produces: `FrameKind`, `FrameSource`, `SequenceFrame`, `SequenceManifest`
- Produces: `validateManifest(value: unknown, manifestUrl: string): SequenceManifest`
- Produces: `loadManifest(...): Promise<SequenceManifest>`
- Consumed by: `src/preload.ts`, `src/app.ts`, `tests/types.contract.ts`

- [ ] **Step 1: 정규화 결과를 요구하는 테스트 작성과 이름 변경**

Run:

```powershell
git mv src/manifest.js src/manifest.ts
git mv tests/manifest.test.js tests/manifest.test.ts
```

`tests/manifest.test.ts`의 첫 검증을 다음 camel case 결과로 바꾼다. 외부 fixture는 snake case를 유지한다.

```ts
const result = validateManifest(validManifest(), MANIFEST_URL);

assert.equal(result.frameCount, 2);
assert.equal(result.intervalMs, 100);
assert.deepEqual(result.depthRange, { nearCm: 0, farCm: 5000 });
assert.equal(result.frames[0]?.color.sourcePath, "color_000000.png");
assert.equal(
  result.frames[0]?.color.url,
  "https://example.test/sequences/color_000000.png",
);
assert.equal(result.frames[1]?.depth.kind, "depth");
```

Run:

```powershell
npm run build
```

Expected: manifest 함수 매개변수의 암시적 `any`와 snake case 반환 타입 불일치로 실패.

- [ ] **Step 2: 외부 입력 가드와 내부 타입 구현**

`src/manifest.ts`에 다음 모델과 가드를 정의한다.

```ts
type UnknownRecord = Record<string, unknown>;

export type FrameKind = "color" | "depth";

export interface FrameSource<TKind extends FrameKind> {
  kind: TKind;
  sourcePath: string;
  url: string;
}

export interface SequenceFrame {
  index: number;
  timeMs: number;
  color: FrameSource<"color">;
  depth: FrameSource<"depth">;
}

export interface SequenceManifest {
  frameCount: number;
  intervalMs: number;
  depthRange: { nearCm: number; farCm: number };
  frames: readonly SequenceFrame[];
}

function requireRecord(value: unknown, field: string): asserts value is UnknownRecord {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new ManifestError(`${field}은 객체여야 합니다.`);
  }
}

function requirePositiveInteger(value: unknown, field: string): asserts value is number {
  if (typeof value !== "number" || !Number.isInteger(value) || value < 1) {
    throw new ManifestError(`${field}은 1 이상의 정수여야 합니다.`);
  }
}
```

`requireFiniteNumber`는 `unknown`을 `number`로 좁히고, `requireNonEmptyString`은 trim한 `string`을 반환한다. `validateManifest`는 기존 검사를 모두 통과한 뒤 다음 형태만 반환한다.

```ts
return {
  frameCount: value.frame_count,
  intervalMs: value.interval_ms,
  depthRange: {
    nearCm: value.depth_near_cm,
    farCm: value.depth_far_cm,
  },
  frames,
};
```

각 `frames` 항목은 spread로 원본 필드를 복사하지 않고 다음처럼 새로 만든다.

```ts
return {
  index: position,
  timeMs: frame.time_ms,
  color: {
    kind: "color",
    sourcePath: color,
    url: resolveFrameUrl(color, manifestUrl, `${field}.color`),
  },
  depth: {
    kind: "depth",
    sourcePath: depth,
    url: resolveFrameUrl(depth, manifestUrl, `${field}.depth`),
  },
};
```

`ManifestError`는 `constructor(message: string, options: ErrorOptions = {})`를 사용한다. fetch 대역은 최소 응답 계약을 가진 `ManifestFetch`로 정의해 테스트가 전체 `Response`를 흉내 내지 않게 한다.

```ts
interface ManifestResponse {
  ok: boolean;
  status?: number;
  url?: string;
  json(): Promise<unknown>;
}

type ManifestFetch = (
  url: string,
  init: { cache: "no-store"; signal: AbortSignal },
) => Promise<ManifestResponse>;

interface LoadManifestOptions {
  timeoutMs?: number;
}

const defaultManifestFetch: ManifestFetch = async (url, init) => {
  return globalThis.fetch(url, init);
};

export declare function loadManifest(
  url?: string,
  fetchImpl?: ManifestFetch,
  options?: LoadManifestOptions,
): Promise<SequenceManifest>;
```

시간 제한 Promise는 `Promise<never>`로, 요청 Promise는 `{ response: ManifestResponse; value: unknown }`으로 타입화한다. `catch`에서 이름을 읽을 때는 `cause instanceof Error`를 확인한다.

- [ ] **Step 3: 아직 JavaScript인 앱에 짧은 전환 어댑터 추가**

`src/app.js`가 정규화된 manifest를 읽되 기존 `preload.js` 입력을 계속 만들도록 다음 세 위치를 바꾼다.

```js
const imageCount = this.manifest.frameCount * 2;
const preloadInput = this.manifest.frames.map((frame) => ({
  index: frame.index,
  time_ms: frame.timeMs,
  colorUrl: frame.color.url,
  depthUrl: frame.depth.url,
}));

this.frames = await preloadFrames(preloadInput, {
  onProgress: ({ completed, total, percent }) => {
    this.updateLoading({ percent, label: `이미지 ${completed} / ${total} 불러오는 중` });
  },
});
```

`configurePlayer`에서는 `intervalMs: this.manifest.intervalMs`를 사용한다. 이 어댑터는 Task 4에서 제거한다.

- [ ] **Step 4: manifest와 전체 회귀 검사 실행**

Run:

```powershell
npm run build
node --test "dist/tests/manifest.test.js"
npm test
```

Expected: manifest 테스트 15개와 전체 Node 테스트 50개 통과. HTTP, JSON, 동일 출처, 전체 제한 시간 오류 원인 유지.

- [ ] **Step 5: manifest 정규화 커밋**

Run:

```powershell
git add src/manifest.ts tests/manifest.test.ts src/app.js
git commit -m "refactor: manifest 경계 타입 정규화" -m "변경 이유: 외부 JSON을 검증 전 unknown으로 격리하고 내부 모듈에 안정된 모델을 제공해야 합니다." -m "핵심 변경: snake case 입력을 중첩된 camel case 모델로 정규화하고 기존 앱을 위한 짧은 전환 어댑터를 추가했습니다." -m "검증 방법: manifest 테스트 15개와 전체 Node 테스트 50개를 실행했습니다."
```

---

### Task 4: 컬러와 깊이 선로딩 타입 전환

**Files:**
- Rename: `src/preload.js` → `src/preload.ts`
- Rename: `tests/preload.test.js` → `tests/preload.test.ts`
- Modify: `src/app.js`

**Interfaces:**
- Consumes: `readonly SequenceFrame[]`
- Produces: `ColorFrameData<TImage>`, `DepthFrameData<TImage>`, `PreloadedFrame<TImage>`
- Produces: `FrameAssetRequest`, `PreloadProgress`, `FrameLoadFailure`
- Produces: 성공과 실패를 `ok`로 구분하는 `AssetLoadResult<TImage>`
- Preserves: 전체 선로딩, 실패 수집, 이벤트 정리, 요청 제한 시간, 지연 이벤트 무효화

- [ ] **Step 1: 판별 가능한 결과를 요구하는 테스트 작성과 이름 변경**

Run:

```powershell
git mv src/preload.js src/preload.ts
git mv tests/preload.test.js tests/preload.test.ts
```

`tests/preload.test.ts`의 fixture를 정규화된 `SequenceFrame[]`로 바꾸고 다음 결과를 단언한다.

```ts
assert.equal(result[0]?.color.kind, "color");
assert.equal(result[0]?.color.sourcePath, "color_000000.png");
assert.equal(result[0]?.color.image.source, "https://example.test/color_000000.png");
assert.equal(result[0]?.depth.kind, "depth");
assert.equal(result[1]?.depth.image.source, "https://example.test/depth_000001.png");
```

Run:

```powershell
npm run build
```

Expected: loader, 이미지 생성자, 진행률 콜백, 결과 합집합의 타입 오류로 실패.

- [ ] **Step 2: 이미지 경계와 선로딩 결과 타입 구현**

`src/preload.ts`에 다음 공개 타입을 정의한다.

```ts
import type { FrameKind, SequenceFrame } from "./manifest.js";

export interface ColorFrameData<TImage = HTMLImageElement> {
  kind: "color";
  sourcePath: string;
  url: string;
  image: TImage;
}

export interface DepthFrameData<TImage = HTMLImageElement> {
  kind: "depth";
  sourcePath: string;
  url: string;
  image: TImage;
}

export interface PreloadedFrame<TImage = HTMLImageElement> {
  index: number;
  timeMs: number;
  color: ColorFrameData<TImage>;
  depth: DepthFrameData<TImage>;
}

export interface FrameAssetRequest {
  framePosition: number;
  frameIndex: number;
  kind: FrameKind;
  sourcePath: string;
  url: string;
}

type AssetLoadResult<TImage> =
  | { ok: true; request: FrameAssetRequest; image: TImage }
  | { ok: false; request: FrameAssetRequest; cause: unknown };

export interface PreloadProgress {
  completed: number;
  total: number;
  percent: number;
}

export interface FrameLoadFailure {
  frameIndex: number;
  kind: FrameKind;
  url: string;
  cause: unknown;
}
```

이미지 대역은 전체 DOM 객체 대신 기존 코드가 쓰는 속성만 가진다.

```ts
export interface LoadableImage {
  decoding: "sync" | "async" | "auto";
  onload: ((event: Event) => void) | null;
  onerror: ((event: Event) => void) | null;
  src: string;
  removeAttribute?(name: string): void;
}

export type ImageConstructor<TImage extends LoadableImage> = new () => TImage;
export type ImageLoader<TImage extends LoadableImage> = (
  url: string,
  ImageType?: ImageConstructor<TImage>,
  options?: { timeoutMs?: number },
) => Promise<TImage>;

export declare function loadImage<TImage extends LoadableImage = HTMLImageElement>(
  url: string,
  ImageType?: ImageConstructor<TImage>,
  options?: { timeoutMs?: number },
): Promise<TImage>;

export class FrameLoadError extends Error {
  readonly failures: readonly FrameLoadFailure[];

  constructor(failures: readonly FrameLoadFailure[]) {
    super(`프레임 이미지 ${failures.length}개를 불러오지 못했습니다.`);
    this.name = "FrameLoadError";
    this.failures = failures;
  }
}

interface PreloadOptions<TImage extends LoadableImage> {
  imageLoader?: ImageLoader<TImage>;
  ImageConstructor?: ImageConstructor<TImage>;
  imageTimeoutMs?: number;
  onProgress?: (progress: PreloadProgress) => void;
}

export declare function preloadFrames<
  TImage extends LoadableImage = HTMLImageElement,
>(
  frames: readonly SequenceFrame[],
  options?: PreloadOptions<TImage>,
): Promise<readonly PreloadedFrame<TImage>[]>;
```

`loadImage`와 `preloadFrames`를 제네릭으로 구현하고 기본 제품 타입은 `HTMLImageElement`로 둔다. 이미지 이벤트를 정리한 뒤에만 resolve 또는 reject하고, 시간 초과 시 `removeAttribute("src")`를 호출한다.

기본 브라우저 생성자를 제네릭 생성자로 연결하는 한 지점에서만 `globalThis.Image as ImageConstructor<TImage>`를 사용한다. 테스트의 `HangingImage`와 `LoadedImage`는 `LoadableImage`를 구현하고 늦은 이벤트 호출에는 `new Event("load")`를 전달한다.

자산 배열은 `frame.color`와 `frame.depth`에서 만들고 결과는 `ok` 필드로 분기한다. 실패 목록은 실패 variant에서만 만들며, 성공 결과 두 개를 같은 framePosition에 결합해 `PreloadedFrame<TImage>`를 반환한다.

- [ ] **Step 3: 앱의 임시 어댑터 제거와 새 결과 연결**

`src/app.js`에서 Task 3의 `preloadInput` 매핑을 삭제하고 manifest 프레임을 직접 전달한다.

```js
this.frames = await preloadFrames(this.manifest.frames, {
  onProgress: ({ completed, total, percent }) => {
    this.updateLoading({ percent, label: `이미지 ${completed} / ${total} 불러오는 중` });
  },
});
```

`renderFrame`은 다음 필드를 사용한다.

```js
this.prepareColorFrame(index, frame.color.image);
this.prepareDepthFrame(index, frame.depth.image);
const elapsed = formatElapsed(frame.timeMs);
```

- [ ] **Step 4: 선로딩과 전체 회귀 검사 실행**

Run:

```powershell
npm run build
node --test "dist/tests/preload.test.js"
npm test
```

Expected: 선로딩 테스트 5개와 전체 Node 테스트 50개 통과. 시간 초과 뒤 핸들러가 null이고 늦은 load 이벤트가 상태를 바꾸지 않음.

- [ ] **Step 5: 선로딩 타입 커밋**

Run:

```powershell
git add src/preload.ts tests/preload.test.ts src/app.js
git commit -m "refactor: 프레임 선로딩 타입 전환" -m "변경 이유: 컬러와 깊이 자산 및 실패 원인을 컴파일 단계에서 구분해야 합니다." -m "핵심 변경: 제네릭 이미지 경계와 판별 가능한 로딩 결과를 도입하고 앱의 임시 manifest 어댑터를 제거했습니다." -m "검증 방법: 선로딩 테스트 5개와 전체 Node 테스트 50개를 실행했습니다."
```

---

### Task 5: 앱과 DOM 경계 타입 전환

**Files:**
- Rename: `src/app.js` → `src/app.ts`
- Rename: `tests/app.test.js` → `tests/app.test.ts`

**Interfaces:**
- Consumes: `SequenceManifest`, `PreloadedFrame<HTMLImageElement>`, `SequencePlayer`
- Produces: `DepthDisplayMode`, `ViewerState`, `FrameRenderError`
- Produces: `startViewer(root: Document = document): Promise<ViewerController>`
- Preserves: 캔버스 원자적 준비, 반영 실패 정리, 오류 원인 정규화, boolean 재생 콜백

- [ ] **Step 1: 앱 파일을 이름 변경하고 접근성 회귀 단언 추가**

Run:

```powershell
git mv src/app.js src/app.ts
git mv tests/app.test.js tests/app.test.ts
```

`tests/app.test.ts`에 다음 단언을 기존 시작 성공 및 오류 테스트에 추가한다.

```ts
assert.equal(root.getElementById("play-toggle")?.attributes.get("aria-label"), undefined);
viewer.updatePlayingState(true);
assert.equal(root.getElementById("play-toggle")?.attributes.get("aria-label"), "일시정지");
viewer.updatePlayingState(false);
assert.equal(root.getElementById("play-toggle")?.attributes.get("aria-label"), "재생");
```

깊이 버튼 listener를 호출한 뒤 `aria-pressed`가 `true`가 되고, 렌더 오류 뒤 모든 `data-viewer-control` 요소가 disabled인지 단언한다. 정적 HTML의 레이블, role, live 속성은 Task 7 브라우저 검사에서 확인한다.

Run:

```powershell
npm run build
```

Expected: DOM 요소, 이벤트, 캔버스 컨텍스트, 오류 원인, fixture의 암시적 `any` 오류로 실패.

- [ ] **Step 2: 앱 상태와 DOM 요소 집합 구현**

`src/app.ts`에 다음 상태와 표시 매핑을 정의한다.

```ts
import type { FrameKind, SequenceManifest } from "./manifest.js";
import type { PreloadedFrame, PreloadProgress } from "./preload.js";

export type DepthDisplayMode = "grayscale" | "colormap";
export type ViewerState = "loading" | "ready" | "playing" | "paused" | "error";

const VIEWER_STATE_LABELS: Record<ViewerState, string> = {
  loading: "로딩 중",
  ready: "준비됨",
  playing: "재생 중",
  paused: "일시정지",
  error: "오류",
};
```

`ViewerElements`에는 현재 20개 요소를 모두 정확한 DOM 타입으로 넣는다.

```ts
interface ViewerElements {
  colorCanvas: HTMLCanvasElement;
  depthCanvas: HTMLCanvasElement;
  depthMode: HTMLButtonElement;
  depthModeBadge: HTMLSpanElement;
  frameReadout: HTMLOutputElement;
  frameSlider: HTMLInputElement;
  loadingLabel: HTMLSpanElement;
  loadingPercent: HTMLOutputElement;
  loadingProgress: HTMLProgressElement;
  nextFrame: HTMLButtonElement;
  playIcon: HTMLSpanElement;
  playLabel: HTMLSpanElement;
  playToggle: HTMLButtonElement;
  playbackSpeed: HTMLSelectElement;
  previousFrame: HTMLButtonElement;
  restart: HTMLButtonElement;
  timeReadout: HTMLOutputElement;
  viewerError: HTMLElement;
  viewerErrorMessage: HTMLParagraphElement;
  viewerState: HTMLDivElement;
}
```

`collectElements(root: Document): ViewerElements`는 각 ID의 존재를 확인한 뒤 object literal로 camel case 필드에 배치한다. 각 요소의 HTML 계약은 `index.html`이 소유하므로, 존재 확인 뒤 해당 구체 타입으로 제한된 단언을 사용한다. 문자열 인덱스 기반 `Object` 접근은 이후 코드에서 제거한다.

`ViewerController`의 상태를 다음처럼 선언한다.

```ts
class ViewerController {
  readonly root: Document;
  readonly elements: ViewerElements;
  readonly colorBufferCanvas: HTMLCanvasElement;
  readonly depthBufferCanvas: HTMLCanvasElement;
  manifest: SequenceManifest | null = null;
  frames: readonly PreloadedFrame[] = [];
  player: SequencePlayer | null = null;
  depthMode: DepthDisplayMode = "grayscale";
}
```

`setViewerState(state: ViewerState)`는 `VIEWER_STATE_LABELS[state]`를 표시한다. `updatePlayingState(isPlaying: boolean)`는 기존 ternary를 유지한다. `FrameRenderError`는 `frameIndex: number`, `kind: FrameKind`, `cause: unknown`을 보존하고 `describeViewerError(error: unknown): string`은 `instanceof` 또는 작은 이름 검사 헬퍼를 거친다.

이벤트의 `currentTarget`은 각 요소를 닫아 두는 callback에서 직접 읽어 별도 넓은 Event 단언을 피한다. `setControlsEnabled`는 다음 선택자를 사용한다.

```ts
const controls = this.root.querySelectorAll<
  HTMLButtonElement | HTMLInputElement | HTMLSelectElement
>("[data-viewer-control]");
```

- [ ] **Step 3: 테스트 fixture를 단일 DOM 경계에서 타입화**

`tests/app.test.ts`의 fake 클래스에는 실제 사용하는 필드와 메서드 타입만 선언한다. `startViewer` 호출을 다음 helper 하나로 모은다.

```ts
async function startTestViewer(root: FakeRoot) {
  return startViewer(root as unknown as Document);
}
```

`LoadedImage`는 `LoadableImage`를 구현하고, 전역 생성자 대입은 테스트 경계에서만 다음 단언을 사용한다.

```ts
globalThis.Image = LoadedImage as unknown as typeof Image;
```

fetch 대역도 같은 fixture 설정 지점에서만 `fakeFetch as unknown as typeof fetch`로 연결하고 정리할 때는 `Reflect.deleteProperty(globalThis, "Image")`를 사용한다. 캔버스 전용 단언은 `getFakeCanvas(root, id): FakeCanvas` helper가 `instanceof FakeCanvas`를 확인한 뒤 반환하도록 모은다.

Node의 `TestContext`와 오류 배열을 명시한다.

```ts
import test, { type TestContext } from "node:test";

async function createStartedViewer(t: TestContext) {
  const originalConsoleError = console.error;
  const reportedErrors: unknown[] = [];
  const root = createFakeRoot();
  console.error = (...data: unknown[]) => {
    reportedErrors.push(data[1]);
  };
  const viewer = await startTestViewer(root);
  t.after(() => {
    console.error = originalConsoleError;
  });
  return { reportedErrors, root, viewer };
}
```

- [ ] **Step 4: 앱과 전체 회귀 검사 실행**

Run:

```powershell
npm run build
node --test "dist/tests/app.test.js"
npm test
```

Expected: 앱 테스트 10개 이상과 전체 기존 Node 테스트 50개 이상 통과. 새 단언은 재생 접근 가능한 이름, 깊이 pressed 상태, 오류 후 disabled 상태를 확인.

- [ ] **Step 5: 앱 타입 커밋**

Run:

```powershell
git add src/app.ts tests/app.test.ts
git commit -m "refactor: 뷰어 DOM 타입 전환" -m "변경 이유: DOM과 캔버스 오류 경계를 명시해 브라우저 상태 변경의 타입 안전성을 확보해야 합니다." -m "핵심 변경: ViewerElements와 화면 상태 매핑을 도입하고 테스트 fixture 단언을 단일 경계로 제한했습니다." -m "검증 방법: 앱 테스트와 전체 Node 테스트를 실행하고 접근성 상태 단언을 확인했습니다."
```

---

### Task 6: TypeScript 전용 구성과 부정 타입 계약 확정

**Files:**
- Create: `tests/types.contract.ts`
- Modify: `tsconfig.json`

**Interfaces:**
- Consumes: `ColorFrameData`, `DepthFrameData`, `PlaybackSpeed`, `SequenceManifest`
- Verifies: 컬러와 깊이 혼용, 미지원 속도, 검증 전 manifest 사용이 컴파일 오류
- Produces: JavaScript 입력을 허용하지 않는 최종 `tsconfig.json`

- [ ] **Step 1: 오류가 나야 하는 타입 조합 작성**

`tests/types.contract.ts`를 먼저 주석 없이 다음처럼 작성한다.

```ts
import type { SequenceManifest } from "../src/manifest.js";
import type { PlaybackSpeed } from "../src/player.js";
import type { ColorFrameData, DepthFrameData } from "../src/preload.js";

function verifyTypeContracts(color: ColorFrameData, value: unknown): void {
  const depth: DepthFrameData = color;
  const speed: PlaybackSpeed = 1.5;
  const manifest: SequenceManifest = value;
  void depth;
  void speed;
  void manifest;
}

void verifyTypeContracts;
```

Run:

```powershell
npm run build
```

Expected: 세 대입 각각에서 타입 오류가 발생.

- [ ] **Step 2: 예상 오류를 컴파일 계약으로 고정**

세 잘못된 대입 바로 위에 설명이 있는 `@ts-expect-error`를 추가한다.

```ts
// @ts-expect-error 컬러 프레임은 깊이 프레임으로 사용할 수 없다.
const depth: DepthFrameData = color;
// @ts-expect-error 지원하지 않는 재생 속도다.
const speed: PlaybackSpeed = 1.5;
// @ts-expect-error 검증 전 unknown은 내부 manifest가 아니다.
const manifest: SequenceManifest = value;
```

Run:

```powershell
npm run build
```

Expected: 성공. 타입이 느슨해지면 `Unused '@ts-expect-error' directive`로 실패해야 함.

- [ ] **Step 3: 최종 TypeScript 전용 설정으로 축소**

먼저 JavaScript 소스와 테스트가 남지 않았는지 확인한다.

Run:

```powershell
rg --files src tests
```

Expected: `src`와 Node 테스트에는 `.ts`만 있고 Python 파일만 별도로 남음.

`tsconfig.json`을 다음 최종 형태로 교체한다.

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "NodeNext",
    "moduleResolution": "NodeNext",
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "strict": true,
    "useUnknownInCatchVariables": true,
    "noEmitOnError": true,
    "forceConsistentCasingInFileNames": true,
    "verbatimModuleSyntax": true,
    "rootDir": ".",
    "outDir": "dist",
    "types": ["node"]
  },
  "include": ["src/**/*.ts", "tests/**/*.ts"],
  "exclude": ["dist", "node_modules"]
}
```

- [ ] **Step 4: 컴파일 산출물과 런타임 의존성 검사**

Run:

```powershell
npm test
npm ls --omit=dev --depth=0
Get-ChildItem dist -Recurse -Filter *.js | ForEach-Object { node --check $_.FullName; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
python -m unittest discover -s tests -p "test_*.py" -v
python -m compileall scripts tests
git status --short
```

Expected: Node 테스트 50개 이상, Python 테스트 4개 통과. `npm ls --omit=dev`에 런타임 패키지가 없고 모든 생성 JavaScript가 문법 검사를 통과. `dist`와 `node_modules`는 status에 나타나지 않음.

- [ ] **Step 5: 타입 계약 커밋**

Run:

```powershell
git add tsconfig.json tests/types.contract.ts
git commit -m "test: TypeScript 계약 검증 확정" -m "변경 이유: 잘못된 프레임 조합과 검증 전 데이터 사용을 컴파일 단계에서 계속 차단해야 합니다." -m "핵심 변경: 부정 타입 계약을 추가하고 임시 JavaScript 허용을 제거해 최종 TypeScript 전용 구성을 확정했습니다." -m "검증 방법: 전체 Node 및 Python 테스트, 생성 JavaScript 문법 검사, 런타임 의존성 검사를 실행했습니다."
```

---

### Task 7: 브라우저 진입점과 사용 문서 전환

**Files:**
- Modify: `index.html`
- Modify: `README.md`

**Interfaces:**
- Consumes: `dist/src/app.js`
- Produces: `npm ci` → `npm run build` → 정적 서버 실행 절차
- Preserves: 기존 UI, 반응형 전환, 키보드와 보조 기술 상태

- [ ] **Step 1: 현재 진입점 실패 확인**

JavaScript 원본이 모두 제거된 상태에서 빌드와 서버를 실행한다.

Run:

```powershell
npm run build
python -m http.server 8000
```

브라우저에서 `http://localhost:8000`을 연다.

Expected: 현재 `index.html`이 `./src/app.js`를 요청하므로 모듈 404가 발생. 확인 후 서버는 `Ctrl+C`로 중지.

- [ ] **Step 2: 컴파일된 브라우저 진입점 지정**

`index.html`의 script 한 줄만 바꾼다.

```html
<script type="module" src="./dist/src/app.js"></script>
```

다른 HTML 구조, 레이블, role, aria 속성은 변경하지 않는다.

- [ ] **Step 3: README 설치와 실행 절차 갱신**

README의 빠른 실행을 다음 순서로 바꾼다.

```powershell
npm ci
npm run build
python scripts/generate_dummy_data.py
python -m http.server 8000
```

자동 검사는 다음처럼 안내한다.

```powershell
npm test
python -m unittest discover -s tests -p "test_*.py" -v
```

외부 런타임 라이브러리와 번들러는 없고 TypeScript는 개발 시 컴파일에만 사용한다고 명시한다. 파일 구성의 `src/*.js`와 `tests/*.test.js` 표기를 `.ts`로 바꾸고 `tsconfig.json`, `package-lock.json`, `dist`의 생성 및 비추적 성격을 설명한다.

- [ ] **Step 4: 정상 브라우저 흐름과 접근성 확인**

Run:

```powershell
npm ci
npm run build
python scripts/generate_dummy_data.py
python -m http.server 8000
```

브라우저에서 다음을 확인한다.

- 1440픽셀과 390픽셀에서 가로 넘침이 없고 760픽셀 이하에서 캔버스가 한 열로 배치됨
- manifest와 60개 이미지가 로딩되고 진행률이 100퍼센트가 됨
- 이전, 다음, 처음으로, 슬라이더, 0.5x, 1x, 2x, 재생과 일시정지가 기존과 동일함
- 깊이 컬러맵 전환 시 배지, 버튼 문구, `aria-pressed`가 함께 바뀜
- 키보드만으로 모든 컨트롤을 이동 및 실행할 수 있고 focus-visible 윤곽이 보임
- 재생 버튼의 접근 가능한 이름, live 상태, 오류 alert 구조가 유지됨
- 정상 흐름에서 콘솔 warning과 error가 0개임

- [ ] **Step 5: 격리된 오류 브라우저 흐름 확인**

저장소 파일을 바꾸지 않고 임시 QA 루트에 `index.html`, `styles.css`, `dist`만 복사해 manifest가 없는 서버를 8001 포트로 연다. 임시 경로는 저장소 밖의 운영체제 임시 폴더를 사용한다.

Run:

```powershell
$viewerQaRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("image-sequence-viewer-qa-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $viewerQaRoot
Copy-Item -LiteralPath index.html -Destination $viewerQaRoot
Copy-Item -LiteralPath styles.css -Destination $viewerQaRoot
Copy-Item -LiteralPath dist -Destination $viewerQaRoot -Recurse
python -m http.server 8001 --directory $viewerQaRoot
```

브라우저에서 `http://localhost:8001`을 확인한 뒤 `Ctrl+C`로 서버를 중지한다.

Expected: 오류 카드가 manifest 확인 안내를 표시하고 컨트롤은 비활성화됨. 오류 경로의 `console.error` 한 건은 현재 계약이므로 허용하되 추가 예외는 없어야 함.

- [ ] **Step 6: 전체 검사와 문서 커밋**

Run:

```powershell
npm test
python -m unittest discover -s tests -p "test_*.py" -v
python -m compileall scripts tests
npm ls --omit=dev --depth=0
git diff --check
git status --short
```

Run:

```powershell
git add index.html README.md
git commit -m "build: TypeScript 브라우저 실행 전환" -m "변경 이유: 브라우저가 컴파일된 ES 모듈을 사용하고 사용자가 빌드 절차를 재현할 수 있어야 합니다." -m "핵심 변경: 진입점을 dist 출력으로 바꾸고 설치, 빌드, 실행, 검사 문서를 갱신했습니다." -m "검증 방법: 전체 자동 검사와 데스크톱 및 390픽셀 브라우저 흐름, 접근성 상태를 확인했습니다."
```

---

### Task 8: 독립 코드 리뷰, 최종 검증, main 통합

**Files:**
- Review: `main...feat/typescript-web-viewer` 전체 diff
- Modify only if findings require: finding이 지목한 파일과 대응 테스트
- Merge target: `main`

**Interfaces:**
- Consumes: Task 1부터 Task 7까지의 커밋
- Produces: 독립 리뷰에서 Critical 및 Important finding 0개인 통합 후보
- Produces: 전체 검증을 통과한 `main` 병합 커밋

- [ ] **Step 1: 구현 범위와 이력 점검**

Run:

```powershell
git status --short --branch
git log --oneline main..feat/typescript-web-viewer
git diff --stat main...feat/typescript-web-viewer
git diff --check main...feat/typescript-web-viewer
```

Expected: 작업 트리가 깨끗하고 계획된 파일만 변경됨. 생성된 `dist`, `node_modules`, 대외비 식별자는 diff와 이력에 없음.

- [ ] **Step 2: 독립 코드 리뷰 요청**

Use: `superpowers:requesting-code-review`

리뷰 범위는 `main...feat/typescript-web-viewer`이며 다음을 우선 확인한다.

- 외부 `unknown` 입력이 검증 전에 내부 타입으로 단언되지 않는지
- 컬러와 깊이 판별 필드가 모든 선로딩 및 렌더 경로에서 유지되는지
- boolean 재생 콜백과 실패 시 재생 중단 계약이 보존되는지
- manifest 전체 제한 시간과 이미지 이벤트 정리가 회귀하지 않는지
- 캔버스 준비 및 반영 실패의 일관 상태가 유지되는지
- 제품 코드에 `any`, 런타임 의존성, 불필요한 추상화가 없는지
- 컴파일 계약과 동작 테스트가 실제 실패 조건을 검증하는지

Expected: 리뷰 결과를 Critical, Important, Minor로 구분하고 파일과 근거를 제시.

- [ ] **Step 3: 리뷰 finding 처리**

Use: `superpowers:receiving-code-review`

각 finding을 재현하거나 코드 근거로 검증한다. 유효한 finding은 먼저 실패 테스트나 컴파일 오류를 추가하고 최소 수정 뒤 관련 테스트와 전체 `npm test`를 실행한다. finding별 수정은 허용 접두사와 한글 명사형 제목, 세 본문 항목을 사용한 작은 커밋으로 남긴다. 근거가 없는 제안은 적용하지 않고 이유를 리뷰 기록에 남긴다.

- [ ] **Step 4: 최종 자동 및 브라우저 검증**

Run:

```powershell
npm ci
npm test
python -m unittest discover -s tests -p "test_*.py" -v
python -m compileall scripts tests
npm ls --omit=dev --depth=0
Get-ChildItem dist -Recurse -Filter *.js | ForEach-Object { node --check $_.FullName; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
git diff --check main...feat/typescript-web-viewer
git status --short --branch
```

Task 7의 정상 및 오류 브라우저 흐름을 다시 실행한다.

Expected: 모든 검사 통과, 런타임 의존성 0개, 콘솔 회귀 없음, 독립 리뷰의 Critical 및 Important finding 0개.

- [ ] **Step 5: feature 브랜치 공유와 main 병합**

사용자 승인 후에만 다음을 실행한다.

Use: `superpowers:finishing-a-development-branch`

```powershell
git push -u origin feat/typescript-web-viewer
git switch main
git pull --ff-only origin main
git merge --no-ff feat/typescript-web-viewer -m "feat: TypeScript 웹 뷰어 전환" -m "변경 이유: 정적 뷰어의 데이터와 상태 경계를 컴파일 단계에서 검증할 수 있어야 합니다." -m "핵심 변경: 단일 tsc 빌드, 명시적 프레임 및 재생 타입, 컴파일 계약, 기존 브라우저 동작 보존을 통합했습니다." -m "검증 방법: 전체 Node 및 Python 테스트, 생성 JavaScript 검사, 브라우저와 접근성 흐름, 독립 코드 리뷰를 완료했습니다."
```

병합 뒤 Task 8 Step 4의 전체 검증을 `main`에서 다시 실행하고 성공한 경우에만 `git push origin main`을 수행한다.

Expected: 로컬과 원격 `main`이 동일한 병합 커밋을 가리키고 작업 트리가 깨끗함.

---

## Final Acceptance Checklist

- [ ] `src` 제품 모듈 5개와 Node 테스트 5개가 TypeScript로 전환됨
- [ ] `tests/types.contract.ts`의 세 부정 계약이 유효함
- [ ] 최종 `tsconfig.json`이 TypeScript 파일만 포함함
- [ ] `package.json`의 `dependencies`가 없고 개발 의존성이 정확히 2개임
- [ ] `npm test`가 기존 50개 이상 동작 테스트를 통과함
- [ ] Python 테스트 4개와 compileall이 통과함
- [ ] 생성 JavaScript 전체가 `node --check`를 통과함
- [ ] manifest 외부 형식과 Python 생성 결과가 변경되지 않음
- [ ] 컬러, 깊이, 재생, 탐색, 속도, 컬러맵, 오류 흐름이 브라우저에서 유지됨
- [ ] 390픽셀 반응형 배치, 키보드 조작, 포커스, live, alert, aria 상태가 유지됨
- [ ] 독립 리뷰의 Critical 및 Important finding이 0개임
- [ ] 생성물과 대외비 식별자가 Git 이력에 없음
- [ ] feature 브랜치와 main 통합 커밋이 규칙을 충족함
