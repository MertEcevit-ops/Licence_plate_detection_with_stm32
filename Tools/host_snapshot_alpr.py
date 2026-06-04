#!/usr/bin/env python3
"""Receive STM32 camera snapshots over UART and run host-side ALPR inference."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TOOLS_DIR.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from alpr_inference import ALPRPipeline, setup_runtime_environment
from receive_snapshot import read_exact, save_image, wait_for_magic
from snapshot_protocol import HEADER_SIZE, MAGIC, parse_header, rgb565_to_rgb888, validate_payload

try:
    import serial
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
    raise


def receive_snapshot(port, output: Path) -> Path:
    print("Waiting for STM32 SNAP frame. Press USER1 on the board...")
    wait_for_magic(port)
    header = parse_header(MAGIC + read_exact(port, HEADER_SIZE - len(MAGIC)))
    print(
        f"Receiving frame {header.frame_id}: {header.width}x{header.height}, "
        f"RGB565, decimation 1/{header.decimation}, {header.payload_size} bytes"
    )

    payload = read_exact(port, header.payload_size)
    validate_payload(header, payload)

    rgb888 = rgb565_to_rgb888(payload)
    saved = save_image(rgb888, header.width, header.height, output)
    print(f"Saved {saved}")
    return Path(saved)


def print_predictions(image_path: Path, predictions) -> None:
    if not predictions:
        print(f"{image_path}: plaka bulunamadi")
        return

    for index, prediction in enumerate(predictions, start=1):
        status = "valid" if prediction.valid_plate else "invalid_ocr"
        image_summary = " ".join(
            f"{name}={path}" for name, path in sorted(prediction.image_outputs.items())
        )
        print(
            f"{image_path} [{index}] conf={prediction.confidence:.3f} "
            f"box={prediction.box} status={status} "
            f"text='{prediction.text}' raw='{prediction.raw_text}'"
            + (f" images: {image_summary}" if image_summary else "")
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Receive STM32 snapshot.png over UART, then run YOLO + OCR ALPR on it"
    )
    parser.add_argument("port", help="Serial port, for example /dev/ttyACM0 or COM5")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="UART baudrate")
    parser.add_argument("--timeout", type=float, default=30.0, help="Serial read timeout in seconds")
    parser.add_argument("--snapshot", default="snapshot.png", help="Snapshot image path")
    parser.add_argument("--pt", default="best.pt", help="YOLO .pt model path")
    parser.add_argument("--h5", default="emnist_ocr.h5", help="Keras OCR .h5 model path")
    parser.add_argument("--conf", type=float, default=0.25, help="YOLO detector confidence threshold")
    parser.add_argument("--output", default="alpr_results", help="Output folder for annotated images")
    parser.add_argument("--count", type=int, default=1, help="Number of snapshots; 0 means run forever")
    parser.add_argument("--show", action="store_true", help="Open OpenCV preview windows")
    parser.add_argument(
        "--no-emnist-transpose",
        action="store_true",
        help="Disable EMNIST transpose/flip preprocessing",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    setup_runtime_environment(Path(".runtime_cache"))

    snapshot_path = Path(args.snapshot)
    output_dir = Path(args.output) if args.output else None
    pipeline = ALPRPipeline(
        yolo_model_path=Path(args.pt),
        ocr_model_path=Path(args.h5),
        detector_confidence=args.conf,
        emnist_transpose=not args.no_emnist_transpose,
    )

    frame_index = 0
    with serial.Serial(args.port, args.baud, timeout=args.timeout) as port:
        port.reset_input_buffer()
        while args.count == 0 or frame_index < args.count:
            frame_index += 1
            image_path = receive_snapshot(port, snapshot_path)
            predictions = pipeline.process_image(image_path, output_dir, args.show)
            print_predictions(image_path, predictions)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
