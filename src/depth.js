const COLOR_STOPS = [
  [0, [12, 35, 84]],
  [0.25, [0, 155, 207]],
  [0.5, [246, 220, 73]],
  [0.75, [245, 123, 32]],
  [1, [210, 35, 42]],
];

export function mapDepthIntensity(value) {
  const normalized = Math.min(Math.max(Number(value), 0), 255) / 255;

  for (let index = 1; index < COLOR_STOPS.length; index += 1) {
    const [upperPosition, upperColor] = COLOR_STOPS[index];
    if (normalized > upperPosition) {
      continue;
    }

    const [lowerPosition, lowerColor] = COLOR_STOPS[index - 1];
    const range = upperPosition - lowerPosition;
    const amount = (normalized - lowerPosition) / range;

    return lowerColor.map((channel, channelIndex) =>
      Math.round(channel + (upperColor[channelIndex] - channel) * amount),
    );
  }

  return [...COLOR_STOPS.at(-1)[1]];
}

export function colorizeDepthPixels(pixels) {
  if (pixels.length % 4 !== 0) {
    throw new RangeError("깊이 픽셀 데이터는 RGBA 네 채널 단위여야 합니다.");
  }

  const result = new Uint8ClampedArray(pixels);

  for (let offset = 0; offset < pixels.length; offset += 4) {
    const intensity = (pixels[offset] + pixels[offset + 1] + pixels[offset + 2]) / 3;
    const [red, green, blue] = mapDepthIntensity(intensity);
    result[offset] = red;
    result[offset + 1] = green;
    result[offset + 2] = blue;
  }

  return result;
}
