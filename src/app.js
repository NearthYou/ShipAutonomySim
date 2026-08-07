import { colorizeDepthPixelsInPlace } from "./depth.js";
import { loadManifest, ManifestError } from "./manifest.js";
import { SequencePlayer } from "./player.js";
import { FrameLoadError, preloadFrames } from "./preload.js";

const ELEMENT_IDS = [
  "color-canvas",
  "depth-canvas",
  "depth-mode",
  "depth-mode-badge",
  "frame-readout",
  "frame-slider",
  "loading-label",
  "loading-percent",
  "loading-progress",
  "next-frame",
  "play-icon",
  "play-label",
  "play-toggle",
  "playback-speed",
  "previous-frame",
  "restart",
  "time-readout",
  "viewer-error",
  "viewer-error-message",
  "viewer-state",
];

const FRAME_KIND_LABELS = {
  color: "컬러",
  depth: "깊이",
};
const MAX_CAUSE_DESCRIPTION_LENGTH = 200;

export class FrameRenderError extends Error {
  constructor(frameIndex, kind, cause) {
    super(`프레임 ${frameIndex}의 ${FRAME_KIND_LABELS[kind]} 렌더링에 실패했습니다.`, { cause });
    this.name = "FrameRenderError";
    this.frameIndex = frameIndex;
    this.kind = kind;
  }
}

export function formatElapsed(timeMs) {
  const safeTime = Math.max(0, Math.floor(Number(timeMs) || 0));
  const minutes = Math.floor(safeTime / 60_000);
  const seconds = Math.floor((safeTime % 60_000) / 1_000);
  const milliseconds = safeTime % 1_000;

  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}.${String(milliseconds).padStart(3, "0")}`;
}

function fileNameFromUrl(url) {
  try {
    const path = new URL(url, "http://localhost/").pathname;
    return decodeURIComponent(path.split("/").filter(Boolean).at(-1) || url);
  } catch {
    return String(url);
  }
}

function normalizeCauseDescription(cause) {
  let description = "";
  try {
    if (typeof cause === "string") {
      description = cause;
    } else if (typeof cause?.message === "string") {
      description = cause.message;
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

export function describeViewerError(error) {
  if (error instanceof ManifestError) {
    return `${error.message} manifest.json의 내용과 로컬 서버 실행 상태를 확인하세요.`;
  }

  if (error instanceof FrameLoadError) {
    const names = error.failures.slice(0, 5).map((failure) => fileNameFromUrl(failure.url));
    const remaining = error.failures.length - names.length;
    const remainingText = remaining > 0 ? `, 그 외 ${remaining}개` : "";
    const timeoutText = error.failures.some((failure) => failure.cause?.name === "TimeoutError")
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

function collectElements(root) {
  return Object.fromEntries(
    ELEMENT_IDS.map((id) => {
      const element = root.getElementById(id);
      if (!element) {
        throw new Error(`필수 화면 요소를 찾을 수 없습니다: ${id}`);
      }
      return [id, element];
    }),
  );
}

function imageSize(image) {
  const width = image.naturalWidth || image.width;
  const height = image.naturalHeight || image.height;
  if (!Number.isFinite(width) || width < 1 || !Number.isFinite(height) || height < 1) {
    throw new Error("이미지 크기를 확인할 수 없습니다.");
  }
  return { width, height };
}

function prepareCanvas(canvas, image) {
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
  constructor(root) {
    this.root = root;
    this.elements = collectElements(root);
    this.colorBufferCanvas = root.createElement("canvas");
    this.depthBufferCanvas = root.createElement("canvas");
    this.manifest = null;
    this.frames = [];
    this.player = null;
    this.depthMode = "grayscale";

    this.bindControls();
  }

  async start() {
    this.setControlsEnabled(false);
    this.setViewerState("로딩 중");
    this.updateLoading({ percent: 0, label: "manifest.json 확인 중" });

    try {
      this.manifest = await loadManifest("./manifest.json");
      const imageCount = this.manifest.frameCount * 2;
      const preloadInput = this.manifest.frames.map((frame) => ({
        index: frame.index,
        time_ms: frame.timeMs,
        colorUrl: frame.color.url,
        depthUrl: frame.depth.url,
      }));
      this.updateLoading({ percent: 0, label: `이미지 0 / ${imageCount} 불러오는 중` });

      this.frames = await preloadFrames(preloadInput, {
        onProgress: ({ completed, total, percent }) => {
          this.updateLoading({ percent, label: `이미지 ${completed} / ${total} 불러오는 중` });
        },
      });

      this.configurePlayer();
      if (!this.renderFrame(0)) {
        return;
      }
      this.updateLoading({ percent: 100, label: "전체 프레임 준비 완료" });
      this.setViewerState("준비됨");
      this.setControlsEnabled(true);
    } catch (error) {
      this.showError(error);
    }
  }

  bindControls() {
    const elements = this.elements;

    elements["play-toggle"].addEventListener("click", () => {
      if (this.player.isPlaying) {
        this.player.pause();
      } else {
        this.player.play();
      }
    });

    elements.restart.addEventListener("click", () => {
      this.setViewerState("일시정지");
      this.player.restart();
    });

    elements["previous-frame"].addEventListener("click", () => {
      this.setViewerState("일시정지");
      this.player.previous();
    });

    elements["next-frame"].addEventListener("click", () => {
      this.setViewerState("일시정지");
      this.player.next();
    });

    elements["frame-slider"].addEventListener("input", (event) => {
      this.setViewerState("일시정지");
      this.player.seek(Number(event.currentTarget.value));
    });

    elements["playback-speed"].addEventListener("change", (event) => {
      this.player.setSpeed(Number(event.currentTarget.value));
    });

    elements["depth-mode"].addEventListener("click", () => {
      this.depthMode = this.depthMode === "grayscale" ? "colormap" : "grayscale";
      this.updateDepthModeControl();
      this.renderFrame(this.player.index);
    });
  }

  configurePlayer() {
    this.elements["frame-slider"].max = String(this.frames.length - 1);
    this.player = new SequencePlayer({
      frameCount: this.frames.length,
      intervalMs: this.manifest.intervalMs,
      onFrameChange: (index) => this.renderFrame(index),
      onPlayingChange: (isPlaying) => this.updatePlayingState(isPlaying),
    });
  }

  renderFrame(index) {
    let isCommitting = false;
    try {
      const frame = this.frames[index];
      if (!frame) {
        throw new Error(`프레임 ${index}을 찾을 수 없습니다.`);
      }

      this.prepareColorFrame(index, frame.colorImage);
      this.prepareDepthFrame(index, frame.depthImage);
      isCommitting = true;
      this.commitFrame(index);
      isCommitting = false;
      this.elements["frame-slider"].value = String(index);
      this.elements["frame-readout"].value = `${frame.index} / ${this.frames.length - 1}`;
      this.elements["frame-readout"].textContent = `${frame.index} / ${this.frames.length - 1}`;
      const elapsed = formatElapsed(frame.time_ms);
      this.elements["time-readout"].value = elapsed;
      this.elements["time-readout"].textContent = elapsed;
      return true;
    } catch (error) {
      if (isCommitting) {
        this.clearVisibleCanvases();
      }
      this.showError(error);
      return false;
    }
  }

  prepareColorFrame(frameIndex, image) {
    try {
      const { context, width, height } = prepareCanvas(this.colorBufferCanvas, image);
      context.clearRect(0, 0, width, height);
      context.drawImage(image, 0, 0, width, height);
    } catch (cause) {
      throw new FrameRenderError(frameIndex, "color", cause);
    }
  }

  prepareDepthFrame(frameIndex, image) {
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

  commitFrame(frameIndex) {
    this.commitCanvas(
      frameIndex,
      "color",
      this.colorBufferCanvas,
      this.elements["color-canvas"],
    );
    this.commitCanvas(
      frameIndex,
      "depth",
      this.depthBufferCanvas,
      this.elements["depth-canvas"],
    );
  }

  commitCanvas(frameIndex, kind, bufferCanvas, visibleCanvas) {
    try {
      const { context, width, height } = prepareCanvas(visibleCanvas, bufferCanvas);
      context.clearRect(0, 0, width, height);
      context.drawImage(bufferCanvas, 0, 0, width, height);
    } catch (cause) {
      throw new FrameRenderError(frameIndex, kind, cause);
    }
  }

  clearVisibleCanvases() {
    for (const canvas of [this.elements["color-canvas"], this.elements["depth-canvas"]]) {
      try {
        const context = canvas.getContext("2d", { alpha: false });
        context?.clearRect(0, 0, canvas.width, canvas.height);
      } catch {
        // 오류 상태에서는 두 출력을 비우기 위한 최선의 시도만 수행합니다.
      }
    }
  }

  updateLoading({ percent, label }) {
    this.elements["loading-progress"].value = percent;
    this.elements["loading-progress"].textContent = `${percent}%`;
    this.elements["loading-percent"].value = `${percent}%`;
    this.elements["loading-percent"].textContent = `${percent}%`;
    this.elements["loading-label"].textContent = label;
  }

  updatePlayingState(isPlaying) {
    this.elements["play-label"].textContent = isPlaying ? "일시정지" : "재생";
    this.elements["play-icon"].textContent = isPlaying ? "Ⅱ" : "▶";
    this.elements["play-toggle"].setAttribute("aria-label", isPlaying ? "일시정지" : "재생");
    this.setViewerState(isPlaying ? "재생 중" : "일시정지");
  }

  updateDepthModeControl() {
    const isColormap = this.depthMode === "colormap";
    this.elements["depth-mode"].setAttribute("aria-pressed", String(isColormap));
    this.elements["depth-mode"].textContent = isColormap
      ? "깊이 원본 보기"
      : "깊이 컬러맵 켜기";
    this.elements["depth-mode-badge"].textContent = isColormap ? "HEAT" : "GRAY";
  }

  setControlsEnabled(enabled) {
    for (const control of this.root.querySelectorAll("[data-viewer-control]")) {
      control.disabled = !enabled;
    }
  }

  setViewerState(label) {
    this.elements["viewer-state"].textContent = label;
  }

  showError(error) {
    this.player?.pause();
    this.setControlsEnabled(false);
    this.setViewerState("오류");
    this.elements["loading-label"].textContent = "불러오기 중단됨";
    this.elements["viewer-error-message"].textContent = describeViewerError(error);
    this.elements["viewer-error"].hidden = false;
    console.error("이미지 시퀀스 뷰어 오류", error);
  }
}

export async function startViewer(root = document) {
  const viewer = new ViewerController(root);
  await viewer.start();
  return viewer;
}

if (typeof document !== "undefined") {
  startViewer(document);
}
