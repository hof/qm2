"""Hand-calculated feature indices and integer arithmetic for training exports."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import chess

from maxima_runner import nnue_format


class nnue_format_tests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("MAXIMA_ENGINE"), "requires built Maxima and nnue_probe")
    def test_native_halfkav2_matches_hand_calculated_king_inclusive_scores(self):
        probe = Path(os.environ["MAXIMA_ENGINE"]).resolve().parent.parent / "tests/nnue_probe"
        self.assertTrue(probe.is_file(), "build nnue_probe for native export validation")
        columns = [[index // 704 + index % 8 - (index % 64) // 8] for index in range(22528)]
        weights = {"ft_bias": [64], "ft_weight": columns, "h1_bias": [0],
                   "h1_weight": [[64, 0]], "h2_bias": [0], "h2_weight": [[64]],
                   "out_bias": 0, "out_weight": [16]}
        # Three active feature weights plus bias64; dense layers preserve lane0.
        cases = [("7k/8/8/8/8/8/P7/K7 w - - 0 1", 79),
                 ("7k/8/8/8/8/8/P7/K7 b - - 0 1", 67),
                 ("7k/8/8/8/8/8/P7/1K6 w - - 0 1", 75),
                 ("7k/8/8/8/8/8/P7/1K6 b - - 0 1", 68)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "halfkav2.nnue"
            path.write_bytes(nnue_format.serialize(weights, "halfkav2_hm"))
            output = subprocess.run([str(probe), str(path)], input="\n".join(fen for fen, _ in cases) + "\n",
                                    text=True, capture_output=True, timeout=15, check=True)
            scores = {line.split("|")[0]: int(line.split("|")[1]) for line in output.stdout.splitlines()}
            for fen, expected in cases:
                self.assertEqual(scores[fen], expected)

    def test_halfkav2_hm_indices_include_kings_and_share_horizontal_mirrors(self):
        board = chess.Board("7k/8/8/8/8/8/P7/K7 w - - 0 1")
        self.assertEqual(nnue_format.features(board, True, "halfkav2_hm"), [2759, 2127, 2808])
        self.assertEqual(nnue_format.features(board, False, "halfkav2_hm"), [2808, 2224, 2759])
        reflected = board.transform(chess.flip_horizontal)
        for color in chess.COLORS:
            self.assertEqual(sorted(nnue_format.features(board, color, "halfkav2_hm")),
                             sorted(nnue_format.features(reflected, color, "halfkav2_hm")))
        self.assertEqual(nnue_format.feature_count_for("halfkav2_hm"), 22528)

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
