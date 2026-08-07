const ALLOWED_SPEEDS = new Set([0.5, 1, 2]);

export class SequencePlayer {
  constructor({
    frameCount,
    intervalMs,
    onFrameChange = () => {},
    onPlayingChange = () => {},
    requestFrame = globalThis.requestAnimationFrame?.bind(globalThis),
    cancelFrame = globalThis.cancelAnimationFrame?.bind(globalThis),
  }) {
    if (!Number.isInteger(frameCount) || frameCount < 1) {
      throw new RangeError("frameCount는 1 이상의 정수여야 합니다.");
    }

    if (!Number.isFinite(intervalMs) || intervalMs <= 0) {
      throw new RangeError("intervalMs는 0보다 큰 숫자여야 합니다.");
    }

    this.frameCount = frameCount;
    this.intervalMs = intervalMs;
    this.index = 0;
    this.speed = 1;
    this.isPlaying = false;

    this.onFrameChange = onFrameChange;
    this.onPlayingChange = onPlayingChange;
    this.requestFrame = requestFrame;
    this.cancelFrame = cancelFrame;

    this.elapsedMs = 0;
    this.lastTimestamp = null;
    this.frameRequestId = null;
    this.tick = this.tick.bind(this);
  }

  play() {
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

  pause() {
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

  restart() {
    this.pause();
    this.updateIndex(0);
  }

  previous() {
    this.pause();
    this.updateIndex(this.index - 1);
  }

  next() {
    this.pause();
    this.updateIndex(this.index + 1);
  }

  seek(index) {
    if (!Number.isFinite(Number(index))) {
      throw new TypeError("프레임 위치는 숫자여야 합니다.");
    }

    this.pause();
    this.updateIndex(Math.round(Number(index)));
  }

  setSpeed(speed) {
    const nextSpeed = Number(speed);
    if (!ALLOWED_SPEEDS.has(nextSpeed)) {
      throw new RangeError("재생 속도는 0.5, 1, 2 중 하나여야 합니다.");
    }

    this.speed = nextSpeed;
  }

  setPlaying(isPlaying) {
    if (this.isPlaying === isPlaying) {
      return;
    }

    this.isPlaying = isPlaying;
    this.onPlayingChange(isPlaying);
  }

  updateIndex(index) {
    const nextIndex = Math.min(Math.max(index, 0), this.frameCount - 1);
    if (nextIndex === this.index) {
      return true;
    }

    this.index = nextIndex;
    return this.onFrameChange(nextIndex) !== false;
  }

  scheduleNextFrame() {
    this.frameRequestId = this.requestFrame(this.tick);
  }

  tick(timestamp) {
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
