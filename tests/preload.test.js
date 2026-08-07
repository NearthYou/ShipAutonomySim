import assert from "node:assert/strict";
import test from "node:test";

import { FrameLoadError, loadImage, preloadFrames } from "../src/preload.js";

class HangingImage {
  static instances = [];

  constructor() {
    this.onload = null;
    this.onerror = null;
    this.removedSource = false;
    HangingImage.instances.push(this);
  }

  set src(value) {
    this.source = value;
  }

  removeAttribute(name) {
    if (name === "src") {
      this.removedSource = true;
    }
  }
}

async function withTestDeadline(promise, timeoutMs = 250) {
  let timeoutId;
  try {
    return await Promise.race([
      promise,
      new Promise((resolve, reject) => {
        timeoutId = setTimeout(
          () => reject(new Error("테스트 제한 시간 안에 이미지 요청이 끝나야 합니다.")),
          timeoutMs,
        );
      }),
    ]);
  } finally {
    clearTimeout(timeoutId);
  }
}

function frames() {
  return [
    {
      index: 0,
      colorUrl: "https://example.test/color_000000.png",
      depthUrl: "https://example.test/depth_000000.png",
      time_ms: 0,
    },
    {
      index: 1,
      colorUrl: "https://example.test/color_000001.png",
      depthUrl: "https://example.test/depth_000001.png",
      time_ms: 100,
    },
  ];
}

test("모든 프레임에 불러온 컬러와 깊이 이미지를 연결한다", async () => {
  const input = frames();

  const result = await preloadFrames(input, {
    imageLoader: async (url) => ({ source: url }),
  });

  assert.equal(result[0].colorImage.source, "https://example.test/color_000000.png");
  assert.equal(result[0].depthImage.source, "https://example.test/depth_000000.png");
  assert.equal(result[1].colorImage.source, "https://example.test/color_000001.png");
  assert.equal(input[0].colorImage, undefined);
});

test("이미지 하나가 끝날 때마다 완료 수와 백분율을 알린다", async () => {
  const progress = [];

  await preloadFrames([frames()[0]], {
    imageLoader: async (url) => ({ source: url }),
    onProgress: (state) => progress.push(state),
  });

  assert.deepEqual(progress, [
    { completed: 1, total: 2, percent: 50 },
    { completed: 2, total: 2, percent: 100 },
  ]);
});

test("실패한 이미지가 있어도 모든 로딩 결과를 수집한다", async () => {
  const attempted = [];
  const progress = [];

  await assert.rejects(
    preloadFrames(frames(), {
      imageLoader: async (url) => {
        attempted.push(url);
        if (url.includes("depth")) {
          throw new Error("손상된 PNG");
        }
        return { source: url };
      },
      onProgress: (state) => progress.push(state.percent),
    }),
    (error) => {
      assert.ok(error instanceof FrameLoadError);
      assert.deepEqual(
        error.failures.map((failure) => [failure.frameIndex, failure.kind]),
        [
          [0, "depth"],
          [1, "depth"],
        ],
      );
      return true;
    },
  );

  assert.equal(attempted.length, 4);
  assert.equal(progress.at(-1), 100);
});

test("이미지 요청이 제한 시간을 넘으면 핸들러와 요청을 정리한다", async () => {
  HangingImage.instances.length = 0;
  const loading = loadImage("https://example.test/hanging.png", HangingImage, { timeoutMs: 5 });
  const image = HangingImage.instances[0];
  const lateLoad = image.onload;

  await assert.rejects(
    withTestDeadline(loading),
    (error) => error.name === "TimeoutError" && /hanging\.png/.test(error.message),
  );

  assert.equal(image.onload, null);
  assert.equal(image.onerror, null);
  assert.equal(image.removedSource, true);
  lateLoad();
  assert.equal(image.onload, null);
});

test("이미지 시간 초과를 프레임과 종류를 보존한 로딩 오류로 모은다", async () => {
  HangingImage.instances.length = 0;

  await assert.rejects(
    withTestDeadline(
      preloadFrames([frames()[0]], {
        ImageConstructor: HangingImage,
        imageTimeoutMs: 5,
      }),
    ),
    (error) => {
      assert.ok(error instanceof FrameLoadError);
      assert.deepEqual(
        error.failures.map(({ frameIndex, kind, cause }) => [frameIndex, kind, cause.name]),
        [
          [0, "color", "TimeoutError"],
          [0, "depth", "TimeoutError"],
        ],
      );
      return true;
    },
  );
});
