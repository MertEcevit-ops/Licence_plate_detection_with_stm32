#!/usr/bin/env python3
"""Run the ALPR pipeline with a YOLO .pt detector and Keras .h5 OCR model.

Default local usage:
    python3 alpr_inference.py --input alpr --sample 5
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


EMNIST_MAPPING = list("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}
TURKISH_PLATE_LETTERS = "ABCDEFGHIJKLMNOPRSTUVYZ"
TURKISH_PLATE_RE = re.compile(
    rf"^(0[1-9]|[1-7][0-9]|8[01])([{TURKISH_PLATE_LETTERS}]{{1,3}})([0-9]{{2,4}})$"
)


def setup_runtime_environment(cache_dir: Path) -> None:
    cache_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(cache_dir / "matplotlib")
    os.environ["YOLO_CONFIG_DIR"] = str(cache_dir / "ultralytics")
    os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
    os.environ["TF_ENABLE_ONEDNN_OPTS"] = "0"
    os.environ["CUDA_VISIBLE_DEVICES"] = "-1"


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
        import tensorflow  # noqa: F401
    except ImportError:
        missing.append("tensorflow")

    if missing:
        raise RuntimeError(
            "Eksik Python paketleri: "
            + ", ".join(missing)
            + ". Kurulum ornegi: python3 -m pip install "
            + " ".join(missing)
        )


def iter_images(path: Path) -> list[Path]:
    if path.is_file():
        if path.suffix.lower() not in IMAGE_SUFFIXES:
            raise ValueError(f"Desteklenmeyen input uzantisi: {path}")
        return [path]

    if not path.is_dir():
        raise FileNotFoundError(path)

    return sorted(p for p in path.iterdir() if p.suffix.lower() in IMAGE_SUFFIXES)


class ALPRPipeline:
    def __init__(
        self,
        yolo_model_path: Path,
        ocr_model_path: Path,
        detector_confidence: float,
        emnist_transpose: bool = False,
    ) -> None:
        require_runtime_imports()

        from ultralytics import YOLO
        import tensorflow as tf

        if not yolo_model_path.exists():
            raise FileNotFoundError(yolo_model_path)
        if not ocr_model_path.exists():
            raise FileNotFoundError(ocr_model_path)

        self.yolo_model = YOLO(str(yolo_model_path))
        self.ocr_model = tf.keras.models.load_model(str(ocr_model_path), compile=False)
        self.detector_confidence = detector_confidence
        self.emnist_transpose = emnist_transpose

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
                characters, debug_plate = self._read_plate_text(plate)
                raw_text = "".join(c.label for c in characters)
                text, valid_plate = normalize_turkish_plate(raw_text)
                image_outputs: dict[str, Path] = {}

                predictions.append(
                    PlatePrediction(
                        box=(x1, y1, x2, y2),
                        confidence=confidence,
                        text=text,
                        raw_text=raw_text,
                        valid_plate=valid_plate,
                        characters=characters,
                        image_outputs=image_outputs,
                    )
                )
                prediction = predictions[-1]

                cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 255, 0), 2)
                cv2.putText(
                    annotated,
                    text if valid_plate else "unreadable",
                    (x1, max(y1 - 8, 0)),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.7,
                    (0, 255, 0),
                    2,
                )

                if output_dir is not None:
                    detection_index = len(predictions)
                    crop_path = output_dir / f"{image_path.stem}_plate_crop_{detection_index}.png"
                    cv2.imwrite(str(crop_path), plate)
                    prediction.image_outputs["crop"] = crop_path

                    if debug_plate is not None:
                        debug_path = output_dir / f"{image_path.stem}_ocr_debug_{detection_index}.png"
                        cv2.imwrite(str(debug_path), debug_plate)
                        prediction.image_outputs["ocr_debug"] = debug_path

                    report = build_detection_report(frame, annotated, plate, debug_plate, prediction)
                    report_path = output_dir / f"{image_path.stem}_report_{detection_index}.png"
                    cv2.imwrite(str(report_path), report)
                    prediction.image_outputs["report"] = report_path

        if output_dir is not None:
            out_path = output_dir / f"{image_path.stem}_alpr.png"
            cv2.imwrite(str(out_path), annotated)
            for prediction in predictions:
                prediction.image_outputs["annotated"] = out_path

        if show:
            cv2.imshow("ALPR output", annotated)
            cv2.waitKey(0)

        return predictions

    def _read_plate_text(self, plate):
        import cv2
        import numpy as np

        if plate.size == 0:
            return [], None

        resized_plate = cv2.resize(plate, None, fx=2.0, fy=2.0, interpolation=cv2.INTER_CUBIC)
        gray = cv2.cvtColor(resized_plate, cv2.COLOR_BGR2GRAY)
        blur = cv2.GaussianBlur(gray, (5, 5), 0)
        _, thresh = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
        contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        char_boxes = []
        plate_h, plate_w = thresh.shape[:2]
        min_height = max(12, int(thresh.shape[0] * 0.20))
        for contour in contours:
            x, y, w, h = cv2.boundingRect(contour)
            aspect_ratio = w / float(h)
            is_character_like = (
                0.12 < aspect_ratio < 1.05
                and h >= min_height
                and h <= int(plate_h * 0.88)
                and w <= int(plate_w * 0.16)
            )
            if is_character_like:
                char_boxes.append((x, y, w, h))

        char_boxes = sorted(char_boxes, key=lambda b: b[0])
        debug_plate = cv2.cvtColor(thresh, cv2.COLOR_GRAY2BGR)
        predictions: list[CharacterPrediction] = []

        for x, y, w, h in char_boxes:
            char_crop = thresh[y : y + h, x : x + w]
            if char_crop.size == 0:
                continue

            char_resized = cv2.resize(char_crop, (28, 28), interpolation=cv2.INTER_AREA)
            if self.emnist_transpose:
                char_resized = cv2.transpose(char_resized)
                char_resized = cv2.flip(char_resized, 1)

            model_input = char_resized.astype("float32") / 255.0
            model_input = np.expand_dims(model_input, axis=-1)
            model_input = np.expand_dims(model_input, axis=0)

            scores = self.ocr_model.predict(model_input, verbose=False)[0]
            pred_class = int(np.argmax(scores))
            label = EMNIST_MAPPING[pred_class] if pred_class < len(EMNIST_MAPPING) else "?"
            confidence = float(scores[pred_class])
            predictions.append(CharacterPrediction((x, y, w, h), label, confidence))

            cv2.rectangle(debug_plate, (x, y), (x + w, y + h), (0, 255, 0), 1)
            cv2.putText(
                debug_plate,
                label,
                (x, max(y - 3, 0)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.45,
                (0, 255, 0),
                1,
            )

        return predictions, debug_plate


def resize_to_width(image: Any, width: int):
    import cv2

    if image is None or image.size == 0:
        return None
    height = max(1, int(image.shape[0] * (width / image.shape[1])))
    return cv2.resize(image, (width, height), interpolation=cv2.INTER_AREA)


def make_panel(image: Any, title: str, width: int):
    import cv2
    import numpy as np

    resized = resize_to_width(image, width)
    if resized is None:
        resized = np.full((80, width, 3), 245, dtype=np.uint8)
    if len(resized.shape) == 2:
        resized = cv2.cvtColor(resized, cv2.COLOR_GRAY2BGR)

    title_bar = np.full((34, width, 3), 32, dtype=np.uint8)
    cv2.putText(title_bar, title, (10, 23), cv2.FONT_HERSHEY_SIMPLEX, 0.62, (255, 255, 255), 1)
    return np.vstack([title_bar, resized])


def fit_panel(image: Any, height: int, width: int):
    import cv2
    import numpy as np

    resized_width = max(1, int(image.shape[1] * (height / image.shape[0])))
    resized = cv2.resize(image, (resized_width, height), interpolation=cv2.INTER_AREA)

    if resized.shape[1] > width:
        return resized[:, :width]

    pad = np.full((height, width - resized.shape[1], 3), 245, dtype=np.uint8)
    return np.hstack([resized, pad])


def build_detection_report(frame: Any, annotated: Any, plate: Any, debug_plate: Any, prediction: PlatePrediction):
    import cv2
    import numpy as np

    width = 900
    annotated_panel = make_panel(annotated, "annotated detection", width)
    crop_panel = make_panel(plate, "plate crop", width // 2)
    debug_panel = make_panel(debug_plate, "ocr debug", width // 2)

    bottom_height = max(crop_panel.shape[0], debug_panel.shape[0])
    crop_panel = fit_panel(crop_panel, bottom_height, width // 2)
    debug_panel = fit_panel(debug_panel, bottom_height, width // 2)
    bottom = np.hstack([crop_panel, debug_panel])

    info = np.full((72, width, 3), 250, dtype=np.uint8)
    status = "valid" if prediction.valid_plate else "invalid_ocr"
    text = prediction.text if prediction.text else "-"
    line1 = f"conf={prediction.confidence:.3f} status={status} text={text}"
    line2 = f"raw={prediction.raw_text if prediction.raw_text else '-'} box={prediction.box}"
    cv2.putText(info, line1[:95], (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.68, (20, 20, 20), 2)
    cv2.putText(info, line2[:110], (12, 58), cv2.FONT_HERSHEY_SIMPLEX, 0.58, (80, 80, 80), 1)

    return np.vstack([info, annotated_panel, bottom])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run YOLO .pt + Keras .h5 ALPR inference")
    parser.add_argument("--input", default="alpr", help="Image file or folder; default: alpr")
    parser.add_argument("--pt", default="best.pt", help="YOLO .pt model path")
    parser.add_argument("--h5", default="emnist_ocr.h5", help="Keras OCR .h5 model path")
    parser.add_argument("--sample", type=int, default=5, help="Random image count for folder inputs")
    parser.add_argument("--seed", type=int, default=None, help="Random seed for reproducible sampling")
    parser.add_argument("--conf", type=float, default=0.25, help="YOLO detector confidence threshold")
    parser.add_argument("--output", default="alpr_results", help="Output folder for annotated images")
    parser.add_argument("--show", action="store_true", help="Open OpenCV preview windows")
    parser.add_argument(
        "--no-emnist-transpose",
        action="store_true",
        help="Disable EMNIST transpose/flip preprocessing",
    )
    return parser.parse_args()


def normalize_turkish_plate(raw_text: str) -> tuple[str, bool]:
    candidate = re.sub(r"[^0-9A-Z]", "", raw_text.upper())
    if TURKISH_PLATE_RE.match(candidate):
        return candidate, True
    return "", False


def main() -> int:
    args = parse_args()
    setup_runtime_environment(Path(".runtime_cache"))
    input_path = Path(args.input)
    output_dir = Path(args.output) if args.output else None

    images = iter_images(input_path)
    if not images:
        print(f"Input icinde goruntu bulunamadi: {input_path}", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    if input_path.is_dir() and args.sample > 0 and len(images) > args.sample:
        images = rng.sample(images, args.sample)

    pipeline = ALPRPipeline(
        yolo_model_path=Path(args.pt),
        ocr_model_path=Path(args.h5),
        detector_confidence=args.conf,
        emnist_transpose=not args.no_emnist_transpose,
    )

    for image_path in images:
        predictions = pipeline.process_image(image_path, output_dir, args.show)
        if not predictions:
            print(f"{image_path}: plaka bulunamadi")
            continue

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

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
