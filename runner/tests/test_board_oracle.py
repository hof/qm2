"""Deterministic independent move/state comparisons through a test-only bridge."""
import os
from pathlib import Path
import random
import subprocess
import unittest

import chess

from maxima_runner.variants import royal_losers_board
from test_variants import rule_fixtures


class board_oracle_tests(unittest.TestCase):
    def test_moves_children_and_history(self):
        engine = os.environ.get("MAXIMA_ENGINE")
        if not engine:
            self.skipTest("MAXIMA_ENGINE is unset")
        probe = Path(engine).resolve().parent.parent / "tests" / "board_oracle_probe"
        self.assertTrue(probe.is_file(), f"missing test bridge: {probe}; rebuild with BUILD_TESTING=ON")
        records = []
        repetition_fen = "4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1"
        fixtures = [chess.STARTING_FEN] + [fen for _, fen, _ in rule_fixtures] + [
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "4k3/3p4/8/8/8/8/8/R3K2R w KQ - 99 1",
            "4k3/P6p/8/8/8/8/8/4K3 w - - 0 1",
            "7k/6Q1/5K2/8/8/8/p7/N7 b - - 100 1",
            "7k/5Q2/5K2/8/8/8/p7/N7 b - - 100 1",
            repetition_fen,
        ]
        fixtures += [
            "4r2k/8/8/8/1b6/8/7P/R3K2R w KQ - 0 1",
            "7k/8/8/3pP3/4K3/8/8/8 w - d6 0 1",
            "4r2k/8/8/3pP3/4K3/8/8/8 w - d6 0 1",
            "7k/8/8/r2pP2K/8/8/8/8 w - d6 0 1",
            "1r5k/P7/8/8/8/8/8/1K6 w - - 0 1",
            "7k/8/8/8/r6K/8/3P4/8 w - - 0 1",
        ]
        for variant, board_type in (("standard", chess.Board), ("losers", royal_losers_board)):
            for fen in fixtures:
                board = board_type(fen)
                records.extend((variant, position) for position in (board, board.mirror()))
            for seed in (17, 2026, 0x5EED):
                rng = random.Random(seed)
                for game in range(8):
                    board = board_type()
                    for ply in range(100):
                        records.append((variant, board.copy()))
                        if board.is_game_over(claim_draw=True):
                            break
                        moves = sorted(board.legal_moves, key=lambda move: move.uci())
                        board.push(rng.choice(moves))
            board = board_type(repetition_fen)
            for move in ["g1f3", "g8f6", "f3g1", "f6g8"] * 2:
                board.push_uci(move)
                records.append((variant, board.copy()))
        # King-only terminal roots are covered separately by search lifecycle tests.
        # The bridge exercises the move picker, which itself does not adjudicate roots.
        records = [(variant, board) for variant, board in records
                   if not (variant == "losers" and board.is_variant_end())]
        self.assertTrue(all(board.is_valid() for _, board in records))
        payload = "".join(f"{variant}\t{board.root().fen(en_passant='fen')}\t"
                          + " ".join(move.uci() for move in board.move_stack) + "\n"
                          for variant, board in records)
        result = subprocess.run([str(probe)], input=payload, text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), len(records))
        children = 0
        for (variant, board), line in zip(records, lines):
            with self.subTest(variant=variant, fen=board.fen(), history=[m.uci() for m in board.move_stack]):
                fen, key, history, child_text = line.split("|")
                self.assertEqual(fen, board.fen(en_passant="fen"))
                actual = [entry.split(":", 1) for entry in child_text.split(";") if entry]
                expected_moves = {move.uci() for move in board.legal_moves}
                self.assertEqual(len(actual), len(expected_moves), "missing or duplicate moves")
                self.assertEqual({move for move, _ in actual}, expected_moves)
                for move, child_fen in actual:
                    child = board.copy()
                    child.push_uci(move)
                    self.assertEqual(child_fen, child.fen(en_passant="fen"))
                    children += 1
                # Compare repetition equivalence for EVERY retained ancestor, not
                # the implementation's Zobrist numbers against another hash scheme.
                ancestor = board.copy()
                expected_equal = []
                while True:
                    expected_equal.append(ancestor._transposition_key() == board._transposition_key())
                    if not ancestor.move_stack:
                        break
                    ancestor.pop()
                actual_equal = [value == key for value in history.rstrip(",").split(",")]
                self.assertEqual(actual_equal, list(reversed(expected_equal)))
        print(f"oracle: {len(records)} positions, {children} legal children; seeds 17, 2026, 24301")


if __name__ == "__main__":
    unittest.main()
