"""draw.bin builder: safety ranking, opponent replies, and the token lookup."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import chess
import chess.polyglot

from maxima_runner import learning, master_books


def stats(uci, white, draws, black):
    return {"uci": uci, "white": white, "draws": draws, "black": black}


tree = {
    chess.Board().epd(): [stats("e2e4", 450, 300, 250), stats("d2d4", 400, 450, 150), stats("g2g4", 10, 0, 90)],
}
after_e4 = chess.Board()
after_e4.push_uci("e2e4")
tree[after_e4.epd()] = [stats("c7c5", 300, 300, 400), stats("e7e5", 250, 550, 200), stats("a7a6", 1, 1, 1)]


class master_books_tests(unittest.TestCase):
    def test_safety_rewards_draws_and_punishes_losses(self):
        self.assertAlmostEqual(master_books.safety(400, 450, 150, chess.WHITE), 0.15)
        self.assertAlmostEqual(master_books.safety(400, 450, 150, chess.BLACK), -0.35)
        self.assertIsNone(master_books.safety(0, 0, 0, chess.WHITE))

    def test_picks_safest_moves_and_follows_common_replies(self):
        asked = []

        def fetch(board):
            asked.append(board.epd())
            return tree.get(board.epd(), [])

        weights = master_books.build(fetch, plies=4)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "draw.bin"
            learning.write_book(weights, path)
            with chess.polyglot.open_reader(str(path)) as reader:
                self.assertEqual([(entry.move.uci(), entry.weight) for entry in reader.find_all(chess.Board())],
                                 [("d2d4", 100)])
                self.assertEqual([entry.move.uci() for entry in reader.find_all(after_e4)], ["e7e5"])
        self.assertIn(after_e4.epd(), asked)  # Black's tree starts from White's common first moves

    def test_fire_picks_decisive_lines_that_still_score_and_extends_the_base(self):
        self.assertAlmostEqual(master_books.sharpness(450, 300, 250, chess.WHITE), 0.7)
        self.assertIsNone(master_books.sharpness(10, 0, 90, chess.WHITE))  # sharp but losing
        weights = master_books.build(lambda board: tree.get(board.epd(), []), plies=4, rank=master_books.sharpness)
        start_key = chess.polyglot.zobrist_hash(chess.Board())
        self.assertEqual({code for key, code in weights if key == start_key},
                         {learning.polyglot_code(chess.Board(), chess.Move.from_uci("e2e4"))})
        reply_key = chess.polyglot.zobrist_hash(after_e4)
        self.assertEqual({code for key, code in weights if key == reply_key},
                         {learning.polyglot_code(after_e4, chess.Move.from_uci("c7c5"))})
        # The base keeps its own choice where it has one; the extension fills the rest.
        base = {(start_key, learning.polyglot_code(chess.Board(), chess.Move.from_uci("f2f4"))): 50}
        merged = master_books.extend(base, weights)
        self.assertEqual({code for key, code in merged if key == start_key}, {next(iter(base))[1]})
        self.assertIn(reply_key, {key for key, _ in merged})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fire.bin"
            learning.write_book(merged, path)
            self.assertEqual(master_books.read_entries(path), merged)

    def test_token_from_environment_or_env_file(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, ".env").write_text("OTHER=1\nLICHESS_TOKEN='lip_file'\n")
            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(master_books.lichess_token(directory), "lip_file")
            with mock.patch.dict(os.environ, {"LICHESS_TOKEN": "lip_env"}):
                self.assertEqual(master_books.lichess_token(directory), "lip_env")
            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertIsNone(master_books.lichess_token(Path(directory) / "missing"))


if __name__ == "__main__":
    unittest.main()
