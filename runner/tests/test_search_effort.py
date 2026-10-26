import unittest
import chess
import chess.engine
import chess.pgn

from maxima_runner.fics_game import annotate_search
from maxima_runner.search_effort import effort, analysis_rank
from maxima_runner.learning import opening_book


class effort_tests(unittest.TestCase):
    def test_search_duration_is_separate_from_server_elapsed(self):
        board = chess.Board()
        node = chess.pgn.Game().add_variation(chess.Move.from_uci("e2e4"))
        annotate_search(node, board, {"score": chess.engine.PovScore(chess.engine.Cp(20), chess.WHITE),
                                    "depth": 12, "nodes": 12345, "nps": 9876, "_search_seconds": 0.125})
        node.set_emt(12)
        self.assertEqual(effort(node), (12345, 9876, 0.125))
        self.assertEqual(node.emt(), 12)

    def test_missing_effort_is_unknown(self):
        node = chess.pgn.Game().add_variation(chess.Move.from_uci("e2e4"))
        node.set_emt(60)
        self.assertEqual(effort(node), (None, None, None))

    def test_nodes_precede_depth_and_duration(self):
        self.assertGreater(analysis_rank(10, 20, False, 100, 200),
                           analysis_rank(20, 20, False, 1000, 100))
        self.assertGreater(analysis_rank(10, 2000, True, None, None),
                           analysis_rank(20, 20, False, 1000, 100))


class learning_settings_tests(unittest.TestCase):
    def test_retired_settings_are_unknown_on_both_servers(self):
        import json
        import tempfile
        from pathlib import Path
        from maxima_runner import fics, lichess
        for module in (fics, lichess):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "settings.json"
                for key in ("learning_nps", "learning_margin"):
                    path.write_text(json.dumps({key: 500000}))
                    with self.subTest(server=module.__name__, key=key):
                        with self.assertRaisesRegex(ValueError, "unknown setting"):
                            module.load_settings(path)
