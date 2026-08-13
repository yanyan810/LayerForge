# LayerForge segmentation model

Phase 2 uses **U2NETP**, the lightweight U²-Net salient-object segmentation model.

- Model: `u2netp.onnx` (about 4.7 MB)
- Place at: `Models/segmentation/u2netp.onnx`
- Download: https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx
- Expected MD5: `8e83ca70e441ab06c318d82300c84806`
- Architecture/source license: Apache License 2.0 (U²-Net)
- ONNX distributor/tooling license: MIT (rembg)
- U²-Net source: https://github.com/xuebinqin/U-2-Net
- rembg model specification: https://github.com/danielgatis/rembg/blob/main/rembg/sessions/u2netp.py

The model file is intentionally excluded from Git. LayerForge never downloads a model at runtime; obtain it explicitly and verify its checksum before use.

## Tensor contract

- Input: one float32 RGB image, NCHW `[1, 3, 320, 320]`
- Normalization: pixel values to `[0, 1]`, then `(value - mean) / std`
- Mean: `[0.485, 0.456, 0.406]`
- Standard deviation: `[0.229, 0.224, 0.225]`
- Output: LayerForge uses output 0, expected as float32 `[1, 1, 320, 320]`
- Postprocess: min-max normalize output 0, clamp to `[0, 1]`, bilinear-resize to the original image size
