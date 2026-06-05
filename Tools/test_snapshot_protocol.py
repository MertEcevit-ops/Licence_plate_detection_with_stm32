import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from snapshot_protocol import (
    AES128_KEY,
    FLAG_AES128_CTR,
    HEADER_SIZE,
    PIXEL_FORMAT_RGB565,
    _aes128_encrypt_block,
    _key_expansion,
    aes128_ctr_crypt,
    build_header,
    decrypt_payload,
    parse_header,
    rgb565_to_rgb888,
    validate_payload,
)


class SnapshotProtocolTests(unittest.TestCase):
    def test_header_roundtrip(self):
        payload = b"\x00\xf8\xe0\x07\x1f\x00\xff\xff"
        raw = build_header(
            width=2,
            height=2,
            bytes_per_pixel=2,
            decimation=2,
            pixel_format=PIXEL_FORMAT_RGB565,
            flags=FLAG_AES128_CTR,
            payload=payload,
            frame_id=7,
        )

        self.assertEqual(len(raw), HEADER_SIZE)
        header = parse_header(raw)
        self.assertEqual(header.width, 2)
        self.assertEqual(header.height, 2)
        self.assertEqual(header.frame_id, 7)
        self.assertEqual(header.flags, FLAG_AES128_CTR)
        validate_payload(header, payload)

    def test_payload_length_rejects_truncation(self):
        payload = b"\x00\xf8\xe0\x07"
        raw = build_header(
            width=2,
            height=1,
            bytes_per_pixel=2,
            decimation=2,
            pixel_format=PIXEL_FORMAT_RGB565,
            flags=0,
            payload=payload,
            frame_id=1,
        )

        header = parse_header(raw)
        with self.assertRaisesRegex(ValueError, "Payload length mismatch"):
            validate_payload(header, payload[:2])

    def test_aes_ctr_roundtrip(self):
        payload = bytes(range(64))
        encrypted = aes128_ctr_crypt(payload, frame_id=7)
        decrypted = aes128_ctr_crypt(encrypted, frame_id=7)

        self.assertNotEqual(encrypted, payload)
        self.assertEqual(decrypted, payload)

    def test_aes128_known_vector(self):
        plaintext = bytes.fromhex("3243f6a8885a308d313198a2e0370734")
        expected = bytes.fromhex("3925841d02dc09fbdc118597196a0b32")

        self.assertEqual(
            _aes128_encrypt_block(_key_expansion(AES128_KEY), plaintext),
            expected,
        )

    def test_decrypt_payload_uses_aes_flag(self):
        payload = bytes(range(8))
        encrypted = aes128_ctr_crypt(payload, frame_id=3)
        raw = build_header(
            width=2,
            height=2,
            bytes_per_pixel=2,
            decimation=2,
            pixel_format=PIXEL_FORMAT_RGB565,
            flags=FLAG_AES128_CTR,
            payload=encrypted,
            frame_id=3,
        )

        header = parse_header(raw)
        self.assertEqual(decrypt_payload(header, encrypted), payload)

    def test_rgb565_conversion_known_colors(self):
        rgb = rgb565_to_rgb888(b"\x00\xf8\xe0\x07\x1f\x00")
        self.assertEqual(rgb, bytes([255, 0, 0, 0, 255, 0, 0, 0, 255]))


if __name__ == "__main__":
    unittest.main()
