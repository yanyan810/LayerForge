"""Validation and loading for LayerForge StyleDataset folders."""
from __future__ import annotations
import json
from dataclasses import dataclass
from pathlib import Path

@dataclass(frozen=True)
class DatasetItem:
    image_path: Path
    prompt: str

def _inside(root: Path, candidate: Path) -> Path:
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValueError(f"Dataset path escapes its root: {candidate}") from error
    return resolved

def load_dataset_items(dataset_path: Path, trigger_word: str) -> list[DatasetItem]:
    root = dataset_path.resolve()
    if not root.is_dir():
        raise ValueError(f"Dataset does not exist: {root}")
    manifest_path = root / "dataset.json"
    if not manifest_path.is_file():
        raise ValueError(f"dataset.json does not exist: {manifest_path}")
    with manifest_path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest.get("items"), list):
        raise ValueError("dataset.json items must be an array.")
    result: list[DatasetItem] = []
    for entry in manifest["items"]:
        if not isinstance(entry, dict) or not entry.get("enabled", True):
            continue
        image_path = _inside(root, root / str(entry.get("image", "")))
        caption_path = _inside(root, root / str(entry.get("caption", "")))
        if not image_path.is_file():
            raise ValueError(f"Dataset image does not exist: {image_path}")
        if image_path.suffix.lower() not in {".png", ".jpg", ".jpeg"}:
            raise ValueError(f"Unsupported training image: {image_path.name}")
        try:
            caption = caption_path.read_text(encoding="utf-8").strip()
        except OSError as error:
            raise ValueError(f"Could not read caption: {caption_path}") from error
        parts = [part for part in (trigger_word.strip(), caption) if part]
        result.append(DatasetItem(image_path, ", ".join(parts) or "style"))
    if not result:
        raise ValueError("Dataset contains no enabled images.")
    return result
