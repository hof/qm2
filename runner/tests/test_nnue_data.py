"""Dataset boundaries: real PGN replay, side-to-move results and leakage guards."""
import json
from pathlib import Path
import tempfile
import unittest

import chess
import chess.engine

from maxima_runner import nnue_data, shortfen


class nnue_data_tests(unittest.TestCase):
    def test_labels_use_completed_exact_iteration_and_total_effort(self):
        exact = {"depth": 7, "nodes": 6000, "score": chess.engine.PovScore(chess.engine.Cp(-30), True)}
        interrupted = {"depth": 8, "nodes": 10000, "lowerbound": True,
                       "score": chess.engine.PovScore(chess.engine.Cp(80), True)}
        provenance = {"string": "search_result partial completed_depth 7 attempted_depth 8"}
        self.assertEqual(nnue_data.completed_label([exact, provenance, interrupted]),
                         {"score_cp": -30, "depth": 7, "nodes": 10000})
        # An unfinished iteration can even have an unbounded partial-root report.
        self.assertEqual(nnue_data.completed_label([exact, provenance, dict(interrupted, lowerbound=False)])["depth"], 7)
        mate = {"depth": 8, "score": chess.engine.PovScore(chess.engine.Mate(2), True)}
        self.assertIsNone(nnue_data.completed_label([exact, mate]))
        self.assertIsNone(nnue_data.completed_label([interrupted]))
    def test_even_stride_samples_both_sides_and_game_split_is_consistent(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "game.pgn"
            path.write_text('[Result "*"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 '
                            '4. d3 Bc5 5. c3 d6 6. O-O O-O 7. Re1 a6 8. Bb3 Ba7 '
                            '9. Nbd2 h6 10. Nf1 Be6 11. Bc2 Qd7 12. Ng3 Rfe8 *\n')
            rows = nnue_data.candidates([path], 10, stride=8, min_ply=4)
            self.assertEqual([row["source"]["ply"] for row in rows], [4, 13, 20])
            self.assertEqual([chess.Board(row["fen"]).turn for row in rows], [True, False, True])
            self.assertEqual(len({row["split"] for row in rows}), 1)
            labeled = [dict(row, score_cp=0) for row in rows]
            output = Path(directory) / "data.mxdata"
            header = {"variant": "standard", "score_pov": "side_to_move", "split_method": "game"}
            nnue_data.save_dataset(output, header, labeled)
            self.assertEqual(len(nnue_data.read_dataset(output)[1]), 3)
    def test_compact_dataset_restores_fen_without_changing_labels_or_split(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "compact.jsonl"
            board = chess.Board()
            row = {"position": shortfen.encode(board), "score_cp": 25,
                   "split": nnue_data.position_split(board)}
            header = {"format": nnue_data.data_format, "version": 2, "variant": "standard",
                      "score_pov": "side_to_move", "positions": 1, "position_encoding": "shortfen"}
            nnue_data.publish_jsonl(path, header, [row])
            _, rows = nnue_data.read_dataset(path)
            self.assertEqual(rows[0]["fen"], chess.STARTING_FEN)
            self.assertEqual(rows[0]["score_cp"], 25)
            self.assertEqual(rows[0]["split"], nnue_data.position_split(board))
    def test_replay_deduplicates_transpositions_and_preserves_result_perspective(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "games.pgn"
            path.write_text('[Result "1-0"]\n\n1. e4 e5 2. Nf3 Nc6 1-0\n\n'
                            '[Result "0-1"]\n\n1. e4 e5 2. Nf3 Nc6 0-1\n')
            rows = nnue_data.candidates([path], 10, stride=1, min_ply=1)
            self.assertEqual(len(rows), 4)
            self.assertEqual([row["result_stm"] for row in rows], [0, 1, 0, 1])
            board = chess.Board(rows[0]["fen"])
            self.assertEqual(board.turn, chess.BLACK)
            board.halfmove_clock = 30
            board.fullmove_number = 50
            self.assertEqual(nnue_data.position_split(board), rows[0]["split"])

    def test_atomic_publication_and_reject_duplicate_or_incomplete_data(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.jsonl"
            board = chess.Board()
            row = {"fen": board.fen(), "score_cp": -25, "split": nnue_data.position_split(board)}
            header = {"format": nnue_data.data_format, "version": 1, "variant": "standard",
                      "score_pov": "side_to_move", "positions": 1}
            nnue_data.publish_jsonl(path, header, [row])
            self.assertEqual(nnue_data.read_dataset(path)[1][0]["score_cp"], -25)
            with self.assertRaises(FileExistsError):
                nnue_data.publish_jsonl(path, header, [])
            path.write_text(json.dumps(dict(header, positions=2)) + "\n" + json.dumps(row) + "\n")
            with self.assertRaises(ValueError):
                nnue_data.read_dataset(path)
            with path.open("a") as stream:
                stream.write(json.dumps(row) + "\n")
            with self.assertRaises(ValueError):
                nnue_data.read_dataset(path)


if __name__ == "__main__":
    unittest.main()
