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
                                    "depth": 12, "nodes": 12345, "nps": 9876, "_search_seconds": 0.125,
                                    "_experience": "recalled d20"})
        node.set_emt(12)
        self.assertEqual(effort(node), (12345, 9876, 0.125))
        self.assertEqual(node.emt(), 12)
        self.assertIn("recalled d20", node.comment)

    def test_missing_effort_is_unknown(self):
        node = chess.pgn.Game().add_variation(chess.Move.from_uci("e2e4"))
        node.set_emt(60)
        self.assertEqual(effort(node), (None, None, None))

    def test_nodes_precede_depth_and_duration(self):
        self.assertGreater(analysis_rank(10, 20, False, 100, 200),
                           analysis_rank(20, 20, False, 1000, 100))
        self.assertGreater(analysis_rank(10, 2000, True, None, None),
                           analysis_rank(20, 20, False, 1000, 100))


class reuse_tests(unittest.TestCase):
    def monitor(self, nodes=1400, cp=20, mate=False, nps=None):
        from maxima_runner.search_effort import reuse_monitor
        return reuse_monitor(chess.Move.from_uci("e2e4"), [20, cp, mate, 5000, 0, nodes, 1000], 1000, nps)

    def report(self, nodes, seconds, move="e2e4", **extra):
        return dict(nodes=nodes, time=seconds, pv=[chess.Move.from_uci(move)], depth=12,
                    score=chess.engine.PovScore(chess.engine.Cp(20), chess.WHITE), **extra)

    def test_threshold_and_samples(self):
        for saved, expected in ((1399, False), (1400, True), (1401, True)):
            monitor = self.monitor(saved)
            self.assertFalse(monitor(self.report(100, .1)))
            self.assertFalse(monitor(self.report(150, .15)))
            self.assertEqual(monitor(self.report(200, .2)), expected)

    def test_unknown_nps_and_floor_require_measurement(self):
        monitor = self.monitor(nps=1000)
        self.assertFalse(monitor(self.report(100, .1)))
        self.assertFalse(monitor(self.report(200, .15)))
        self.assertFalse(monitor(self.report(300, .2)))  # measured 2000 > floor
        self.assertEqual(monitor.nps, 2000)

    def test_disagreement_bounds_incomplete_and_exhausted_saved_work(self):
        monitor = self.monitor()
        self.assertFalse(monitor(self.report(100, .1, lowerbound=True)))
        self.assertIsNone(monitor.first)
        report = self.report(100, .1)
        del report["score"]
        self.assertFalse(monitor(report))
        self.assertIsNone(monitor.first)
        self.assertFalse(monitor(self.report(100, .1)))
        self.assertFalse(monitor(self.report(200, .2, "d2d4")))
        self.assertFalse(monitor(self.report(1500, 2)))

    def test_mate_exception_agrees_and_stop_only_once(self):
        monitor = self.monitor(nodes=None, cp=2000, mate=True)
        self.assertFalse(monitor(self.report(100, .1, "d2d4")))
        self.assertTrue(monitor(self.report(101, .11)))
        self.assertFalse(monitor(self.report(102, .12)))

    def test_highest_nps_is_retained(self):
        monitor = self.monitor(nodes=100000)
        monitor(self.report(100, .1, "d2d4"))
        monitor(self.report(500, .2, "d2d4"))
        monitor(self.report(600, .3, "d2d4"))
        self.assertAlmostEqual(monitor.nps, 4000)


class learning_settings_tests(unittest.TestCase):
    def test_both_servers_validate_effort_settings(self):
        import json
        import tempfile
        from pathlib import Path
        from maxima_runner import fics, lichess
        for module in (fics, lichess):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "settings.json"
                path.write_text(json.dumps({"learning_nps": 500000, "learning_margin": .5}))
                settings = module.load_settings(path)
                self.assertEqual(settings["learning_nps"], 500000)
                self.assertEqual(settings["learning_margin"], .5)
                for key, bad in (("learning_nps", 0), ("learning_nps", -1), ("learning_margin", -1),
                                 ("learning_margin", None), ("learning_nps", True), ("learning_nps", float("nan"))):
                    path.write_text(json.dumps({key: bad}))
                    with self.subTest(server=module.__name__, key=key, bad=bad):
                        with self.assertRaises(ValueError):
                            module.load_settings(path)
