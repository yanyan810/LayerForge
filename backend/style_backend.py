"""LayerForge Style AI training Backend entry point."""
from __future__ import annotations
import json
import sys
from pathlib import Path

def out(message: str) -> None:
    print(f"[LayerForge] {message}", flush=True)

def fail(message: str) -> int:
    print(f"[ERROR] {message}", file=sys.stderr, flush=True)
    return 1

def load_config(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        config = json.load(stream)
    for key in ("dataset", "output_name", "output_dir", "base_model"):
        if not str(config.get(key, "")).strip():
            raise ValueError(f"Training config field '{key}' is required.")
    return config

def main() -> int:
    if len(sys.argv) != 2:
        return fail("Usage: style_backend.py <training_config.json>")
    try:
        config = load_config(Path(sys.argv[1]))
        out("Backend started")
        from trainer.dataset_loader import load_dataset_items
        items = load_dataset_items(Path(config["dataset"]), str(config.get("trigger_word", "lfstyle")))
        out("Loading dataset...")
        out(f"Images: {len(items)}")
        try:
            import torch
        except ImportError:
            return fail("Missing Python package: torch. Install backend/requirements.txt")
        if not torch.cuda.is_available():
            return fail("CUDA is not available.")
        properties = torch.cuda.get_device_properties(0)
        out("CUDA available")
        out(f"PyTorch: {torch.__version__} (CUDA {torch.version.cuda})")
        out(f"GPU: {torch.cuda.get_device_name(0)}")
        out(f"VRAM: {properties.total_memory // (1024 * 1024)} MB")
        try:
            from trainer.lora_trainer import train_lora
        except ImportError as error:
            package = (error.name or "unknown").split(".")[0]
            return fail(f"Missing Python package: {package}. Install backend/requirements.txt")
        train_lora(config, items, out)
        out("Complete")
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        return fail(str(error))
    except Exception as error:
        if "out of memory" in str(error).lower():
            fail("CUDA out of memory.")
            print("Try: Resolution 512; Batch Size 1; enable Gradient Checkpointing; reduce LoRA Rank.", file=sys.stderr, flush=True)
            return 1
        return fail(f"Training failed: {error}")

if __name__ == "__main__":
    raise SystemExit(main())
