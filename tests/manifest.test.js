import assert from "node:assert/strict";
import test from "node:test";

import { ManifestError, loadManifest, validateManifest } from "../src/manifest.js";

const MANIFEST_URL = "https://example.test/sequences/manifest.json";

function validManifest() {
  return {
    frame_count: 2,
    interval_ms: 100,
    depth_near_cm: 0,
    depth_far_cm: 5000,
    frames: [
      {
        index: 0,
        color: "color_000000.png",
        depth: "depth_000000.png",
        time_ms: 0,
      },
      {
        index: 1,
        color: "color_000001.png",
        depth: "depth_000001.png",
        time_ms: 100,
      },
    ],
  };
}

test("manifest 위치를 기준으로 프레임 URL을 해석한다", () => {
  const manifest = validManifest();

  const result = validateManifest(manifest, MANIFEST_URL);

  assert.equal(result.frames[0].colorUrl, "https://example.test/sequences/color_000000.png");
  assert.equal(result.frames[1].depthUrl, "https://example.test/sequences/depth_000001.png");
  assert.equal(manifest.frames[0].colorUrl, undefined);
});

test("프레임 수와 배열 길이가 다르면 거부한다", () => {
  const manifest = validManifest();
  manifest.frame_count = 3;

  assert.throws(
    () => validateManifest(manifest, MANIFEST_URL),
    (error) => error instanceof ManifestError && /frame_count/.test(error.message),
  );
});

test("재생 간격이 0이면 거부한다", () => {
  const manifest = validManifest();
  manifest.interval_ms = 0;

  assert.throws(
    () => validateManifest(manifest, MANIFEST_URL),
    (error) => error instanceof ManifestError && /interval_ms/.test(error.message),
  );
});

test("가까운 깊이가 먼 깊이보다 작지 않으면 거부한다", () => {
  const manifest = validManifest();
  manifest.depth_near_cm = 5000;

  assert.throws(
    () => validateManifest(manifest, MANIFEST_URL),
    (error) => error instanceof ManifestError && /depth_near_cm/.test(error.message),
  );
});

test("프레임 인덱스가 순서대로 이어지지 않으면 거부한다", () => {
  const manifest = validManifest();
  manifest.frames[1].index = 4;

  assert.throws(
    () => validateManifest(manifest, MANIFEST_URL),
    (error) => error instanceof ManifestError && /frames\[1\]\.index/.test(error.message),
  );
});

test("프레임 시간이 이전보다 작아지면 거부한다", () => {
  const manifest = validManifest();
  manifest.frames[1].time_ms = -1;

  assert.throws(
    () => validateManifest(manifest, MANIFEST_URL),
    (error) => error instanceof ManifestError && /frames\[1\]\.time_ms/.test(error.message),
  );
});

test("이미지 파일명이 비어 있으면 거부한다", () => {
  const manifest = validManifest();
  manifest.frames[0].depth = "   ";

  assert.throws(
    () => validateManifest(manifest, MANIFEST_URL),
    (error) => error instanceof ManifestError && /frames\[0\]\.depth/.test(error.message),
  );
});

test("HTTP 응답의 최종 URL을 사용해 manifest를 불러온다", async () => {
  const response = {
    ok: true,
    status: 200,
    url: MANIFEST_URL,
    async json() {
      return validManifest();
    },
  };

  const result = await loadManifest("./manifest.json", async () => response);

  assert.equal(result.frame_count, 2);
  assert.equal(result.frames[0].depthUrl, "https://example.test/sequences/depth_000000.png");
});

test("HTTP 오류를 사용자가 이해할 수 있는 manifest 오류로 바꾼다", async () => {
  const response = {
    ok: false,
    status: 404,
    statusText: "Not Found",
    url: MANIFEST_URL,
  };

  await assert.rejects(
    loadManifest("./manifest.json", async () => response),
    (error) => error instanceof ManifestError && /404/.test(error.message),
  );
});

test("잘못된 JSON을 원인을 보존한 manifest 오류로 바꾼다", async () => {
  const cause = new SyntaxError("예상하지 못한 토큰");
  const response = {
    ok: true,
    status: 200,
    url: MANIFEST_URL,
    async json() {
      throw cause;
    },
  };

  await assert.rejects(
    loadManifest("./manifest.json", async () => response),
    (error) =>
      error instanceof ManifestError && /JSON/.test(error.message) && error.cause === cause,
  );
});

test("네트워크 실패 원인을 보존한다", async () => {
  const cause = new Error("연결 거부");

  await assert.rejects(
    loadManifest("./manifest.json", async () => {
      throw cause;
    }),
    (error) => error instanceof ManifestError && error.cause === cause,
  );
});
