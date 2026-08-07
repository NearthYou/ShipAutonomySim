import assert from "node:assert/strict";
import test from "node:test";

import {
  describeViewerError,
  formatElapsed,
  FrameRenderError,
  startViewer,
} from "../src/app.js";
import { ManifestError } from "../src/manifest.js";
import { FrameLoadError } from "../src/preload.js";

const VIEWER_ELEMENT_IDS = [
  "color-canvas",
  "depth-canvas",
  "depth-mode",
  "depth-mode-badge",
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
  "time-readout",
  "viewer-error",
  "viewer-error-message",
  "viewer-state",
];

class FakeElement {
  constructor(id) {
    this.id = id;
    this.attributes = new Map();
    this.disabled = false;
    this.hidden = id === "viewer-error";
    this.listeners = new Map();
    this.textContent = "";
    this.value = "";
  }

  addEventListener(type, listener) {
    this.listeners.set(type, listener);
  }

  setAttribute(name, value) {
    this.attributes.set(name, value);
  }
}

class FakeCanvas extends FakeElement {
  constructor(id, root) {
    super(id);
    this.root = root;
    this.width = 640;
    this.height = 360;
    this.lastSource = null;
    this.context = {
      clearRect: () => {
        this.lastSource = null;
      },
      drawImage: (image) => {
        this.lastSource = image.source ?? image.lastSource;
      },
      getImageData: (x, y, width, height) => {
        if (this.root.failDepthRead) {
          throw this.root.depthReadError;
        }
        const imageData = {
          data: new Uint8ClampedArray(width * height * 4),
          source: this.lastSource,
        };
        return imageData;
      },
      putImageData: (imageData) => {
        this.lastSource = imageData.source;
      },
    };
  }

  getContext(type) {
    return type === "2d" ? this.context : null;
  }
}

function createFakeRoot() {
  const root = {
    createdCanvases: [],
    depthReadError: new Error("깊이 픽셀 읽기 실패"),
    failDepthRead: false,
  };
  const elements = Object.fromEntries(
    VIEWER_ELEMENT_IDS.map((id) => [
      id,
      id.endsWith("canvas") ? new FakeCanvas(id, root) : new FakeElement(id),
    ]),
  );
  const controls = [
    "depth-mode",
    "frame-slider",
    "next-frame",
    "play-toggle",
    "playback-speed",
    "previous-frame",
    "restart",
  ].map((id) => elements[id]);

  return Object.assign(root, {
    createElement(tagName) {
      if (tagName !== "canvas") {
        throw new Error(`지원하지 않는 테스트 요소입니다: ${tagName}`);
      }
      const canvas = new FakeCanvas(`buffer-${root.createdCanvases.length}`, root);
      root.createdCanvases.push(canvas);
      return canvas;
    },
    getElementById(id) {
      return elements[id] ?? null;
    },
    querySelectorAll(selector) {
      return selector === "[data-viewer-control]" ? controls : [];
    },
  });
}

class LoadedImage {
  constructor() {
    this.naturalWidth = 2;
    this.naturalHeight = 1;
    this.onload = null;
    this.onerror = null;
  }

  set src(value) {
    this.source = value;
    queueMicrotask(() => this.onload?.());
  }
}

async function createStartedViewer(t) {
  const originalFetch = globalThis.fetch;
  const originalImage = globalThis.Image;
  const originalConsoleError = console.error;
  const hadImage = Object.hasOwn(globalThis, "Image");
  const root = createFakeRoot();
  const reportedErrors = [];
  const manifest = {
    frame_count: 2,
    interval_ms: 100,
    depth_near_cm: 0,
    depth_far_cm: 5000,
    frames: [
      { index: 0, color: "color_000000.png", depth: "depth_000000.png", time_ms: 0 },
      { index: 1, color: "color_000001.png", depth: "depth_000001.png", time_ms: 100 },
    ],
  };

  globalThis.fetch = async () => ({
    ok: true,
    status: 200,
    url: "https://viewer.test/manifest.json",
    async json() {
      return manifest;
    },
  });
  globalThis.Image = LoadedImage;
  console.error = (label, error) => reportedErrors.push(error);

  t.after(() => {
    globalThis.fetch = originalFetch;
    if (hadImage) {
      globalThis.Image = originalImage;
    } else {
      delete globalThis.Image;
    }
    console.error = originalConsoleError;
  });

  const viewer = await startViewer(root);
  return { reportedErrors, root, viewer };
}

test("경과 시간을 분과 초 및 밀리초로 표시한다", () => {
  assert.equal(formatElapsed(0), "00:00.000");
  assert.equal(formatElapsed(61_234), "01:01.234");
  assert.equal(formatElapsed(3_661_007), "61:01.007");
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
  const colorCanvas = root.getElementById("color-canvas");
  const depthCanvas = root.getElementById("depth-canvas");
  assert.match(colorCanvas.lastSource, /color_000000\.png/);
  assert.match(depthCanvas.lastSource, /depth_000000\.png/);

  viewer.depthMode = "colormap";
  root.failDepthRead = true;
  const didRender = viewer.renderFrame(1);

  assert.equal(didRender, false);
  assert.match(colorCanvas.lastSource, /color_000000\.png/);
  assert.match(depthCanvas.lastSource, /depth_000000\.png/);
  assert.equal(root.getElementById("frame-readout").textContent, "0 / 1");
  assert.equal(root.getElementById("viewer-state").textContent, "오류");
  assert.equal(root.getElementById("viewer-error").hidden, false);
});

test("컬러맵 준비에는 두 재사용 버퍼만 사용한다", async (t) => {
  const { root, viewer } = await createStartedViewer(t);

  assert.equal(root.createdCanvases.length, 2);

  viewer.depthMode = "colormap";
  assert.equal(viewer.renderFrame(1), true);
  assert.equal(viewer.renderFrame(0), true);
  assert.equal(root.createdCanvases.length, 2);
});

test("캔버스 오류에 프레임과 데이터 종류 및 원인을 보존한다", async (t) => {
  const { reportedErrors, root, viewer } = await createStartedViewer(t);
  viewer.depthMode = "colormap";
  root.failDepthRead = true;

  viewer.renderFrame(1);

  const error = reportedErrors.at(-1);
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
