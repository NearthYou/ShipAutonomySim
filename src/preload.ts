import type { FrameKind, SequenceFrame } from "./manifest.js";

export interface ColorFrameData<TImage = HTMLImageElement> {
  kind: "color";
  sourcePath: string;
  url: string;
  image: TImage;
}

export interface DepthFrameData<TImage = HTMLImageElement> {
  kind: "depth";
  sourcePath: string;
  url: string;
  image: TImage;
}

export interface PreloadedFrame<TImage = HTMLImageElement> {
  index: number;
  timeMs: number;
  color: ColorFrameData<TImage>;
  depth: DepthFrameData<TImage>;
}

export interface FrameAssetRequest {
  framePosition: number;
  frameIndex: number;
  kind: FrameKind;
  sourcePath: string;
  url: string;
}

type AssetLoadResult<TImage> =
  | { ok: true; request: FrameAssetRequest; image: TImage }
  | { ok: false; request: FrameAssetRequest; cause: unknown };

export interface PreloadProgress {
  completed: number;
  total: number;
  percent: number;
}

export interface FrameLoadFailure {
  frameIndex: number;
  kind: FrameKind;
  url: string;
  cause: unknown;
}

export interface LoadableImage {
  decoding: "sync" | "async" | "auto";
  onload: ((event: Event) => void) | null;
  onerror: ((event: Event) => void) | null;
  src: string;
  removeAttribute?(name: string): void;
}

export type ImageConstructor<TImage extends LoadableImage> = new () => TImage;
export type ImageLoader<TImage extends LoadableImage> = (
  url: string,
  ImageType?: ImageConstructor<TImage>,
  options?: { timeoutMs?: number },
) => Promise<TImage>;

export class FrameLoadError extends Error {
  readonly failures: readonly FrameLoadFailure[];

  constructor(failures: readonly FrameLoadFailure[]) {
    super(`프레임 이미지 ${failures.length}개를 불러오지 못했습니다.`);
    this.name = "FrameLoadError";
    this.failures = failures;
  }
}

const DEFAULT_IMAGE_TIMEOUT_MS = 10_000;

export function loadImage<TImage extends LoadableImage = HTMLImageElement>(
  url: string,
  ImageType: ImageConstructor<TImage> = globalThis.Image as unknown as ImageConstructor<TImage>,
  { timeoutMs = DEFAULT_IMAGE_TIMEOUT_MS }: { timeoutMs?: number } = {},
): Promise<TImage> {
  return new Promise((resolve, reject) => {
    if (typeof ImageType !== "function") {
      reject(new Error("이 환경에서는 이미지를 불러올 수 없습니다."));
      return;
    }

    const image = new ImageType();
    image.decoding = "async";
    let settled = false;
    let timeoutId: ReturnType<typeof setTimeout> | undefined;
    const clearHandlers = (): void => {
      image.onload = null;
      image.onerror = null;
    };
    const settle = (callback: () => void, { cancel = false }: { cancel?: boolean } = {}): void => {
      if (settled) return;

      settled = true;
      if (timeoutId !== undefined) clearTimeout(timeoutId);
      clearHandlers();
      if (cancel && typeof image.removeAttribute === "function") {
        image.removeAttribute("src");
      }
      callback();
    };
    image.onload = () => {
      settle(() => resolve(image));
    };
    image.onerror = () => {
      settle(() => reject(new Error(`이미지 요청에 실패했습니다: ${url}`)));
    };
    timeoutId = setTimeout(() => {
      const error = new Error(`이미지 요청이 ${timeoutMs}ms 후 시간 초과되었습니다: ${url}`);
      error.name = "TimeoutError";
      settle(() => reject(error), { cancel: true });
    }, timeoutMs);

    try {
      image.src = url;
    } catch (cause) {
      settle(() => reject(cause));
    }
  });
}

interface PreloadOptions<TImage extends LoadableImage> {
  imageLoader?: ImageLoader<TImage>;
  ImageConstructor?: ImageConstructor<TImage>;
  imageTimeoutMs?: number;
  onProgress?: (progress: PreloadProgress) => void;
}

export async function preloadFrames<
  TImage extends LoadableImage = HTMLImageElement,
>(
  frames: readonly SequenceFrame[],
  {
    imageLoader = loadImage,
    ImageConstructor,
    imageTimeoutMs = DEFAULT_IMAGE_TIMEOUT_MS,
    onProgress = () => {},
  }: PreloadOptions<TImage> = {},
): Promise<readonly PreloadedFrame<TImage>[]> {
  const assets: FrameAssetRequest[] = frames.flatMap((frame, framePosition) => [
    {
      framePosition,
      frameIndex: frame.index,
      kind: frame.color.kind,
      sourcePath: frame.color.sourcePath,
      url: frame.color.url,
    },
    {
      framePosition,
      frameIndex: frame.index,
      kind: frame.depth.kind,
      sourcePath: frame.depth.sourcePath,
      url: frame.depth.url,
    },
  ]);

  let completed = 0;
  const total = assets.length;

  const settled: AssetLoadResult<TImage>[] = await Promise.all(
    assets.map(async (request): Promise<AssetLoadResult<TImage>> => {
      try {
        const image = await imageLoader(request.url, ImageConstructor, {
          timeoutMs: imageTimeoutMs,
        });
        return { ok: true, request, image };
      } catch (cause) {
        return { ok: false, request, cause };
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

  const failures: FrameLoadFailure[] = settled.flatMap((result) =>
    result.ok
      ? []
      : [
          {
            frameIndex: result.request.frameIndex,
            kind: result.request.kind,
            url: result.request.url,
            cause: result.cause,
          },
        ],
  );

  if (failures.length > 0) {
    throw new FrameLoadError(failures);
  }

  return frames.map((frame, framePosition): PreloadedFrame<TImage> => {
    const colorResult = settled[framePosition * 2];
    const depthResult = settled[framePosition * 2 + 1];

    if (!colorResult?.ok || !depthResult?.ok) {
      throw new Error(`프레임 ${frame.index}의 선로딩 결과를 구성할 수 없습니다.`);
    }

    return {
      index: frame.index,
      timeMs: frame.timeMs,
      color: {
        kind: "color",
        sourcePath: frame.color.sourcePath,
        url: frame.color.url,
        image: colorResult.image,
      },
      depth: {
        kind: "depth",
        sourcePath: frame.depth.sourcePath,
        url: frame.depth.url,
        image: depthResult.image,
      },
    };
  });
}
