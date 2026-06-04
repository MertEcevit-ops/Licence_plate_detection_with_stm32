import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from snapshot_protocol import (
    FLAG_CRC32,
    HEADER_SIZE,
    PIXEL_FORMAT_RGB565,
    build_header,
    parse_header,
    rgb565_to_rgb888,
    validate_payload,
)


class SnapshotProtocolTests(unittest.TestCase):
    def test_header_roundtrip_and_payload_crc(self):
        payload = b"\x00\xf8\xe0\x07\x1f\x00\xff\xff"
        raw = build_header(
            width=2,
            height=2,
            bytes_per_pixel=2,
            decimation=2,
            pixel_format=PIXEL_FORMAT_RGB565,
            flags=FLAG_CRC32,
            payload=payload,
            frame_id=7,
        )

        self.assertEqual(len(raw), HEADER_SIZE)
        header = parse_header(raw)
        self.assertEqual(header.width, 2)
        self.assertEqual(header.height, 2)
        self.assertEqual(header.frame_id, 7)
        validate_payload(header, payload)

    def test_payload_crc_rejects_corruption(self):
        payload = b"\x00\xf8\xe0\x07"
        raw = build_header(
            width=2,
            height=1,
            bytes_per_pixel=2,
            decimation=2,
            pixel_format=PIXEL_FORMAT_RGB565,
            flags=FLAG_CRC32,
            payload=payload,
            frame_id=1,
        )

        header = parse_header(raw)
        with self.assertRaisesRegex(ValueError, "Payload CRC mismatch"):
            validate_payload(header, b"\x00\xf8\x00\x00")

    def test_rgb565_conversion_known_colors(self):
        rgb = rgb565_to_rgb888(b"\x00\xf8\xe0\x07\x1f\x00")
        self.assertEqual(rgb, bytes([255, 0, 0, 0, 255, 0, 0, 0, 255]))


if __name__ == "__main__":
    unittest.main()
