export class FrameLoadError extends Error {
  constructor(failures) {
    super(`프레임 이미지 ${failures.length}개를 불러오지 못했습니다.`);
    this.name = "FrameLoadError";
    this.failures = failures;
  }
}

const DEFAULT_IMAGE_TIMEOUT_MS = 10_000;

export function loadImage(
  url,
  ImageConstructor = globalThis.Image,
  { timeoutMs = DEFAULT_IMAGE_TIMEOUT_MS } = {},
) {
  return new Promise((resolve, reject) => {
    if (typeof ImageConstructor !== "function") {
      reject(new Error("이 환경에서는 이미지를 불러올 수 없습니다."));
      return;
    }

    const image = new ImageConstructor();
    image.decoding = "async";
    let settled = false;
    let timeoutId;
    const clearHandlers = () => {
      image.onload = null;
      image.onerror = null;
    };
    const settle = (callback, value, { cancel = false } = {}) => {
      if (settled) {
        return;
      }

      settled = true;
      clearTimeout(timeoutId);
      clearHandlers();
      if (cancel && typeof image.removeAttribute === "function") {
        image.removeAttribute("src");
      }
      callback(value);
    };
    image.onload = () => {
      settle(resolve, image);
    };
    image.onerror = () => {
      settle(reject, new Error(`이미지 요청에 실패했습니다: ${url}`));
    };
    timeoutId = setTimeout(() => {
      const error = new Error(`이미지 요청이 ${timeoutMs}ms 후 시간 초과되었습니다: ${url}`);
      error.name = "TimeoutError";
      settle(reject, error, { cancel: true });
    }, timeoutMs);

    try {
      image.src = url;
    } catch (cause) {
      settle(reject, cause);
    }
  });
}

export async function preloadFrames(
  frames,
  {
    imageLoader = loadImage,
    ImageConstructor = globalThis.Image,
    imageTimeoutMs = DEFAULT_IMAGE_TIMEOUT_MS,
    onProgress = () => {},
  } = {},
) {
  const assets = frames.flatMap((frame, framePosition) => [
    {
      framePosition,
      frameIndex: frame.index,
      kind: "color",
      url: frame.colorUrl,
    },
    {
      framePosition,
      frameIndex: frame.index,
      kind: "depth",
      url: frame.depthUrl,
    },
  ]);

  let completed = 0;
  const total = assets.length;

  const settled = await Promise.all(
    assets.map(async (asset) => {
      try {
        const image = await imageLoader(asset.url, ImageConstructor, {
          timeoutMs: imageTimeoutMs,
        });
        return { ...asset, image };
      } catch (cause) {
        return { ...asset, cause };
      } finally {
        completed += 1;
        onProgress({
          completed,
          total,
          percent: Math.round((completed / total) * 100),
        });
      }
    }),
  );

  const failures = settled
    .filter((asset) => !asset.image)
    .map(({ frameIndex, kind, url, cause }) => ({ frameIndex, kind, url, cause }));

  if (failures.length > 0) {
    throw new FrameLoadError(failures);
  }

  return frames.map((frame, framePosition) => {
    const colorAsset = settled[framePosition * 2];
    const depthAsset = settled[framePosition * 2 + 1];

    return {
      ...frame,
      colorImage: colorAsset.image,
      depthImage: depthAsset.image,
    };
  });
}
