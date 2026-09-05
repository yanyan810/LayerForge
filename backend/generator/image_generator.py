"""Stable Diffusion 1.x LoRA image generation."""
from __future__ import annotations
import json
import random
from datetime import datetime
from pathlib import Path

_cached_pipe = None
_cached_key = None

def generate_image(config: dict, log) -> tuple[Path, int]:
    global _cached_pipe, _cached_key
    import torch
    from diffusers import StableDiffusionPipeline
    from PIL.PngImagePlugin import PngInfo

    adapters = config.get("adapters") or [{"type": "style", "path": config["lora_path"], "strength": config.get("lora_strength", 0.8)}]
    primary = adapters[0]; lora = Path(primary["path"]).resolve()
    if not lora.is_file() or lora.suffix.lower() != ".safetensors":
        raise ValueError(f"LoRA file does not exist: {lora}")
    prompt = str(config.get("prompt", "")).strip()
    trigger = str(config.get("trigger_word", "")).strip()
    if not prompt: raise ValueError("Prompt is required.")
    if trigger and trigger.lower() not in prompt.lower(): prompt = f"{trigger}, {prompt}"
    width, height = int(config.get("width", 512)), int(config.get("height", 512))
    steps = int(config.get("steps", 25)); guidance = float(config.get("guidance_scale", 7.5))
    strength = float(primary.get("strength", config.get("lora_strength", 0.8)))
    if width < 64 or height < 64 or width % 8 or height % 8: raise ValueError("Width and height must be multiples of 8 and at least 64.")
    if not 1 <= steps <= 100: raise ValueError("Steps must be between 1 and 100.")
    if not 0.0 <= strength <= 2.0: raise ValueError("LoRA strength must be between 0 and 2.")
    base_seed = int(config.get("seed", -1)); count = max(1, min(8, int(config.get("image_count", 1))))
    log(f"Images: {count}")
    enable_safety_checker = bool(config.get("enable_safety_checker", False))
    pipeline_options = {"dtype": torch.float16}
    if not enable_safety_checker:
        pipeline_options.update({"safety_checker": None, "requires_safety_checker": False})
    cache_key=(str(config["base_model"]),enable_safety_checker)
    if _cached_pipe is None or _cached_key != cache_key:
        log("Loading base model...")
        if _cached_pipe is not None: del _cached_pipe; torch.cuda.empty_cache()
        _cached_pipe=StableDiffusionPipeline.from_pretrained(str(config["base_model"]),**pipeline_options);_cached_pipe.to("cuda");_cached_pipe.enable_attention_slicing();_cached_pipe.set_progress_bar_config(disable=True);_cached_key=cache_key
    else: log("Reusing loaded base model.")
    pipe=_cached_pipe
    try: pipe.unload_lora_weights()
    except Exception: pass
    log("Loading LoRA...")
    names=[];weights=[]
    for index, adapter in enumerate(adapters):
        path=Path(adapter["path"]).resolve(); name=f"layerforge_{index}"; pipe.load_lora_weights(path.parent,weight_name=path.name,adapter_name=name);names.append(name);weights.append(float(adapter.get("strength",0.8)))
    pipe.set_adapters(names,adapter_weights=weights)
    print("[Progress] 0", flush=True); log("Generating...")
    last = -1
    def progress_callback(_pipe, step, _timestep, callback_kwargs):
        nonlocal last
        percent = min(99, (current_image * steps + step + 1) * 100 // (steps * count))
        if percent != last: print(f"[Progress] {percent}", flush=True); last = percent
        return callback_kwargs
    output_root = Path(config["output_dir"]).resolve() / str(config.get("style_name") or lora.stem)
    output_root.mkdir(parents=True, exist_ok=True)
    output=None
    for current_image in range(count):
        seed=random.SystemRandom().randrange(0,2**63-1) if base_seed<0 else base_seed+current_image;log(f"Seed: {seed}")
        image=pipe(prompt=prompt,negative_prompt=str(config.get("negative_prompt","")),width=width,height=height,num_inference_steps=steps,guidance_scale=guidance,generator=torch.Generator(device="cuda").manual_seed(seed),callback_on_step_end=progress_callback,callback_on_step_end_tensor_inputs=[]).images[0]
        stem=f"{datetime.now():%Y%m%d_%H%M%S_%f}_{seed}";temporary=output_root/f".{stem}.incomplete.png";output=output_root/f"{stem}.png"
        metadata_values={"style_name":str(config.get("style_name") or lora.stem),"prompt":prompt,"negative_prompt":str(config.get("negative_prompt","")),"seed":seed,"generated_time":datetime.now().isoformat(),"base_model":str(config["base_model"]),"lora":str(lora),"adapters":adapters,"lora_strength":strength,"width":width,"height":height,"steps":steps,"guidance_scale":guidance,"enable_safety_checker":enable_safety_checker}
        metadata=PngInfo()
        for key,value in metadata_values.items():metadata.add_text(key,json.dumps(value,ensure_ascii=False) if isinstance(value,(list,dict)) else str(value))
        image.save(temporary,pnginfo=metadata);temporary.replace(output);output.with_suffix(".json").write_text(json.dumps(metadata_values,ensure_ascii=False,indent=2),encoding="utf-8");print(f"[Result] {output}",flush=True)
    print("[Progress] 100", flush=True); log(f"Output: {output}")
    return output, seed

def unload_cached_model() -> None:
    global _cached_pipe, _cached_key
    if _cached_pipe is not None:
        del _cached_pipe; _cached_pipe=None; _cached_key=None
        import torch; torch.cuda.empty_cache()

def generate_comparison(config: dict, log) -> None:
    comparisons=config.get("comparisons") or []
    if not comparisons: raise ValueError("Select at least one Style for comparison.")
    for index,style in enumerate(comparisons):
        request=dict(config);request["style_name"]=style["name"];request["lora_path"]=style["path"];request["lora_strength"]=style.get("strength",.8);request["adapters"]=[{"type":"style","path":style["path"],"strength":style.get("strength",.8)}];request["image_count"]=1
        log(f"Comparison {index+1}/{len(comparisons)}: {style['name']}");generate_image(request,log)
