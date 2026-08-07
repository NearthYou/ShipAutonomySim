"""외부 패키지 없이 웹 뷰어용 PNG 시퀀스를 생성한다."""

from __future__ import annotations

import argparse
import binascii
import json
import math
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_chunk(kind: bytes, data: bytes) -> bytes:
    checksum = binascii.crc32(kind + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", checksum)


def write_png(
    path: Path,
    width: int,
    height: int,
    rows: list[bytes],
    *,
    color_type: int,
) -> None:
    if color_type not in (0, 2):
        raise ValueError("PNG 색상 형식은 회색조 0 또는 RGB 2여야 합니다.")

    channels = 1 if color_type == 0 else 3
    expected_row_size = width * channels
    if len(rows) != height or any(len(row) != expected_row_size for row in rows):
        raise ValueError("PNG 행 데이터 크기가 이미지 크기와 일치하지 않습니다.")

    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    scanlines = b"".join(b"\x00" + row for row in rows)
    payload = (
        PNG_SIGNATURE
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(payload)


def shape_geometry(frame_index: int, frame_count: int, width: int, height: int) -> tuple[int, int, int, float]:
    progress = frame_index / (frame_count - 1) if frame_count > 1 else 0.5
    radius = max(2, round(min(width, height) * (0.1 + progress * 0.08)))
    horizontal_range = max(0, width - radius * 2 - 1)
    center_x = radius + round(horizontal_range * progress)
    center_y = round(height * (0.5 + math.sin(progress * math.tau) * 0.17))
    center_y = min(max(center_y, radius), max(radius, height - radius - 1))
    return center_x, center_y, radius, progress


def color_rows(frame_index: int, frame_count: int, width: int, height: int) -> list[bytes]:
    center_x, center_y, radius, progress = shape_geometry(
        frame_index, frame_count, width, height
    )
    radius_squared = radius * radius
    width_scale = max(1, width - 1)
    height_scale = max(1, height - 1)
    grid_width = max(8, width // 8)
    grid_height = max(8, height // 6)
    rows: list[bytes] = []

    for y in range(height):
        row = bytearray()
        for x in range(width):
            red = 12 + round(18 * x / width_scale)
            green = 18 + round(18 * y / height_scale)
            blue = 28 + round(20 * (1 - x / width_scale))

            if x % grid_width == 0 or y % grid_height == 0:
                red += 9
                green += 9
                blue += 9

            distance_squared = (x - center_x) ** 2 + (y - center_y) ** 2
            if distance_squared <= radius_squared:
                red = 255
                green = 80 + round(70 * progress)
                blue = 28

            row.extend((min(red, 255), min(green, 255), min(blue, 255)))
        rows.append(bytes(row))

    return rows


def depth_rows(frame_index: int, frame_count: int, width: int, height: int) -> list[bytes]:
    center_x, center_y, radius, progress = shape_geometry(
        frame_index, frame_count, width, height
    )
    radius_squared = radius * radius
    near_intensity = round(90 + 150 * progress)
    height_scale = max(1, height - 1)
    rows: list[bytes] = []

    for y in range(height):
        row = bytearray()
        for x in range(width):
            value = 20 + round(14 * y / height_scale)
            distance_squared = (x - center_x) ** 2 + (y - center_y) ** 2
            if distance_squared <= radius_squared:
                distance = math.sqrt(distance_squared) / max(1, radius)
                value = round(near_intensity - distance * 18)
            row.append(min(max(value, 0), 255))
        rows.append(bytes(row))

    return rows


def require_positive(value: int, name: str) -> None:
    if not isinstance(value, int) or value < 1:
        raise ValueError(f"{name}은 1 이상의 정수여야 합니다.")


def generate_dataset(
    output: Path,
    *,
    frame_count: int = 30,
    width: int = 320,
    height: int = 180,
    interval_ms: int = 100,
) -> Path:
    require_positive(frame_count, "frame_count")
    require_positive(width, "width")
    require_positive(height, "height")
    require_positive(interval_ms, "interval_ms")

    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    frames = []

    for frame_index in range(frame_count):
        color_name = f"color_{frame_index:06d}.png"
        depth_name = f"depth_{frame_index:06d}.png"

        write_png(
            output / color_name,
            width,
            height,
            color_rows(frame_index, frame_count, width, height),
            color_type=2,
        )
        write_png(
            output / depth_name,
            width,
            height,
            depth_rows(frame_index, frame_count, width, height),
            color_type=0,
        )

        frames.append(
            {
                "index": frame_index,
                "color": color_name,
                "depth": depth_name,
                "time_ms": frame_index * interval_ms,
            }
        )

    manifest = {
        "frame_count": frame_count,
        "interval_ms": interval_ms,
        "depth_near_cm": 0,
        "depth_far_cm": 5000,
        "frames": frames,
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest_path


def positive_integer(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("1 이상의 정수를 입력하세요.")
    return parsed


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="웹 뷰어용 더미 이미지 시퀀스를 생성합니다.")
    parser.add_argument("--output", type=Path, default=Path.cwd(), help="출력 폴더")
    parser.add_argument("--frames", type=positive_integer, default=30, help="프레임 수")
    parser.add_argument("--width", type=positive_integer, default=320, help="이미지 너비")
    parser.add_argument("--height", type=positive_integer, default=180, help="이미지 높이")
    parser.add_argument(
        "--interval-ms", type=positive_integer, default=100, help="프레임 간격 밀리초"
    )
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    manifest_path = generate_dataset(
        arguments.output,
        frame_count=arguments.frames,
        width=arguments.width,
        height=arguments.height,
        interval_ms=arguments.interval_ms,
    )
    print(f"더미 데이터 생성을 마쳤습니다: {manifest_path}")


if __name__ == "__main__":
    main()
