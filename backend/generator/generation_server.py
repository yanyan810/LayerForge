"""Persistent local generation worker using atomic JSON request files."""
from __future__ import annotations
import json,time,sys
from pathlib import Path
from generator.image_generator import generate_image,generate_comparison,unload_cached_model

def serve(request_dir: Path,log) -> None:
    request_dir.mkdir(parents=True,exist_ok=True);log(f"Persistent worker ready: {request_dir}")
    while True:
        requests=sorted(request_dir.glob("*.json"),key=lambda p:p.stat().st_mtime_ns)
        if not requests: time.sleep(.05);continue
        request=requests[0]
        try:
            config=json.loads(request.read_text(encoding="utf-8"));command=config.get("command","generate")
            request.unlink(missing_ok=True)
            if command=="shutdown":unload_cached_model();log("Persistent worker shutdown");return
            if command=="unload":unload_cached_model();log("Model unloaded");continue
            if command=="compare":generate_comparison(config,log)
            else:generate_image(config,log)
            log("Request complete")
        except Exception as error:
            print(f"[ERROR] Persistent request failed: {error}",file=sys.stderr,flush=True)
