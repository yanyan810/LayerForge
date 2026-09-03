# LayerForge Python Backend

Phase 2では、C++側のプロセス起動・UTF-8 JSON・stdout/stderrストリーミング・停止処理を確認するためのテストBackendです。AIライブラリは使用しません。

```powershell
python backend/style_backend.py runtime/training_config.json
```

Phase 3では、`style_backend.py`の進捗出力契約（`[Progress] 0`〜`[Progress] 100`）を維持したままLoRA Trainerへ接続します。
