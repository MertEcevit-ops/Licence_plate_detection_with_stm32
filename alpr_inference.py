#!/usr/bin/env python3
"""Run the ALPR pipeline with a YOLO .pt detector and EasyOCR Engine.
Local usage with popup windows:
    python3 alpr_inference.py --input alpr/images --sample 5 --show
"""

from __future__ import annotations
import argparse
import os
import random
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}

def setup_runtime_environment(cache_dir: Path) -> None:
    cache_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(cache_dir / "matplotlib")
    os.environ["YOLO_CONFIG_DIR"] = str(cache_dir / "ultralytics")

@dataclass
class CharacterPrediction:
    box: tuple[int, int, int, int]
    label: str
    confidence: float

@dataclass
class PlatePrediction:
    box: tuple[int, int, int, int]
    confidence: float
    text: str
    raw_text: str
    valid_plate: bool
    characters: list[CharacterPrediction]
    image_outputs: dict[str, Path]

def require_runtime_imports():
    missing = []
    try:
        import cv2  # noqa: F401
    except ImportError:
        missing.append("opencv-python")
    try:
        import numpy  # noqa: F401
    except ImportError:
        missing.append("numpy")
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
            "Eksik Python paketleri: " + ", ".join(missing) + 
            ". Kurulum: pip install " + " ".join(missing)
        )

def iter_images(path: Path) -> list[Path]:
    if path.is_file():
        if path.suffix.lower() not in IMAGE_SUFFIXES:
            raise ValueError(f"Desteklenmeyen input uzantisi: {path}")
        return [path]
    if not path.is_dir():
        raise FileNotFoundError(path)
    return sorted(p for p in path.iterdir() if p.suffix.lower() in IMAGE_SUFFIXES)


# --- DASHBOARD YARDIMCI FONKSİYONLARI ---
def fit_image(img, max_w, max_h):
    import cv2
    h, w = img.shape[:2]
    scale = min(max_w / w, max_h / h)
    if scale > 1.5: scale = 1.5 
    return cv2.resize(img, (int(w * scale), int(h * scale)))

def build_dashboard(annot, pl, dbg_pl, final_text, is_valid):
    import cv2
    import numpy as np
    
    # 1200x700 boyutunda koyu gri arka plan
    ch, cw = 700, 1200
    canvas = np.full((ch, cw, 3), 40, dtype=np.uint8)

    # 1. Sol Panel: Tam Araç Görseli
    annot_fit = fit_image(annot, 760, 660)
    h_a, w_a = annot_fit.shape[:2]
    y_offset_a = (ch - h_a) // 2
    x_offset_a = 20
    canvas[y_offset_a:y_offset_a+h_a, x_offset_a:x_offset_a+w_a] = annot_fit

    # Sağ Taraf X Ekseni Başlangıcı
    x_right = 800

    # 2. Sağ Panel - Adım 1: YOLO Kırpması
    pl_fit = fit_image(pl, 360, 200)
    cv2.putText(canvas, "1. YOLO Plaka Tespiti", (x_right, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 255, 200), 2)
    canvas[70:70+pl_fit.shape[0], x_right:x_right+pl_fit.shape[1]] = pl_fit

    # 3. Sağ Panel - Adım 2: EasyOCR
    dbg_fit = fit_image(dbg_pl, 360, 200)
    y_dbg = 70 + pl_fit.shape[0] + 50
    cv2.putText(canvas, "2. EasyOCR Analizi", (x_right, y_dbg - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 255, 200), 2)
    canvas[y_dbg:y_dbg+dbg_fit.shape[0], x_right:x_right+dbg_fit.shape[1]] = dbg_fit

    # 4. Sağ Panel - Adım 3: NİHAİ DÜZ METİN (YENİ EKLENEN KISIM)
    y_final = y_dbg + dbg_fit.shape[0] + 50
    cv2.putText(canvas, "3. Nihai Okunan Plaka", (x_right, y_final - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 255, 200), 2)
    
    # Plaka formata uygunsa yeşil, uyduruksa/boşsa kırmızı kutu yazısı
    text_color = (0, 255, 0) if is_valid else (0, 0, 255)
    display_text = final_text if final_text else "OKUNAMADI"
    
    # Şık bir kutu çizelim
    cv2.rectangle(canvas, (x_right, y_final), (x_right + 360, y_final + 60), (60, 60, 60), -1)
    # Metni ortalayarak veya hizalayarak yaz
    cv2.putText(canvas, display_text, (x_right + 20, y_final + 45), cv2.FONT_HERSHEY_SIMPLEX, 1.4, text_color, 3)

    cv2.putText(canvas, "Sonraki resim icin bir tusa basin...", (x_right, 670), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (150, 150, 150), 1)

    return canvas


class ALPRPipeline:
    def __init__(self, yolo_model_path: Path, detector_confidence: float) -> None:
        require_runtime_imports()
        from ultralytics import YOLO
        import easyocr

        if not yolo_model_path.exists():
            raise FileNotFoundError(yolo_model_path)

        print("--- Modeller Yukleniyor (Lokal CPU Modu) ---")
        self.yolo_model = YOLO(str(yolo_model_path))
        self.ocr_reader = easyocr.Reader(['en'], gpu=False)
        self.detector_confidence = detector_confidence

    def process_image(self, image_path: Path, output_dir: Path | None, show: bool) -> list[PlatePrediction]:
        import cv2

        if output_dir is not None:
            output_dir.mkdir(parents=True, exist_ok=True)

        frame = cv2.imread(str(image_path))
        if frame is None:
            raise ValueError(f"Goruntu okunamadi: {image_path}")

        annotated = frame.copy()
        predictions: list[PlatePrediction] = []

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
                
                characters, debug_plate, raw_ocr_text = self._read_plate_text_with_boxes(plate)
                
                # --- AKILLI FİLTRE BURADA DEVREYE GİRİYOR ---
                text, valid_plate = normalize_turkish_plate(raw_ocr_text)
                final_display_text = text if valid_plate else raw_ocr_text
                image_outputs: dict[str, Path] = {}

                predictions.append(
                    PlatePrediction(
                        box=(x1, y1, x2, y2),
                        confidence=confidence,
                        text=text,
                        raw_text=raw_ocr_text,
                        valid_plate=valid_plate,
                        characters=characters,
                        image_outputs=image_outputs,
                    )
                )
                prediction = predictions[-1]

                main_text_y = y1 - 12
                if main_text_y < 30: 
                    main_text_y = y2 + 35 

                cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0) if valid_plate else (0,165,255), 3)
                cv2.putText(
                    annotated,
                    final_display_text if final_display_text else "PLAKA",
                    (x1, main_text_y),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    1.2,
                    (0, 255, 0) if valid_plate else (0,165,255),
                    3,
                )

                if show:
                    # Dashboard'a final_display_text ve valid_plate durumunu gönderiyoruz
                    dashboard = build_dashboard(annotated, plate, debug_plate, final_display_text, valid_plate)
                    cv2.imshow("ALPR Analiz Paneli", dashboard)
                    cv2.waitKey(0)

        return predictions

    def _read_plate_text_with_boxes(self, plate):
        import cv2
        import numpy as np

        if plate.size == 0:
            return [], None, ""

        resized_plate = cv2.resize(plate, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_CUBIC)
        debug_plate = resized_plate.copy()

        ocr_results = self.ocr_reader.readtext(plate, detail=1)
        final_text = ""
        char_predictions = []

        if not ocr_results:
            return char_predictions, debug_plate, final_text

        # 1. YÜKSEKLİK (HEIGHT) FİLTRESİ: En büyük metin kutusunu bul
        heights = [bbox[2][1] - bbox[0][1] for bbox, text, prob in ocr_results]
        max_h = max(heights) if heights else 0

        for (bbox, text, prob) in ocr_results:
            h = bbox[2][1] - bbox[0][1]
            
            # Eğer kutu yüksekliği referans yüksekliğin %40'ından kısaysa bu alt yazıdır (galeri ismi vs), geç!
            if h < max_h * 0.4:
                continue
                
            # Eğer okunan sadece 'TR' logonsuysa doğrudan ele!
            if text.strip().upper() == 'TR':
                continue

            x_min = int(bbox[0][0] * 2)
            y_min = int(bbox[0][1] * 2)
            x_max = int(bbox[2][0] * 2)
            y_max = int(bbox[2][1] * 2)

            cleaned_text = re.sub(r'[^A-Z0-9]', '', text.upper())
            if not cleaned_text:
                continue

            final_text += cleaned_text
            char_predictions.append(CharacterPrediction((x_min, y_min, x_max, y_max), cleaned_text, float(prob)))

            cv2.rectangle(debug_plate, (x_min, y_min), (x_max, y_max), (0, 255, 0), 2)
            
            char_text_y = y_min - 6
            if char_text_y < 20: 
                char_text_y = y_max + 22 

            cv2.putText(
                debug_plate,
                cleaned_text,
                (x_min, char_text_y),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.7,
                (0, 255, 0),
                2,
            )

        return char_predictions, debug_plate, final_text


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run YOLO .pt + EasyOCR ALPR Inference")
    parser.add_argument("--input", default="alpr/images", help="Gorsel klasoru")
    parser.add_argument("--pt", default="yolov8_plaka.pt", help="YOLO .pt model yolu")
    parser.add_argument("--sample", type=int, default=5, help="Rastgele resim adedi")
    parser.add_argument("--seed", type=int, default=None, help="Random seed")
    parser.add_argument("--conf", type=float, default=0.25, help="YOLO guven esigi")
    parser.add_argument("--output", default="alpr_results", help="Kayit klasoru")
    parser.add_argument("--show", action="store_true", help="Canli pencereleri ac")
    return parser.parse_args()


def normalize_turkish_plate(raw_text: str) -> tuple[str, bool]:
    import re
    # Harici karakterleri temizle
    clean = re.sub(r"[^0-9A-Z]", "", raw_text.upper())
    
    # Bazen 'TR' harfleri plaka ile bitişik okunur (TR06DN4461 gibi), onları tıraşla
    if clean.startswith("TR") and len(clean) > 2 and clean[2].isdigit():
        clean = clean[2:]
        
    # 2. AKILLI REGEX: Tam Türk plakası formatını arar (01-81 İl, 1-3 Harf, 2-4 Rakam)
    match = re.search(r"(0[1-9]|[1-7][0-9]|8[01])([A-Z]{1,3})([0-9]{2,4})", clean)
    
    if match:
        # Eğer formata uyuyorsa sadece o jilet gibi kısmı al
        return match.group(0), True
        
    return clean, False


def main() -> int:
    args = parse_args()
    setup_runtime_environment(Path(".runtime_cache"))
    input_path = Path(args.input)
    output_dir = Path(args.output) if args.output else None

    images = iter_images(input_path)
    if not images:
        print(f"Gorsel bulunamadi: {input_path}", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    if input_path.is_dir() and args.sample > 0 and len(images) > args.sample:
        images = rng.sample(images, args.sample)

    pipeline = ALPRPipeline(
        yolo_model_path=Path(args.pt),
        detector_confidence=args.conf,
    )

    for image_path in images:
        predictions = pipeline.process_image(image_path, output_dir, args.show)
        if not predictions:
            print(f"{image_path}: plaka bulunamadi")
            continue

        for index, prediction in enumerate(predictions, start=1):
            status = "valid" if prediction.valid_plate else "invalid_format"
            print(
                f"{image_path} [{index}] conf={prediction.confidence:.3f} "
                f"status={status} text='{prediction.text}'"
            )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())