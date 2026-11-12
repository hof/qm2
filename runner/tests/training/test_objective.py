"""Optional training-environment checks against hand-calculated targets/gradients."""
import math
import unittest

import torch

from maxima_runner.nnue_training import loss, result_tensor


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


if __name__ == "__main__":
    unittest.main()
