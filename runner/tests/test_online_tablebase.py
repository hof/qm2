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


eight_pieces = chess.Board("4k3/1p6/8/3p4/2P1P3/8/6P1/4K2R w - - 0 1")  # captures c4d5 and e4d5


def capture_answers(categories):
    """probe answering per capture: the category for the side to move after it."""
    def probe(after, timeout):
        category = categories[after.peek().uci()]
        return {"move": None, "category": category, "dtz": 9, "dtm": None}
    return patch.object(online_tablebase, "probe", side_effect=probe)


class capture_tests(unittest.TestCase):
    def test_eight_pieces_take_a_capture_into_a_won_ending_or_else_keep_a_draw(self):
        with capture_answers({"c4d5": "draw", "e4d5": "loss"}):
            found = online_tablebase.capture_probe(eight_pieces)
        self.assertEqual((found["move"].uci(), found["category"]), ("e4d5", "win"))
        with capture_answers({"c4d5": "draw", "e4d5": "win"}):
            found = online_tablebase.capture_probe(eight_pieces)
        self.assertEqual((found["move"].uci(), found["category"]), ("c4d5", "draw"))

    def test_a_drawing_capture_replaces_only_a_search_below_the_draw(self):
        import asyncio
        from types import SimpleNamespace
        from maxima_runner import move_choice
        engine_move = chess.Move.from_uci("h1h8")

        class session:
            def __init__(self, cp):
                self.cp = cp

            async def play(self, board, limit, **search):
                await asyncio.sleep(0.05)
                return SimpleNamespace(move=engine_move, info={"score": chess.engine.PovScore(
                    chess.engine.Cp(self.cp), chess.WHITE)})

        def choose(cp):
            lookups = move_choice.tablebase_lookups(True, lambda line: None)
            with capture_answers({"c4d5": "draw", "e4d5": "win"}):
                return asyncio.run(move_choice.choose_move(session(cp), eight_pieces, chess.engine.Limit(),
                                                           tablebase=lookups, options={"DrawContempt": 20}))[0]
        self.assertEqual(choose(-50).uci(), "c4d5")
        self.assertEqual(choose(50), engine_move)


if __name__ == "__main__":
    unittest.main()


class draw_offer_tests(unittest.TestCase):
    def test_offers_after_a_streak_at_the_draw_score_and_repeats_at_the_server_limit(self):
        from maxima_runner.move_choice import draw_offers
        import chess.engine

        def info(cp):
            return {"score": chess.engine.PovScore(chess.engine.Cp(cp), chess.WHITE), "depth": 30}

        offers = draw_offers()
        self.assertEqual([number for number in range(1, 27) if offers.due(info(12), 12)], [4, 15, 26])
        offers.reset()
        self.assertFalse(any([offers.due(info(12), 12) for _ in range(3)] + [offers.due({"source": "book"}, 12)]))
        self.assertFalse(any([offers.due(info(12), 12) for _ in range(3)] + [offers.due(info(13), 12)]))

    def test_a_due_offer_waits_while_we_are_winning_on_time(self):
        from maxima_runner.move_choice import draw_offers, winning_on_time
        import chess.engine

        settled = {"score": chess.engine.PovScore(chess.engine.Cp(12), chess.WHITE), "depth": 30}
        self.assertTrue(winning_on_time(60, 20, 0, False))
        self.assertTrue(winning_on_time(60, 20, 0, True))
        self.assertFalse(winning_on_time(60, 50, 0, False))
        self.assertTrue(winning_on_time(60, 5, 2, False))
        self.assertFalse(winning_on_time(60, 5, 2, True))
        self.assertFalse(winning_on_time(60, 5, 3, False))
        offers = draw_offers()
        self.assertFalse(any(offers.due(settled, 12, True) for _ in range(6)))
        self.assertTrue(offers.due(settled, 12, False))

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
        self.assertFalse(accept_draw(chess.Board(), {"score": score(10), "depth": 30}, contempt=10, ahead=True))
        self.assertFalse(accept_draw(rook_ending, {"tablebase": {"category": "draw"}}, ahead=True))
        self.assertTrue(accept_draw(bishop, {}, ahead=True))


class describe_tests(unittest.TestCase):
    def test_only_actual_hits_prefix_shared_search_description(self):
        from maxima_runner.move_choice import describe
        from maxima_runner.fics import search_whisper
        import chess.engine
        info = {"score": chess.engine.PovScore(chess.engine.Cp(20), chess.WHITE), "depth": 12, "nps": 1000}
        ordinary = describe(info, chess.WHITE)
        self.assertEqual(describe(dict(info, _ponder_hit=False), chess.WHITE), ordinary)
        hit = dict(info, _ponder_hit=True)
        self.assertEqual(describe(hit, chess.WHITE), "pondered " + ordinary)
        self.assertTrue(search_whisper(hit, chess.WHITE, "1. e4").startswith("pondered d12"))


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
