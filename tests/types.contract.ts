import type { SequenceManifest } from "../src/manifest.js";
import type { PlaybackSpeed } from "../src/player.js";
import type { ColorFrameData, DepthFrameData } from "../src/preload.js";

function verifyTypeContracts(color: ColorFrameData, value: unknown): void {
  // @ts-expect-error 컬러 프레임은 깊이 프레임으로 사용할 수 없다.
  const depth: DepthFrameData = color;
  // @ts-expect-error 지원하지 않는 재생 속도다.
  const speed: PlaybackSpeed = 1.5;
  // @ts-expect-error 검증 전 unknown은 내부 manifest가 아니다.
  const manifest: SequenceManifest = value;
  void depth;
  void speed;
  void manifest;
}

void verifyTypeContracts;
