import { ManifestError, validateManifest } from "./manifest.js";
import type { SequenceManifest } from "./manifest.js";

type UnknownRecord = Record<string, unknown>;

const MAGIC = "SIVPACK1";
const MAGIC_SIZE = 8;
const PREFIX_SIZE = 12;
const SUPPORTED_VERSION = 1;
const DEFAULT_BUNDLE_TIMEOUT_MS = 10_000;
const PNG_SIGNATURE = Uint8Array.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

export class BundleError extends Error {
  constructor(message: string, options: ErrorOptions = {}) {
    super(message, options);
    this.name = "BundleError";
  }
}

export interface BundleAssetIndex {
  path: string;
  mediaType: "image/png";
  offset: number;
  length: number;
}

export interface BundleSequenceSource {
  kind: "bundle";
  sourceUrl: string;
  manifest: SequenceManifest;
  assets: readonly BundleAssetIndex[];
  objectUrls: readonly string[];
  buffer: ArrayBuffer;
  payloadOffset: number;
  revoke(): void;
}

export interface BundleAccessOrders {
  sequential: readonly number[];
  random: readonly number[];
}

export interface BundleBenchmarkMetric {
  averageMsPerImage: number;
  sampleCount: number;
  checksum: number;
}

export interface BundleBenchmarkResult {
  sequential: BundleBenchmarkMetric;
  random: BundleBenchmarkMetric;
}

interface ParseBundleOptions {
  createObjectUrl?: (blob: Blob) => string;
  revokeObjectUrl?: (url: string) => void;
}

interface BenchmarkOptions {
  passes?: number;
  seed?: number;
  now?: () => number;
}

interface BundleResponse {
  ok: boolean;
  status?: number;
  url?: string;
  arrayBuffer(): Promise<ArrayBuffer>;
}

type BundleFetch = (
  url: string,
  init: { cache: "no-store"; signal: AbortSignal },
) => Promise<BundleResponse>;

interface LoadBundleOptions extends ParseBundleOptions {
  timeoutMs?: number;
}

function requireRecord(value: unknown, field: string): asserts value is UnknownRecord {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new BundleError(`${field}는 객체여야 합니다.`);
  }
}

function requireString(value: unknown, field: string): string {
  if (typeof value !== "string" || value.trim() === "" || value !== value.trim()) {
    throw new BundleError(`${field}는 공백 없는 문자열이어야 합니다.`);
  }
  return value;
}

function requireSafeInteger(value: unknown, field: string, { positive = false } = {}): number {
  if (
    typeof value !== "number" ||
    !Number.isSafeInteger(value) ||
    value < 0 ||
    (positive && value === 0)
  ) {
    throw new BundleError(`${field}는 ${positive ? "양의 " : "0 이상의 "}정수여야 합니다.`);
  }
  return value;
}

function hasPngSignature(bytes: Uint8Array, start: number, length: number): boolean {
  if (length < PNG_SIGNATURE.byteLength) return false;
  return PNG_SIGNATURE.every((value, index) => bytes[start + index] === value);
}

function defaultCreateObjectUrl(blob: Blob): string {
  if (typeof URL.createObjectURL !== "function") {
    throw new BundleError("이 브라우저는 Blob URL을 만들 수 없습니다.");
  }
  return URL.createObjectURL(blob);
}

function defaultRevokeObjectUrl(url: string): void {
  URL.revokeObjectURL?.(url);
}

export function parseBundle(
  buffer: ArrayBuffer,
  sourceUrl: string,
  {
    createObjectUrl = defaultCreateObjectUrl,
    revokeObjectUrl = defaultRevokeObjectUrl,
  }: ParseBundleOptions = {},
): BundleSequenceSource {
  const bytes = new Uint8Array(buffer);
  if (bytes.byteLength <= PREFIX_SIZE) {
    throw new BundleError("SIV 파일이 header와 payload를 포함하지 않습니다.");
  }

  for (let index = 0; index < MAGIC_SIZE; index += 1) {
    if (bytes[index] !== MAGIC.charCodeAt(index)) {
      throw new BundleError("SIV magic이 올바르지 않습니다.");
    }
  }

  const headerLength = new DataView(buffer).getUint32(MAGIC_SIZE, true);
  const headerEnd = PREFIX_SIZE + headerLength;
  if (headerLength === 0 || headerEnd > bytes.byteLength) {
    throw new BundleError("SIV header 길이가 파일 범위를 벗어납니다.");
  }

  let headerValue: unknown;
  try {
    const headerText = new TextDecoder("utf-8", { fatal: true }).decode(
      bytes.subarray(PREFIX_SIZE, headerEnd),
    );
    headerValue = JSON.parse(headerText) as unknown;
  } catch (cause) {
    throw new BundleError("SIV header가 올바른 UTF-8 JSON이 아닙니다.", { cause });
  }

  requireRecord(headerValue, "SIV header");
  if (headerValue.format !== "ship-image-sequence") {
    throw new BundleError("지원하지 않는 SIV format입니다.");
  }
  if (headerValue.version !== SUPPORTED_VERSION) {
    throw new BundleError(`지원하지 않는 SIV version입니다: ${String(headerValue.version)}`);
  }
  const manifestJson = requireString(headerValue.manifest_json, "manifest_json");
  if (!Array.isArray(headerValue.assets) || headerValue.assets.length === 0) {
    throw new BundleError("SIV assets는 비어 있지 않은 배열이어야 합니다.");
  }

  const payloadLength = bytes.byteLength - headerEnd;
  const seenPaths = new Set<string>();
  let expectedOffset = 0;
  const assets = headerValue.assets.map((value, position): BundleAssetIndex => {
    const field = `assets[${position}]`;
    requireRecord(value, field);
    const path = requireString(value.path, `${field}.path`);
    if (seenPaths.has(path)) {
      throw new BundleError(`${field}.path가 중복되었습니다: ${path}`);
    }
    if (value.media_type !== "image/png") {
      throw new BundleError(`${field}.media_type은 image/png여야 합니다.`);
    }
    const offset = requireSafeInteger(value.offset, `${field}.offset`);
    const length = requireSafeInteger(value.length, `${field}.length`, { positive: true });
    if (offset !== expectedOffset || offset + length > payloadLength) {
      throw new BundleError(`${field}의 payload 범위가 연속된 파일 경계와 맞지 않습니다.`);
    }
    if (!hasPngSignature(bytes, headerEnd + offset, length)) {
      throw new BundleError(`${field}의 payload가 PNG signature를 갖지 않습니다.`);
    }
    expectedOffset += length;
    seenPaths.add(path);
    return { path, mediaType: "image/png", offset, length };
  });
  if (expectedOffset !== payloadLength) {
    throw new BundleError("SIV payload에 index가 가리키지 않는 bytes가 있습니다.");
  }

  let manifestValue: unknown;
  try {
    manifestValue = JSON.parse(manifestJson) as unknown;
  } catch (cause) {
    throw new BundleError("manifest_json이 올바른 JSON이 아닙니다.", { cause });
  }

  let validatedManifest: SequenceManifest;
  try {
    validatedManifest = validateManifest(manifestValue, sourceUrl);
  } catch (cause) {
    if (cause instanceof ManifestError) {
      throw new BundleError(`SIV 내부 manifest가 올바르지 않습니다. ${cause.message}`, { cause });
    }
    throw cause;
  }

  const assetByPath = new Map(assets.map((asset) => [asset.path, asset] as const));
  for (const frame of validatedManifest.frames) {
    for (const source of [frame.color, frame.depth]) {
      if (!assetByPath.has(source.sourcePath)) {
        throw new BundleError(`manifest asset이 SIV index에 없습니다: ${source.sourcePath}`);
      }
    }
  }

  const objectUrls: string[] = [];
  const objectUrlByPath = new Map<string, string>();
  try {
    for (const asset of assets) {
      const start = headerEnd + asset.offset;
      const blob = new Blob([buffer.slice(start, start + asset.length)], {
        type: asset.mediaType,
      });
      const objectUrl = createObjectUrl(blob);
      if (typeof objectUrl !== "string" || objectUrl === "") {
        throw new BundleError(`Blob URL 생성 결과가 비어 있습니다: ${asset.path}`);
      }
      objectUrls.push(objectUrl);
      objectUrlByPath.set(asset.path, objectUrl);
    }
  } catch (cause) {
    for (const objectUrl of objectUrls) revokeObjectUrl(objectUrl);
    if (cause instanceof BundleError) throw cause;
    throw new BundleError("SIV asset의 Blob URL을 만들지 못했습니다.", { cause });
  }

  const manifestWithObjectUrls: SequenceManifest = {
    ...validatedManifest,
    frames: validatedManifest.frames.map((frame) => ({
      ...frame,
      color: {
        ...frame.color,
        url: objectUrlByPath.get(frame.color.sourcePath) ?? "",
      },
      depth: {
        ...frame.depth,
        url: objectUrlByPath.get(frame.depth.sourcePath) ?? "",
      },
    })),
  };

  let revoked = false;
  return {
    kind: "bundle",
    sourceUrl,
    manifest: manifestWithObjectUrls,
    assets,
    objectUrls,
    buffer,
    payloadOffset: headerEnd,
    revoke() {
      if (revoked) return;
      revoked = true;
      for (const objectUrl of objectUrls) revokeObjectUrl(objectUrl);
    },
  };
}

function nextXorshift32(state: number): number {
  let next = state >>> 0;
  next ^= next << 13;
  next ^= next >>> 17;
  next ^= next << 5;
  return next >>> 0;
}

export function createBundleAccessOrders(
  assetCount: number,
  seed = 0x51f15e5d,
): BundleAccessOrders {
  if (!Number.isSafeInteger(assetCount) || assetCount < 1) {
    throw new BundleError("benchmark asset 수는 1 이상의 정수여야 합니다.");
  }
  if (!Number.isSafeInteger(seed) || seed < 0 || seed > 0xffffffff) {
    throw new BundleError("benchmark seed는 uint32 범위의 정수여야 합니다.");
  }

  const sequential = Array.from({ length: assetCount }, (_value, index) => index);
  const random = [...sequential];
  let state = seed >>> 0;
  for (let index = random.length - 1; index > 0; index -= 1) {
    state = nextXorshift32(state);
    const swapIndex = state % (index + 1);
    [random[index], random[swapIndex]] = [random[swapIndex]!, random[index]!];
  }
  return { sequential, random };
}

export function benchmarkBundleAccess(
  source: BundleSequenceSource,
  {
    passes = 3,
    seed = 0x51f15e5d,
    now = () => performance.now(),
  }: BenchmarkOptions = {},
): BundleBenchmarkResult {
  if (!Number.isSafeInteger(passes) || passes < 1) {
    throw new BundleError("benchmark pass 수는 1 이상의 정수여야 합니다.");
  }
  const orders = createBundleAccessOrders(source.assets.length, seed);

  const copyAsset = (assetIndex: number): Uint8Array => {
    const asset = source.assets[assetIndex];
    if (!asset) throw new BundleError(`benchmark asset index가 없습니다: ${assetIndex}`);
    const start = source.payloadOffset + asset.offset;
    return new Uint8Array(source.buffer.slice(start, start + asset.length));
  };

  for (const assetIndex of orders.sequential) copyAsset(assetIndex);

  const measure = (order: readonly number[]): BundleBenchmarkMetric => {
    let checksum = 0;
    const startedAt = now();
    for (let pass = 0; pass < passes; pass += 1) {
      for (const assetIndex of order) {
        const copy = copyAsset(assetIndex);
        checksum = (checksum + (copy[0] ?? 0) + (copy.at(-1) ?? 0)) >>> 0;
      }
    }
    const endedAt = now();
    const elapsedMs = endedAt - startedAt;
    const sampleCount = order.length * passes;
    if (!Number.isFinite(elapsedMs) || elapsedMs < 0) {
      throw new BundleError("benchmark clock 결과가 올바르지 않습니다.");
    }
    return {
      averageMsPerImage: elapsedMs / sampleCount,
      sampleCount,
      checksum,
    };
  };

  return {
    sequential: measure(orders.sequential),
    random: measure(orders.random),
  };
}

const defaultBundleFetch: BundleFetch = async (url, init) => globalThis.fetch(url, init);

export async function loadBundle(
  url: string,
  fetchImpl: BundleFetch = defaultBundleFetch,
  {
    timeoutMs = DEFAULT_BUNDLE_TIMEOUT_MS,
    createObjectUrl,
    revokeObjectUrl,
  }: LoadBundleOptions = {},
): Promise<BundleSequenceSource> {
  const controller = new AbortController();
  let timeoutId: ReturnType<typeof setTimeout> | undefined;
  const timeout = new Promise<never>((_resolve, reject) => {
    timeoutId = setTimeout(() => {
      const cause = new Error(`SIV 요청이 ${timeoutMs}ms 안에 끝나지 않았습니다.`);
      cause.name = "TimeoutError";
      controller.abort(cause);
      reject(cause);
    }, timeoutMs);
  });

  const request = Promise.resolve().then(async () => {
    let response: BundleResponse;
    try {
      response = await fetchImpl(url, { cache: "no-store", signal: controller.signal });
    } catch (cause) {
      throw new BundleError("SIV 파일을 가져오지 못했습니다.", { cause });
    }
    if (!response?.ok) {
      throw new BundleError(`SIV 요청이 실패했습니다. HTTP 상태: ${response?.status ?? "알 수 없음"}.`);
    }
    let buffer: ArrayBuffer;
    try {
      buffer = await response.arrayBuffer();
    } catch (cause) {
      throw new BundleError("SIV 응답 bytes를 읽지 못했습니다.", { cause });
    }
    const resolvedUrl =
      response.url || new URL(url, globalThis.location?.href ?? "http://localhost/").href;
    return parseBundle(buffer, resolvedUrl, { createObjectUrl, revokeObjectUrl });
  });

  try {
    return await Promise.race([request, timeout]);
  } catch (cause) {
    if (cause instanceof Error && cause.name === "TimeoutError") {
      throw new BundleError(`SIV 요청이 ${timeoutMs}ms 뒤 시간 초과되었습니다.`, { cause });
    }
    throw cause;
  } finally {
    if (timeoutId !== undefined) clearTimeout(timeoutId);
  }
}
