"""Online tablebase: which positions are asked, and how answers are read."""
import io
import json
import unittest
from unittest.mock import patch

import chess

from maxima_runner import online_tablebase

rook_ending = chess.Board("8/8/8/4k3/8/8/2R5/4K3 w - - 0 1")


def answer(category, uci):
    payload = {"category": category, "dtz": 27, "dtm": 27, "moves": [{"uci": uci, "category": "loss"}]}
    return patch.object(online_tablebase.urllib.request, "urlopen",
                        side_effect=lambda *a, **k: io.BytesIO(json.dumps(payload).encode()))


class online_tablebase_tests(unittest.TestCase):
    def test_asks_only_for_seven_pieces_without_castling(self):
        self.assertTrue(online_tablebase.covers(rook_ending))
        self.assertFalse(online_tablebase.covers(chess.Board()))
        self.assertFalse(online_tablebase.covers(chess.Board("r3k3/8/8/8/8/8/8/4K3 b q - 0 1")))
        with patch.object(online_tablebase.urllib.request, "urlopen") as request:
            self.assertIsNone(online_tablebase.probe(chess.Board()))
            request.assert_not_called()

    def test_reads_best_move_and_rejects_unknown_or_illegal(self):
        with answer("win", "c2d2"):
            self.assertEqual(online_tablebase.probe(rook_ending)["move"], chess.Move.from_uci("c2d2"))
        with answer("unknown", "c2d2"):
            self.assertIsNone(online_tablebase.probe(rook_ending))
        with answer("win", "c2c8c"):
            with self.assertRaises(ValueError):
                online_tablebase.probe(rook_ending)
        with answer("win", "e5e4"):
            self.assertIsNone(online_tablebase.probe(rook_ending))


if __name__ == "__main__":
    unittest.main()


class dead_draw_tests(unittest.TestCase):
    def test_accepts_only_dead_draws(self):
        from maxima_runner.move_choice import dead_draw
        import chess.engine
        bishop = chess.Board("8/8/8/4k3/8/8/8/4KB2 b - - 0 1")
        self.assertTrue(dead_draw(bishop, {}))
        self.assertTrue(dead_draw(rook_ending, {"tablebase": {"category": "blessed-loss"}}))
        self.assertFalse(dead_draw(rook_ending, {"tablebase": {"category": "win"}}))
        level = chess.engine.PovScore(chess.engine.Cp(0), chess.WHITE)
        self.assertTrue(dead_draw(rook_ending, {"score": level, "tbhits": 12}))
        self.assertFalse(dead_draw(rook_ending, {"score": level}))
        self.assertFalse(dead_draw(chess.Board(), {"score": level, "tbhits": 12}))


class describe_tests(unittest.TestCase):
    def test_engine_tablebase_moves_are_not_called_book(self):
        from maxima_runner.move_choice import describe
        import chess.engine
        won = chess.engine.PovScore(chess.engine.Cp(28000), chess.WHITE)
        self.assertEqual(describe({"score": won, "depth": 0, "tbhits": 1}, chess.WHITE), "TB win")
        self.assertEqual(describe({"score": won, "depth": 0, "tbhits": 1}, chess.BLACK), "TB loss")
        self.assertTrue(describe({"score": won, "depth": 20}, chess.WHITE).startswith("d20 +280.00"))
