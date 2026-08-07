import assert from "node:assert/strict";
import test from "node:test";

import { describeViewerError, formatElapsed } from "../src/app.js";
import { ManifestError } from "../src/manifest.js";
import { FrameLoadError } from "../src/preload.js";

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

test("예상하지 못한 오류에는 다시 시도할 방법을 안내한다", () => {
  const message = describeViewerError(new Error("알 수 없는 오류"));

  assert.match(message, /새로고침/);
  assert.match(message, /다시 생성/);
});
