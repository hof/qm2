"""Optional training-environment checks against hand-calculated targets/gradients."""
import math
import unittest

import torch

from maxima_runner.nnue_training import constant_baselines, loss, result_tensor


class objective_tests(unittest.TestCase):
    def test_known_draw_eval_and_win_result_blend(self):
        scores = torch.tensor([0.0], dtype=torch.float64, requires_grad=True)
        labels = torch.tensor([0.0], dtype=torch.float64)
        results = torch.tensor([1.0], dtype=torch.float64)
        objective = loss(torch, scores, labels, results, 0.5)
        # sigmoid(0)=0.5, blended target=0.75, squared error=1/16.
        self.assertAlmostEqual(objective.item(), 1 / 16, places=14)
        objective.backward()
        self.assertAlmostEqual(scores.grad.item(), -math.log(10) / 3200, places=14)
        self.assertEqual(loss(torch, scores, labels, results, 1).item(), 0)
        self.assertEqual(loss(torch, scores, labels, results, 0).item(), 0.25)

    def test_unknown_result_retains_full_search_target(self):
        labels = torch.tensor([0.0, 100.0], dtype=torch.float64)
        results = result_tensor(torch, [{"result_stm": None}, {}])
        self.assertEqual(loss(torch, labels, labels, results, 0.3).item(), 0)

    def test_constant_baseline_fits_training_targets_without_validation_leakage(self):
        train = [{"score_cp": 0, "result_stm": 1}]
        validation = [{"score_cp": 0, "result_stm": 0}]
        baselines = constant_baselines(torch, train, validation, 0.5)
        # Training target .75 -> cp=400*log10(3). Validation target is .25.
        fitted = baselines["train_mean"]
        self.assertAlmostEqual(fitted["score_cp"], 400 * math.log10(3), places=12)
        self.assertAlmostEqual(fitted["train"]["loss"], 0, places=14)
        self.assertAlmostEqual(fitted["validation"]["loss"], 0.25, places=14)
        self.assertAlmostEqual(baselines["zero_cp"]["validation"]["loss"], 1 / 16, places=14)

    def test_constant_baseline_handles_unanimous_results_and_missing_validation(self):
        baselines = constant_baselines(torch, [{"score_cp": 0, "result_stm": 1}], [], 0)
        self.assertTrue(math.isfinite(baselines["train_mean"]["score_cp"]))
        self.assertLess(baselines["train_mean"]["train"]["loss"], 1e-28)
        self.assertIsNone(baselines["train_mean"]["validation"])


if __name__ == "__main__":
    unittest.main()
