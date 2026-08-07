import { colorizeDepthPixelsInPlace } from "./depth.js";
import { loadManifest, ManifestError } from "./manifest.js";
import type { FrameKind, SequenceManifest } from "./manifest.js";
import { SequencePlayer } from "./player.js";
import { FrameLoadError, preloadFrames } from "./preload.js";
import type { PreloadedFrame, PreloadProgress } from "./preload.js";

export type DepthDisplayMode = "grayscale" | "colormap";
export type ViewerState = "loading" | "ready" | "playing" | "paused" | "error";

const VIEWER_STATE_LABELS: Record<ViewerState, string> = {
  loading: "로딩 중",
  ready: "준비됨",
  playing: "재생 중",
  paused: "일시정지",
  error: "오류",
};

const FRAME_KIND_LABELS: Record<FrameKind, string> = {
  color: "컬러",
  depth: "깊이",
};
const MAX_CAUSE_DESCRIPTION_LENGTH = 200;

interface ViewerElements {
  colorCanvas: HTMLCanvasElement;
  depthCanvas: HTMLCanvasElement;
  depthMode: HTMLButtonElement;
  depthModeBadge: HTMLSpanElement;
  frameReadout: HTMLOutputElement;
  frameSlider: HTMLInputElement;
  loadingLabel: HTMLSpanElement;
  loadingPercent: HTMLOutputElement;
  loadingProgress: HTMLProgressElement;
  nextFrame: HTMLButtonElement;
  playIcon: HTMLSpanElement;
  playLabel: HTMLSpanElement;
  playToggle: HTMLButtonElement;
  playbackSpeed: HTMLSelectElement;
  previousFrame: HTMLButtonElement;
  restart: HTMLButtonElement;
  timeReadout: HTMLOutputElement;
  viewerError: HTMLElement;
  viewerErrorMessage: HTMLParagraphElement;
  viewerState: HTMLDivElement;
}

interface LoadingUpdate extends Pick<PreloadProgress, "percent"> {
  label: string;
}

type DrawableImage = HTMLImageElement | HTMLCanvasElement;

export class FrameRenderError extends Error {
  readonly frameIndex: number;
  readonly kind: FrameKind;

  constructor(frameIndex: number, kind: FrameKind, cause: unknown) {
    super(`프레임 ${frameIndex}의 ${FRAME_KIND_LABELS[kind]} 렌더링에 실패했습니다.`, {
      cause,
    });
    this.name = "FrameRenderError";
    this.frameIndex = frameIndex;
    this.kind = kind;
  }
}

export function formatElapsed(timeMs: unknown): string {
  const safeTime = Math.max(0, Math.floor(Number(timeMs) || 0));
  const minutes = Math.floor(safeTime / 60_000);
  const seconds = Math.floor((safeTime % 60_000) / 1_000);
  const milliseconds = safeTime % 1_000;

  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${String(milliseconds).padStart(3, "0")}`;
}

function fileNameFromUrl(url: string): string {
  try {
    const path = new URL(url, "http://localhost/").pathname;
    return decodeURIComponent(path.split("/").filter(Boolean).at(-1) || url);
  } catch {
    return String(url);
  }
}

function stringProperty(value: unknown, property: "message" | "name"): string | undefined {
  if (value === null || typeof value !== "object" || !(property in value)) return undefined;
  const candidate = (value as Record<string, unknown>)[property];
  return typeof candidate === "string" ? candidate : undefined;
}

function normalizeCauseDescription(cause: unknown): string {
  let description = "";
  try {
    if (typeof cause === "string") {
      description = cause;
    } else {
      description = stringProperty(cause, "message") ?? "";
    }
  } catch {
    return "알 수 없는 오류";
  }

  const normalized = description
    .replace(/[\u0000-\u001f\u007f]+/g, " ")
    .replace(/\s+/g, " ")
    .trim();
  return normalized.slice(0, MAX_CAUSE_DESCRIPTION_LENGTH) || "알 수 없는 오류";
}

export function describeViewerError(error: unknown): string {
  if (error instanceof ManifestError) {
    return `${error.message} manifest.json의 내용과 로컬 서버 실행 상태를 확인하세요.`;
  }

  if (error instanceof FrameLoadError) {
    const names = error.failures.slice(0, 5).map((failure) => fileNameFromUrl(failure.url));
    const remaining = error.failures.length - names.length;
    const remainingText = remaining > 0 ? `, 그 외 ${remaining}개` : "";
    const timeoutText = error.failures.some(
      (failure) => stringProperty(failure.cause, "name") === "TimeoutError",
    )
      ? " 시간 초과가 계속되면 로컬 HTTP 서버 응답 상태를 확인하세요."
      : "";
    return `프레임 이미지 ${error.failures.length}개를 불러오지 못했습니다. 실패 파일: ${names.join(", ")}${remainingText}. 파일명과 manifest.json의 경로를 확인하세요.${timeoutText}`;
  }

  if (error instanceof FrameRenderError) {
    const kindLabel = FRAME_KIND_LABELS[error.kind];
    const causeDescription = normalizeCauseDescription(error.cause);
    return `프레임 ${error.frameIndex}의 ${kindLabel} 데이터를 렌더링하지 못했습니다. 해당 ${kindLabel} 이미지 파일을 확인하거나 다시 생성하세요. 원인: ${causeDescription}.`;
  }

  return "뷰어를 실행하는 중 오류가 발생했습니다. 페이지를 새로고침하고 더미 데이터를 다시 생성해 보세요.";
}

function requireElement<TElement extends HTMLElement>(root: Document, id: string): TElement {
  const element = root.getElementById(id);
  if (!element) {
    throw new Error(`필수 화면 요소를 찾을 수 없습니다: ${id}`);
  }
  return element as TElement;
}

function collectElements(root: Document): ViewerElements {
  return {
    colorCanvas: requireElement<HTMLCanvasElement>(root, "color-canvas"),
    depthCanvas: requireElement<HTMLCanvasElement>(root, "depth-canvas"),
    depthMode: requireElement<HTMLButtonElement>(root, "depth-mode"),
    depthModeBadge: requireElement<HTMLSpanElement>(root, "depth-mode-badge"),
    frameReadout: requireElement<HTMLOutputElement>(root, "frame-readout"),
    frameSlider: requireElement<HTMLInputElement>(root, "frame-slider"),
    loadingLabel: requireElement<HTMLSpanElement>(root, "loading-label"),
    loadingPercent: requireElement<HTMLOutputElement>(root, "loading-percent"),
    loadingProgress: requireElement<HTMLProgressElement>(root, "loading-progress"),
    nextFrame: requireElement<HTMLButtonElement>(root, "next-frame"),
    playIcon: requireElement<HTMLSpanElement>(root, "play-icon"),
    playLabel: requireElement<HTMLSpanElement>(root, "play-label"),
    playToggle: requireElement<HTMLButtonElement>(root, "play-toggle"),
    playbackSpeed: requireElement<HTMLSelectElement>(root, "playback-speed"),
    previousFrame: requireElement<HTMLButtonElement>(root, "previous-frame"),
    restart: requireElement<HTMLButtonElement>(root, "restart"),
    timeReadout: requireElement<HTMLOutputElement>(root, "time-readout"),
    viewerError: requireElement<HTMLElement>(root, "viewer-error"),
    viewerErrorMessage: requireElement<HTMLParagraphElement>(root, "viewer-error-message"),
    viewerState: requireElement<HTMLDivElement>(root, "viewer-state"),
  };
}

function imageSize(image: DrawableImage): { width: number; height: number } {
  const naturalWidth = "naturalWidth" in image ? image.naturalWidth : 0;
  const naturalHeight = "naturalHeight" in image ? image.naturalHeight : 0;
  const width = naturalWidth || image.width;
  const height = naturalHeight || image.height;
  if (!Number.isFinite(width) || width < 1 || !Number.isFinite(height) || height < 1) {
    throw new Error("이미지 크기를 확인할 수 없습니다.");
  }
  return { width, height };
}

function prepareCanvas(
  canvas: HTMLCanvasElement,
  image: DrawableImage,
): { context: CanvasRenderingContext2D; width: number; height: number } {
  const { width, height } = imageSize(image);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }

  const context = canvas.getContext("2d", { alpha: false });
  if (!context) {
    throw new Error("2D 캔버스를 사용할 수 없습니다.");
  }

  return { context, width, height };
}

class ViewerController {
  readonly root: Document;
  readonly elements: ViewerElements;
  readonly colorBufferCanvas: HTMLCanvasElement;
  readonly depthBufferCanvas: HTMLCanvasElement;
  manifest: SequenceManifest | null = null;
  frames: readonly PreloadedFrame[] = [];
  player: SequencePlayer | null = null;
  depthMode: DepthDisplayMode = "grayscale";

  constructor(root: Document) {
    this.root = root;
    this.elements = collectElements(root);
    this.colorBufferCanvas = root.createElement("canvas");
    this.depthBufferCanvas = root.createElement("canvas");

    this.bindControls();
  }

  async start(): Promise<void> {
    this.setControlsEnabled(false);
    this.setViewerState("loading");
    this.updateLoading({ percent: 0, label: "manifest.json 확인 중" });

    try {
      this.manifest = await loadManifest("./manifest.json");
      const imageCount = this.manifest.frameCount * 2;
      this.updateLoading({ percent: 0, label: `이미지 0 / ${imageCount} 불러오는 중` });

      this.frames = await preloadFrames(this.manifest.frames, {
        onProgress: ({ completed, total, percent }) => {
          this.updateLoading({ percent, label: `이미지 ${completed} / ${total} 불러오는 중` });
        },
      });

      this.configurePlayer();
      if (!this.renderFrame(0)) return;
      this.updateLoading({ percent: 100, label: "전체 프레임 준비 완료" });
      this.setViewerState("ready");
      this.setControlsEnabled(true);
    } catch (error) {
      this.showError(error);
    }
  }

  private bindControls(): void {
    const elements = this.elements;

    elements.playToggle.addEventListener("click", () => {
      const player = this.player;
      if (!player) return;
      if (player.isPlaying) {
        player.pause();
      } else {
        player.play();
      }
    });

    elements.restart.addEventListener("click", () => {
      if (!this.player) return;
      this.setViewerState("paused");
      this.player.restart();
    });

    elements.previousFrame.addEventListener("click", () => {
      if (!this.player) return;
      this.setViewerState("paused");
      this.player.previous();
    });

    elements.nextFrame.addEventListener("click", () => {
      if (!this.player) return;
      this.setViewerState("paused");
      this.player.next();
    });

    elements.frameSlider.addEventListener("input", () => {
      if (!this.player) return;
      this.setViewerState("paused");
      this.player.seek(Number(elements.frameSlider.value));
    });

    elements.playbackSpeed.addEventListener("change", () => {
      this.player?.setSpeed(Number(elements.playbackSpeed.value));
    });

    elements.depthMode.addEventListener("click", () => {
      if (!this.player) return;
      this.depthMode = this.depthMode === "grayscale" ? "colormap" : "grayscale";
      this.updateDepthModeControl();
      this.renderFrame(this.player.index);
    });
  }

  private configurePlayer(): void {
    if (!this.manifest) {
      throw new Error("manifest가 준비되지 않았습니다.");
    }

    this.elements.frameSlider.max = String(this.frames.length - 1);
    this.player = new SequencePlayer({
      frameCount: this.frames.length,
      intervalMs: this.manifest.intervalMs,
      onFrameChange: (index) => this.renderFrame(index),
      onPlayingChange: (isPlaying) => this.updatePlayingState(isPlaying),
    });
  }

  renderFrame(index: number): boolean {
    let isCommitting = false;
    try {
      const frame = this.frames[index];
      if (!frame) {
        throw new Error(`프레임 ${index}을 찾을 수 없습니다.`);
      }

      this.prepareColorFrame(index, frame.color.image);
      this.prepareDepthFrame(index, frame.depth.image);
      isCommitting = true;
      this.commitFrame(index);
      isCommitting = false;
      this.elements.frameSlider.value = String(index);
      this.elements.frameReadout.value = `${frame.index} / ${this.frames.length - 1}`;
      this.elements.frameReadout.textContent = `${frame.index} / ${this.frames.length - 1}`;
      const elapsed = formatElapsed(frame.timeMs);
      this.elements.timeReadout.value = elapsed;
      this.elements.timeReadout.textContent = elapsed;
      return true;
    } catch (error) {
      if (isCommitting) this.clearVisibleCanvases();
      this.showError(error);
      return false;
    }
  }

  private prepareColorFrame(frameIndex: number, image: HTMLImageElement): void {
    try {
      const { context, width, height } = prepareCanvas(this.colorBufferCanvas, image);
      context.clearRect(0, 0, width, height);
      context.drawImage(image, 0, 0, width, height);
    } catch (cause) {
      throw new FrameRenderError(frameIndex, "color", cause);
    }
  }

  private prepareDepthFrame(frameIndex: number, image: HTMLImageElement): void {
    try {
      const { context, width, height } = prepareCanvas(this.depthBufferCanvas, image);
      context.clearRect(0, 0, width, height);

      if (this.depthMode === "grayscale") {
        context.drawImage(image, 0, 0, width, height);
        return;
      }

      context.drawImage(image, 0, 0, width, height);
      const imageData = context.getImageData(0, 0, width, height);
      colorizeDepthPixelsInPlace(imageData.data);
      context.putImageData(imageData, 0, 0);
    } catch (cause) {
      throw new FrameRenderError(frameIndex, "depth", cause);
    }
  }

  private commitFrame(frameIndex: number): void {
    this.commitCanvas(frameIndex, "color", this.colorBufferCanvas, this.elements.colorCanvas);
    this.commitCanvas(frameIndex, "depth", this.depthBufferCanvas, this.elements.depthCanvas);
  }

  private commitCanvas(
    frameIndex: number,
    kind: FrameKind,
    bufferCanvas: HTMLCanvasElement,
    visibleCanvas: HTMLCanvasElement,
  ): void {
    try {
      const { context, width, height } = prepareCanvas(visibleCanvas, bufferCanvas);
      context.clearRect(0, 0, width, height);
      context.drawImage(bufferCanvas, 0, 0, width, height);
    } catch (cause) {
      throw new FrameRenderError(frameIndex, kind, cause);
    }
  }

  private clearVisibleCanvases(): void {
    for (const canvas of [this.elements.colorCanvas, this.elements.depthCanvas]) {
      try {
        const context = canvas.getContext("2d", { alpha: false });
        context?.clearRect(0, 0, canvas.width, canvas.height);
      } catch {
        // 오류 상태에서는 두 출력을 비우기 위한 최선의 시도만 수행합니다.
      }
    }
  }

  private updateLoading({ percent, label }: LoadingUpdate): void {
    this.elements.loadingProgress.value = percent;
    this.elements.loadingProgress.textContent = `${percent}%`;
    this.elements.loadingPercent.value = `${percent}%`;
    this.elements.loadingPercent.textContent = `${percent}%`;
    this.elements.loadingLabel.textContent = label;
  }

  updatePlayingState(isPlaying: boolean): void {
    this.elements.playLabel.textContent = isPlaying ? "일시정지" : "재생";
    this.elements.playIcon.textContent = isPlaying ? "Ⅱ" : "▶";
    this.elements.playToggle.setAttribute("aria-label", isPlaying ? "일시정지" : "재생");
    this.setViewerState(isPlaying ? "playing" : "paused");
  }

  private updateDepthModeControl(): void {
    const isColormap = this.depthMode === "colormap";
    this.elements.depthMode.setAttribute("aria-pressed", String(isColormap));
    this.elements.depthMode.textContent = isColormap ? "깊이 원본 보기" : "깊이 컬러맵 켜기";
    this.elements.depthModeBadge.textContent = isColormap ? "HEAT" : "GRAY";
  }

  private setControlsEnabled(enabled: boolean): void {
    const controls = this.root.querySelectorAll<
      HTMLButtonElement | HTMLInputElement | HTMLSelectElement
    >("[data-viewer-control]");
    for (const control of controls) {
      control.disabled = !enabled;
    }
  }

  private setViewerState(state: ViewerState): void {
    this.elements.viewerState.textContent = VIEWER_STATE_LABELS[state];
  }

  private showError(error: unknown): void {
    this.player?.pause();
    this.setControlsEnabled(false);
    this.setViewerState("error");
    this.elements.loadingLabel.textContent = "불러오기 중단됨";
    this.elements.viewerErrorMessage.textContent = describeViewerError(error);
    this.elements.viewerError.hidden = false;
    console.error("이미지 시퀀스 뷰어 오류", error);
  }
}

export async function startViewer(root: Document = document): Promise<ViewerController> {
  const viewer = new ViewerController(root);
  await viewer.start();
  return viewer;
}

if (typeof document !== "undefined") {
  void startViewer(document);
}
