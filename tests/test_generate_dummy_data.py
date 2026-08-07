import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from scripts.generate_dummy_data import generate_dataset


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_png(path: Path):
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise AssertionError(f"PNG 서명이 올바르지 않습니다: {path.name}")

    offset = 8
    width = height = color_type = None
    compressed = []
    while offset < len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        kind = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length

        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (bit_depth, compression, filtering, interlace) != (8, 0, 0, 0):
                raise AssertionError("지원하는 8비트 비인터레이스 PNG가 아닙니다.")
        elif kind == b"IDAT":
            compressed.append(payload)
        elif kind == b"IEND":
            break

    channels = 3 if color_type == 2 else 1
    raw = zlib.decompress(b"".join(compressed))
    stride = width * channels
    rows = []
    cursor = 0
    for _ in range(height):
        if raw[cursor] != 0:
            raise AssertionError("테스트 생성기는 필터 0을 사용해야 합니다.")
        cursor += 1
        rows.append(raw[cursor : cursor + stride])
        cursor += stride

    return {
        "width": width,
        "height": height,
        "color_type": color_type,
        "pixels": b"".join(rows),
    }


class GenerateDummyDataTests(unittest.TestCase):
    def test_generates_matching_frame_files_and_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "dataset"

            generate_dataset(output, frame_count=3, width=16, height=10, interval_ms=75)

            manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["frame_count"], 3)
            self.assertEqual(manifest["interval_ms"], 75)
            self.assertEqual(manifest["depth_near_cm"], 0)
            self.assertEqual(manifest["depth_far_cm"], 5000)
            self.assertEqual(
                manifest["frames"],
                [
                    {
                        "index": 0,
                        "color": "color_000000.png",
                        "depth": "depth_000000.png",
                        "time_ms": 0,
                    },
                    {
                        "index": 1,
                        "color": "color_000001.png",
                        "depth": "depth_000001.png",
                        "time_ms": 75,
                    },
                    {
                        "index": 2,
                        "color": "color_000002.png",
                        "depth": "depth_000002.png",
                        "time_ms": 150,
                    },
                ],
            )
            self.assertEqual(len(list(output.glob("color_*.png"))), 3)
            self.assertEqual(len(list(output.glob("depth_*.png"))), 3)

    def test_writes_rgb_color_and_grayscale_depth_pngs(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_dataset(output, frame_count=2, width=20, height=12, interval_ms=100)

            color = read_png(output / "color_000000.png")
            depth = read_png(output / "depth_000000.png")

            self.assertEqual((color["width"], color["height"], color["color_type"]), (20, 12, 2))
            self.assertEqual((depth["width"], depth["height"], depth["color_type"]), (20, 12, 0))

    def test_moves_color_shape_and_brightens_near_depth_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            generate_dataset(output, frame_count=3, width=32, height=18, interval_ms=100)

            first_color = read_png(output / "color_000000.png")["pixels"]
            last_color = read_png(output / "color_000002.png")["pixels"]
            first_depth = read_png(output / "depth_000000.png")["pixels"]
            last_depth = read_png(output / "depth_000002.png")["pixels"]

            self.assertNotEqual(first_color, last_color)
            self.assertGreater(max(last_depth), max(first_depth))

    def test_rejects_non_positive_dimensions_and_frame_count(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)

            with self.assertRaises(ValueError):
                generate_dataset(output, frame_count=0, width=16, height=10, interval_ms=100)
            with self.assertRaises(ValueError):
                generate_dataset(output, frame_count=1, width=0, height=10, interval_ms=100)
            with self.assertRaises(ValueError):
                generate_dataset(output, frame_count=1, width=16, height=10, interval_ms=0)


if __name__ == "__main__":
    unittest.main()
