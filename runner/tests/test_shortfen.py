"""Short position text has known layouts and preserves orthodox state exactly."""
import random
import unittest

import chess

from maxima_runner import shortfen


class shortfen_tests(unittest.TestCase):
    def test_known_layouts_and_implicit_tail(self):
        cases = [
            (chess.STARTING_FEN, "rnbqkbnrpppppppp32PPPPPPPPRNBQKBNR w KQkq - 0 1"),
            ("8/8/8/4k3/8/8/8/R3K3 w - - 0 1", "28k27R3K w - - 0 1"),
            ("1K1k4/1P6/8/8/8/8/r7/2R5 w - - 0 1", "1K1k5P38r9R w - - 0 1"),
            ("7k/8/8/8/8/8/8/K7 b - - 42 91", "7k48K b - - 42 91"),
            ("7k/8/8/3pP3/8/8/8/K7 w - d6 0 1", "7k19pP27K w - d6 0 1"),
        ]
        for fen, compact in cases:
            with self.subTest(compact=compact):
                self.assertEqual(shortfen.encode(chess.Board(fen)), compact)
                self.assertEqual(shortfen.decode(compact).fen(en_passant="fen"), fen)

    def test_rejects_malformed_runs_and_invalid_positions(self):
        for placement in ["65Kk", "9999999999999Kk", "0Kk", "07Kk", "K/62k", "K63k", "64", "K62?"]:
            with self.subTest(placement=placement), self.assertRaises(ValueError):
                shortfen.decode(placement + " w - - 0 1")
        with self.assertRaises(ValueError):
            shortfen.decode("7k48K w - -")

    def test_reachable_positions_preserve_every_fen_field(self):
        generator = random.Random(2026)
        board = chess.Board()
        for _ in range(240):
            if board.is_game_over():
                board.reset()
            move = generator.choice(list(board.legal_moves))
            board.push(move)
            recovered = shortfen.decode(shortfen.encode(board))
            self.assertEqual(recovered.fen(en_passant="fen"), board.fen(en_passant="fen"))
            self.assertEqual(set(recovered.legal_moves), set(board.legal_moves))
