from __future__ import annotations
import csv, json, os
from pathlib import Path

class AutoTagger:
    def __init__(self, model_id: str, log):
        try:
            import numpy as np
            import onnxruntime as ort
            from huggingface_hub import snapshot_download
        except ImportError as error: raise RuntimeError(f"Missing Python package: {error.name}. Install backend/requirements.txt") from error
        self.np=np; log("Downloading Auto Tagger model if not cached...")
        root=Path(snapshot_download(model_id,allow_patterns=["model.onnx","selected_tags.csv"]))
        # The CUDA provider requires a CUDA/cuDNN major version matching the ORT wheel.
        # CPU is the reliable default alongside LayerForge's independent PyTorch CUDA runtime.
        providers=["CPUExecutionProvider"]
        self.session=ort.InferenceSession(str(root/"model.onnx"),providers=providers)
        active=self.session.get_providers()[0]; log(f"Auto Tagger device: {'CUDA' if active=='CUDAExecutionProvider' else 'CPU'}")
        self.input=self.session.get_inputs()[0]; shape=self.input.shape; self.size=int(shape[1] if isinstance(shape[1],int) else shape[2])
        with (root/"selected_tags.csv").open("r",encoding="utf-8") as stream: self.tags=[(row["name"],int(row["category"])) for row in csv.DictReader(stream)]
    def tag(self,path:Path,general:float,character:float,rating:bool,characters:bool,replace:bool)->str:
        from PIL import Image
        image=Image.open(path).convert("RGBA"); bg=Image.new("RGBA",image.size,"WHITE"); bg.alpha_composite(image); image=bg.convert("RGB")
        side=max(image.size); square=Image.new("RGB",(side,side),"WHITE"); square.paste(image,((side-image.width)//2,(side-image.height)//2)); square=square.resize((self.size,self.size),Image.Resampling.BICUBIC)
        array=self.np.asarray(square,dtype=self.np.float32)[:,:,::-1][None,:,:,:]; probabilities=self.session.run(None,{self.input.name:array})[0][0]; result=[]
        for (name,category),probability in zip(self.tags,probabilities):
            if (category==0 and probability>=general) or (category==4 and characters and probability>=character) or (category==9 and rating and probability>=general): result.append((float(probability),name.replace("_"," ") if replace else name))
        result.sort(reverse=True); return ", ".join(name for _,name in result)

def caption_dataset(config:dict,log)->None:
    root=Path(config["dataset"]).resolve(); manifest=json.loads((root/"dataset.json").read_text(encoding="utf-8")); requested=set(config.get("items",[])); selected=config.get("mode","all")=="selected"; targets=[]
    for entry in manifest["items"]:
        if entry.get("enabled",True) and (not selected or Path(entry["image"]).stem in requested): targets.append((root/entry["image"],root/entry["caption"]))
    if not targets: raise ValueError("No enabled images selected for captioning.")
    tagger=AutoTagger(str(config.get("model","SmilingWolf/wd-eva02-large-tagger-v3")),log); log(f"Images: {len(targets)}"); print("[Progress] 0",flush=True); captioned=skipped=0
    for index,(image,caption) in enumerate(targets,1):
        if not image.is_file(): raise ValueError(f"Image not found: {image}")
        if not config.get("overwrite_existing",False) and caption.is_file() and caption.read_text(encoding="utf-8").strip(): skipped+=1
        else:
            tags=tagger.tag(image,float(config.get("general_threshold",.35)),float(config.get("character_threshold",.85)),bool(config.get("include_rating_tags",False)),bool(config.get("include_character_tags",False)),bool(config.get("replace_underscores",True)))
            temporary=caption.with_suffix(caption.suffix+".tmp"); temporary.write_text(tags,encoding="utf-8"); os.replace(temporary,caption); captioned+=1
        log(f"Tagging {index} / {len(targets)}"); print(f"[Progress] {index*100//len(targets)}",flush=True)
    log(f"Captioned: {captioned}"); log(f"Skipped: {skipped}")
