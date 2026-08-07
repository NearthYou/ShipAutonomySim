import assert from "node:assert/strict";
import test from "node:test";

import { SequencePlayer } from "../src/player.js";
import type { SequencePlayerOptions } from "../src/player.js";

interface TestScheduler {
  request: (callback: FrameRequestCallback) => number;
  cancel: (id: number) => void;
  run: (timestamp: number) => void;
  readonly pendingCount: number;
}

function createScheduler(): TestScheduler {
  let nextId = 1;
  const callbacks = new Map<number, FrameRequestCallback>();

  return {
    request(callback) {
      const id = nextId;
      nextId += 1;
      callbacks.set(id, callback);
      return id;
    },
    cancel(id) {
      callbacks.delete(id);
    },
    run(timestamp) {
      const entry = callbacks.entries().next().value;
      assert.ok(entry, "실행할 애니메이션 프레임이 있어야 합니다.");
      const [id, callback] = entry;
      callbacks.delete(id);
      callback(timestamp);
    },
    get pendingCount() {
      return callbacks.size;
    },
  };
}

function createPlayer(overrides: Partial<SequencePlayerOptions> = {}) {
  const scheduler = createScheduler();
  const frameChanges: number[] = [];
  const playingChanges: boolean[] = [];
  const player = new SequencePlayer({
    frameCount: 3,
    intervalMs: 100,
    onFrameChange: (index) => {
      frameChanges.push(index);
    },
    onPlayingChange: (isPlaying) => {
      playingChanges.push(isPlaying);
    },
    requestFrame: scheduler.request,
    cancelFrame: scheduler.cancel,
    ...overrides,
  });

  return { player, scheduler, frameChanges, playingChanges };
}

test("이전과 다음 이동은 프레임 범위를 벗어나지 않는다", () => {
  const { player, frameChanges } = createPlayer();

  player.previous();
  player.next();
  player.next();
  player.next();

  assert.equal(player.index, 2);
  assert.deepEqual(frameChanges, [1, 2]);
});

test("탐색과 처음으로 이동하면 재생을 멈춘다", () => {
  const { player, playingChanges } = createPlayer();

  player.play();
  player.seek(2);
  assert.equal(player.index, 2);
  assert.equal(player.isPlaying, false);

  player.play();
  player.restart();
  assert.equal(player.index, 0);
  assert.equal(player.isPlaying, false);
  assert.deepEqual(playingChanges, [true, false, true, false]);
});

test("탐색 위치를 유효한 프레임 범위로 제한한다", () => {
  const { player } = createPlayer();

  player.seek(-10);
  assert.equal(player.index, 0);
  player.seek(99);
  assert.equal(player.index, 2);
});

test("허용된 세 재생 속도만 사용한다", () => {
  const { player } = createPlayer();

  for (const speed of [0.5, 1, 2]) {
    player.setSpeed(speed);
    assert.equal(player.speed, speed);
  }

  assert.throws(() => player.setSpeed(1.5), RangeError);
});

test("기본 속도에서 간격마다 다음 프레임으로 이동한다", () => {
  const { player, scheduler, frameChanges } = createPlayer();

  player.play();
  scheduler.run(0);
  scheduler.run(99);
  assert.equal(player.index, 0);
  scheduler.run(100);

  assert.equal(player.index, 1);
  assert.deepEqual(frameChanges, [1]);
});

test("2배 속도는 절반 간격마다 이동한다", () => {
  const { player, scheduler } = createPlayer();
  player.setSpeed(2);

  player.play();
  scheduler.run(0);
  scheduler.run(50);

  assert.equal(player.index, 1);
});

test("0.5배 속도는 두 배 간격마다 이동한다", () => {
  const { player, scheduler } = createPlayer();
  player.setSpeed(0.5);

  player.play();
  scheduler.run(0);
  scheduler.run(199);
  assert.equal(player.index, 0);
  scheduler.run(200);

  assert.equal(player.index, 1);
});

test("마지막 프레임에 도달하면 재생을 멈춘다", () => {
  const { player, scheduler, playingChanges } = createPlayer();

  player.play();
  scheduler.run(0);
  scheduler.run(200);

  assert.equal(player.index, 2);
  assert.equal(player.isPlaying, false);
  assert.equal(scheduler.pendingCount, 0);
  assert.deepEqual(playingChanges, [true, false]);
});

test("프레임 변경 처리 중 멈추면 다음 애니메이션 프레임을 예약하지 않는다", () => {
  const scheduler = createScheduler();
  let player: SequencePlayer;
  player = new SequencePlayer({
    frameCount: 3,
    intervalMs: 100,
    onFrameChange: () => player.pause(),
    requestFrame: (callback) => scheduler.request(callback),
    cancelFrame: (id) => scheduler.cancel(id),
  });

  player.play();
  scheduler.run(0);
  scheduler.run(100);

  assert.equal(player.index, 1);
  assert.equal(player.isPlaying, false);
  assert.equal(scheduler.pendingCount, 0);
});

test("마지막 프레임에서 재생하면 처음부터 다시 시작한다", () => {
  const { player, scheduler, frameChanges } = createPlayer();
  player.seek(2);
  frameChanges.length = 0;

  player.play();

  assert.equal(player.index, 0);
  assert.equal(player.isPlaying, true);
  assert.equal(scheduler.pendingCount, 1);
  assert.deepEqual(frameChanges, [0]);
});

test("마지막 프레임의 초기 렌더가 실패하면 오류 상태로 정지한다", () => {
  const scheduler = createScheduler();
  const playingChanges: boolean[] = [];
  let viewerState = "준비됨";
  const player = new SequencePlayer({
    frameCount: 3,
    intervalMs: 100,
    onFrameChange: (index) => {
      if (index === 0) {
        viewerState = "오류";
        return false;
      }
      return true;
    },
    onPlayingChange: (isPlaying) => {
      playingChanges.push(isPlaying);
      viewerState = isPlaying ? "재생 중" : "일시정지";
    },
    requestFrame: (callback) => scheduler.request(callback),
    cancelFrame: (id) => scheduler.cancel(id),
  });
  player.seek(2);

  player.play();

  assert.equal(player.isPlaying, false);
  assert.equal(scheduler.pendingCount, 0);
  assert.deepEqual(playingChanges, []);
  assert.equal(viewerState, "오류");
});

test("일시정지하면 예약 프레임을 취소하고 재개 시간을 초기화한다", () => {
  const { player, scheduler } = createPlayer();

  player.play();
  scheduler.run(0);
  scheduler.run(75);
  player.pause();
  assert.equal(scheduler.pendingCount, 0);

  player.play();
  scheduler.run(1_000);
  scheduler.run(1_099);
  assert.equal(player.index, 0);
  scheduler.run(1_100);

  assert.equal(player.index, 1);
});
