export class FrameLoadError extends Error {
  constructor(failures) {
    super(`프레임 이미지 ${failures.length}개를 불러오지 못했습니다.`);
    this.name = "FrameLoadError";
    this.failures = failures;
  }
}

export function loadImage(url, ImageConstructor = globalThis.Image) {
  return new Promise((resolve, reject) => {
    if (typeof ImageConstructor !== "function") {
      reject(new Error("이 환경에서는 이미지를 불러올 수 없습니다."));
      return;
    }

    const image = new ImageConstructor();
    image.decoding = "async";
    const clearHandlers = () => {
      image.onload = null;
      image.onerror = null;
    };
    image.onload = () => {
      clearHandlers();
      resolve(image);
    };
    image.onerror = () => {
      clearHandlers();
      reject(new Error(`이미지 요청에 실패했습니다: ${url}`));
    };
    image.src = url;
  });
}

export async function preloadFrames(
  frames,
  { imageLoader = loadImage, onProgress = () => {} } = {},
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
        const image = await imageLoader(asset.url);
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
