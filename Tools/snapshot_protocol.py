"""Shared host-side implementation of the STM32 snapshot UART protocol."""

from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass


MAGIC = b"SNAP"
VERSION = 2
HEADER_SIZE = 32
PIXEL_FORMAT_RGB565 = 1
FLAG_CRC32 = 0x01


@dataclass(frozen=True)
class SnapshotHeader:
    width: int
    height: int
    bytes_per_pixel: int
    decimation: int
    pixel_format: int
    flags: int
    payload_size: int
    frame_id: int
    payload_crc32: int
    header_crc32: int

    @property
    def expected_payload_size(self) -> int:
        return self.width * self.height * self.bytes_per_pixel


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_header(raw: bytes) -> SnapshotHeader:
    if len(raw) != HEADER_SIZE:
        raise ValueError(f"Header must be {HEADER_SIZE} bytes, got {len(raw)}")
    if raw[:4] != MAGIC:
        raise ValueError("Invalid snapshot magic")

    version, header_size, pixel_format, flags = struct.unpack_from("<BBBB", raw, 4)
    if version != VERSION:
        raise ValueError(f"Unsupported protocol version {version}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"Unsupported header size {header_size}")

    expected_header_crc = crc32(raw[:28])
    header_crc32 = struct.unpack_from("<I", raw, 28)[0]
    if header_crc32 != expected_header_crc:
        raise ValueError(
            f"Header CRC mismatch: got 0x{header_crc32:08x}, "
            f"expected 0x{expected_header_crc:08x}"
        )

    width, height, bpp, decimation = struct.unpack_from("<HHHH", raw, 8)
    payload_size, frame_id, payload_crc32 = struct.unpack_from("<III", raw, 16)
    header = SnapshotHeader(
        width=width,
        height=height,
        bytes_per_pixel=bpp,
        decimation=decimation,
        pixel_format=pixel_format,
        flags=flags,
        payload_size=payload_size,
        frame_id=frame_id,
        payload_crc32=payload_crc32,
        header_crc32=header_crc32,
    )
    validate_header(header)
    return header


def build_header(
    *,
    width: int,
    height: int,
    bytes_per_pixel: int,
    decimation: int,
    pixel_format: int,
    flags: int,
    payload: bytes,
    frame_id: int,
) -> bytes:
    payload_crc = crc32(payload)
    payload_size = len(payload)
    raw = bytearray(HEADER_SIZE)
    raw[:4] = MAGIC
    struct.pack_into("<BBBB", raw, 4, VERSION, HEADER_SIZE, pixel_format, flags)
    struct.pack_into("<HHHH", raw, 8, width, height, bytes_per_pixel, decimation)
    struct.pack_into("<III", raw, 16, payload_size, frame_id, payload_crc)
    struct.pack_into("<I", raw, 28, crc32(bytes(raw[:28])))
    return bytes(raw)


def validate_header(header: SnapshotHeader) -> None:
    if header.pixel_format != PIXEL_FORMAT_RGB565:
        raise ValueError(f"Unsupported pixel format {header.pixel_format}")
    if header.bytes_per_pixel != 2:
        raise ValueError(f"Unsupported bytes_per_pixel {header.bytes_per_pixel}")
    if header.width <= 0 or header.height <= 0:
        raise ValueError("Invalid frame dimensions")
    if header.payload_size != header.expected_payload_size:
        raise ValueError(
            f"Payload size mismatch: got {header.payload_size}, "
            f"expected {header.expected_payload_size}"
        )


def validate_payload(header: SnapshotHeader, payload: bytes) -> None:
    if len(payload) != header.payload_size:
        raise ValueError(
            f"Payload length mismatch: got {len(payload)}, expected {header.payload_size}"
        )
    if header.flags & FLAG_CRC32:
        actual = crc32(payload)
        if actual != header.payload_crc32:
            raise ValueError(
                f"Payload CRC mismatch: got 0x{actual:08x}, "
                f"expected 0x{header.payload_crc32:08x}"
            )


def rgb565_to_rgb888(payload: bytes) -> bytes:
    if len(payload) % 2:
        raise ValueError("RGB565 payload length must be even")

    rgb = bytearray((len(payload) // 2) * 3)
    out = 0
    for i in range(0, len(payload), 2):
        value = payload[i] | (payload[i + 1] << 8)
        r5 = (value >> 11) & 0x1F
        g6 = (value >> 5) & 0x3F
        b5 = value & 0x1F
        rgb[out] = (r5 << 3) | (r5 >> 2)
        rgb[out + 1] = (g6 << 2) | (g6 >> 4)
        rgb[out + 2] = (b5 << 3) | (b5 >> 2)
        out += 3
    return bytes(rgb)
