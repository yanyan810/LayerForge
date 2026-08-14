"""Export SAM2.1 Small prompt decoders for Phase 4H offline evaluation.

The point dimension is dynamic: the first two prompts are the XYXY box corners,
followed by zero or more positive/negative points. This avoids adding extra
"not-a-point" embeddings when no refinement points are supplied.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np
import onnx
import onnxruntime as ort
import torch

OPSET = 17
class SamPromptMaskDecoder(torch.nn.Module):
    def __init__(self, model: torch.nn.Module, multimask: bool):
        super().__init__()
        self.prompt_encoder = model.sam_prompt_encoder
        self.mask_decoder = model.sam_mask_decoder
        self.multimask = multimask
        self.register_buffer("image_pe", model.sam_prompt_encoder.get_dense_pe())

    def forward(
        self,
        image_embed: torch.Tensor,
        high_res_0: torch.Tensor,
        high_res_1: torch.Tensor,
        prompt_points_1024: torch.Tensor,
        prompt_labels: torch.Tensor,
    ):
        sparse, dense = self.prompt_encoder(
            points=(prompt_points_1024, prompt_labels), boxes=None, masks=None
        )
        low_res_masks, predicted_iou, _, _ = self.mask_decoder(
            image_embeddings=image_embed,
            image_pe=self.image_pe,
            sparse_prompt_embeddings=sparse,
            dense_prompt_embeddings=dense,
            multimask_output=self.multimask,
            repeat_image=False,
            high_res_features=[high_res_0, high_res_1],
        )
        return torch.clamp(low_res_masks, -32.0, 32.0), predicted_iou


def export_decoder(module: torch.nn.Module, path: pathlib.Path) -> None:
    image_embed = torch.zeros(1, 256, 64, 64, dtype=torch.float32)
    high_res_0 = torch.zeros(1, 32, 256, 256, dtype=torch.float32)
    high_res_1 = torch.zeros(1, 64, 128, 128, dtype=torch.float32)
    points = torch.zeros(1, 3, 2, dtype=torch.float32)
    labels = torch.ones((1, 3), dtype=torch.int32)
    points[0, 0] = torch.tensor([200.0, 200.0])
    points[0, 1] = torch.tensor([800.0, 800.0])
    labels[0, :2] = torch.tensor([2, 3], dtype=torch.int32)
    path.parent.mkdir(parents=True, exist_ok=True)
    with torch.inference_mode():
        torch.onnx.export(
            module.eval(),
            (image_embed, high_res_0, high_res_1, points, labels),
            str(path),
            input_names=["image_embed", "high_res_0", "high_res_1", "prompt_points_1024", "prompt_labels"],
            output_names=["mask_logits", "predicted_iou"],
            dynamic_axes={
                "prompt_points_1024": {1: "num_prompts"},
                "prompt_labels": {1: "num_prompts"},
            },
            opset_version=OPSET,
            do_constant_folding=True,
            dynamo=False,
        )
    onnx.checker.check_model(str(path))
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    print(f"validated {path} ({path.stat().st_size:,} bytes)")
    print("  inputs :", [(item.name, item.shape, item.type) for item in session.get_inputs()])
    print("  outputs:", [(item.name, item.shape, item.type) for item in session.get_outputs()])


def compare_box_only(legacy_path: pathlib.Path, prompt_path: pathlib.Path) -> None:
    rng = np.random.default_rng(4)
    features = {
        "image_embed": rng.standard_normal((1, 256, 64, 64), dtype=np.float32),
        "high_res_0": rng.standard_normal((1, 32, 256, 256), dtype=np.float32),
        "high_res_1": rng.standard_normal((1, 64, 128, 128), dtype=np.float32),
    }
    box = np.array([[160.0, 120.0, 820.0, 780.0]], dtype=np.float32)
    points = np.zeros((1, 2, 2), dtype=np.float32)
    labels = np.array([[2, 3]], dtype=np.int32)
    points[0, 0] = box[0, :2]
    points[0, 1] = box[0, 2:]
    legacy = ort.InferenceSession(str(legacy_path), providers=["CPUExecutionProvider"])
    prompt = ort.InferenceSession(str(prompt_path), providers=["CPUExecutionProvider"])
    legacy_outputs = legacy.run(None, {**features, "box_xyxy_1024": box})
    prompt_outputs = prompt.run(None, {**features, "prompt_points_1024": points, "prompt_labels": labels})
    print("box-only parity mask_max_abs=", float(np.max(np.abs(legacy_outputs[0] - prompt_outputs[0]))))
    print("box-only parity iou_max_abs =", float(np.max(np.abs(legacy_outputs[1] - prompt_outputs[1]))))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sam2-source", type=pathlib.Path, required=True)
    parser.add_argument("--checkpoint", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--legacy-decoder", type=pathlib.Path)
    arguments = parser.parse_args()
    sys.path.insert(0, str(arguments.sam2_source.resolve()))
    from sam2.build_sam import build_sam2

    model = build_sam2(
        "configs/sam2.1/sam2.1_hiera_s.yaml",
        str(arguments.checkpoint.resolve()),
        device="cpu",
        mode="eval",
        apply_postprocessing=False,
    )
    single = arguments.output / "sam2.1-hiera-small-prompt-decoder.onnx"
    multi = arguments.output / "sam2.1-hiera-small-prompt-multimask-decoder.onnx"
    export_decoder(SamPromptMaskDecoder(model, False), single)
    export_decoder(SamPromptMaskDecoder(model, True), multi)
    if arguments.legacy_decoder:
        compare_box_only(arguments.legacy_decoder, single)


if __name__ == "__main__":
    main()
