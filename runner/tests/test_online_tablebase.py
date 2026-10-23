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


class draw_offer_tests(unittest.TestCase):
    def test_accepts_dead_draws_and_scores_no_better_than_a_draw(self):
        from maxima_runner.move_choice import accept_draw
        import chess.engine

        def score(cp):
            return chess.engine.PovScore(chess.engine.Cp(cp), chess.WHITE)

        bishop = chess.Board("8/8/8/4k3/8/8/8/4KB2 b - - 0 1")
        self.assertTrue(accept_draw(bishop, {}))
        self.assertTrue(accept_draw(rook_ending, {"tablebase": {"category": "blessed-loss"}}))
        self.assertFalse(accept_draw(rook_ending, {"tablebase": {"category": "win"}}))
        self.assertTrue(accept_draw(rook_ending, {"score": score(0), "depth": 30, "tbhits": 12}))
        self.assertFalse(accept_draw(chess.Board(), {"score": score(0), "depth": 30}))
        self.assertTrue(accept_draw(chess.Board(), {"score": score(10), "depth": 30}, contempt=10))
        self.assertFalse(accept_draw(chess.Board(), {"score": score(11), "depth": 30}, contempt=10))
        self.assertTrue(accept_draw(chess.Board(), {"score": score(-60), "depth": 30}, contempt=-50))


class describe_tests(unittest.TestCase):
    def test_engine_tablebase_moves_are_not_called_book(self):
        from maxima_runner.move_choice import describe
        import chess.engine
        won = chess.engine.PovScore(chess.engine.Cp(28000), chess.WHITE)
        self.assertEqual(describe({"score": won, "depth": 0, "tbhits": 1}, chess.WHITE), "TB win")
        self.assertEqual(describe({"score": won, "depth": 0, "tbhits": 1}, chess.BLACK), "TB loss")
        self.assertTrue(describe({"score": won, "depth": 20}, chess.WHITE).startswith("d20 +280.00"))


class local_tables_tests(unittest.TestCase):
    def test_online_lookups_start_beyond_complete_local_sets(self):
        import tempfile
        from pathlib import Path
        from maxima_runner import move_choice
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(online_tablebase.local_pieces(directory), 0)
            for name in (f"K{piece}vK" for piece in "QRBNP"):  # the complete 3-man WDL set
                (Path(directory) / f"{name}.rtbw").write_bytes(b"")
            self.assertEqual(online_tablebase.local_pieces(directory), 3)
            (Path(directory) / "KQvKR.rtbw").write_bytes(b"")  # an incomplete 4-man set does not count
            self.assertEqual(online_tablebase.local_pieces(directory), 3)
        lookups = move_choice.tablebase_lookups(True, lambda line: None, local_pieces=3)
        with patch.object(online_tablebase, "probe") as probe:
            self.assertIsNone(lookups.ask(chess.Board("8/8/8/4k3/8/8/8/4K2R w - - 0 1")))
            probe.assert_not_called()
