"""Stable Diffusion 1.x LoRA image generation."""
from __future__ import annotations
import json
import random
from datetime import datetime
from pathlib import Path

def generate_image(config: dict, log) -> tuple[Path, int]:
    import torch
    from diffusers import StableDiffusionPipeline
    from PIL.PngImagePlugin import PngInfo

    lora = Path(config["lora_path"]).resolve()
    if not lora.is_file() or lora.suffix.lower() != ".safetensors":
        raise ValueError(f"LoRA file does not exist: {lora}")
    prompt = str(config.get("prompt", "")).strip()
    trigger = str(config.get("trigger_word", "")).strip()
    if not prompt: raise ValueError("Prompt is required.")
    if trigger and trigger.lower() not in prompt.lower(): prompt = f"{trigger}, {prompt}"
    width, height = int(config.get("width", 512)), int(config.get("height", 512))
    steps = int(config.get("steps", 25)); guidance = float(config.get("guidance_scale", 7.5))
    strength = float(config.get("lora_strength", 0.8))
    if width < 64 or height < 64 or width % 8 or height % 8: raise ValueError("Width and height must be multiples of 8 and at least 64.")
    if not 1 <= steps <= 100: raise ValueError("Steps must be between 1 and 100.")
    if not 0.0 <= strength <= 2.0: raise ValueError("LoRA strength must be between 0 and 2.")
    seed = int(config.get("seed", -1)); seed = random.SystemRandom().randrange(0, 2**63 - 1) if seed < 0 else seed
    log(f"Seed: {seed}"); log("Loading base model...")
    pipe = StableDiffusionPipeline.from_pretrained(str(config["base_model"]), dtype=torch.float16)
    pipe.to("cuda"); pipe.enable_attention_slicing(); pipe.set_progress_bar_config(disable=True)
    log("Loading LoRA...")
    pipe.load_lora_weights(lora.parent, weight_name=lora.name, adapter_name="layerforge")
    pipe.set_adapters("layerforge", adapter_weights=strength)
    print("[Progress] 0", flush=True); log("Generating...")
    last = -1
    def progress_callback(_pipe, step, _timestep, callback_kwargs):
        nonlocal last
        percent = min(99, (step + 1) * 100 // steps)
        if percent != last: print(f"[Progress] {percent}", flush=True); last = percent
        return callback_kwargs
    image = pipe(prompt=prompt, negative_prompt=str(config.get("negative_prompt", "")), width=width, height=height,
        num_inference_steps=steps, guidance_scale=guidance, generator=torch.Generator(device="cuda").manual_seed(seed),
        callback_on_step_end=progress_callback, callback_on_step_end_tensor_inputs=[]).images[0]
    output_root = Path(config["output_dir"]).resolve() / lora.stem
    output_root.mkdir(parents=True, exist_ok=True)
    stem = f"{datetime.now():%Y%m%d_%H%M%S_%f}_{seed}"
    temporary = output_root / f".{stem}.incomplete.png"; output = output_root / f"{stem}.png"
    metadata_values = {"prompt": prompt, "negative_prompt": str(config.get("negative_prompt", "")), "seed": seed,
        "base_model": str(config["base_model"]), "lora": str(lora), "lora_strength": strength, "steps": steps, "guidance_scale": guidance}
    metadata = PngInfo()
    for key, value in metadata_values.items(): metadata.add_text(key, str(value))
    image.save(temporary, pnginfo=metadata); temporary.replace(output)
    output.with_suffix(".json").write_text(json.dumps(metadata_values, ensure_ascii=False, indent=2), encoding="utf-8")
    print("[Progress] 100", flush=True); log(f"Output: {output}"); print(f"[Result] {output}", flush=True)
    del pipe; torch.cuda.empty_cache()
    return output, seed
