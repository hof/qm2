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
