#!/usr/bin/env python3
"""Live STM32 ALPR pipeline for AES-encrypted SNAP frames.

The board sends RGB565 frames over UART using the SNAP v2 protocol. The payload
is AES-128-CTR encrypted when the firmware sets the AES flag; this script
decrypts it, converts it to OpenCV BGR, then runs YOLO + EasyOCR live.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path

try:
    import cv2
    import numpy as np
except ImportError:
    cv2 = None
    np = None


TOOLS_DIR = Path(__file__).resolve().parent / "Tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from snapshot_protocol import (  # noqa: E402
    FLAG_AES128_CTR,
    HEADER_SIZE,
    MAGIC,
    decrypt_payload,
    parse_header,
    rgb565_to_rgb888,
    validate_payload,
)


WINDOW_NAME = "ALPR Analiz Paneli"
TURKISH_PLATE_LETTERS = "ABCDEFGHIJKLMNOPRSTUVYZ"
TURKISH_PLATE_RE = re.compile(
    rf"(0[1-9]|[1-7][0-9]|8[01])([{TURKISH_PLATE_LETTERS}]{{1,3}})([0-9]{{2,4}})"
)


@dataclass
class LiveDetection:
    box: tuple[int, int, int, int]
    confidence: float
    text: str
    raw_text: str
    valid_plate: bool
    plate_image: "np.ndarray"
    debug_plate: "np.ndarray"


def setup_runtime_environment(cache_dir: Path) -> None:
    cache_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(cache_dir / "matplotlib")
    os.environ["YOLO_CONFIG_DIR"] = str(cache_dir / "ultralytics")


def require_runtime_imports() -> None:
    missing = []

    if cv2 is None:
        missing.append("opencv-python")
    if np is None:
        missing.append("numpy")

    try:
        import serial  # noqa: F401
    except ImportError:
        missing.append("pyserial")

    try:
        from ultralytics import YOLO  # noqa: F401
    except ImportError:
        missing.append("ultralytics")

    try:
        import easyocr  # noqa: F401
    except ImportError:
        missing.append("easyocr")

    if missing:
        raise RuntimeError(
            "Eksik Python paketleri: "
            + ", ".join(missing)
            + ". Kurulum: pip install "
            + " ".join(missing)
        )


def normalize_turkish_plate(raw_text: str) -> tuple[str, bool]:
    clean = re.sub(r"[^0-9A-Z]", "", raw_text.upper())
    if clean.startswith("TR") and len(clean) > 2 and clean[2].isdigit():
        clean = clean[2:]

    match = TURKISH_PLATE_RE.search(clean)
    if match:
        return match.group(0), True
    return clean, False


def fit_image(img: "np.ndarray", max_w: int, max_h: int) -> "np.ndarray":
    if img is None or img.size == 0:
        return np.zeros((max_h, max_w, 3), dtype=np.uint8)

    h, w = img.shape[:2]
    if h <= 0 or w <= 0:
        return np.zeros((max_h, max_w, 3), dtype=np.uint8)

    scale = min(max_w / w, max_h / h)
    if scale > 1.5:
        scale = 1.5

    new_w = max(1, int(w * scale))
    new_h = max(1, int(h * scale))
    return cv2.resize(img, (new_w, new_h))


def put_text_fit(
    canvas: "np.ndarray",
    text: str,
    origin: tuple[int, int],
    max_width: int,
    color: tuple[int, int, int],
    initial_scale: float = 1.4,
    min_scale: float = 0.55,
    thickness: int = 3,
) -> None:
    font_scale = initial_scale
    while font_scale > min_scale:
        (text_w, _), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, font_scale, thickness)
        if text_w <= max_width:
            break
        font_scale -= 0.1

    cv2.putText(
        canvas,
        text,
        origin,
        cv2.FONT_HERSHEY_SIMPLEX,
        max(font_scale, min_scale),
        color,
        thickness,
    )


def build_dashboard(
    annotated: "np.ndarray",
    plate: "np.ndarray",
    debug_plate: "np.ndarray",
    final_text: str,
    is_valid: bool,
) -> "np.ndarray":
    canvas_h, canvas_w = 700, 1200
    canvas = np.full((canvas_h, canvas_w, 3), 40, dtype=np.uint8)

    annotated_fit = fit_image(annotated, 760, 660)
    h_annot, w_annot = annotated_fit.shape[:2]
    y_annot = (canvas_h - h_annot) // 2
    canvas[y_annot : y_annot + h_annot, 20 : 20 + w_annot] = annotated_fit

    x_right = 800
    plate_fit = fit_image(plate, 360, 200)
    cv2.putText(
        canvas,
        "1. YOLO Plaka Tespiti",
        (x_right, 50),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (200, 255, 200),
        2,
    )
    canvas[70 : 70 + plate_fit.shape[0], x_right : x_right + plate_fit.shape[1]] = plate_fit

    debug_fit = fit_image(debug_plate, 360, 200)
    y_debug = 70 + plate_fit.shape[0] + 50
    cv2.putText(
        canvas,
        "2. EasyOCR Analizi",
        (x_right, y_debug - 20),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (200, 255, 200),
        2,
    )
    canvas[y_debug : y_debug + debug_fit.shape[0], x_right : x_right + debug_fit.shape[1]] = debug_fit

    y_final = y_debug + debug_fit.shape[0] + 50
    cv2.putText(
        canvas,
        "3. Nihai Okunan Plaka",
        (x_right, y_final - 20),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.7,
        (200, 255, 200),
        2,
    )

    text_color = (0, 255, 0) if is_valid else (0, 0, 255)
    display_text = final_text if final_text else "OKUNAMADI"
    cv2.rectangle(canvas, (x_right, y_final), (x_right + 360, y_final + 60), (60, 60, 60), -1)
    put_text_fit(canvas, display_text, (x_right + 18, y_final + 45), 325, text_color)

    cv2.putText(
        canvas,
        "STM32'den yeni AES frame bekleniyor...",
        (x_right, 670),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.5,
        (150, 150, 255),
        1,
    )
    return canvas


class LiveALPRPipeline:
    def __init__(self, yolo_model_path: Path, detector_confidence: float) -> None:
        from ultralytics import YOLO
        import easyocr

        if not yolo_model_path.exists():
            raise FileNotFoundError(yolo_model_path)

        print("--- AI modelleri yukleniyor ---")
        self.yolo_model = YOLO(str(yolo_model_path))
        self.ocr_reader = easyocr.Reader(["en"], gpu=False)
        self.detector_confidence = detector_confidence

    def show_idle(self) -> None:
        dummy_frame = np.zeros((240, 400, 3), dtype=np.uint8)
        empty_plate = np.zeros((100, 200, 3), dtype=np.uint8)
        cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
        cv2.imshow(WINDOW_NAME, build_dashboard(dummy_frame, empty_plate, empty_plate, "", False))
        cv2.waitKey(1)

    def process_frame(self, frame: "np.ndarray") -> str | None:
        annotated = frame.copy()
        detections: list[LiveDetection] = []

        results = self.yolo_model(frame, conf=self.detector_confidence, verbose=False)
        for result in results:
            for box in result.boxes:
                x1, y1, x2, y2 = [int(v) for v in box.xyxy[0]]
                x1, y1 = max(x1, 0), max(y1, 0)
                x2, y2 = min(x2, frame.shape[1]), min(y2, frame.shape[0])
                if x2 <= x1 or y2 <= y1:
                    continue

                plate = frame[y1:y2, x1:x2]
                confidence = float(box.conf[0]) if box.conf is not None else 0.0
                debug_plate, raw_ocr_text = self._read_plate(plate)
                text, valid_plate = normalize_turkish_plate(raw_ocr_text)
                final_display_text = text if valid_plate else raw_ocr_text

                detections.append(
                    LiveDetection(
                        box=(x1, y1, x2, y2),
                        confidence=confidence,
                        text=final_display_text,
                        raw_text=raw_ocr_text,
                        valid_plate=valid_plate,
                        plate_image=plate,
                        debug_plate=debug_plate,
                    )
                )

                main_text_y = y1 - 12
                if main_text_y < 30:
                    main_text_y = y2 + 35

                color = (0, 255, 0) if valid_plate else (0, 165, 255)
                cv2.rectangle(annotated, (x1, y1), (x2, y2), color, 3)
                cv2.putText(
                    annotated,
                    final_display_text if final_display_text else "PLAKA",
                    (x1, main_text_y),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    1.2,
                    color,
                    3,
                )

        if detections:
            best = max(detections, key=lambda item: (item.valid_plate, item.confidence))
            dashboard = build_dashboard(
                annotated,
                best.plate_image,
                best.debug_plate,
                best.text,
                best.valid_plate,
            )
            best_plate = best.text
        else:
            empty_plate = np.zeros((100, 200, 3), dtype=np.uint8)
            dashboard = build_dashboard(annotated, empty_plate, empty_plate, "", False)
            best_plate = None

        cv2.imshow(WINDOW_NAME, dashboard)
        cv2.waitKey(1)
        return best_plate

    def _read_plate(self, plate: "np.ndarray") -> tuple["np.ndarray", str]:
        if plate.size == 0:
            return plate.copy(), ""

        resized = cv2.resize(plate, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_CUBIC)
        debug_plate = resized.copy()
        ocr_results = self.ocr_reader.readtext(plate, detail=1)
        final_text = ""

        if not ocr_results:
            return debug_plate, final_text

        heights = [bbox[2][1] - bbox[0][1] for bbox, _, _ in ocr_results]
        max_h = max(heights) if heights else 0

        for bbox, text, _prob in ocr_results:
            h = bbox[2][1] - bbox[0][1]
            if h < max_h * 0.4 or text.strip().upper() == "TR":
                continue

            cleaned = re.sub(r"[^A-Z0-9]", "", text.upper())
            if not cleaned:
                continue

            x_min = int(bbox[0][0] * 2)
            y_min = int(bbox[0][1] * 2)
            x_max = int(bbox[2][0] * 2)
            y_max = int(bbox[2][1] * 2)

            final_text += cleaned
            cv2.rectangle(debug_plate, (x_min, y_min), (x_max, y_max), (0, 255, 0), 2)
            char_y = y_min - 6 if y_min >= 26 else y_max + 22
            cv2.putText(
                debug_plate,
                cleaned,
                (x_min, char_y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
            )

        return debug_plate, final_text


def pump_gui() -> None:
    try:
        cv2.waitKey(1)
    except cv2.error:
        pass


def read_exact(port, size: int, idle_timeout: float) -> bytes:
    data = bytearray()
    last_byte_at = time.monotonic()

    while len(data) < size:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
            last_byte_at = time.monotonic()
        elif time.monotonic() - last_byte_at >= idle_timeout:
            raise TimeoutError(f"UART read timed out after {len(data)} / {size} bytes")

        pump_gui()

    return bytes(data)


def wait_for_magic_and_gui(port) -> None:
    window = bytearray()
    print("STM32'den SNAP frame bekleniyor...")

    while True:
        pump_gui()
        byte = port.read(1)
        if not byte:
            continue

        window += byte
        if len(window) > len(MAGIC):
            del window[0]
        if bytes(window) == MAGIC:
            return


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Live STM32 AES SNAP ALPR inference")
    parser.add_argument("--port", required=True, help="Seri port, ornek: /dev/ttyACM0 veya COM5")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="UART baudrate")
    parser.add_argument("--timeout", type=float, default=5.0, help="Iki UART okuma arasindaki zaman asimi")
    parser.add_argument("--pt", default="yolov8_plaka.pt", help="YOLO .pt model yolu")
    parser.add_argument("--conf", type=float, default=0.25, help="YOLO guven esigi")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    setup_runtime_environment(Path(".runtime_cache"))
    require_runtime_imports()

    import serial

    pipeline = LiveALPRPipeline(Path(args.pt), args.conf)
    pipeline.show_idle()

    try:
        with serial.Serial(args.port, args.baud, timeout=0.05) as port:
            port.reset_input_buffer()

            while True:
                try:
                    wait_for_magic_and_gui(port)
                    header_raw = MAGIC + read_exact(port, HEADER_SIZE - len(MAGIC), args.timeout)
                    header = parse_header(header_raw)

                    aes_state = "AES-128-CTR" if header.flags & FLAG_AES128_CTR else "plain"
                    print(
                        f"Frame {header.frame_id}: {header.width}x{header.height}, "
                        f"{header.payload_size} bytes, {aes_state}"
                    )

                    encrypted_payload = read_exact(port, header.payload_size, args.timeout)
                    validate_payload(header, encrypted_payload)
                    payload = decrypt_payload(header, encrypted_payload)

                    rgb888 = rgb565_to_rgb888(payload)
                    img_rgb = np.frombuffer(rgb888, dtype=np.uint8).reshape(
                        (header.height, header.width, 3)
                    )
                    img_bgr = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2BGR)

                    print("Gorsel cozuldu; yapay zeka analiz ediyor...")
                    plate = pipeline.process_frame(img_bgr)
                    if plate:
                        print(f"Okunan plaka: {plate}")

                except TimeoutError:
                    continue
                except KeyboardInterrupt:
                    print("\nCikis yapiliyor...")
                    break
                except Exception as exc:
                    print(f"Hata: {exc}")
                    port.reset_input_buffer()
    finally:
        try:
            cv2.destroyAllWindows()
        except cv2.error:
            pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
