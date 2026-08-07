export type Rgb = [red: number, green: number, blue: number];

type ColorStop = readonly [position: number, color: readonly [number, number, number]];
type WritablePixels = Uint8ClampedArray | number[];

const COLOR_STOPS: readonly ColorStop[] = [
  [0, [12, 35, 84]],
  [0.25, [0, 155, 207]],
  [0.5, [246, 220, 73]],
  [0.75, [245, 123, 32]],
  [1, [210, 35, 42]],
];

function writeDepthColor(value: unknown, target: WritablePixels, offset: number): void {
  const normalized = Math.min(Math.max(Number(value), 0), 255) / 255;

  for (let index = 1; index < COLOR_STOPS.length; index += 1) {
    const [upperPosition, upperColor] = COLOR_STOPS[index];
    if (normalized > upperPosition) continue;

    const [lowerPosition, lowerColor] = COLOR_STOPS[index - 1];
    const amount = (normalized - lowerPosition) / (upperPosition - lowerPosition);

    for (let channel = 0; channel < 3; channel += 1) {
      target[offset + channel] = Math.round(
        lowerColor[channel] + (upperColor[channel] - lowerColor[channel]) * amount,
      );
    }
    return;
  }

  const [, lastColor] = COLOR_STOPS[COLOR_STOPS.length - 1];
  for (let channel = 0; channel < 3; channel += 1) {
    target[offset + channel] = lastColor[channel];
  }
}

export function mapDepthIntensity(value: unknown): Rgb {
  const color: Rgb = [0, 0, 0];
  writeDepthColor(value, color, 0);
  return color;
}

export function colorizeDepthPixels(pixels: ArrayLike<number>): Uint8ClampedArray {
  return colorizeDepthPixelsInPlace(new Uint8ClampedArray(pixels));
}

export function colorizeDepthPixelsInPlace(
  pixels: Uint8ClampedArray,
): Uint8ClampedArray {
  if (pixels.length % 4 !== 0) {
    throw new RangeError("깊이 픽셀 데이터는 RGBA 네 채널 단위여야 합니다.");
  }

  for (let offset = 0; offset < pixels.length; offset += 4) {
    const intensity = (pixels[offset] + pixels[offset + 1] + pixels[offset + 2]) / 3;
    writeDepthColor(intensity, pixels, offset);
  }

  return pixels;
}
