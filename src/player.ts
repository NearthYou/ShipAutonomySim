export type PlaybackSpeed = 0.5 | 1 | 2;
export type PlaybackState = "paused" | "playing";
export type FrameChangeHandler = (index: number) => boolean | void;
export type PlayingChangeHandler = (isPlaying: boolean) => void;
export type RequestFrame = (callback: FrameRequestCallback) => number;
export type CancelFrame = (id: number) => void;

export interface SequencePlayerOptions {
  frameCount: number;
  intervalMs: number;
  onFrameChange?: FrameChangeHandler;
  onPlayingChange?: PlayingChangeHandler;
  requestFrame?: RequestFrame;
  cancelFrame?: CancelFrame;
}

function isPlaybackSpeed(value: number): value is PlaybackSpeed {
  return value === 0.5 || value === 1 || value === 2;
}

export class SequencePlayer {
  readonly frameCount: number;
  readonly intervalMs: number;
  index = 0;
  speed: PlaybackSpeed = 1;
  private state: PlaybackState = "paused";
  private readonly onFrameChange: FrameChangeHandler;
  private readonly onPlayingChange: PlayingChangeHandler;
  private readonly requestFrame: RequestFrame | undefined;
  private readonly cancelFrame: CancelFrame | undefined;
  private elapsedMs = 0;
  private lastTimestamp: number | null = null;
  private frameRequestId: number | null = null;

  constructor({
    frameCount,
    intervalMs,
    onFrameChange = () => {},
    onPlayingChange = () => {},
    requestFrame = globalThis.requestAnimationFrame?.bind(globalThis),
    cancelFrame = globalThis.cancelAnimationFrame?.bind(globalThis),
  }: SequencePlayerOptions) {
    if (!Number.isInteger(frameCount) || frameCount < 1) {
      throw new RangeError("frameCount는 1 이상의 정수여야 합니다.");
    }

    if (!Number.isFinite(intervalMs) || intervalMs <= 0) {
      throw new RangeError("intervalMs는 0보다 큰 숫자여야 합니다.");
    }

    this.frameCount = frameCount;
    this.intervalMs = intervalMs;
    this.onFrameChange = onFrameChange;
    this.onPlayingChange = onPlayingChange;
    this.requestFrame = requestFrame;
    this.cancelFrame = cancelFrame;

    this.tick = this.tick.bind(this);
  }

  get isPlaying(): boolean {
    return this.state === "playing";
  }

  play(): void {
    if (this.isPlaying) {
      return;
    }

    if (typeof this.requestFrame !== "function") {
      throw new Error("이 환경에서는 애니메이션 프레임을 요청할 수 없습니다.");
    }

    if (this.index === this.frameCount - 1) {
      const didRender = this.updateIndex(0);
      if (!didRender) {
        return;
      }
    }

    this.elapsedMs = 0;
    this.lastTimestamp = null;
    this.setPlaying(true);
    this.scheduleNextFrame();
  }

  pause(): void {
    if (!this.isPlaying) {
      return;
    }

    if (this.frameRequestId !== null && typeof this.cancelFrame === "function") {
      this.cancelFrame(this.frameRequestId);
    }

    this.frameRequestId = null;
    this.elapsedMs = 0;
    this.lastTimestamp = null;
    this.setPlaying(false);
  }

  restart(): void {
    this.pause();
    this.updateIndex(0);
  }

  previous(): void {
    this.pause();
    this.updateIndex(this.index - 1);
  }

  next(): void {
    this.pause();
    this.updateIndex(this.index + 1);
  }

  seek(index: unknown): void {
    if (!Number.isFinite(Number(index))) {
      throw new TypeError("프레임 위치는 숫자여야 합니다.");
    }

    this.pause();
    this.updateIndex(Math.round(Number(index)));
  }

  setSpeed(speed: unknown): void {
    const nextSpeed = Number(speed);
    if (!isPlaybackSpeed(nextSpeed)) {
      throw new RangeError("재생 속도는 0.5, 1, 2 중 하나여야 합니다.");
    }

    this.speed = nextSpeed;
  }

  private setPlaying(isPlaying: boolean): void {
    const nextState: PlaybackState = isPlaying ? "playing" : "paused";
    if (this.state === nextState) return;
    this.state = nextState;
    this.onPlayingChange(isPlaying);
  }

  private updateIndex(index: number): boolean {
    const nextIndex = Math.min(Math.max(index, 0), this.frameCount - 1);
    if (nextIndex === this.index) {
      return true;
    }

    this.index = nextIndex;
    return this.onFrameChange(nextIndex) !== false;
  }

  private scheduleNextFrame(): void {
    if (typeof this.requestFrame !== "function") {
      throw new Error("이 환경에서는 애니메이션 프레임을 요청할 수 없습니다.");
    }
    this.frameRequestId = this.requestFrame(this.tick);
  }

  private tick(timestamp: number): void {
    this.frameRequestId = null;
    if (!this.isPlaying) {
      return;
    }

    if (this.lastTimestamp === null) {
      this.lastTimestamp = timestamp;
      this.scheduleNextFrame();
      return;
    }

    const deltaMs = Math.max(0, timestamp - this.lastTimestamp);
    this.lastTimestamp = timestamp;
    this.elapsedMs += deltaMs;

    const frameDuration = this.intervalMs / this.speed;
    const steps = Math.floor(this.elapsedMs / frameDuration);

    if (steps > 0) {
      this.elapsedMs -= steps * frameDuration;
      this.updateIndex(Math.min(this.index + steps, this.frameCount - 1));

      if (this.index === this.frameCount - 1) {
        this.pause();
        return;
      }
    }

    if (this.isPlaying) {
      this.scheduleNextFrame();
    }
  }
}
