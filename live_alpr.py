#!/usr/bin/env python3
"""
STM32 Live ALPR Pipeline
Hem UART üzerinden STM32'den gelen fotoğrafı alır, hem de YOLO + EasyOCR ile işleyip 
anlık olarak Canlı Dashboard üzerinde gösterir.
"""

from __future__ import annotations
import argparse
import os
import re
import sys
import time
import struct
import binascii
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import cv2
import numpy as np

# --- PROTOKOL SABİTLERİ VE FONKSİYONLARI ---
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
        raise ValueError(f"Header must be {HEADER_SIZE} bytes")
    if raw[:4] != MAGIC:
        raise ValueError("Invalid snapshot magic")

    version, header_size, pixel_format, flags = struct.unpack_from("<BBBB", raw, 4)
    expected_header_crc = crc32(raw[:28])
    header_crc32 = struct.unpack_from("<I", raw, 28)[0]
    
    if header_crc32 != expected_header_crc:
        raise ValueError("Header CRC mismatch")

    width, height, bpp, decimation = struct.unpack_from("<HHHH", raw, 8)
    payload_size, frame_id, payload_crc32 = struct.unpack_from("<III", raw, 16)
    
    header = SnapshotHeader(width, height, bpp, decimation, pixel_format, flags, payload_size, frame_id, payload_crc32, header_crc32)
    if header.pixel_format != PIXEL_FORMAT_RGB565 or header.bytes_per_pixel != 2:
        raise ValueError("Unsupported format, must be RGB565")
    return header

def validate_payload(header: SnapshotHeader, payload: bytes) -> None:
    if len(payload) != header.payload_size:
        raise ValueError("Payload length mismatch")
    if header.flags & FLAG_CRC32:
        if crc32(payload) != header.payload_crc32:
            raise ValueError("Payload CRC mismatch")

def rgb565_to_rgb888(payload: bytes) -> bytes:
    rgb = bytearray((len(payload) // 2) * 3)
    out = 0
    for i in range(0, len(payload), 2):
        value = payload[i] | (payload[i + 1] << 8)
        r5, g6, b5 = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
        rgb[out], rgb[out + 1], rgb[out + 2] = (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)
        out += 3
    return bytes(rgb)


# --- AI VE DASHBOARD SABİTLERİ ---
TURKISH_PLATE_LETTERS = "ABCDEFGHIJKLMNOPRSTUVYZ"
TURKISH_PLATE_RE = re.compile(rf"^(0[1-9]|[1-7][0-9]|8[01])([{TURKISH_PLATE_LETTERS}]{{1,3}})([0-9]{{2,4}})$")

def fit_image(img, max_w, max_h):
    h, w = img.shape[:2]
    scale = min(max_w / w, max_h / h)
    if scale > 1.5: scale = 1.5 
    return cv2.resize(img, (int(w * scale), int(h * scale)))

def build_dashboard(annot, pl, dbg_pl, final_text, is_valid):
    ch, cw = 700, 1200
    canvas = np.full((ch, cw, 3), 40, dtype=np.uint8)

    annot_fit = fit_image(annot, 760, 660)
    h_a, w_a = annot_fit.shape[:2]
    y_offset_a = (ch - h_a) // 2
    canvas[y_offset_a:y_offset_a+h_a, 20:20+w_a] = annot_fit

    x_right = 800
    pl_fit = fit_image(pl, 360, 200)
    cv2.putText(canvas, "1. YOLO Plaka Tespiti", (x_right, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 255, 200), 2)
    canvas[70:70+pl_fit.shape[0], x_right:x_right+pl_fit.shape[1]] = pl_fit

    dbg_fit = fit_image(dbg_pl, 360, 200)
    y_dbg = 70 + pl_fit.shape[0] + 50
    cv2.putText(canvas, "2. EasyOCR Analizi", (x_right, y_dbg - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 255, 200), 2)
    canvas[y_dbg:y_dbg+dbg_fit.shape[0], x_right:x_right+dbg_fit.shape[1]] = dbg_fit

    y_final = y_dbg + dbg_fit.shape[0] + 50
    cv2.putText(canvas, "3. Nihai Okunan Plaka", (x_right, y_final - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 255, 200), 2)
    
    text_color = (0, 255, 0) if is_valid else (0, 0, 255)
    display_text = final_text if final_text else "OKUNAMADI"
    
    cv2.rectangle(canvas, (x_right, y_final), (x_right + 360, y_final + 60), (60, 60, 60), -1)
    cv2.putText(canvas, display_text, (x_right + 20, y_final + 45), cv2.FONT_HERSHEY_SIMPLEX, 1.4, text_color, 3)

    cv2.putText(canvas, "STM32'den yeni goruntu bekleniyor...", (x_right, 670), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (150, 150, 255), 1)
    return canvas

def normalize_turkish_plate(raw_text: str) -> tuple[str, bool]:
    clean = re.sub(r"[^0-9A-Z]", "", raw_text.upper())
    if clean.startswith("TR") and len(clean) > 2 and clean[2].isdigit():
        clean = clean[2:]
    match = re.search(r"(0[1-9]|[1-7][0-9]|8[01])([A-Z]{1,3})([0-9]{2,4})", clean)
    if match: return match.group(0), True
    return clean, False


class LiveALPRPipeline:
    def __init__(self, yolo_model_path: Path, detector_confidence: float) -> None:
        try:
            from ultralytics import YOLO
            import easyocr
        except ImportError:
            raise RuntimeError("Gerekli paketler eksik. pip install ultralytics easyocr opencv-python numpy pyserial")

        print("--- AI Modelleri Yukleniyor ---")
        self.yolo_model = YOLO(str(yolo_model_path))
        self.ocr_reader = easyocr.Reader(['en'], gpu=False)
        self.detector_confidence = detector_confidence

    def process_frame(self, frame: np.ndarray):
        annotated = frame.copy()
        results = self.yolo_model(frame, conf=self.detector_confidence, verbose=False)
        
        best_plate = None
        best_dashboard = None

        for result in results:
            for box in result.boxes:
                x1, y1, x2, y2 = map(int, box.xyxy[0])
                x1, y1 = max(x1, 0), max(y1, 0)
                x2, y2 = min(x2, frame.shape[1]), min(y2, frame.shape[0])
                if x2 <= x1 or y2 <= y1: continue

                plate = frame[y1:y2, x1:x2]
                debug_plate, raw_ocr_text = self._read_plate(plate)
                
                text, valid_plate = normalize_turkish_plate(raw_ocr_text)
                final_display_text = text if valid_plate else raw_ocr_text

                main_text_y = y1 - 12
                if main_text_y < 30: main_text_y = y2 + 35 

                cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0) if valid_plate else (0,165,255), 3)
                cv2.putText(annotated, final_display_text if final_display_text else "PLAKA",
                            (x1, main_text_y), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 255, 0) if valid_plate else (0,165,255), 3)

                best_plate = final_display_text
                best_dashboard = build_dashboard(annotated, plate, debug_plate, final_display_text, valid_plate)

        # Eğer plaka bulunamazsa boş bir dashboard göster
        if best_dashboard is None:
            empty_plate = np.zeros((100, 200, 3), dtype=np.uint8)
            best_dashboard = build_dashboard(annotated, empty_plate, empty_plate, "", False)

        cv2.imshow("ALPR Analiz Paneli", best_dashboard)
        cv2.waitKey(1) # Ekrana bas ve UART dinlemeye geri dön
        return best_plate

    def _read_plate(self, plate):
        if plate.size == 0: return plate.copy(), ""
        resized = cv2.resize(plate, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_CUBIC)
        debug_plate = resized.copy()
        ocr_results = self.ocr_reader.readtext(plate, detail=1)
        final_text = ""

        if ocr_results:
            heights = [b[2][1] - b[0][1] for b, t, p in ocr_results]
            max_h = max(heights) if heights else 0
            for (bbox, text, prob) in ocr_results:
                h = bbox[2][1] - bbox[0][1]
                if h < max_h * 0.4 or text.strip().upper() == 'TR': continue

                x_min, y_min = int(bbox[0][0] * 2), int(bbox[0][1] * 2)
                x_max, y_max = int(bbox[2][0] * 2), int(bbox[2][1] * 2)
                cleaned = re.sub(r'[^A-Z0-9]', '', text.upper())
                if not cleaned: continue
                
                final_text += cleaned
                cv2.rectangle(debug_plate, (x_min, y_min), (x_max, y_max), (0, 255, 0), 2)
                char_y = y_min - 6 if y_min - 6 >= 20 else y_max + 22 
                cv2.putText(debug_plate, cleaned, (x_min, char_y), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        return debug_plate, final_text


def read_exact(port, size):
    data = bytearray()
    while len(data) < size:
        chunk = port.read(size - len(data))
        if not chunk: raise TimeoutError("UART Okuma Zaman Aşımı")
        data.extend(chunk)
    return bytes(data)

def wait_for_magic_and_gui(port):
    import serial
    window = bytearray()
    print("STM32'den tetik (SNAP) bekleniyor...")
    while True:
        # GUI'nin donmasını engelle
        if cv2.getWindowProperty("ALPR Analiz Paneli", cv2.WND_PROP_VISIBLE) >= 1:
            cv2.waitKey(1)
        
        try:
            byte = port.read(1)
        except serial.SerialException:
            break

        if not byte: continue
        
        window += byte
        if len(window) > len(MAGIC): del window[0]
        if bytes(window) == MAGIC: return

def main():
    parser = argparse.ArgumentParser(description="Live STM32 ALPR Inference")
    parser.add_argument("--port", required=True, help="Seri port örn: /dev/ttyACM0 veya COM5")
    parser.add_argument("--baud", type=int, default=115200, help="UART baudrate")
    parser.add_argument("--pt", default="yolov8_plaka.pt", help="YOLO .pt modeli")
    parser.add_argument("--conf", type=float, default=0.25, help="YOLO güven eşiği")
    args = parser.parse_args()

    import serial
    pipeline = LiveALPRPipeline(Path(args.pt), args.conf)

    # Başlangıçta boş siyah ekran oluştur ki GUI açılsın
    dummy_frame = np.zeros((480, 640, 3), dtype=np.uint8)
    pipeline.process_frame(dummy_frame)

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        port.reset_input_buffer()
        while True:
            try:
                wait_for_magic_and_gui(port)
                # Header'ı çek
                header_raw = MAGIC + read_exact(port, HEADER_SIZE - len(MAGIC))
                header = parse_header(header_raw)
                
                print(f"Görsel Alınıyor: {header.width}x{header.height} | Boyut: {header.payload_size} bytes")
                payload = read_exact(port, header.payload_size)
                validate_payload(header, payload)
                
                # RGB565 -> OpenCV BGR dönüşümü
                rgb888_bytes = rgb565_to_rgb888(payload)
                img_rgb = np.frombuffer(rgb888_bytes, dtype=np.uint8).reshape((header.height, header.width, 3))
                img_bgr = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2BGR) # OpenCV BGR formatı kullanır
                
                # Saniyede diske yazmaya gerek yok, direkt RAM'den AI modeline!
                print("Görsel yakalandı! Yapay Zeka analiz ediyor...")
                pipeline.process_frame(img_bgr)
                
            except TimeoutError:
                continue
            except KeyboardInterrupt:
                print("\nÇıkış yapılıyor...")
                break
            except Exception as e:
                print(f"Hata: {e}")
                port.reset_input_buffer()

if __name__ == "__main__":
    main()