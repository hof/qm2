"""Hand-authored rule expectations, independent of the Maxima engine."""
import unittest

import chess

from maxima_runner.variants import royal_losers_board


# Each set is an explicit rule expectation, not generated from the engine.
rule_fixtures = [
    ("capture", "4k3/7p/8/8/8/8/p7/R3K3 w - - 0 1", {"a1a2"}),
    ("pinned capture", "k3r3/7p/8/8/8/3p4/4B3/4K3 w - - 0 1",
     {"e1d1", "e1f1", "e1d2", "e1f2"}),
    ("capture evasion", "4k3/7p/8/8/8/8/4r3/4K2R w - - 0 1", {"e1e2"}),
    ("quiet evasion", "k3r3/7p/8/8/8/8/p7/R3K3 w - - 0 1",
     {"e1d1", "e1f1", "e1d2", "e1f2"}),
    ("en passant", "4k3/7p/8/3pP3/8/8/8/4K3 w - d6 0 1", {"e5d6"}),
    ("illegal en passant", "k7/7p/8/r4pPK/8/8/8/8 w - f6 0 1",
     {"g5g6", "h5h4", "h5h6"}),
    ("capture promotions", "1r2k3/P6p/8/8/8/8/8/4K3 w - - 0 1",
     {"a7b8q", "a7b8r", "a7b8b", "a7b8n"}),
]


class variant_tests(unittest.TestCase):
    def test_explicit_move_sets_and_mirrors(self):
        for name, fen, expected in rule_fixtures:
            board = royal_losers_board(fen)
            for mirrored in (False, True):
                with self.subTest(name=name, mirrored=mirrored):
                    self.assertTrue(board.is_valid())
                    self.assertEqual({move.uci() for move in board.legal_moves}, expected)
                    orthodox = chess.Board(board.fen())
                    for move in orthodox.legal_moves:
                        self.assertEqual(board.is_legal(move), move.uci() in expected)
                board = board.mirror()
                expected = {chess.Move(chess.square_mirror(chess.Move.from_uci(move).from_square),
                                       chess.square_mirror(chess.Move.from_uci(move).to_square),
                                       promotion=chess.Move.from_uci(move).promotion).uci()
                            for move in expected}

    def test_masks_do_not_hide_compulsory_captures(self):
        board = royal_losers_board(rule_fixtures[0][1])
        self.assertEqual(list(board.generate_legal_moves(chess.BB_E1)), [])
        self.assertEqual(list(board.generate_legal_moves(to_mask=chess.BB_B1)), [])

    def test_castling_and_quiet_promotions(self):
        board = royal_losers_board("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1")
        # The rooks must capture instead of castling.
        self.assertEqual({m.uci() for m in board.legal_moves}, {"a1a8", "h1h8"})
        board = royal_losers_board("4k3/7p/8/8/8/8/8/R3K2R w KQ - 0 1")
        # h1h7 is compulsory; remove that target to test ordinary castles.
        board.remove_piece_at(chess.H7)
        board.set_piece_at(chess.D7, chess.Piece(chess.PAWN, chess.BLACK))
        self.assertTrue(board.is_legal(chess.Move.from_uci("e1g1")))
        self.assertTrue(board.is_legal(chess.Move.from_uci("e1c1")))
        board = royal_losers_board("4k3/P6p/8/8/8/8/8/4K3 w - - 0 1")
        self.assertEqual({m.uci() for m in board.legal_moves if m.promotion},
                         {"a7a8q", "a7a8r", "a7a8b", "a7a8n"})

    def test_terminal_precedence(self):
        fixtures = [
            ("7k/7p/8/8/8/8/8/K7 w - - 100 1", chess.WHITE),
            ("7k/8/8/8/8/8/P7/K7 w - - 100 1", chess.BLACK),
            ("7k/8/8/8/8/8/8/K7 w - - 100 1", chess.WHITE),
            ("7k/6Q1/5K2/8/8/8/p7/N7 b - - 100 1", chess.BLACK),
            ("7k/5Q2/5K2/8/8/8/p7/N7 b - - 100 1", chess.BLACK),
        ]
        for fen, winner in fixtures:
            board = royal_losers_board(fen)
            self.assertTrue(board.is_valid())
            self.assertEqual(board.outcome(claim_draw=True).winner, winner)
            self.assertTrue(board.is_game_over())
            self.assertEqual(board.copy().outcome().winner, winner)

    def test_draws_and_history(self):
        board = royal_losers_board("4k1n1/8/8/8/8/8/8/4K1N1 w - - 0 1")
        initial = board.fen()
        for move in ["g1f3", "g8f6", "f3g1", "f6g8"] * 2:
            board.push_uci(move)
        self.assertTrue(board.is_repetition(3))
        self.assertEqual(board.outcome(claim_draw=True).termination, chess.Termination.THREEFOLD_REPETITION)
        copied = board.copy()
        while board.move_stack:
            board.pop()
        self.assertEqual(board.fen(), initial)
        self.assertTrue(copied.is_repetition(3))
        board.halfmove_clock = 100
        self.assertFalse(board.is_insufficient_material())
        self.assertEqual(board.outcome(claim_draw=True).termination, chess.Termination.FIFTY_MOVES)


if __name__ == "__main__":
    unittest.main()
