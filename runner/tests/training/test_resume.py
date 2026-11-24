"""Interrupted training and export must produce the uninterrupted artifacts."""
import contextlib
import io
import json
from pathlib import Path
import random
import tempfile
import unittest
from unittest.mock import patch

import chess
import torch

from maxima_runner import nnue_data, nnue_training


class resume_tests(unittest.TestCase):
    def test_default_export_is_native(self):
        output = self.root / "native-default"
        argv = ["nnue:train", str(self.dataset), "--output", str(output),
                "--epochs", "1", "--width", "2", "--hidden", "2", "--batch-size", "3"]
        with patch("sys.argv", argv), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(nnue_training.main(), 0)
        self.assertEqual((output / "network.nnue").read_bytes()[:4], bytes.fromhex("314e584d"))

    def setUp(self):
        torch.set_num_threads(1)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        board, rng, rows, counts = chess.Board(), random.Random(17), [], {"train": 0, "validation": 0}
        while counts != {"train": 7, "validation": 3}:
            split = nnue_data.position_split(board)
            if counts[split] < {"train": 7, "validation": 3}[split]:
                rows.append({"fen": board.fen(), "score_cp": len(rows) * 31 - 100,
                             "split": split, "result_stm": [0, 0.5, 1][len(rows) % 3],
                             "source": {"sha256": "a" * 64, "game": 1, "ply": board.ply()}})
                counts[split] += 1
            board.push(rng.choice(list(board.legal_moves)))
        self.dataset = self.root / "data.mxdata"
        nnue_data.save_dataset(self.dataset, {"variant": "standard", "score_pov": "side_to_move"}, rows)

    def run_training(self, output, *extra):
        argv = ["nnue:train", str(self.dataset), "--output", str(output), "--epochs", "2",
                "--architecture", "halfkav2_hm", "--width", "2", "--hidden", "2",
                "--batch-size", "3", "--metrics-batch-size", "4", *extra]
        with patch("sys.argv", argv), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(nnue_training.main(), 0)

    def checkpoint(self, directory):
        return torch.load(directory / "training.pt", weights_only=True)

    def assert_nested_equal(self, left, right):
        if isinstance(left, torch.Tensor):
            self.assertTrue(torch.equal(left, right))
        elif isinstance(left, dict):
            self.assertEqual(left.keys(), right.keys())
            for key in left:
                self.assert_nested_equal(left[key], right[key])
        elif isinstance(left, (tuple, list)):
            self.assertEqual(len(left), len(right))
            for a, b in zip(left, right):
                self.assert_nested_equal(a, b)
        else:
            self.assertEqual(left, right)

    def test_resume_matches_uninterrupted_after_mid_epoch_and_mid_export(self):
        full, resumed = self.root / "full", self.root / "resumed"
        self.run_training(full)
        self.run_training(resumed, "--max-batches", "1")
        state = self.checkpoint(resumed)
        self.assertEqual((state["epoch"], state["next_batch"], state["phase"]), (0, 3, "training"))
        self.assertFalse((resumed / "network.nnue").exists())
        self.run_training(resumed, "--resume", "--max-batches", "5")
        self.assertEqual(self.checkpoint(resumed)["next_batch"], 7)
        # Training completed; pause after checking exactly four exported positions.
        self.run_training(resumed, "--resume", "--max-batches", "1")
        self.assertEqual(self.checkpoint(resumed)["phase"], "export")
        self.assertEqual(self.checkpoint(resumed)["export_checked"], 4)
        self.assertFalse((resumed / "model.pt").exists())
        self.run_training(resumed, "--resume")
        self.assert_nested_equal(self.checkpoint(full), self.checkpoint(resumed))
        self.assertEqual((full / "network.nnue").read_bytes(), (resumed / "network.nnue").read_bytes())
        self.assertEqual(json.loads((full / "report.json").read_text()),
                         json.loads((resumed / "report.json").read_text()))
        self.run_training(resumed, "--resume")  # Completed run is idempotent.

    def test_restores_transformer_bound_before_further_optimization(self):
        from maxima_runner.nnue_model import halfkp_model

        def wide_bound_model(*args):
            model = halfkp_model(*args)
            model.ft_bound = 4
            with torch.no_grad():
                model.ft_bias.fill_(3)
            return model

        full, resumed = self.root / "wide-full", self.root / "wide-resumed"
        with patch.object(nnue_training, "training_libraries", return_value=(torch, wide_bound_model)):
            self.run_training(full)
            self.run_training(resumed, "--max-batches", "1")
        # Resume constructs the ordinary bound1 model before restoring the checkpoint.
        self.run_training(resumed, "--resume")
        self.assertEqual(self.checkpoint(resumed)["ft_bound"], 4)
        self.assertGreater(self.checkpoint(resumed)["model"]["ft_bias"].min().item(), 1)
        self.assert_nested_equal(self.checkpoint(full), self.checkpoint(resumed))

    def test_resume_rejects_changed_settings_and_input_without_touching_checkpoint(self):
        output = self.root / "paused"
        self.run_training(output, "--max-batches", "1")
        before = (output / "training.pt").read_bytes()
        for extra in [("--seed", "7"), ("--learning-rate", "0.01"), ("--epochs", "3")]:
            with self.assertRaisesRegex(ValueError, "mismatch"):
                self.run_training(output, "--resume", *extra)
        # A valid, different dataset must fail the content hash check too.
        header, rows = nnue_data.read_dataset(self.dataset)
        rows[0]["score_cp"] += 1
        changed = self.root / "changed.mxdata"
        nnue_data.save_dataset(changed, header, rows)
        self.dataset = changed
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.run_training(output, "--resume")
        self.assertEqual(before, (output / "training.pt").read_bytes())
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.run_training(output)

    def test_atomic_checkpoint_retains_previous_state_on_write_failure(self):
        target = self.root / "checkpoint"
        target.write_bytes(b"previous")

        def fail(stream):
            stream.write(b"partial")
            raise OSError("simulated disk failure")

        with self.assertRaisesRegex(OSError, "disk failure"):
            nnue_training.atomic_write(target, fail)
        self.assertEqual(target.read_bytes(), b"previous")
        self.assertEqual(list(self.root.glob(".checkpoint.*")), [])


if __name__ == "__main__":
    unittest.main()
