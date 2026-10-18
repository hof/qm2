"""Run the engine lifecycle contracts with the pinned NNUE evaluator enabled."""
import contextlib
from pathlib import Path
import sys
import unittest
import os
import shutil
import tempfile
from unittest.mock import patch

network_path = str(Path(sys.argv.pop(2)).resolve())
import test_search_lifecycle as lifecycle


original_running_engine = lifecycle.running_engine


@contextlib.contextmanager
def running_nnue_engine():
    with original_running_engine() as engine:
        engine.send("setoption name EvalFile value " + network_path)
        engine.wait_for("info string NNUE loaded")
        engine.send("isready")
        engine.wait_for("readyok")
        yield engine


lifecycle.running_engine = running_nnue_engine


class nnue_lifecycle_tests(lifecycle.search_lifecycle_tests):
    def test_failed_reload_and_missing_model(self):
        with lifecycle.running_engine() as engine:
            engine.send("setoption name EvalFile value " + network_path + ".missing")
            engine.wait_for("info string NNUE load failed:")
            engine.send("eval")
            engine.wait_for("info string NNUE evaluation cp ")
            engine.start_search("go depth 3")
            engine.wait_for_bestmove()
            engine.send("setoption name EvalFile value <empty>")
            engine.wait_for("info string NNUE load failed:")
            engine.start_search("go depth 3")
            engine.wait_for_bestmove()

    def test_model_switch_joins_search_and_restarts(self):
        with lifecycle.running_engine() as engine:
            engine.start_search()
            engine.send("setoption name EvalFile value " + network_path)
            engine.wait_for_bestmove()
            engine.wait_for("info string NNUE loaded")
            for _ in range(2):
                engine.send("setoption name EvalFile value " + network_path)
                engine.wait_for("info string NNUE loaded")
                engine.start_search("go depth 3")
                engine.wait_for_bestmove()

    def test_missing_default_model_rejects_search_and_recovers(self):
        with patch.dict(os.environ, MAXIMA_EVAL_FILE=network_path + ".missing"):
            with original_running_engine() as engine:
                engine.send("setoption name OwnBook value false")
                engine.send("position startpos")
                engine.send("go depth 3")
                engine.wait_for("info string ERROR: NNUE network required:")
                self.assertFalse(any(line.startswith("bestmove ") for line in engine.transcript))
                engine.send("setoption name EvalFile value " + network_path)
                engine.wait_for("info string NNUE loaded")
                engine.send("go depth 3")
                engine.wait_for_bestmove()

    def test_eval_nnue_auto_discovery_and_removed_options(self):
        with tempfile.TemporaryDirectory() as directory:
            shutil.copyfile(network_path, Path(directory) / "eval.nnue")
            with patch.dict(os.environ, MAXIMA_EVAL_FILE=""):
                engine = lifecycle.engine_session(directory)
            try:
                engine.send("uci")
                engine.wait_for("uciok")
                for name in ("UseNNUE", "KingAttackShelter", "KingAttackPieces"):
                    self.assertFalse(any(line.startswith("option name " + name + " ")
                                         for line in engine.transcript))
                engine.send("setoption name OwnBook value false")
                engine.send("position startpos")
                engine.send("eval")
                engine.wait_for("info string NNUE evaluation cp ")
                engine.send("go depth 3")
                engine.wait_for_bestmove()
                engine.send("quit")
                engine.wait_for_exit()
            finally:
                if engine.process.poll() is None:
                    engine.process.kill()
                engine.process.wait()
                engine.reader.join(timeout=5)
                engine.process.stdin.close()
                engine.process.stdout.close()

    def test_static_evaluation_in_check(self):
        with lifecycle.running_engine() as engine:
            engine.send("position fen 7k/8/8/8/8/8/r7/K7 w - - 0 1")
            engine.send("eval")
            engine.wait_for("info string NNUE static evaluation unavailable in check")
            engine.send("go depth 2")
            engine.wait_for_bestmove()


if __name__ == "__main__":
    unittest.main()
