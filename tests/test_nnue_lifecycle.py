"""Run the engine lifecycle contracts with the pinned NNUE evaluator enabled."""
import contextlib
from pathlib import Path
import sys
import unittest

network_path = str(Path(sys.argv.pop(2)).resolve())
import test_search_lifecycle as lifecycle


original_running_engine = lifecycle.running_engine


@contextlib.contextmanager
def running_nnue_engine():
    with original_running_engine() as engine:
        engine.send("setoption name EvalFile value " + network_path)
        engine.wait_for("info string NNUE loaded")
        engine.send("setoption name UseNNUE value true")
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
            engine.wait_for("info string NNUE unloaded; HCE selected")
            engine.send("setoption name UseNNUE value true")
            engine.wait_for("info string NNUE enable failed:")
            engine.start_search("go depth 3")
            engine.wait_for_bestmove()

    def test_model_switch_joins_search_and_restarts(self):
        with lifecycle.running_engine() as engine:
            engine.start_search()
            engine.send("setoption name EvalFile value " + network_path)
            engine.wait_for_bestmove()
            engine.wait_for("info string NNUE loaded")
            for value in ("false", "true", "false", "true"):
                engine.send("setoption name UseNNUE value " + value)
                engine.start_search("go depth 3")
                engine.wait_for_bestmove()

    def test_static_evaluation_in_check(self):
        with lifecycle.running_engine() as engine:
            engine.send("position fen 7k/8/8/8/8/8/r7/K7 w - - 0 1")
            engine.send("eval")
            engine.wait_for("info string NNUE static evaluation unavailable in check")
            engine.send("go depth 2")
            engine.wait_for_bestmove()


if __name__ == "__main__":
    unittest.main()
