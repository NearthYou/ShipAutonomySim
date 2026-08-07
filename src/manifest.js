export class ManifestError extends Error {
  constructor(message, options = {}) {
    super(message, options);
    this.name = "ManifestError";
  }
}

const DEFAULT_MANIFEST_TIMEOUT_MS = 10_000;

function requireRecord(value, field) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new ManifestError(`${field}은 객체여야 합니다.`);
  }
}

function requirePositiveInteger(value, field) {
  if (!Number.isInteger(value) || value < 1) {
    throw new ManifestError(`${field}은 1 이상의 정수여야 합니다.`);
  }
}

function requireFiniteNumber(value, field, { minimum = null, positive = false } = {}) {
  if (!Number.isFinite(value)) {
    throw new ManifestError(`${field}은 유한한 숫자여야 합니다.`);
  }

  if (positive && value <= 0) {
    throw new ManifestError(`${field}은 0보다 커야 합니다.`);
  }

  if (minimum !== null && value < minimum) {
    throw new ManifestError(`${field}은 ${minimum} 이상이어야 합니다.`);
  }
}

function requireNonEmptyString(value, field) {
  if (typeof value !== "string" || value.trim() === "") {
    throw new ManifestError(`${field}은 비어 있지 않은 문자열이어야 합니다.`);
  }

  return value.trim();
}

function resolveFrameUrl(path, manifestUrl, field) {
  let resolvedUrl;
  let resolvedManifestUrl;
  try {
    resolvedUrl = new URL(path, manifestUrl);
    resolvedManifestUrl = new URL(manifestUrl);
  } catch (cause) {
    throw new ManifestError(`${field} 경로를 해석할 수 없습니다.`, { cause });
  }

  if (resolvedUrl.origin !== resolvedManifestUrl.origin) {
    throw new ManifestError(
      `${field}는 manifest.json과 동일한 출처여야 합니다. manifest 기준 상대 경로 또는 같은 출처의 절대 URL을 사용하세요.`,
    );
  }

  return resolvedUrl.href;
}

export function validateManifest(value, manifestUrl) {
  requireRecord(value, "manifest 최상위 값");
  requirePositiveInteger(value.frame_count, "frame_count");
  requireFiniteNumber(value.interval_ms, "interval_ms", { positive: true });
  requireFiniteNumber(value.depth_near_cm, "depth_near_cm");
  requireFiniteNumber(value.depth_far_cm, "depth_far_cm");

  if (value.depth_near_cm >= value.depth_far_cm) {
    throw new ManifestError("depth_near_cm은 depth_far_cm보다 작아야 합니다.");
  }

  if (!Array.isArray(value.frames)) {
    throw new ManifestError("frames는 프레임 객체 배열이어야 합니다.");
  }

  if (value.frames.length !== value.frame_count) {
    throw new ManifestError(
      `frame_count(${value.frame_count})와 frames 길이(${value.frames.length})가 일치해야 합니다.`,
    );
  }

  let previousTime = -1;
  const frames = value.frames.map((frame, position) => {
    const field = `frames[${position}]`;
    requireRecord(frame, field);

    if (frame.index !== position) {
      throw new ManifestError(`${field}.index는 ${position}이어야 합니다.`);
    }

    const color = requireNonEmptyString(frame.color, `${field}.color`);
    const depth = requireNonEmptyString(frame.depth, `${field}.depth`);
    requireFiniteNumber(frame.time_ms, `${field}.time_ms`, { minimum: 0 });

    if (frame.time_ms < previousTime) {
      throw new ManifestError(`${field}.time_ms는 이전 프레임 시간보다 작을 수 없습니다.`);
    }

    previousTime = frame.time_ms;

    return {
      ...frame,
      color,
      depth,
      colorUrl: resolveFrameUrl(color, manifestUrl, `${field}.color`),
      depthUrl: resolveFrameUrl(depth, manifestUrl, `${field}.depth`),
    };
  });

  return {
    ...value,
    frames,
  };
}

export async function loadManifest(
  url = "./manifest.json",
  fetchImpl = globalThis.fetch,
  { timeoutMs = DEFAULT_MANIFEST_TIMEOUT_MS } = {},
) {
  const controller = new AbortController();
  let timeoutId;
  const timeout = new Promise((resolve, reject) => {
    timeoutId = setTimeout(() => {
      const cause = new Error(`manifest 요청이 ${timeoutMs}ms 안에 끝나지 않았습니다.`);
      cause.name = "TimeoutError";
      reject(cause);
      controller.abort(cause);
    }, timeoutMs);
  });
  const request = Promise.resolve().then(async () => {
    let response;
    try {
      response = await fetchImpl(url, { cache: "no-store", signal: controller.signal });
    } catch (cause) {
      throw new ManifestError(
        "manifest.json을 가져오지 못했습니다. 로컬 HTTP 서버와 파일 위치를 확인하세요.",
        { cause },
      );
    }

    if (!response?.ok) {
      const status = response?.status ?? "알 수 없음";
      throw new ManifestError(
        `manifest.json 요청에 실패했습니다. HTTP 상태: ${status}. 파일 위치를 확인하세요.`,
      );
    }

    let value;
    try {
      value = await response.json();
    } catch (cause) {
      throw new ManifestError("manifest.json이 올바른 JSON 형식이 아닙니다.", { cause });
    }

    return { response, value };
  });

  let result;
  try {
    result = await Promise.race([request, timeout]);
  } catch (cause) {
    if (cause?.name === "TimeoutError") {
      throw new ManifestError(
        `manifest.json 요청이 ${timeoutMs}ms 후 시간 초과되었습니다. 파일 경로와 로컬 HTTP 서버 응답 상태를 확인하세요.`,
        { cause },
      );
    }

    throw cause;
  } finally {
    clearTimeout(timeoutId);
  }

  const { response, value } = result;
  const baseUrl = response.url || new URL(url, globalThis.location?.href ?? "http://localhost/").href;
  return validateManifest(value, baseUrl);
}
