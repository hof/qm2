"""Hand-calculated feature indices and integer arithmetic for training exports."""
import unittest

import chess

from maxima_runner import nnue_format


class nnue_format_tests(unittest.TestCase):
    def test_both_perspectives_have_known_feature_indices(self):
        white = chess.Board("7k/8/8/8/8/8/P7/K7 w - - 0 1")
        self.assertEqual(nnue_format.features(white, chess.WHITE), [9])
        self.assertEqual(nnue_format.features(white, chess.BLACK), [120])
        black = chess.Board("7k/7p/8/8/8/8/8/K7 b - - 0 1")
        self.assertEqual(nnue_format.features(black, chess.BLACK), [9])
        self.assertEqual(nnue_format.features(black, chess.WHITE), [120])

    def test_serialized_one_lane_network_has_hand_calculated_scores(self):
        columns = [[0] for _ in range(nnue_format.feature_count)]
        columns[9] = [32]
        weights = {"ft_bias": [0], "ft_weight": columns, "h1_bias": [0],
                   "h1_weight": [[64, 0]], "h2_bias": [0], "h2_weight": [[64]],
                   "out_bias": -1, "out_weight": [-1]}
        serialized = nnue_format.serialize(weights)
        self.assertEqual(len(serialized), 21022697)
        integer = nnue_format.integer_network(serialized)
        white = chess.Board("7k/8/8/8/8/8/P7/K7 w - - 0 1")
        # Active friendly pawn -> activation32 -> (-32-1)/16 -> raw-2 -> cp0.
        self.assertEqual(integer.evaluate(white), (-2, 0))
        white.turn = chess.BLACK
        self.assertEqual(integer.evaluate(white), (0, 0))
        with self.assertRaises(ValueError):
            nnue_format.integer_network(serialized[:-1])


if __name__ == "__main__":
    unittest.main()
