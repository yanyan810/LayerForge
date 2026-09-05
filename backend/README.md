# LayerForge Python Backend

Phase 3ではStable Diffusion 1.x互換DiffusersモデルのUNetへPEFT LoRA Adapterを追加し、LayerForge Datasetで学習します。LayerForge起動時に依存関係やモデルを自動Downloadすることはありません。

## Phase 3 Setup

Python 3.12をインストール後、リポジトリ直下で`setup_backend.cmd`を明示的に実行してください。`runtime/python`へ専用venvを作成し、CUDA 12.8版PyTorchと`requirements.txt`をインストールします。

LayerForgeのPython欄には`runtime/python/Scripts/python.exe`を指定します。Base ModelにはローカルDiffusersモデルフォルダ、またはHugging Face model IDを指定できます。初回取得後はHugging Face標準Cacheが再利用されます。

完成した重みは`models/lora/<Output Name>/<Output Name>.safetensors`へ保存されます。進捗契約は`[Progress] 0`～`[Progress] 100`です。

## Phase 4.5 Auto Caption

`caption`モードは`SmilingWolf/wd-eva02-large-tagger-v3`の`model.onnx`と`selected_tags.csv`をHugging Face Cacheへ初回実行時だけ取得します。現在はPyTorch CUDA環境とのDLL競合を避けるためONNX Runtime CPU Providerを使用します。

```powershell
runtime/python/Scripts/python.exe -u backend/style_backend.py caption runtime/caption_config.json
```

CaptionにはDanbooru-style tagsだけを保存し、Trigger WordはTraining時に別途追加されます。

## Phase 5 Generation

- `style.json` version 2は既定Prompt、Negative Prompt、Strength、Steps、Guidanceを保存します。version 1も読み込み可能です。
- Generation Configは従来の`lora_path`に加えて、将来のCharacter LoRA向けの`adapters`配列に対応します。
- `image_count`は1回のモデルロードで1～8枚生成します。固定Seedは`baseSeed + index`、`-1`は画像ごとにランダムです。
- `style_backend.py serve <request-directory>`で常駐workerを起動します。JSON要求の`generate`、`compare`、`unload`、`shutdown`を処理します。
- Base ModelとSafety Checker設定が同じ場合はPipelineを再利用し、LoRA Adapterだけを交換します。
- 全生成結果にPNG metadataとHistoryタブ用JSON sidecarを保存します。
