"""Known mirrored columns, preserved dense bytes, and safe initialization bounds."""
import unittest

import torch

from maxima_runner import nnue_format
from maxima_runner.nnue_model import halfkp_model
from maxima_runner.nnue_warm_start import initialize_from_halfkp


class warm_start_tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        columns = [[0] for _ in range(41024)]
        # King e1 / mirrored king d1, own pawn, a2 / mirrored h2.
        columns[4 * 641 + 1 + 8] = [2]
        columns[3 * 641 + 1 + 15] = [5]
        columns[4 * 641 + 1 + 9] = [-2]
        columns[3 * 641 + 1 + 14] = [-5]
        columns[4 * 641 + 1 + 10] = [300]
        columns[3 * 641 + 1 + 13] = [300]
        weights = {"ft_weight": columns, "ft_bias": [205],
                   "h1_weight": [[23, -7]], "h1_bias": [300],
                   "h2_weight": [[11]], "h2_bias": [-90],
                   "out_weight": [-17], "out_bias": 121}
        cls.teacher = nnue_format.serialize(weights)
        cls.native = nnue_format.serialize(dict(weights, ft_weight=[[0] for _ in range(22528)]),
                                           "halfkav2_hm")

    def model(self):
        return halfkp_model(256, 32, "halfkav2_hm").double()

    def test_projection_rounding_king_columns_dense_copy_and_constraints(self):
        model = self.model()
        metadata = initialize_from_halfkp(torch, model, self.teacher)
        weights = model.integer_weights()
        self.assertEqual(weights["ft_weight"][8][0], 4)
        self.assertEqual(weights["ft_weight"][9][0], -4)
        self.assertEqual(weights["ft_weight"][10][0], 300)
        self.assertEqual(weights["ft_bias"][0], 205)
        for bucket in range(32):
            self.assertEqual(model.ft_weight[bucket * 704 + 640:(bucket + 1) * 704].count_nonzero(), 0)
        serialized = nnue_format.serialize(weights, "halfkav2_hm")
        dense_start = nnue_format.transformer_start + 4 + 512 + 512 * 22528
        self.assertEqual(serialized[dense_start:], self.teacher[nnue_format.network_start:])
        model.constrain()
        self.assertEqual(model.ft_weight[10, 0].item(), 300 / 127)
        self.assertEqual(model.ft_bias[0].item(), 205 / 127)
        self.assertEqual(metadata["ft_bound"], 4)
        with torch.no_grad():
            model.ft_weight[10, 0] = 10
        model.constrain()
        self.assertEqual(model.ft_weight[10, 0].item(), 4)

    def test_rejects_wrong_source_and_unsafe_bounds_without_changing_model(self):
        model = self.model()
        bias = model.ft_bias.detach().clone()
        with self.assertRaisesRegex(ValueError, "legacy HalfKP"):
            initialize_from_halfkp(torch, model, self.native)
        teacher = bytearray(self.teacher)
        import struct
        struct.pack_into("<h", teacher, nnue_format.transformer_start + 4, 509)
        with self.assertRaisesRegex(ValueError, "training bounds"):
            initialize_from_halfkp(torch, model, teacher)
        self.assertTrue(torch.equal(bias, model.ft_bias))
        self.assertEqual(model.ft_bound, 1)

    def test_rejects_compact_geometry_and_preserves_random_start_bounds(self):
        model = halfkp_model(8, 8, "halfkav2_hm").double()
        with self.assertRaisesRegex(ValueError, "width256"):
            initialize_from_halfkp(torch, model, self.teacher)
        with torch.no_grad():
            model.ft_bias.fill_(3)
        model.constrain()
        self.assertEqual(model.ft_bias.max().item(), 1)


if __name__ == "__main__":
    unittest.main()
