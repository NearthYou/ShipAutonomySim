import assert from "node:assert/strict";
import test from "node:test";

import {
  BundleError,
  benchmarkBundleAccess,
  createBundleAccessOrders,
  parseBundle,
} from "../src/bundle.js";

interface HeaderAssetFixture {
  path: string;
  media_type: string;
  offset: number;
  length: number;
}

interface HeaderFixture {
  format: string;
  version: number;
  manifest_json: string;
  assets: HeaderAssetFixture[];
}

const png = (marker: number): Uint8Array =>
  Uint8Array.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, marker]);

const manifest = {
  frame_count: 2,
  interval_ms: 100,
  depth_near_cm: 0,
  depth_far_cm: 2500,
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

const assetFixtures = [
  { path: "color_000000.png", bytes: png(1) },
  { path: "depth_000000.png", bytes: png(2) },
  { path: "color_000001.png", bytes: png(3) },
  { path: "depth_000001.png", bytes: png(4) },
];

function makeBundle(
  mutateHeader: (header: HeaderFixture) => void = () => {},
  mutatePayload: (payload: Uint8Array) => void = () => {},
): ArrayBuffer {
  let offset = 0;
  const assets = assetFixtures.map(({ path, bytes }) => {
    const asset = {
      path,
      media_type: "image/png",
      offset,
      length: bytes.byteLength,
    };
    offset += bytes.byteLength;
    return asset;
  });
  const header: HeaderFixture = {
    format: "ship-image-sequence",
    version: 1,
    manifest_json: JSON.stringify(manifest),
    assets,
  };
  mutateHeader(header);

  const headerBytes = new TextEncoder().encode(JSON.stringify(header));
  const payload = new Uint8Array(offset);
  let payloadOffset = 0;
  for (const { bytes } of assetFixtures) {
    payload.set(bytes, payloadOffset);
    payloadOffset += bytes.byteLength;
  }
  mutatePayload(payload);

  const bundle = new Uint8Array(12 + headerBytes.byteLength + payload.byteLength);
  bundle.set(new TextEncoder().encode("SIVPACK1"), 0);
  new DataView(bundle.buffer).setUint32(8, headerBytes.byteLength, true);
  bundle.set(headerBytes, 12);
  bundle.set(payload, 12 + headerBytes.byteLength);
  return bundle.buffer;
}

test("SIV를 검증하고 manifest 프레임을 Blob URL로 연결한다", () => {
  const created: Array<{ url: string; size: number; type: string }> = [];
  const revoked: string[] = [];
  const source = parseBundle(makeBundle(), "https://example.test/run/sequence.siv", {
    createObjectUrl(blob) {
      const url = `blob:fixture/${created.length}`;
      created.push({ url, size: blob.size, type: blob.type });
      return url;
    },
    revokeObjectUrl(url) {
      revoked.push(url);
    },
  });

  assert.equal(source.manifest.frameCount, 2);
  assert.deepEqual(source.manifest.depthRange, { nearCm: 0, farCm: 2500 });
  assert.equal(source.manifest.frames[0]?.color.url, "blob:fixture/0");
  assert.equal(source.manifest.frames[0]?.depth.url, "blob:fixture/1");
  assert.equal(source.manifest.frames[1]?.color.url, "blob:fixture/2");
  assert.equal(source.manifest.frames[1]?.depth.url, "blob:fixture/3");
  assert.deepEqual(
    created,
    [0, 1, 2, 3].map((index) => ({
      url: `blob:fixture/${index}`,
      size: 9,
      type: "image/png",
    })),
  );

  source.revoke();
  source.revoke();
  assert.deepEqual(revoked, created.map(({ url }) => url));
});

test("잘못된 magic을 거부한다", () => {
  const bytes = new Uint8Array(makeBundle());
  bytes[0] = 0;
  assert.throws(() => parseBundle(bytes.buffer, "https://example.test/sequence.siv"), BundleError);
});

test("파일 밖으로 넘어가는 header 길이를 거부한다", () => {
  const buffer = makeBundle();
  new DataView(buffer).setUint32(8, buffer.byteLength, true);
  assert.throws(() => parseBundle(buffer, "https://example.test/sequence.siv"), BundleError);
});

test("지원하지 않는 SIV version을 거부한다", () => {
  const buffer = makeBundle((header) => {
    header.version = 2;
  });
  assert.throws(() => parseBundle(buffer, "https://example.test/sequence.siv"), BundleError);
});

test("중복 asset 경로를 거부한다", () => {
  const buffer = makeBundle((header) => {
    const firstPath = header.assets[0]?.path;
    if (firstPath && header.assets[1]) header.assets[1].path = firstPath;
  });
  assert.throws(() => parseBundle(buffer, "https://example.test/sequence.siv"), BundleError);
});

test("payload 범위를 넘는 asset을 거부한다", () => {
  const buffer = makeBundle((header) => {
    const asset = header.assets.at(-1);
    if (asset) asset.offset = 1_000_000;
  });
  assert.throws(() => parseBundle(buffer, "https://example.test/sequence.siv"), BundleError);
});

test("manifest가 요구하는 asset이 없으면 거부한다", () => {
  const buffer = makeBundle((header) => {
    header.assets.pop();
  });
  assert.throws(() => parseBundle(buffer, "https://example.test/sequence.siv"), BundleError);
});

test("PNG signature가 아닌 payload를 거부한다", () => {
  const buffer = makeBundle(undefined, (payload) => {
    payload[0] = 0;
  });
  assert.throws(() => parseBundle(buffer, "https://example.test/sequence.siv"), BundleError);
});

test("고정 seed 접근 순서와 compressed byte 복사 평균을 재현한다", () => {
  const source = parseBundle(makeBundle(), "https://example.test/run/sequence.siv", {
    createObjectUrl: (_blob) => "blob:fixture",
    revokeObjectUrl: (_url) => {},
  });
  assert.deepEqual(createBundleAccessOrders(4, 0x12345678), {
    sequential: [0, 1, 2, 3],
    random: [3, 0, 2, 1],
  });

  const clockValues = [10, 22, 40, 60];
  const result = benchmarkBundleAccess(source, {
    passes: 3,
    seed: 0x12345678,
    now: () => clockValues.shift() ?? 60,
  });

  assert.equal(result.sequential.sampleCount, 12);
  assert.equal(result.sequential.averageMsPerImage, 1);
  assert.equal(result.sequential.checksum, 1674);
  assert.equal(result.random.sampleCount, 12);
  assert.equal(result.random.averageMsPerImage, 20 / 12);
  assert.equal(result.random.checksum, 1674);
  source.revoke();
});
