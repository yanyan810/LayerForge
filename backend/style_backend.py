"""Phase 2 process/streaming test backend for LayerForge."""

import json
import sys
import time


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: style_backend.py <training_config.json>", file=sys.stderr, flush=True)
        return 2

    try:
        with open(sys.argv[1], "r", encoding="utf-8") as config_file:
            config = json.load(config_file)
    except (OSError, json.JSONDecodeError) as error:
        print(f"Could not load config: {error}", file=sys.stderr, flush=True)
        return 1

    print("[LayerForge] Backend started", flush=True)
    print(f"[LayerForge] Dataset: {config['dataset']}", flush=True)
    for progress in range(0, 101, 10):
        print(f"[Progress] {progress}", flush=True)
        time.sleep(0.3)
    print("[LayerForge] Complete", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
