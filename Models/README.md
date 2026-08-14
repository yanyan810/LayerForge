# LayerForge AI models

Model weights and exported ONNX files are intentionally excluded from Git. LayerForge never downloads models or starts Python at runtime. Prepare the files explicitly, verify their checksums, and place them at the paths below.

## Character segmentation (U2NETP)

- Model: U2NETP (`u2netp.onnx`, 4,574,861 bytes)
- Runtime path: `Models/segmentation/u2netp.onnx`
- Download: https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx
- MD5: `8e83ca70e441ab06c318d82300c84806`
- Architecture license: Apache License 2.0; rembg distribution/tooling: MIT
- Input: float32 NCHW `[1,3,320,320]`, ImageNet mean/std normalization
- Output: float32 `[1,1,320,320]`; LayerForge min-max normalizes and resizes it to the source image

## Hair detection (Grounding DINO Base)

- Model: `IDEA-Research/grounding-dino-base`
- Revision: `12bdfa3120f3e7ec7b434d90674b3396eccf88eb`
- License: Apache License 2.0
- Source: https://huggingface.co/IDEA-Research/grounding-dino-base/tree/12bdfa3120f3e7ec7b434d90674b3396eccf88eb
- Original weight: `model.safetensors`, 933,400,872 bytes
- Original SHA-256: `5548F844C928C4B6F411FA8CBCC2BFA8DBBBA437CB1D513975519F93C2A9ED21`
- Runtime path: `Models/GroundingDINO/grounding-dino-base-hair-800x1333.onnx`
- Exported size: 966,802,655 bytes
- Exported SHA-256: `18B429B7F1C96CB34DA4833DAD000999FE8820E3BC6A1AAE9E45B397EE7C968D`

The ONNX graph uses fixed float32 image input `[1,3,800,1333]` plus bool valid-pixel mask `[1,800,1333]`. The tokenized prompt `hair.` (`[CLS] hair . [SEP]`) and text encoder are embedded in the graph. Outputs are 900 normalized CXCYWH boxes and 256-token logits. LayerForge applies box threshold 0.30; text threshold 0.25 is represented by the fixed positive `hair` prompt contract and is retained as the phrase-selection baseline.

## Hair segmentation (SAM2.1 Hiera Small)

- Model: SAM2.1 Hiera Small
- Official source revision used for export: `2b90b9f5ceec907a1c18123530e92e794ad901a4`
- License: Apache License 2.0
- Official source: https://github.com/facebookresearch/sam2/tree/2b90b9f5ceec907a1c18123530e92e794ad901a4
- Checkpoint: https://dl.fbaipublicfiles.com/segment_anything_2/092824/sam2.1_hiera_small.pt
- Checkpoint size: 184,416,285 bytes
- Checkpoint SHA-256: `6D1AA6F30DE5C92224F8172114DE081D104BBD23DD9DC5C58996F0CAD5DC4D38`
- Encoder path: `Models/SAM2/sam2.1-hiera-small-encoder.onnx`
- Encoder size / SHA-256: 137,931,433 bytes / `F54079EDC70C9EE151742D3637DA8AAEDB8BBEDE88B5E5122925B877CC026A73`
- Legacy Box-only decoder: `Models/SAM2/sam2.1-hiera-small-decoder.onnx`
- Legacy decoder size / SHA-256: 20,560,147 bytes / `95A5D6819A5AA8D66AF738D1CCE6B4C266473EFDD74D04675F9E77C32DA8F863`
- Runtime prompt decoder: `Models/SAM2/sam2.1-hiera-small-prompt-decoder.onnx`
- Prompt decoder size / SHA-256: 20,702,190 bytes / `F6F4A8783EA0FCB94252DCD766EB04DA6C1B74628659DAE9FDDB90A4AB63F7B4`

The encoder accepts normalized float32 `[1,3,1024,1024]` and returns `image_embed [1,256,64,64]`, `high_res_0 [1,32,256,256]`, and `high_res_1 [1,64,128,128]`. The runtime decoder accepts those cached features plus dynamic `prompt_points_1024 [1,N,2]` and `prompt_labels [1,N]`. Labels 2/3 are the XYXY box corners, 1 is a positive point, and 0 is a negative point. It returns continuous `mask_logits [1,1,256,256]` and `predicted_iou [1,1]`. LayerForge sigmoid-converts and resizes the selected logits; it does not hard-AND the result with the Character mask.

## Phase 4H prompt refinement

`Scripts/export_phase4h_decoder.py` exports the single-mask runtime decoder and an offline-only multimask decoder. `Scripts/evaluate_phase4h_prompts.py` compares Box-only, manual positive/negative strategies, distance-transform auto prompts, and all three multimask candidates without adding evaluation images or output masks to Git.

At runtime, LayerForge runs Box-only decoding first, chooses one conservative positive point at the deepest interior of the Box-only raw mask, and runs the prompt decoder again with the cached encoder features. The refined raw mask is accepted only when predicted IoU improves by at least 0.08, its area remains within 0.75x-1.35x of baseline, its IoU with baseline is at least 0.70, and connected components do not increase materially. Otherwise the exact Box-only result is retained. This keeps the second decoder call off the image encoder path and provides a quality-gated fallback for normal images.

## Offline export

`Scripts/export_phase4e_models.py` exports and validates all three Phase 4E ONNX graphs with ONNX Runtime. It requires an offline Python model-preparation environment with PyTorch, Transformers 4.57, ONNX, ONNX Runtime, Pillow, TorchVision, Hydra, IOPATH, the fixed Grounding DINO snapshot, the official SAM2 source checkout, and the official checkpoint.

Example:

```powershell
python Scripts/export_phase4e_models.py `
  --dino-model path/to/grounding-dino-base `
  --sam2-source path/to/sam2 `
  --sam2-checkpoint path/to/sam2.1_hiera_small.pt `
  --output Models
```

Python is used only to prepare the ONNX artifacts. `LayerForge.exe` loads them directly with the native ONNX Runtime C++ API.

## Phase 4F inference runtime

The Windows application uses the MIT-licensed `Microsoft.ML.OnnxRuntime.DirectML` NuGet package 1.24.4 and its `Microsoft.AI.DirectML` 1.15.4 dependency. The package includes the CPU provider as well as DirectML, so a separate CPU runtime package must not be referenced at the same time.

The supported runtime modes are:

- `Auto`: try DirectML for Grounding DINO and the SAM2 encoder, then fall back to CPU if session initialization fails
- `CPU`: use CPU for every model
- `DirectML`: request DirectML for Grounding DINO and the SAM2 encoder, with the same safe CPU fallback

U2NETP remains on CPU because its measured GPU gain was small relative to the additional GPU session cost. The SAM2 decoder remains on CPU because its graph fails DirectML graph-fusion initialization on the tested runtime. This CPU/DirectML hybrid keeps Phase 4E model inputs, outputs, thresholds, and mask post-processing unchanged.

## Phase 4G asynchronous execution

`AIModelManager` owns the four ONNX Runtime sessions and keeps their lazy-load and warm-session behavior. Application analysis jobs run one at a time on a C++20 `std::jthread`; the worker produces CPU masks, images, boxes, timings, and errors only. The main thread consumes the completed result and creates all DX12 textures together.

Opening another image requests a cooperative stop and increments an image generation ID. ONNX Runtime inference is allowed to finish safely, but a result whose generation or job ID is stale is discarded. The SAM image-feature cache is reused only for the same image generation and is invalidated after an image change. Application shutdown requests stop and joins the worker before model sessions or graphics resources are destroyed.

## Phase 4E support range

Recommended:

- One image with one main anime-style character
- Medium or large character with a visible head and hair
- PNG or JPEG input

Limited:

- Multiple or tightly adjacent characters
- Very small or distant characters
- Heavy occlusion or effects that resemble hair
- Portrait-oriented detector crops, because Phase 4E uses a fixed landscape ONNX canvas
