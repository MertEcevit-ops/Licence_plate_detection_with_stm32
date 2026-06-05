"""Shared host-side implementation of the STM32 snapshot UART protocol."""

from __future__ import annotations

import struct
from dataclasses import dataclass


MAGIC = b"SNAP"
VERSION = 2
HEADER_SIZE = 32
PIXEL_FORMAT_RGB565 = 1
FLAG_AES128_CTR = 0x02

AES128_KEY = bytes(
    [
        0x2B,
        0x7E,
        0x15,
        0x16,
        0x28,
        0xAE,
        0xD2,
        0xA6,
        0xAB,
        0xF7,
        0x15,
        0x88,
        0x09,
        0xCF,
        0x4F,
        0x3C,
    ]
)
AES_CTR_NONCE = b"LPDETECT"
AES_BLOCK_SIZE = 16
AES128_ROUNDS = 10

SBOX = (
    0x63,
    0x7C,
    0x77,
    0x7B,
    0xF2,
    0x6B,
    0x6F,
    0xC5,
    0x30,
    0x01,
    0x67,
    0x2B,
    0xFE,
    0xD7,
    0xAB,
    0x76,
    0xCA,
    0x82,
    0xC9,
    0x7D,
    0xFA,
    0x59,
    0x47,
    0xF0,
    0xAD,
    0xD4,
    0xA2,
    0xAF,
    0x9C,
    0xA4,
    0x72,
    0xC0,
    0xB7,
    0xFD,
    0x93,
    0x26,
    0x36,
    0x3F,
    0xF7,
    0xCC,
    0x34,
    0xA5,
    0xE5,
    0xF1,
    0x71,
    0xD8,
    0x31,
    0x15,
    0x04,
    0xC7,
    0x23,
    0xC3,
    0x18,
    0x96,
    0x05,
    0x9A,
    0x07,
    0x12,
    0x80,
    0xE2,
    0xEB,
    0x27,
    0xB2,
    0x75,
    0x09,
    0x83,
    0x2C,
    0x1A,
    0x1B,
    0x6E,
    0x5A,
    0xA0,
    0x52,
    0x3B,
    0xD6,
    0xB3,
    0x29,
    0xE3,
    0x2F,
    0x84,
    0x53,
    0xD1,
    0x00,
    0xED,
    0x20,
    0xFC,
    0xB1,
    0x5B,
    0x6A,
    0xCB,
    0xBE,
    0x39,
    0x4A,
    0x4C,
    0x58,
    0xCF,
    0xD0,
    0xEF,
    0xAA,
    0xFB,
    0x43,
    0x4D,
    0x33,
    0x85,
    0x45,
    0xF9,
    0x02,
    0x7F,
    0x50,
    0x3C,
    0x9F,
    0xA8,
    0x51,
    0xA3,
    0x40,
    0x8F,
    0x92,
    0x9D,
    0x38,
    0xF5,
    0xBC,
    0xB6,
    0xDA,
    0x21,
    0x10,
    0xFF,
    0xF3,
    0xD2,
    0xCD,
    0x0C,
    0x13,
    0xEC,
    0x5F,
    0x97,
    0x44,
    0x17,
    0xC4,
    0xA7,
    0x7E,
    0x3D,
    0x64,
    0x5D,
    0x19,
    0x73,
    0x60,
    0x81,
    0x4F,
    0xDC,
    0x22,
    0x2A,
    0x90,
    0x88,
    0x46,
    0xEE,
    0xB8,
    0x14,
    0xDE,
    0x5E,
    0x0B,
    0xDB,
    0xE0,
    0x32,
    0x3A,
    0x0A,
    0x49,
    0x06,
    0x24,
    0x5C,
    0xC2,
    0xD3,
    0xAC,
    0x62,
    0x91,
    0x95,
    0xE4,
    0x79,
    0xE7,
    0xC8,
    0x37,
    0x6D,
    0x8D,
    0xD5,
    0x4E,
    0xA9,
    0x6C,
    0x56,
    0xF4,
    0xEA,
    0x65,
    0x7A,
    0xAE,
    0x08,
    0xBA,
    0x78,
    0x25,
    0x2E,
    0x1C,
    0xA6,
    0xB4,
    0xC6,
    0xE8,
    0xDD,
    0x74,
    0x1F,
    0x4B,
    0xBD,
    0x8B,
    0x8A,
    0x70,
    0x3E,
    0xB5,
    0x66,
    0x48,
    0x03,
    0xF6,
    0x0E,
    0x61,
    0x35,
    0x57,
    0xB9,
    0x86,
    0xC1,
    0x1D,
    0x9E,
    0xE1,
    0xF8,
    0x98,
    0x11,
    0x69,
    0xD9,
    0x8E,
    0x94,
    0x9B,
    0x1E,
    0x87,
    0xE9,
    0xCE,
    0x55,
    0x28,
    0xDF,
    0x8C,
    0xA1,
    0x89,
    0x0D,
    0xBF,
    0xE6,
    0x42,
    0x68,
    0x41,
    0x99,
    0x2D,
    0x0F,
    0xB0,
    0x54,
    0xBB,
    0x16,
)
RCON = (0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36)


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

    @property
    def expected_payload_size(self) -> int:
        return self.width * self.height * self.bytes_per_pixel


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

    width, height, bpp, decimation = struct.unpack_from("<HHHH", raw, 8)
    payload_size, frame_id = struct.unpack_from("<II", raw, 16)
    header = SnapshotHeader(
        width=width,
        height=height,
        bytes_per_pixel=bpp,
        decimation=decimation,
        pixel_format=pixel_format,
        flags=flags,
        payload_size=payload_size,
        frame_id=frame_id,
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
    raw = bytearray(HEADER_SIZE)
    raw[:4] = MAGIC
    struct.pack_into("<BBBB", raw, 4, VERSION, HEADER_SIZE, pixel_format, flags)
    struct.pack_into("<HHHH", raw, 8, width, height, bytes_per_pixel, decimation)
    struct.pack_into("<II", raw, 16, len(payload), frame_id)
    struct.pack_into("<II", raw, 24, 0, 0)
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


def decrypt_payload(header: SnapshotHeader, payload: bytes) -> bytes:
    if header.flags & FLAG_AES128_CTR:
        return aes128_ctr_crypt(payload, header.frame_id)
    return payload


def aes128_ctr_crypt(data: bytes, frame_id: int) -> bytes:
    round_key = _key_expansion(AES128_KEY)
    counter = bytearray(AES_CTR_NONCE + frame_id.to_bytes(4, "big") + b"\x00\x00\x00\x00")
    output = bytearray(len(data))

    for offset in range(0, len(data), AES_BLOCK_SIZE):
        stream_block = _aes128_encrypt_block(round_key, counter)
        _increment_counter(counter)
        block = data[offset : offset + AES_BLOCK_SIZE]
        for index, value in enumerate(block):
            output[offset + index] = value ^ stream_block[index]

    return bytes(output)


def _key_expansion(key: bytes) -> bytes:
    round_key = bytearray(key)
    bytes_generated = len(key)
    rcon_iteration = 1

    while bytes_generated < 176:
        temp = list(round_key[bytes_generated - 4 : bytes_generated])

        if bytes_generated % len(key) == 0:
            temp = temp[1:] + temp[:1]
            temp = [SBOX[value] for value in temp]
            temp[0] ^= RCON[rcon_iteration]
            rcon_iteration += 1

        for value in temp:
            round_key.append(round_key[bytes_generated - len(key)] ^ value)
            bytes_generated += 1

    return bytes(round_key)


def _aes128_encrypt_block(round_key: bytes, block: bytes | bytearray) -> bytes:
    state = bytearray(block)
    _add_round_key(state, round_key, 0)

    for round_index in range(1, AES128_ROUNDS):
        _sub_bytes(state)
        _shift_rows(state)
        _mix_columns(state)
        _add_round_key(state, round_key, round_index)

    _sub_bytes(state)
    _shift_rows(state)
    _add_round_key(state, round_key, AES128_ROUNDS)
    return bytes(state)


def _add_round_key(state: bytearray, round_key: bytes, round_index: int) -> None:
    start = round_index * AES_BLOCK_SIZE
    for index in range(AES_BLOCK_SIZE):
        state[index] ^= round_key[start + index]


def _sub_bytes(state: bytearray) -> None:
    for index, value in enumerate(state):
        state[index] = SBOX[value]


def _shift_rows(state: bytearray) -> None:
    original = state[:]
    state[0] = original[0]
    state[4] = original[4]
    state[8] = original[8]
    state[12] = original[12]
    state[1] = original[5]
    state[5] = original[9]
    state[9] = original[13]
    state[13] = original[1]
    state[2] = original[10]
    state[6] = original[14]
    state[10] = original[2]
    state[14] = original[6]
    state[3] = original[15]
    state[7] = original[3]
    state[11] = original[7]
    state[15] = original[11]


def _xtime(value: int) -> int:
    return ((value << 1) ^ (((value >> 7) & 1) * 0x1B)) & 0xFF


def _mix_columns(state: bytearray) -> None:
    for index in range(0, AES_BLOCK_SIZE, 4):
        t = state[index]
        tmp = state[index] ^ state[index + 1] ^ state[index + 2] ^ state[index + 3]

        tm = _xtime(state[index] ^ state[index + 1])
        state[index] ^= tm ^ tmp

        tm = _xtime(state[index + 1] ^ state[index + 2])
        state[index + 1] ^= tm ^ tmp

        tm = _xtime(state[index + 2] ^ state[index + 3])
        state[index + 2] ^= tm ^ tmp

        tm = _xtime(state[index + 3] ^ t)
        state[index + 3] ^= tm ^ tmp


def _increment_counter(counter: bytearray) -> None:
    for index in range(AES_BLOCK_SIZE - 1, -1, -1):
        counter[index] = (counter[index] + 1) & 0xFF
        if counter[index] != 0:
            break


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
