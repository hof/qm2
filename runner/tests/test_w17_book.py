"""The losers opening book builder: every reply booked, resumable, proofs stop the search."""
import os
from pathlib import Path
import stat
import sys
import tempfile
import time
import unittest

import chess

from maxima_runner import learning, w17_book
from maxima_runner.variants import royal_losers_board

# A UCI engine that completes depth 1 with the first legal losers move, then waits for stop.
fake_engine = f"""#!{sys.executable}
import os, sys
sys.path.insert(0, {str(Path(__file__).resolve().parents[1])!r})
from maxima_runner.variants import royal_losers_board
score = os.environ.get("FAKE_SCORE", "cp 5")
board = None
for line in sys.stdin:
    words = line.split()
    if words[:1] == ["isready"]:
        print("readyok", flush=True)
    elif words[:2] == ["position", "fen"]:
        board = royal_losers_board(" ".join(words[2:]))
        move = sorted(board.legal_moves, key=lambda move: move.uci())[0].uci()
    elif words[:1] == ["go"]:
        print(f"info depth 1 seldepth 1 score {{score}} nodes 1 pv {{move}}", flush=True)
    elif words[:1] == ["stop"]:
        print(f"bestmove {{move}}", flush=True)
    elif words[:1] == ["quit"]:
        break
"""


class w17_book_tests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.engine = self.root / "engine"
        self.engine.write_text(fake_engine)
        self.engine.chmod(self.engine.stat().st_mode | stat.S_IXUSR)
        self.output = self.root / "w17_search.pgn"

    def tearDown(self):
        os.environ.pop("FAKE_SCORE", None)
        self.directory.cleanup()

    def test_our_first_move_is_booked_against_every_reply_and_a_rerun_resumes(self):
        self.assertEqual(w17_book.build(self.output, 1, 0, 2, self.engine, 16, log=lambda line: None), 21)
        self.assertEqual(w17_book.build(self.output, 1, 0, 2, self.engine, 16, log=lambda line: None), 0)
        book = self.root / "book_w17.bin"
        learning.build_repertoire([self.output], book)
        start = royal_losers_board()
        for reply in list(start.legal_moves):
            board = start.copy()
            board.push(reply)
            opening = learning.opening_book([], book_path=book, variant="losers", rated=False)
            move, _ = opening.choose_book(board)
            self.assertEqual(move, sorted(board.legal_moves, key=chess.Move.uci)[0])

    def test_white_first_moves_side_by_side_share_the_book(self):
        self.assertEqual(w17_book.build(self.output, 1, 0, 2, self.engine, 16, log=lambda line: None,
                                        first=["a3", "h3"]), 22)
        book = self.root / "book_w17.bin"
        learning.build_repertoire([self.output], book)
        shares = learning.opening_book.read_book(book, royal_losers_board())
        self.assertEqual(shares, {chess.Move.from_uci("a2a3"): 0.5, chess.Move.from_uci("h2h3"): 0.5})

    def test_a_proven_mate_stops_the_search_at_once_and_is_booked_as_a_proof(self):
        os.environ["FAKE_SCORE"] = "mate 3"
        started = time.monotonic()
        self.assertEqual(w17_book.build(self.output, 1, 60, 4, self.engine, 16, log=lambda line: None), 21)
        self.assertLess(time.monotonic() - started, 30)
        self.assertEqual(self.output.read_text().count('[Proof "mate 3"]'), 21)


if __name__ == "__main__":
    unittest.main()
