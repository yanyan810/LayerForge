"""Export the Phase 4E inference models used by the native C++ application.

This is an offline model-preparation tool. LayerForge.exe never starts Python.
The exported contracts are deliberately fixed-shape so ONNX Runtime can fully
optimize the large vision graphs.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import onnx
import onnxruntime as ort
import torch


DINO_HEIGHT = 800
DINO_WIDTH = 1333
SAM_SIZE = 1024
OPSET = 18


class FixedHairGroundingDino(torch.nn.Module):
    """Grounding DINO with the tokenized `hair.` prompt baked into the graph."""

    def __init__(self, model: torch.nn.Module, prompt_inputs: dict[str, torch.Tensor]):
        super().__init__()
        self.model = model
        self.register_buffer("input_ids", prompt_inputs["input_ids"])
        self.register_buffer("token_type_ids", prompt_inputs["token_type_ids"])
        self.register_buffer("attention_mask", prompt_inputs["attention_mask"])

    def forward(self, pixel_values: torch.Tensor, pixel_mask: torch.Tensor):
        output = self.model(
            pixel_values=pixel_values,
            pixel_mask=pixel_mask,
            input_ids=self.input_ids,
            token_type_ids=self.token_type_ids,
            attention_mask=self.attention_mask,
            return_dict=True,
        )
        return output.logits, output.pred_boxes


class SamImageEncoder(torch.nn.Module):
    """Official SAM2 image predictor's cacheable image-embedding half."""

    feature_sizes = ((256, 256), (128, 128), (64, 64))

    def __init__(self, model: torch.nn.Module):
        super().__init__()
        self.model = model

    def forward(self, image: torch.Tensor):
        backbone = self.model.forward_image(image)
        _, vision_features, _, _ = self.model._prepare_backbone_features(backbone)
        if self.model.directly_add_no_mem_embed:
            vision_features[-1] = vision_features[-1] + self.model.no_mem_embed
        features = [
            feature.permute(1, 2, 0).view(1, -1, *size)
            for feature, size in zip(vision_features[::-1], self.feature_sizes[::-1])
        ][::-1]
        return features[-1], features[0], features[1]


class SamBoxMaskDecoder(torch.nn.Module):
    """Official SAM2 prompt encoder + mask decoder for one XYXY box."""

    def __init__(self, model: torch.nn.Module):
        super().__init__()
        self.prompt_encoder = model.sam_prompt_encoder
        self.mask_decoder = model.sam_mask_decoder
        self.register_buffer("box_labels", torch.tensor([[2, 3]], dtype=torch.int32))
        self.register_buffer("image_pe", model.sam_prompt_encoder.get_dense_pe())

    def forward(
        self,
        image_embed: torch.Tensor,
        high_res_0: torch.Tensor,
        high_res_1: torch.Tensor,
        box_xyxy_1024: torch.Tensor,
    ):
        box_points = box_xyxy_1024.reshape(1, 2, 2)
        sparse, dense = self.prompt_encoder(
            points=(box_points, self.box_labels), boxes=None, masks=None
        )
        low_res_masks, predicted_iou, _, _ = self.mask_decoder(
            image_embeddings=image_embed,
            image_pe=self.image_pe,
            sparse_prompt_embeddings=sparse,
            dense_prompt_embeddings=dense,
            multimask_output=False,
            repeat_image=False,
            high_res_features=[high_res_0, high_res_1],
        )
        return torch.clamp(low_res_masks, -32.0, 32.0), predicted_iou


def export_legacy(
    module: torch.nn.Module,
    inputs: tuple[torch.Tensor, ...],
    path: pathlib.Path,
    input_names: list[str],
    output_names: list[str],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    module.eval()
    with torch.inference_mode():
        torch.onnx.export(
            module,
            inputs,
            str(path),
            input_names=input_names,
            output_names=output_names,
            opset_version=OPSET,
            do_constant_folding=True,
            dynamo=False,
        )
    onnx.checker.check_model(str(path))
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    print(f"validated {path} ({path.stat().st_size:,} bytes)")
    print("  inputs :", [(item.name, item.shape, item.type) for item in session.get_inputs()])
    print("  outputs:", [(item.name, item.shape, item.type) for item in session.get_outputs()])


def export_dino(model_directory: pathlib.Path, output: pathlib.Path) -> None:
    from transformers import AutoModelForZeroShotObjectDetection, AutoProcessor

    processor = AutoProcessor.from_pretrained(model_directory, local_files_only=True)
    prompt = processor.tokenizer("hair.", return_tensors="pt")
    model = AutoModelForZeroShotObjectDetection.from_pretrained(
        model_directory, local_files_only=True
    ).eval()
    wrapper = FixedHairGroundingDino(model, prompt).eval()
    pixels = torch.zeros(1, 3, DINO_HEIGHT, DINO_WIDTH, dtype=torch.float32)
    valid = torch.ones(1, DINO_HEIGHT, DINO_WIDTH, dtype=torch.bool)
    export_legacy(
        wrapper,
        (pixels, valid),
        output,
        ["pixel_values", "pixel_mask"],
        ["logits", "boxes"],
    )


def export_sam(
    source_directory: pathlib.Path,
    checkpoint: pathlib.Path,
    encoder_output: pathlib.Path,
    decoder_output: pathlib.Path,
) -> None:
    sys.path.insert(0, str(source_directory.resolve()))
    from sam2.build_sam import build_sam2

    model = build_sam2(
        "configs/sam2.1/sam2.1_hiera_s.yaml",
        str(checkpoint.resolve()),
        device="cpu",
        mode="eval",
        apply_postprocessing=False,
    )
    encoder = SamImageEncoder(model).eval()
    image = torch.zeros(1, 3, SAM_SIZE, SAM_SIZE, dtype=torch.float32)
    export_legacy(
        encoder,
        (image,),
        encoder_output,
        ["image"],
        ["image_embed", "high_res_0", "high_res_1"],
    )
    with torch.inference_mode():
        image_embed, high_res_0, high_res_1 = encoder(image)
    decoder = SamBoxMaskDecoder(model).eval()
    box = torch.tensor([[256.0, 128.0, 768.0, 768.0]], dtype=torch.float32)
    export_legacy(
        decoder,
        (image_embed, high_res_0, high_res_1, box),
        decoder_output,
        ["image_embed", "high_res_0", "high_res_1", "box_xyxy_1024"],
        ["mask_logits", "predicted_iou"],
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dino-model", type=pathlib.Path)
    parser.add_argument("--sam2-source", type=pathlib.Path)
    parser.add_argument("--sam2-checkpoint", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("Models"))
    parser.add_argument("--only", choices=("all", "dino", "sam"), default="all")
    arguments = parser.parse_args()

    if arguments.only in ("all", "dino"):
        if arguments.dino_model is None:
            parser.error("--dino-model is required for Grounding DINO export")
        export_dino(
            arguments.dino_model,
            arguments.output / "GroundingDINO" / "grounding-dino-base-hair-800x1333.onnx",
        )
    if arguments.only in ("all", "sam"):
        if arguments.sam2_source is None or arguments.sam2_checkpoint is None:
            parser.error("--sam2-source and --sam2-checkpoint are required for SAM2 export")
        export_sam(
            arguments.sam2_source,
            arguments.sam2_checkpoint,
            arguments.output / "SAM2" / "sam2.1-hiera-small-encoder.onnx",
            arguments.output / "SAM2" / "sam2.1-hiera-small-decoder.onnx",
        )


if __name__ == "__main__":
    main()
