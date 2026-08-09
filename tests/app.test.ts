import assert from "node:assert/strict";
import test, { type TestContext } from "node:test";

import {
  describeViewerError,
  formatBenchmarkMetric,
  formatElapsed,
  FrameRenderError,
  loadViewerSource,
  startViewer,
  viewerSourceLabel,
} from "../src/app.js";
import { BundleError } from "../src/bundle.js";
import type { BundleSequenceSource } from "../src/bundle.js";
import { ManifestError } from "../src/manifest.js";
import type { SequenceManifest } from "../src/manifest.js";
import { FrameLoadError } from "../src/preload.js";
import type { LoadableImage } from "../src/preload.js";

const VIEWER_ELEMENT_IDS = [
  "color-canvas",
  "benchmark-button",
  "benchmark-panel",
  "benchmark-random",
  "benchmark-sequential",
  "depth-canvas",
  "depth-far-label",
  "depth-mode",
  "depth-mode-badge",
  "depth-near-label",
  "frame-readout",
  "frame-slider",
  "loading-label",
  "loading-percent",
  "loading-progress",
  "next-frame",
  "play-icon",
  "play-label",
  "play-toggle",
  "playback-speed",
  "previous-frame",
  "restart",
  "source-badge",
  "summary-depth-range",
  "summary-frame-count",
  "summary-interval",
  "time-readout",
  "viewer-error",
  "viewer-error-message",
  "viewer-state",
];

type FakeListener = (event: Event) => void;

class FakeElement {
  readonly id: string;
  readonly attributes = new Map<string, string>();
  disabled = false;
  hidden: boolean;
  readonly listeners = new Map<string, FakeListener>();
  textContent = "";
  value = "";
  max = "";

  constructor(id: string) {
    this.id = id;
    this.hidden = id === "viewer-error";
  }

  addEventListener(type: string, listener: FakeListener): void {
    this.listeners.set(type, listener);
  }

  setAttribute(name: string, value: string): void {
    this.attributes.set(name, value);
  }
}

interface FakeDrawable {
  source?: string;
  lastSource?: string | null;
}

interface FakeImageData {
  data: Uint8ClampedArray;
  source: string | null;
}

interface FakeCanvasContext {
  clearRect(): void;
  drawImage(image: FakeDrawable): void;
  getImageData(x: number, y: number, width: number, height: number): FakeImageData;
  putImageData(imageData: FakeImageData): void;
}

interface FakeRoot {
  readonly createdCanvases: FakeCanvas[];
  readonly depthCommitError: Error;
  readonly depthReadError: Error;
  failDepthCommit: boolean;
  failDepthRead: boolean;
  createElement(tagName: string): FakeCanvas;
  getElementById(id: string): FakeElement;
  querySelectorAll(selector: string): FakeElement[];
}

class FakeCanvas extends FakeElement {
  readonly root: FakeRoot;
  width = 640;
  height = 360;
  lastSource: string | null = null;
  readonly context: FakeCanvasContext;

  constructor(id: string, root: FakeRoot) {
    super(id);
    this.root = root;
    this.context = {
      clearRect: () => {
        this.lastSource = null;
      },
      drawImage: (image) => {
        if (this.id === "depth-canvas" && this.root.failDepthCommit) {
          throw this.root.depthCommitError;
        }
        this.lastSource = image.source ?? image.lastSource ?? null;
      },
      getImageData: (_x, _y, width, height) => {
        if (this.root.failDepthRead) {
          throw this.root.depthReadError;
        }
        return {
          data: new Uint8ClampedArray(width * height * 4),
          source: this.lastSource,
        };
      },
      putImageData: (imageData) => {
        this.lastSource = imageData.source;
      },
    };
  }

  getContext(type: string): FakeCanvasContext | null {
    return type === "2d" ? this.context : null;
  }
}

function createFakeRoot(): FakeRoot {
  const elements: Record<string, FakeElement> = {};
  const controls: FakeElement[] = [];
  const root: FakeRoot = {
    createdCanvases: [],
    depthCommitError: new Error("깊이 캔버스 반영 실패"),
    depthReadError: new Error("깊이 픽셀 읽기 실패"),
    failDepthCommit: false,
    failDepthRead: false,
    createElement(tagName) {
      if (tagName !== "canvas") {
        throw new Error(`지원하지 않는 테스트 요소입니다: ${tagName}`);
      }
      const canvas = new FakeCanvas(`buffer-${root.createdCanvases.length}`, root);
      root.createdCanvases.push(canvas);
      return canvas;
    },
    getElementById(id) {
      const element = elements[id];
      if (!element) throw new Error(`테스트 화면 요소를 찾을 수 없습니다: ${id}`);
      return element;
    },
    querySelectorAll(selector) {
      return selector === "[data-viewer-control]" ? controls : [];
    },
  };

  for (const id of VIEWER_ELEMENT_IDS) {
    elements[id] = id.endsWith("canvas") ? new FakeCanvas(id, root) : new FakeElement(id);
  }

  for (const id of [
    "benchmark-button",
    "depth-mode",
    "frame-slider",
    "next-frame",
    "play-toggle",
    "playback-speed",
    "previous-frame",
    "restart",
  ]) {
    controls.push(root.getElementById(id));
  }

  return root;
}

function getFakeCanvas(root: FakeRoot, id: string): FakeCanvas {
  const element = root.getElementById(id);
  assert.ok(element instanceof FakeCanvas);
  return element;
}

class LoadedImage implements LoadableImage {
  decoding: LoadableImage["decoding"] = "auto";
  naturalWidth = 2;
  naturalHeight = 1;
  width = 2;
  height = 1;
  onload: ((event: Event) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  source = "";

  get src(): string {
    return this.source;
  }

  set src(value: string) {
    this.source = value;
    queueMicrotask(() => this.onload?.(new Event("load")));
  }
}

async function startTestViewer(root: FakeRoot) {
  return startViewer(root as unknown as Document);
}

async function createStartedViewer(t: TestContext) {
  const originalFetch = globalThis.fetch;
  const originalImage = globalThis.Image;
  const originalConsoleError = console.error;
  const hadImage = Object.hasOwn(globalThis, "Image");
  const root = createFakeRoot();
  const reportedErrors: unknown[] = [];
  const manifest = {
    frame_count: 2,
    interval_ms: 100,
    depth_near_cm: 0,
    depth_far_cm: 2500,
    frames: [
      { index: 0, color: "color_000000.png", depth: "depth_000000.png", time_ms: 0 },
      { index: 1, color: "color_000001.png", depth: "depth_000001.png", time_ms: 100 },
    ],
  };
  const fakeFetch = async () => ({
    ok: true,
    status: 200,
    url: "https://viewer.test/manifest.json",
    async json() {
      return manifest;
    },
  });

  globalThis.fetch = fakeFetch as unknown as typeof fetch;
  globalThis.Image = LoadedImage as unknown as typeof Image;
  console.error = (...data: unknown[]) => {
    reportedErrors.push(data[1]);
  };

  t.after(() => {
    globalThis.fetch = originalFetch;
    if (hadImage) {
      globalThis.Image = originalImage;
    } else {
      Reflect.deleteProperty(globalThis, "Image");
    }
    console.error = originalConsoleError;
  });

  const viewer = await startTestViewer(root);
  return { reportedErrors, root, viewer };
}

test("경과 시간을 분과 초 및 밀리초로 표시한다", () => {
  assert.equal(formatElapsed(0), "00:00.000");
  assert.equal(formatElapsed(61_234), "01:01.234");
  assert.equal(formatElapsed(3_661_007), "61:01.007");
});

const loadedManifest: SequenceManifest = {
  frameCount: 1,
  intervalMs: 100,
  depthRange: { nearCm: 0, farCm: 2500 },
  frames: [
    {
      index: 0,
      timeMs: 0,
      color: { kind: "color", sourcePath: "color.png", url: "blob:color" },
      depth: { kind: "depth", sourcePath: "depth.png", url: "blob:depth" },
    },
  ],
};

test("bundle query가 있으면 SIV loader만 선택한다", async () => {
  const calls: string[] = [];
  const bundleSource: BundleSequenceSource = {
    kind: "bundle",
    sourceUrl: "https://viewer.test/data/sequence.siv",
    manifest: loadedManifest,
    assets: [],
    objectUrls: [],
    buffer: new ArrayBuffer(0),
    payloadOffset: 0,
    revoke() {},
  };

  const source = await loadViewerSource("?bundle=data/sequence.siv", {
    pageUrl: "https://viewer.test/index.html",
    manifestLoader: async (url) => {
      calls.push(`manifest:${url}`);
      return loadedManifest;
    },
    bundleLoader: async (url) => {
      calls.push(`bundle:${url}`);
      return bundleSource;
    },
  });

  assert.equal(source.kind, "bundle");
  assert.equal(source.manifest, loadedManifest);
  assert.equal(source.bundleSource, bundleSource);
  assert.deepEqual(calls, ["bundle:https://viewer.test/data/sequence.siv"]);
});

test("bundle query가 없으면 기존 manifest loader만 선택한다", async () => {
  const calls: string[] = [];
  const source = await loadViewerSource("", {
    pageUrl: "https://viewer.test/index.html",
    manifestLoader: async (url) => {
      calls.push(`manifest:${url}`);
      return loadedManifest;
    },
    bundleLoader: async (url) => {
      calls.push(`bundle:${url}`);
      throw new Error("호출되면 안 됩니다.");
    },
  });

  assert.equal(source.kind, "manifest");
  assert.equal(source.manifest, loadedManifest);
  assert.equal(source.bundleSource, null);
  assert.deepEqual(calls, ["manifest:./manifest.json"]);
});

test("비어 있거나 교차 출처인 bundle query를 거부한다", async () => {
  await assert.rejects(
    loadViewerSource("?bundle=", { pageUrl: "https://viewer.test/index.html" }),
    BundleError,
  );
  await assert.rejects(
    loadViewerSource("?bundle=https://other.test/sequence.siv", {
      pageUrl: "https://viewer.test/index.html",
    }),
    BundleError,
  );
});

test("source badge와 benchmark 결과를 짧고 명확하게 표시한다", () => {
  assert.equal(viewerSourceLabel("bundle"), "SIV BINARY");
  assert.equal(viewerSourceLabel("manifest"), "MANIFEST + PNG");
  assert.equal(
    formatBenchmarkMetric({ averageMsPerImage: 0.12567, sampleCount: 24, checksum: 9 }),
    "0.126 ms/image, 24 samples",
  );
});

test("manifest 모드는 요약을 표시하고 binary benchmark를 비활성화한다", async (t) => {
  const { root } = await createStartedViewer(t);

  assert.equal(root.getElementById("source-badge").textContent, "MANIFEST + PNG");
  assert.equal(root.getElementById("summary-frame-count").textContent, "2");
  assert.equal(root.getElementById("summary-interval").textContent, "100 ms");
  assert.equal(root.getElementById("summary-depth-range").textContent, "0 - 2500 cm");
  assert.equal(root.getElementById("benchmark-panel").hidden, true);
  assert.equal(root.getElementById("benchmark-button").disabled, true);
});

test("manifest 오류에 원인과 확인할 파일을 안내한다", () => {
  const message = describeViewerError(
    new ManifestError("frames[2].depth는 비어 있지 않은 문자열이어야 합니다."),
  );

  assert.match(message, /frames\[2\]\.depth/);
  assert.match(message, /manifest\.json/);
});

test("프레임 로딩 오류에 실패 파일명을 안내한다", () => {
  const error = new FrameLoadError([
    {
      frameIndex: 3,
      kind: "depth",
      url: "http://localhost:8000/depth_000003.png",
      cause: new Error("손상된 PNG"),
    },
    {
      frameIndex: 4,
      kind: "color",
      url: "http://localhost:8000/color_000004.png",
      cause: new Error("파일 없음"),
    },
  ]);

  const message = describeViewerError(error);

  assert.match(message, /depth_000003\.png/);
  assert.match(message, /color_000004\.png/);
  assert.match(message, /2개/);
});

test("이미지 시간 초과에는 파일 경로와 서버 응답 확인을 안내한다", () => {
  const cause = new Error("이미지 요청 시간 초과");
  cause.name = "TimeoutError";
  const error = new FrameLoadError([
    {
      frameIndex: 3,
      kind: "depth",
      url: "http://localhost:8000/depth_000003.png",
      cause,
    },
  ]);

  const message = describeViewerError(error);

  assert.match(message, /시간 초과/);
  assert.match(message, /파일명과 manifest\.json의 경로/);
  assert.match(message, /서버 응답/);
});

test("깊이 변환 실패 시 두 캔버스와 표시 프레임을 이전 상태로 유지한다", async (t) => {
  const { root, viewer } = await createStartedViewer(t);
  const colorCanvas = getFakeCanvas(root, "color-canvas");
  const depthCanvas = getFakeCanvas(root, "depth-canvas");
  assert.match(colorCanvas.lastSource ?? "", /color_000000\.png/);
  assert.match(depthCanvas.lastSource ?? "", /depth_000000\.png/);

  viewer.depthMode = "colormap";
  root.failDepthRead = true;
  const didRender = viewer.renderFrame(1);

  assert.equal(didRender, false);
  assert.match(colorCanvas.lastSource ?? "", /color_000000\.png/);
  assert.match(depthCanvas.lastSource ?? "", /depth_000000\.png/);
  assert.equal(root.getElementById("frame-readout").textContent, "0 / 1");
  assert.equal(root.getElementById("viewer-state").textContent, "오류");
  assert.equal(root.getElementById("viewer-error").hidden, false);
  assert.ok(
    root.querySelectorAll("[data-viewer-control]").every((control) => control.disabled),
  );
});

test("컬러맵 준비에는 두 재사용 버퍼만 사용한다", async (t) => {
  const { root, viewer } = await createStartedViewer(t);
  const playToggle = root.getElementById("play-toggle");
  assert.equal(playToggle.attributes.get("aria-label"), undefined);
  viewer.updatePlayingState(true);
  assert.equal(playToggle.attributes.get("aria-label"), "일시정지");
  viewer.updatePlayingState(false);
  assert.equal(playToggle.attributes.get("aria-label"), "재생");

  const depthMode = root.getElementById("depth-mode");
  const clickDepthMode = depthMode.listeners.get("click");
  assert.ok(clickDepthMode);
  clickDepthMode(new Event("click"));
  assert.equal(depthMode.attributes.get("aria-pressed"), "true");

  assert.equal(root.createdCanvases.length, 2);

  viewer.depthMode = "colormap";
  assert.equal(viewer.renderFrame(1), true);
  assert.equal(viewer.renderFrame(0), true);
  assert.equal(root.createdCanvases.length, 2);
});

test("깊이 캔버스 반영 실패 시 두 출력을 비우고 표시 프레임을 유지한다", async (t) => {
  const { reportedErrors, root, viewer } = await createStartedViewer(t);
  const colorCanvas = getFakeCanvas(root, "color-canvas");
  const depthCanvas = getFakeCanvas(root, "depth-canvas");
  root.failDepthCommit = true;

  const didRender = viewer.renderFrame(1);

  assert.equal(didRender, false);
  assert.equal(colorCanvas.lastSource, null);
  assert.equal(depthCanvas.lastSource, null);
  assert.equal(root.getElementById("frame-readout").textContent, "0 / 1");
  assert.equal(root.getElementById("viewer-state").textContent, "오류");
  assert.equal(root.getElementById("viewer-error").hidden, false);
  const error = reportedErrors.at(-1);
  assert.ok(error instanceof FrameRenderError);
  assert.equal(error.name, "FrameRenderError");
  assert.equal(error.frameIndex, 1);
  assert.equal(error.kind, "depth");
  assert.equal(error.cause, root.depthCommitError);
  const message = root.getElementById("viewer-error-message").textContent;
  assert.match(message, /프레임 1/);
  assert.match(message, /깊이/);
  assert.match(message, /깊이 캔버스 반영 실패/);
});

test("캔버스 오류에 프레임과 데이터 종류 및 원인을 보존한다", async (t) => {
  const { reportedErrors, root, viewer } = await createStartedViewer(t);
  viewer.depthMode = "colormap";
  root.failDepthRead = true;

  viewer.renderFrame(1);

  const error = reportedErrors.at(-1);
  assert.ok(error instanceof FrameRenderError);
  assert.equal(error.name, "FrameRenderError");
  assert.equal(error.frameIndex, 1);
  assert.equal(error.kind, "depth");
  assert.equal(error.cause, root.depthReadError);
  const message = root.getElementById("viewer-error-message").textContent;
  assert.match(message, /프레임 1/);
  assert.match(message, /깊이/);
  assert.match(message, /확인하거나 다시 생성/);
});

test("캔버스 오류 안내에 정규화한 실제 원인을 포함한다", () => {
  const cause = new Error("  SecurityError:\n깊이 캔버스\t읽기 실패  ");

  const message = describeViewerError(new FrameRenderError(4, "depth", cause));

  assert.match(message, /원인: SecurityError: 깊이 캔버스 읽기 실패/);
  assert.doesNotMatch(message, /[\n\t]/);
});

test("예상하지 못한 오류에는 다시 시도할 방법을 안내한다", () => {
  const message = describeViewerError(new Error("알 수 없는 오류"));

  assert.match(message, /새로고침/);
  assert.match(message, /다시 생성/);
});
