import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "backend"))
from trainer.dataset_loader import load_dataset_items

class StyleDatasetLoaderTests(unittest.TestCase):
    def test_enabled_items_and_trigger_word(self):
        root = Path(__file__).parent / "fixtures" / "dataset_enabled"
        items = load_dataset_items(root, "lfstyle")
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0].prompt, "lfstyle, 1girl")

    def test_empty_caption_uses_trigger(self):
        root = Path(__file__).parent / "fixtures" / "dataset_empty_caption"
        self.assertEqual(load_dataset_items(root, "lfstyle")[0].prompt, "lfstyle")

if __name__ == "__main__":
    unittest.main()
