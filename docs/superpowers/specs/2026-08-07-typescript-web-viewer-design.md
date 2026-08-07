# TypeScript 이미지 시퀀스 웹 뷰어 전환 설계

## 목표

현재 JavaScript 기반 정적 이미지 시퀀스 뷰어를 TypeScript로 전환한다. 전환 뒤에도 프레임워크와 번들러 없이 브라우저 기본 ES 모듈을 사용하며, 기존 기능과 오류 처리 동작을 보존한다.

성공 조건은 다음과 같다.

- 브라우저가 `tsc`로 생성한 ES 모듈을 실행한다.
- 컬러 프레임, 깊이 프레임, manifest, 선로딩 결과, 재생 속도, 재생 상태, 깊이 표시 상태가 명시적 타입을 가진다.
- 검증 전 외부 데이터와 예외 원인은 `unknown`으로 취급한다.
- TypeScript의 `strict` 검사를 통과한다.
- 런타임 의존성은 0개를 유지한다.
- 기존 Node 테스트와 Python 테스트, 브라우저 동작을 보존한다.
- 생성된 JavaScript는 Git에서 제외한다.

## 범위

포함 범위는 다음과 같다.

- `src/*.js`를 `src/*.ts`로 전환
- `tests/*.test.js`를 `tests/*.test.ts`로 전환
- `tests/types.contract.ts` 컴파일 계약 추가
- 단일 `tsconfig.json`과 최소 npm 빌드 명령 추가
- `index.html`이 컴파일된 ES 모듈을 불러오도록 변경
- 외부 manifest와 내부 도메인 모델 사이의 검증 및 정규화 경계 도입
- DOM 요소, 오류, 콜백과 테스트 대역의 타입 명시
- README의 설치, 빌드, 테스트와 실행 방법 갱신

다음은 포함하지 않는다.

- 프레임워크, 번들러, TypeScript 실행 로더 도입
- 서버 애플리케이션이나 API 추가
- manifest JSON 형식 변경
- 화면 디자인이나 사용자 기능 변경
- Python 더미 데이터 생성기의 TypeScript 전환
- 클래스 계층, 브랜드 숫자 타입 또는 범용 타입 프레임워크 도입

## 빌드 아키텍처

하나의 `tsconfig.json`이 제품 코드와 Node 테스트를 함께 컴파일한다.

```text
src/*.ts
tests/*.test.ts
tests/types.contract.ts
        |
        | tsc
        v
dist/src/*.js
dist/tests/*.test.js
dist/tests/types.contract.js (컴파일 전용, 테스트 실행 제외)
```

`rootDir`은 저장소 루트, `outDir`은 `dist`로 두어 원본 디렉터리 구조를 유지한다. 브라우저는 `dist/src/app.js`를 ES 모듈로 불러오고, Node 테스트 러너는 `dist/tests`의 컴파일된 테스트만 실행한다.

컴파일러 설정은 다음 원칙을 따른다.

- `target`: `ES2022`
- `module`과 `moduleResolution`: `NodeNext`
- `lib`: `ES2022`, `DOM`, `DOM.Iterable`
- `strict`: 활성화
- `useUnknownInCatchVariables`: 활성화
- `noEmitOnError`: 활성화
- `forceConsistentCasingInFileNames`: 활성화
- `verbatimModuleSyntax`: 활성화
- `rootDir`: `.`
- `outDir`: `dist`
- 포함 대상: `src/**/*.ts`, `tests/**/*.ts`
- 제외 대상: `dist`

TypeScript 원본에서도 로컬 모듈 경로는 `.js` 확장자를 사용한다. 예를 들어 `app.ts`는 `./depth.js`를 가져오며, `tsc`가 브라우저와 Node가 직접 실행할 수 있는 경로를 그대로 생성한다.

`package.json`에는 빌드와 테스트 스크립트, `typescript`와 `@types/node` 개발 의존성만 추가하며 `dependencies`는 두지 않는다.

- `build`: `tsc -p tsconfig.json`
- `test`: 빌드 후 `node --test "dist/tests/*.test.js"` 실행

개발 의존성은 `typescript`와 `@types/node`만 허용한다. 버전과 무결성 정보는 `package-lock.json`에 기록한다. 브라우저 실행에 필요한 `dependencies` 항목은 두지 않는다.

`dist/`는 `.gitignore`에 추가하며 커밋하지 않는다. 사용자는 `npm install`, `npm run build`, 정적 HTTP 서버 실행 순서로 뷰어를 연다.

## 외부 입력과 내부 manifest

`manifest.json`의 공개 형식과 snake case 필드는 바꾸지 않는다. JSON 파싱 결과는 항상 `unknown`으로 받고 기존 런타임 검증을 모두 통과한 뒤 내부 모델로 변환한다.

```ts
export function validateManifest(
  value: unknown,
  manifestUrl: string,
): SequenceManifest;
```

검증된 내부 모델은 camel case 이름과 중첩된 컬러 및 깊이 소스를 사용한다.

```ts
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
  depthRange: {
    nearCm: number;
    farCm: number;
  };
  frames: readonly SequenceFrame[];
}
```

검증 함수만 외부 필드인 `frame_count`, `interval_ms`, `depth_near_cm`, `depth_far_cm`, `time_ms`, `color`, `depth`를 읽는다. 이후 모듈은 정규화된 내부 모델만 사용한다.

동일 출처 검사, 프레임 수와 인덱스 검사, 시간 순서와 깊이 범위 검사는 현재 동작을 그대로 유지한다.

## 컬러 및 깊이 프레임

선로딩 결과는 컬러와 깊이를 구분하는 판별 필드를 가진다.

```ts
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
```

`preloadFrames`는 이미지 타입을 제네릭으로 받는다. 제품 코드에서는 `HTMLImageElement`를 사용하고 테스트에서는 작은 가짜 이미지 타입을 사용한다. 이 경계 덕분에 테스트가 브라우저 객체를 흉내 내기 위해 넓은 타입 단언을 사용할 필요가 없다.

선로딩 내부의 개별 요청 결과는 판별 가능한 성공 및 실패 합집합으로 표현한다.

```ts
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
```

진행률과 실패도 명시적 타입을 가진다.

```ts
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

현재의 전체 선로딩, 실패 수집, 제한 시간과 지연 이벤트 무효화 동작은 바꾸지 않는다.

## 재생과 화면 상태

재생 속도와 상태는 제한된 합집합 타입으로 표현한다.

```ts
export type PlaybackSpeed = 0.5 | 1 | 2;
export type PlaybackState = "paused" | "playing";
export type DepthDisplayMode = "grayscale" | "colormap";
export type ViewerState = "loading" | "ready" | "playing" | "paused" | "error";
```

`SequencePlayer`는 내부에 `PlaybackState`를 저장하고 기존 사용처를 위해 `isPlaying` 읽기 접근자를 제공한다. 기존 호출 계약을 보존하기 위해 상태 변경 콜백은 `(isPlaying: boolean) => void`를 유지한다.

프레임 변경 콜백은 `(index: number) => boolean | void`로 명시한다. 기존과 같이 `false`만 렌더링 실패를 뜻하고 `true` 또는 반환값 없음은 재생을 계속한다. 마지막 프레임에서 처음으로 돌아갈 때 콜백이 `false`를 반환하면 재생 활성화와 애니메이션 프레임 예약을 중단한다.

화면 상태 문자열은 `ViewerState`와 한국어 표시 문구의 `Record` 매핑으로 관리한다. `ViewerState`는 컨트롤러가 시작된 뒤의 상태만 나타내며, 정적 HTML의 초기 문구인 `로딩 전`은 초기화 전 표시로 남긴다. 깊이 버튼과 배지는 `DepthDisplayMode`에서 파생한다.

## 모듈 책임과 타입 소유권

범용 `types.ts` 파일은 만들지 않는다. 각 타입은 책임을 가진 모듈에서 정의하고 필요한 곳에서 `import type`으로 가져온다.

- `manifest.ts`: manifest, 프레임 소스, 런타임 입력 검증
- `preload.ts`: 컬러 및 깊이 프레임 데이터, 진행률, 실패와 이미지 로더
- `player.ts`: 재생 속도, 재생 상태, 프레임 변경 콜백과 재생 시계
- `depth.ts`: RGB 튜플과 픽셀 컬러맵 변환
- `app.ts`: DOM 요소 집합, 깊이 표시 모드, 뷰어 상태와 렌더 오류

기존 모듈 경계를 유지하고 TypeScript 전환과 무관한 파일 분해나 추상화를 추가하지 않는다.

## DOM 경계

`app.ts`는 필요한 DOM 요소를 시작 시 한 번 수집해 명시적 `ViewerElements`로 변환한다. 요소가 없으면 현재와 같은 즉시 오류를 발생시킨다.

```ts
interface ViewerElements {
  colorCanvas: HTMLCanvasElement;
  depthCanvas: HTMLCanvasElement;
  depthMode: HTMLButtonElement;
  frameSlider: HTMLInputElement;
  playbackSpeed: HTMLSelectElement;
  viewerState: HTMLElement;
}
```

실제 인터페이스에는 현재 사용하는 모든 요소를 포함한다. 이후 코드는 문자열 식별자로 `Object`를 조회하지 않고 타입이 정해진 필드를 사용한다.

`startViewer`와 `ViewerController`의 입력은 실제 `Document`로 고정한다. 테스트의 가짜 DOM은 작은 fixture 타입을 유지하고, `startViewer`를 호출하는 단일 fixture 헬퍼에서만 `unknown`을 거친 `Document` 타입 단언을 허용한다. 별도 DOM 포트 추상화나 제품 코드의 테스트용 `any`는 추가하지 않는다.

기존 HTML과 CSS의 접근성 계약도 보존한다. 연결된 레이블, 키보드 조작과 포커스 표시, 로딩 및 화면 상태의 live 알림, 오류의 alert 알림, 재생 버튼의 동적 접근 가능한 이름, 깊이 모드의 pressed 상태, 준비 전과 오류 후의 컨트롤 비활성화 동작을 바꾸지 않는다.

## 오류 처리

다음 오류 클래스와 사용자 동작은 유지한다.

- `ManifestError`: HTTP, JSON, 계약과 동일 출처 오류
- `FrameLoadError`: 프레임 종류, 인덱스, URL과 원인을 포함한 이미지 오류
- `FrameRenderError`: 컬러 또는 깊이 종류, 프레임 인덱스와 원인을 포함한 캔버스 오류

오류의 `cause`와 `catch` 변수는 `unknown`으로 취급한다. 오류 이름이나 메시지를 사용할 때는 타입을 확인하는 작은 헬퍼를 거친다.

manifest의 단일 전체 제한 시간, 이미지 이벤트 정리, 두 캔버스의 원자적 준비, 반영 실패 시 일관 상태 유지, 사용자 오류 원인 정규화와 길이 제한을 그대로 보존한다.

## 테스트 전략

기존 JavaScript 테스트 50개는 동일한 동작 단언을 유지한 채 TypeScript로 전환한다. 테스트는 원본 `.ts`를 직접 실행하지 않고 제품 코드와 같은 `tsc` 출력인 `dist/tests`에서 실행한다.

재생기 테스트의 상태 변경 콜백은 기존 `true`와 `false` 단언을 유지한다. 프레임 변경 콜백도 `false`일 때만 재생이 중단되는 현재 계약을 그대로 검증한다.

`tests/types.contract.ts`는 실행 테스트가 아닌 컴파일 계약을 검증한다. `@ts-expect-error`를 사용해 다음 잘못된 조합이 실제로 컴파일 오류가 되는지 확인한다.

- 컬러 프레임 데이터를 깊이 프레임 데이터로 사용
- `1.5`를 `PlaybackSpeed`로 사용
- 검증하지 않은 `unknown` JSON을 `SequenceManifest`로 사용

Python `unittest` 4개와 더미 데이터 형식은 변경하지 않는다.

전체 검증은 다음을 포함한다.

- `tsc` 빌드 성공
- 컴파일된 Node 테스트 전체 통과
- Python 테스트 전체 통과
- 생성된 모든 JavaScript의 `node --check` 통과
- Python `compileall` 통과
- `npm ls --omit=dev` 결과 런타임 의존성 0개
- `dist/`가 Git 추적 및 커밋에서 제외됨
- `git diff --check` 통과
- 실제 브라우저의 정상 로딩, 재생, 탐색, 깊이 모드와 기존 오류 경로 확인
- 데스크톱과 390픽셀 화면의 배치 및 콘솔 오류 확인
- 키보드 조작과 포커스 표시, 레이블 연결, live 및 alert 알림 확인
- 재생 버튼의 접근 가능한 이름, 깊이 버튼의 pressed 상태, 컨트롤 비활성화 전환 확인

## 전환 원칙

모듈과 대응 테스트를 한 쌍씩 전환해 오류 범위를 작게 유지한다. 파일 확장자만 한꺼번에 바꾼 뒤 모든 오류를 동시에 해결하지 않는다.

각 전환 단계는 다음 조건을 만족해야 한다.

- 타입 오류가 예상한 경계에서 먼저 드러남
- 동작 테스트가 기존 계약을 계속 검증함
- 최소 타입과 구현 변경으로 컴파일 및 테스트 통과
- `feat`, `fix`, `test`, `docs`, `chore`, `perf`, `refactor`, `build` 접두사와 한글 명사형 제목의 작은 커밋
- 커밋 본문에 변경 이유, 핵심 변경, 검증 방법 포함

전환 브랜치는 최신 main에서 만든 `feat/typescript-web-viewer`를 사용한다. 설계 문서와 구현 계획을 먼저 확정하고 커밋한 뒤 제품 코드 전환을 시작한다.
