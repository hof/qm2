"""Earlier searches of a position: played directly when strong enough, otherwise searched longer."""
import asyncio
import unittest

import chess
import chess.engine
import chess.polyglot

from maxima_runner import fics, learning, move_choice, uci


def book_with(board, *, nodes=30_000_000, nps=1_000_000, clock=None, value=0.9, ply_search=False):
    opening = learning.opening_book([])
    key = f"{chess.polyglot.zobrist_hash(board):016x}"
    opening.search_ply = 0 if ply_search else None
    opening.analyses[key] = ["e2e4", 21, 40, 0, 3000, board.halfmove_clock if clock is None else clock, nodes, nps]
    opening.layers[0][(key, "e2e4")] = [value, 1.0]
    opening.results[0][(key, "e2e4")] = [1, 0, 0]
    return opening


class fake_session:
    def __init__(self):
        self.options = None

    async def play(self, board, limit, **search):
        self.options = search.get("options")
        return chess.engine.PlayResult(chess.Move.from_uci("d2d4"), None, {"nps": 900_000})


class remembered_tests(unittest.TestCase):
    def setUp(self):
        self.board = chess.Board()

    def test_strong_search_is_played_and_shown(self):
        opening = book_with(self.board)
        move, reason = opening.choose(self.board, 10.0)
        self.assertEqual(move.uci(), "e2e4")
        self.assertEqual(reason, "recalled d21")
        self.assertFalse(opening.long_think)
        line = fics.book_whisper("1. e4", opening.why, opening, compact=True)
        self.assertEqual(line, "1. e4 | Learn d21 (30.0M nodes)")

    def test_live_speed_replaces_the_saved_one(self):
        opening = book_with(self.board)
        opening.live_nps = 100_000_000
        self.assertIsNone(opening.choose(self.board, 10.0))
        self.assertTrue(opening.long_think)

    def test_weak_search_thinks_longer(self):
        opening = book_with(self.board, nodes=1000)
        self.assertIsNone(opening.choose(self.board, 10.0))
        self.assertTrue(opening.long_think)

    def test_bad_line_is_searched_longer_not_repeated(self):
        opening = book_with(self.board, value=0.0)
        opening.layers[0][next(iter(opening.layers[0]))] = [0.0, 40.0]
        self.assertIsNone(opening.choose(self.board, 10.0))
        self.assertTrue(opening.long_think)

    def test_other_halfmove_clock_is_another_position(self):
        opening = book_with(self.board, clock=7)
        self.assertIsNone(opening.choose(self.board, 10.0))
        self.assertFalse(opening.long_think)

    def test_deliberate_search_stays_ordinary(self):
        opening = book_with(self.board, ply_search=True)
        self.assertIsNone(opening.choose(self.board, 10.0))
        self.assertFalse(opening.long_think)

    def test_time_factor_is_sent_and_reset(self):
        session, limit = fake_session(), chess.engine.Limit(white_clock=300, black_clock=300)
        opening = book_with(self.board, nodes=1000)
        asyncio.run(move_choice.choose_move(session, self.board, limit, opening=opening))
        self.assertEqual(session.options["TimeFactor"], move_choice.long_think_percent)
        opening.analyses.clear()
        asyncio.run(move_choice.choose_move(session, self.board, limit, opening=opening))
        self.assertIsNone(session.options["TimeFactor"])
        self.assertEqual(opening.live_nps, 900_000)

    def test_ponder_hit_keeps_the_options(self):
        class protocol:
            options = {}
            ponder_hit = False

            async def play(self, board, limit, **search):
                raise RuntimeError(search["options"])

        session = uci.engine_session(protocol(), 1)
        session.protocol.options = {"TimeFactor": None}
        session.pondering, session.ponder_fen = True, self.board.fen()
        session.variant_settings = lambda board: {}
        session.play_settings = lambda board, options: dict(options)
        with self.assertRaises(RuntimeError) as caught:
            asyncio.run(session.play(self.board, chess.engine.Limit(time=1), options={"TimeFactor": 175}))
        self.assertEqual(caught.exception.args[0], {})


if __name__ == "__main__":
    unittest.main()
