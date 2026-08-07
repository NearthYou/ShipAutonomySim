import assert from "node:assert/strict";
import test from "node:test";

import * as depth from "../src/depth.js";

const { colorizeDepthPixels, mapDepthIntensity } = depth;

test("어두운 깊이값은 차가운 색으로 바꾼다", () => {
  const color = mapDepthIntensity(0);

  assert.deepEqual(color, [12, 35, 84]);
  assert.ok(color[2] > color[0]);
});

test("밝은 깊이값은 따뜻한 색으로 바꾼다", () => {
  const color = mapDepthIntensity(255);

  assert.deepEqual(color, [210, 35, 42]);
  assert.ok(color[0] > color[2]);
});

test("색상 정지점 사이 값을 선형 보간한다", () => {
  assert.deepEqual(mapDepthIntensity(127.5), [246, 220, 73]);
});

test("깊이 밝기를 유효 범위로 제한한다", () => {
  assert.deepEqual(mapDepthIntensity(-10), [12, 35, 84]);
  assert.deepEqual(mapDepthIntensity(300), [210, 35, 42]);
});

test("픽셀의 RGB 평균을 깊이 밝기로 사용한다", () => {
  const source = new Uint8ClampedArray([255, 0, 0, 200]);

  const result = colorizeDepthPixels(source);

  assert.deepEqual([...result], [82, 177, 162, 200]);
});

test("여러 픽셀의 알파를 보존하고 원본을 바꾸지 않는다", () => {
  const source = new Uint8ClampedArray([
    0, 0, 0, 128,
    255, 255, 255, 64,
  ]);

  const result = colorizeDepthPixels(source);

  assert.deepEqual(
    [...result],
    [
      12, 35, 84, 128,
      210, 35, 42, 64,
    ],
  );
  assert.deepEqual(
    [...source],
    [
      0, 0, 0, 128,
      255, 255, 255, 64,
    ],
  );
  assert.notEqual(result, source);
});

test("픽셀 배열을 추가 복사 없이 제자리 변환한다", () => {
  const source = new Uint8ClampedArray([
    0, 0, 0, 128,
    255, 255, 255, 64,
  ]);

  assert.equal(typeof depth.colorizeDepthPixelsInPlace, "function");
  const result = depth.colorizeDepthPixelsInPlace(source);

  assert.equal(result, source);
  assert.deepEqual(
    [...source],
    [
      12, 35, 84, 128,
      210, 35, 42, 64,
    ],
  );
});

test("완전한 RGBA 픽셀이 아니면 거부한다", () => {
  assert.throws(
    () => colorizeDepthPixels(new Uint8ClampedArray([0, 0, 0])),
    RangeError,
  );
});
