"""Offline visual and numeric evaluation of Phase 4H SAM prompt strategies."""

from __future__ import annotations

import argparse
import csv
import pathlib
import time

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

SAM_SIZE = 1024
MASK_SIZE = 256
MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def preprocess(image: Image.Image) -> np.ndarray:
    resized = image.convert("RGB").resize((SAM_SIZE, SAM_SIZE), Image.Resampling.BILINEAR)
    values = np.asarray(resized, dtype=np.float32) / 255.0
    return ((values - MEAN) / STD).transpose(2, 0, 1)[None]


def prompt(box: tuple[float, float, float, float], width: int, height: int,
           extras: list[tuple[float, float, int]]) -> tuple[np.ndarray, np.ndarray]:
    sx, sy = SAM_SIZE / width, SAM_SIZE / height
    points = [[box[0] * sx, box[1] * sy], [box[2] * sx, box[3] * sy]]
    labels = [2, 3]
    for x, y, label in extras:
        points.append([x * sx, y * sy]); labels.append(label)
    return np.asarray(points, np.float32)[None], np.asarray(labels, np.int32)[None]


def relative_point(box: tuple[float, float, float, float], rx: float, ry: float, label: int) -> tuple[float, float, int]:
    return box[0] + (box[2] - box[0]) * rx, box[1] + (box[3] - box[1]) * ry, label


def auto_points(mask: np.ndarray, box: tuple[float, float, float, float], count: int,
                include_negative: bool) -> list[tuple[float, float, int]]:
    """Choose conservative prompts using only the box-only mask and Hair Box."""
    height, width = mask.shape
    x0 = max(0, int(box[0])); y0 = max(0, int(box[1]))
    x1 = min(width, int(np.ceil(box[2]))); y1 = min(height, int(np.ceil(box[3])))
    region = mask[y0:y1, x0:x1]
    distance = ndimage.distance_transform_edt(region)
    positives: list[tuple[float, float, int]] = []
    work = distance.copy()
    suppress_y = max(3, int((y1 - y0) * 0.18))
    suppress_x = max(3, int((x1 - x0) * 0.18))
    for _ in range(count):
        local_y, local_x = np.unravel_index(np.argmax(work), work.shape)
        if work[local_y, local_x] <= 0:
            break
        positives.append((float(x0 + local_x), float(y0 + local_y), 1))
        yy0, yy1 = max(0, local_y - suppress_y), min(work.shape[0], local_y + suppress_y + 1)
        xx0, xx1 = max(0, local_x - suppress_x), min(work.shape[1], local_x + suppress_x + 1)
        work[yy0:yy1, xx0:xx1] = 0

    if include_negative:
        # Search the central/lower head area for a stable non-mask pixel. This is a
        # weak face/neck prior, not a face detector and not a hard semantic rule.
        nx0 = int(region.shape[1] * 0.30); nx1 = int(region.shape[1] * 0.70)
        ny0 = int(region.shape[0] * 0.42); ny1 = int(region.shape[0] * 0.82)
        outside_distance = ndimage.distance_transform_edt(~region)
        candidate = np.zeros_like(outside_distance)
        candidate[ny0:ny1, nx0:nx1] = outside_distance[ny0:ny1, nx0:nx1]
        local_y, local_x = np.unravel_index(np.argmax(candidate), candidate.shape)
        if candidate[local_y, local_x] > 0:
            positives.append((float(x0 + local_x), float(y0 + local_y), 0))
    return positives


def source_mask(logits: np.ndarray, width: int, height: int, threshold: float = 0.50) -> np.ndarray:
    low = logits.reshape(MASK_SIZE, MASK_SIZE)
    image = Image.fromarray(low.astype(np.float32), mode="F").resize((width, height), Image.Resampling.BILINEAR)
    cutoff = np.log(threshold / (1.0 - threshold))
    return np.asarray(image) >= cutoff


def metrics(mask: np.ndarray, baseline: np.ndarray, predicted_iou: float) -> dict[str, float | int]:
    intersection = np.logical_and(mask, baseline).sum()
    union = np.logical_or(mask, baseline).sum()
    _, components = ndimage.label(mask)
    base_area = max(1, int(baseline.sum()))
    return {
        "predicted_iou": predicted_iou,
        "area": int(mask.sum()),
        "area_ratio": float(mask.sum() / base_area),
        "baseline_iou": float(intersection / max(1, union)),
        "components": int(components),
    }


def overlay(image: Image.Image, mask: np.ndarray, color: tuple[int, int, int]) -> Image.Image:
    base = np.asarray(image.convert("RGB"), dtype=np.float32)
    tint = np.empty_like(base); tint[:] = color
    alpha = mask[..., None].astype(np.float32) * 0.42
    return Image.fromarray(np.clip(base * (1 - alpha) + tint * alpha, 0, 255).astype(np.uint8))


def annotated(image: Image.Image, box: tuple[float, float, float, float], extras: list[tuple[float, float, int]]) -> Image.Image:
    result = image.convert("RGB").copy(); draw = ImageDraw.Draw(result)
    draw.rectangle(box, outline=(30, 230, 120), width=4)
    for x, y, label in extras:
        color = (40, 255, 80) if label == 1 else (255, 55, 55)
        radius = 10
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), outline=color, width=5)
        draw.line((x - radius, y, x + radius, y), fill=color, width=3)
        draw.line((x, y - radius, x, y + radius), fill=color, width=3)
    return result


def panel(title: str, image: Image.Image, size: tuple[int, int]) -> Image.Image:
    result = Image.new("RGB", (size[0], size[1] + 28), (10, 12, 15))
    fitted = image.copy(); fitted.thumbnail(size, Image.Resampling.LANCZOS)
    result.paste(fitted, ((size[0] - fitted.width) // 2, 28 + (size[1] - fitted.height) // 2))
    ImageDraw.Draw(result).text((8, 6), title, fill=(230, 230, 230))
    return result


def evaluate(name: str, image_path: pathlib.Path, box: tuple[float, float, float, float],
             encoder: ort.InferenceSession, legacy: ort.InferenceSession, single: ort.InferenceSession,
             multi: ort.InferenceSession, output: pathlib.Path) -> list[dict[str, object]]:
    image = Image.open(image_path).convert("RGB"); width, height = image.size
    features_values = encoder.run(None, {"image": preprocess(image)})
    features = dict(zip(("image_embed", "high_res_0", "high_res_1"), features_values))
    box_input = np.asarray(box, np.float32)[None] * np.asarray([[SAM_SIZE / width, SAM_SIZE / height] * 2], np.float32)
    start = time.perf_counter(); base_logits, base_iou = legacy.run(None, {**features, "box_xyxy_1024": box_input})
    base_ms = (time.perf_counter() - start) * 1000
    baseline = source_mask(base_logits[0, 0], width, height)

    point_start = time.perf_counter()
    automatic = {
        "F_auto_distance_1": auto_points(baseline, box, 1, False),
        "G_auto_distance_3": auto_points(baseline, box, 3, False),
        "H_auto_distance_3_negative": auto_points(baseline, box, 3, True),
    }
    point_ms = (time.perf_counter() - point_start) * 1000
    strategies = {
        "A_box_only": [],
        "B_center_positive": [relative_point(box, 0.50, 0.50, 1)],
        "B_top_positive": [relative_point(box, 0.50, 0.18, 1)],
        "C_three_positive": [relative_point(box, 0.50, 0.15, 1), relative_point(box, 0.25, 0.32, 1), relative_point(box, 0.75, 0.32, 1)],
        "D_top_positive_face_negative": [relative_point(box, 0.50, 0.16, 1), relative_point(box, 0.50, 0.62, 0)],
        "E_three_positive_two_negative": [relative_point(box, 0.50, 0.15, 1), relative_point(box, 0.22, 0.34, 1), relative_point(box, 0.78, 0.34, 1), relative_point(box, 0.50, 0.58, 0), relative_point(box, 0.50, 0.82, 0)],
        **automatic,
    }
    rows: list[dict[str, object]] = []
    strategy_logits: dict[str, np.ndarray] = {"A_box_only": base_logits[0, 0]}
    panels = [panel("Original + Hair Box", annotated(image, box, []), (420, 300)), panel("Box-only overlay", overlay(image, baseline, (20, 220, 110)), (420, 300))]
    for strategy, extras in strategies.items():
        if strategy == "A_box_only":
            mask, iou, decoder_ms = baseline, float(base_iou[0, 0]), base_ms
        else:
            points, labels = prompt(box, width, height, extras)
            start = time.perf_counter(); logits, predicted = single.run(None, {**features, "prompt_points_1024": points, "prompt_labels": labels})
            decoder_ms = (time.perf_counter() - start) * 1000
            strategy_logits[strategy] = logits[0, 0]
            mask, iou = source_mask(logits[0, 0], width, height), float(predicted[0, 0])
        values = metrics(mask, baseline, iou)
        rows.append({"image": name, "strategy": strategy, "point_ms": point_ms if strategy in automatic else 0.0,
                     "decoder_ms": decoder_ms, **values})
        if strategy != "A_box_only":
            prompted = annotated(overlay(image, mask, (30, 130, 255)), box, extras)
            panels.append(panel(strategy, prompted, (420, 300)))
            difference = np.zeros((height, width, 3), dtype=np.uint8)
            difference[np.logical_and(mask, ~baseline)] = (40, 180, 255)
            difference[np.logical_and(baseline, ~mask)] = (255, 80, 70)
            Image.fromarray(difference).save(output / f"{name}_{strategy}_difference.png")
            Image.fromarray((mask * 255).astype(np.uint8)).save(output / f"{name}_{strategy}_mask.png")

    # Compare all three multimask candidates for the conservative D strategy.
    multi_extras = strategies["D_top_positive_face_negative"]
    points, labels = prompt(box, width, height, multi_extras)
    start = time.perf_counter(); logits, predicted = multi.run(None, {**features, "prompt_points_1024": points, "prompt_labels": labels})
    multi_ms = (time.perf_counter() - start) * 1000
    for index in range(3):
        mask = source_mask(logits[0, index], width, height)
        values = metrics(mask, baseline, float(predicted[0, index]))
        rows.append({"image": name, "strategy": f"M_multimask_{index}", "point_ms": 0.0,
                     "decoder_ms": multi_ms, **values})
        panels.append(panel(f"Multimask {index}", overlay(image, mask, (180, 80, 255)), (420, 300)))

    columns = 3; rows_count = (len(panels) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * 420, rows_count * 328), (5, 7, 10))
    for index, item in enumerate(panels): sheet.paste(item, ((index % columns) * 420, (index // columns) * 328))
    sheet.save(output / f"{name}_comparison.png")
    if name == "difficult":
        threshold_panels = [panel("Original + Hair Box", annotated(image, box, []), (420, 300))]
        for title, strategy, threshold, color in (
            ("Box-only Raw threshold 0.13", "A_box_only", 0.13, (255, 170, 30)),
            ("Box-only Raw threshold 0.50", "A_box_only", 0.50, (20, 220, 110)),
            ("Box + Positive Raw 0.50", "F_auto_distance_1", 0.50, (30, 130, 255)),
            ("Box + Positive + Negative Raw 0.50", "D_top_positive_face_negative", 0.50, (185, 80, 255)),
        ):
            threshold_mask = source_mask(strategy_logits[strategy], width, height, threshold)
            threshold_panels.append(panel(title, overlay(image, threshold_mask, color), (420, 300)))
        threshold_sheet = Image.new("RGB", (3 * 420, 2 * 328), (5, 7, 10))
        for index, item in enumerate(threshold_panels):
            threshold_sheet.paste(item, ((index % 3) * 420, (index // 3) * 328))
        threshold_sheet.save(output / "difficult_raw_threshold_comparison.png")
    Image.fromarray((baseline * 255).astype(np.uint8)).save(output / f"{name}_A_box_only_mask.png")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--encoder", type=pathlib.Path, required=True)
    parser.add_argument("--legacy", type=pathlib.Path, required=True)
    parser.add_argument("--single", type=pathlib.Path, required=True)
    parser.add_argument("--multi", type=pathlib.Path, required=True)
    parser.add_argument("--resources", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    sessions = [ort.InferenceSession(str(path), providers=["CPUExecutionProvider"]) for path in (args.encoder, args.legacy, args.single, args.multi)]
    cases = {
        "normal": ("testPicter.png", (954.737, 44.6984, 1678.14, 716.353)),
        "difficult": ("20251218113150_802_802528.png", (528.286, 11.743, 993.895, 501.8)),
        "heavy": ("26_06_12_20_56_04.png", (1047.83, 225.163, 1249.28, 789.294)),
        "close": ("20251221194145_045_045365.png", (803.202, 145.355, 1147.3, 490.549)),
    }
    all_rows = []
    for name, (filename, box) in cases.items():
        all_rows.extend(evaluate(name, args.resources / filename, box, *sessions, args.output))
    with (args.output / "metrics.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=all_rows[0].keys()); writer.writeheader(); writer.writerows(all_rows)
    for row in all_rows: print(row)


if __name__ == "__main__":
    main()
