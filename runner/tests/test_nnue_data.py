"""Dataset boundaries: real PGN replay, side-to-move results and leakage guards."""
import json
from pathlib import Path
import tempfile
import unittest

import chess

from maxima_runner import nnue_data, shortfen


class nnue_data_tests(unittest.TestCase):
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
