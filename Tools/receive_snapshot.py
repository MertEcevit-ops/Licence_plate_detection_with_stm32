#!/usr/bin/env python3
"""Receive one RGB565 snapshot frame from the board over UART."""

import argparse
import sys
from pathlib import Path

from snapshot_protocol import (
    HEADER_SIZE,
    MAGIC,
    decrypt_payload,
    parse_header,
    rgb565_to_rgb888,
    validate_payload,
)

try:
    import serial
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    raise


def read_exact(port, size):
    data = bytearray()
    while len(data) < size:
        chunk = port.read(size - len(data))
        if not chunk:
            raise TimeoutError(f"Timed out after {len(data)} / {size} bytes")
        data.extend(chunk)
    return bytes(data)


def wait_for_magic(port):
    window = bytearray()
    while True:
        byte = port.read(1)
        if not byte:
            raise TimeoutError("Timed out while waiting for SNAP header")
        window += byte
        if len(window) > len(MAGIC):
            del window[0]
        if bytes(window) == MAGIC:
            return


def save_image(rgb888, width, height, output):
    output = Path(output)

    try:
        from PIL import Image
    except ImportError:
        ppm = output.with_suffix(".ppm")
        with ppm.open("wb") as f:
            f.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
            f.write(rgb888)
        return ppm

    image = Image.frombytes("RGB", (width, height), rgb888)
    image.save(output)
    return output


def main():
    parser = argparse.ArgumentParser(description="Receive one STM32 RGB565 snapshot over UART")
    parser.add_argument("port", help="Serial port, for example /dev/ttyACM0 or COM5")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="UART baudrate")
    parser.add_argument("-o", "--output", default="snapshot.png", help="Output image path")
    parser.add_argument("--timeout", type=float, default=30.0, help="Serial read timeout in seconds")
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as port:
        port.reset_input_buffer()
        print("Waiting for SNAP frame...")
        wait_for_magic(port)
        header = parse_header(MAGIC + read_exact(port, HEADER_SIZE - len(MAGIC)))

        print(
            f"Receiving frame {header.frame_id}: {header.width}x{header.height}, "
            f"RGB565, decimation 1/{header.decimation}, {header.payload_size} bytes"
        )
        payload = read_exact(port, header.payload_size)
        validate_payload(header, payload)

    payload = decrypt_payload(header, payload)
    rgb888 = rgb565_to_rgb888(payload)
    saved = save_image(rgb888, header.width, header.height, args.output)
    print(f"Saved {saved}")


if __name__ == "__main__":
    main()
