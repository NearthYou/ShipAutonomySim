import assert from "node:assert/strict";
import test from "node:test";

import { FrameLoadError, preloadFrames } from "../src/preload.js";

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
