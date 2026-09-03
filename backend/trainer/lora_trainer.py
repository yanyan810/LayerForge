"""Stable Diffusion 1.x UNet LoRA trainer used by LayerForge."""
from __future__ import annotations
import json
import math
import os
import random
import shutil
from pathlib import Path

def train_lora(config: dict, items: list, log) -> Path:
    import torch
    import torch.nn.functional as functional
    from accelerate import Accelerator
    from diffusers import AutoencoderKL, DDPMScheduler, StableDiffusionPipeline, UNet2DConditionModel
    from diffusers.utils import convert_state_dict_to_diffusers
    from peft import LoraConfig
    from peft.utils import get_peft_model_state_dict
    from PIL import Image
    from torch.utils.data import DataLoader, Dataset
    from torchvision import transforms
    from transformers import CLIPTextModel, CLIPTokenizer

    seed = int(config.get("seed", 42))
    random.seed(seed); torch.manual_seed(seed); torch.cuda.manual_seed_all(seed)
    resolution = int(config.get("resolution", 512))
    batch_size = int(config.get("train_batch_size", 1))
    accumulation = int(config.get("gradient_accumulation_steps", 1))
    epochs = int(config.get("epochs", 10))
    rank = int(config.get("rank", 16))
    precision = str(config.get("mixed_precision", "fp16"))
    if resolution < 64 or batch_size < 1 or accumulation < 1 or epochs < 1 or rank < 1:
        raise ValueError("Training numeric settings must be positive.")

    accelerator = Accelerator(gradient_accumulation_steps=accumulation, mixed_precision=precision)
    model = str(config["base_model"])
    log("Loading base model...")
    tokenizer = CLIPTokenizer.from_pretrained(model, subfolder="tokenizer")
    text_encoder = CLIPTextModel.from_pretrained(model, subfolder="text_encoder")
    vae = AutoencoderKL.from_pretrained(model, subfolder="vae")
    unet = UNet2DConditionModel.from_pretrained(model, subfolder="unet")
    scheduler = DDPMScheduler.from_pretrained(model, subfolder="scheduler")
    vae.requires_grad_(False); text_encoder.requires_grad_(False); unet.requires_grad_(False)
    unet.add_adapter(LoraConfig(r=rank, lora_alpha=rank, init_lora_weights="gaussian",
        target_modules=["to_k", "to_q", "to_v", "to_out.0"]))
    if bool(config.get("gradient_checkpointing", True)):
        unet.enable_gradient_checkpointing()

    transform = transforms.Compose([
        transforms.Resize(resolution, interpolation=transforms.InterpolationMode.BILINEAR),
        transforms.CenterCrop(resolution), transforms.ToTensor(), transforms.Normalize([0.5], [0.5]),
    ])

    class StyleImages(Dataset):
        def __len__(self): return len(items)
        def __getitem__(self, index):
            item = items[index]
            with Image.open(item.image_path) as source:
                pixels = transform(source.convert("RGB"))
            tokens = tokenizer(item.prompt, max_length=tokenizer.model_max_length,
                padding="max_length", truncation=True, return_tensors="pt").input_ids[0]
            return {"pixel_values": pixels, "input_ids": tokens}

    loader = DataLoader(StyleImages(), batch_size=batch_size, shuffle=True, num_workers=0, pin_memory=True)
    parameters = [parameter for parameter in unet.parameters() if parameter.requires_grad]
    optimizer = torch.optim.AdamW(parameters, lr=float(config.get("learning_rate", 1e-4)))
    unet, optimizer, loader = accelerator.prepare(unet, optimizer, loader)
    weight_dtype = torch.float16 if precision == "fp16" else torch.float32
    vae.to(accelerator.device, dtype=weight_dtype); text_encoder.to(accelerator.device, dtype=weight_dtype)
    total_steps = max(1, math.ceil(len(loader) / accumulation) * epochs)
    completed_steps = 0; last_percent = 0
    log("Starting LoRA training")
    print("[Progress] 0", flush=True)
    for epoch in range(epochs):
        log(f"Epoch {epoch + 1} / {epochs}")
        unet.train()
        for batch in loader:
            with accelerator.accumulate(unet):
                pixels = batch["pixel_values"].to(accelerator.device, dtype=weight_dtype)
                latents = vae.encode(pixels).latent_dist.sample() * vae.config.scaling_factor
                noise = torch.randn_like(latents)
                timesteps = torch.randint(0, scheduler.config.num_train_timesteps, (latents.shape[0],), device=latents.device).long()
                noisy_latents = scheduler.add_noise(latents, noise, timesteps)
                hidden_states = text_encoder(batch["input_ids"].to(accelerator.device), return_dict=False)[0]
                prediction = unet(noisy_latents, timesteps, hidden_states, return_dict=False)[0]
                target = scheduler.get_velocity(latents, noise, timesteps) if scheduler.config.prediction_type == "v_prediction" else noise
                loss = functional.mse_loss(prediction.float(), target.float(), reduction="mean")
                accelerator.backward(loss)
                if accelerator.sync_gradients: accelerator.clip_grad_norm_(parameters, 1.0)
                optimizer.step(); optimizer.zero_grad(set_to_none=True)
            if accelerator.sync_gradients:
                completed_steps += 1
                percent = min(100, completed_steps * 100 // total_steps)
                if percent != last_percent: print(f"[Progress] {percent}", flush=True); last_percent = percent

    accelerator.wait_for_everyone()
    output_root = Path(config["output_dir"]).resolve()
    output_name = str(config["output_name"]).strip()
    if any(character in output_name for character in '<>:"/\\|?*') or output_name in {".", ".."}:
        raise ValueError("Output name contains invalid filename characters.")
    destination = output_root / output_name
    temporary = output_root / f".{output_name}.incomplete"
    if temporary.exists(): shutil.rmtree(temporary)
    temporary.mkdir(parents=True)
    log("Saving LoRA...")
    unwrapped = accelerator.unwrap_model(unet)
    state = convert_state_dict_to_diffusers(get_peft_model_state_dict(unwrapped))
    weight_name = f"{output_name}.safetensors"
    StableDiffusionPipeline.save_lora_weights(temporary, unet_lora_layers=state,
        weight_name=weight_name, safe_serialization=True)
    info = {key: config[key] for key in ("base_model", "dataset", "epochs", "resolution", "learning_rate", "rank", "trigger_word")}
    info["images"] = len(items)
    (temporary / "training_info.json").write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding="utf-8")
    destination.mkdir(parents=True, exist_ok=True)
    os.replace(temporary / weight_name, destination / weight_name)
    os.replace(temporary / "training_info.json", destination / "training_info.json")
    temporary.rmdir()
    print("[Progress] 100", flush=True)
    log(f"Output: {destination / weight_name}")
    return destination / weight_name
