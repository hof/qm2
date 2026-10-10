import contextlib
from pathlib import Path
import shutil
import queue
import subprocess
import struct
import sys
import tempfile
import threading
import time
import unittest


engine_path = sys.argv.pop(1)
timeout_seconds = 5


class engine_session:
    def __init__(self, directory):
        self.process = subprocess.Popen(
            [engine_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, cwd=directory,
        )
        self.lines = queue.Queue()
        self.transcript = []
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        for line in self.process.stdout:
            self.lines.put(line.rstrip())
        self.lines.put(None)

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def wait_for(self, prefix):
        deadline = time.monotonic() + timeout_seconds
        while True:
            try:
                line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                raise AssertionError(f"Timed out waiting for {prefix}: {self.transcript[-10:]}")
            if line is None:
                raise AssertionError(f"Engine exited waiting for {prefix}: {self.transcript[-10:]}")
            self.transcript.append(line)
            if line.startswith(prefix):
                return line

    def start_search(self, command="go infinite"):
        self.send("position startpos")
        self.send(command)
        # Ensure the worker actually searched before attempting cancellation.
        self.wait_for("info depth 2 ")

    def wait_for_bestmove(self):
        line = self.wait_for("bestmove ")
        if line.split()[1] == "0000":
            raise AssertionError("A searched starting position must return a move")

    def wait_for_exit(self):
        result = self.process.wait(timeout=timeout_seconds)
        if result != 0:
            raise AssertionError(f"Engine exited with status {result}")


@contextlib.contextmanager
def running_engine():
    with tempfile.TemporaryDirectory(prefix="qm2-lifecycle-") as directory:
        engine = engine_session(directory)
        try:
            yield engine
        finally:
            if engine.process.poll() is None:
                engine.process.kill()
            engine.process.wait()
            engine.reader.join(timeout=timeout_seconds)
            engine.process.stdin.close()
            engine.process.stdout.close()


class search_lifecycle_tests(unittest.TestCase):
    def test_zero_time_returns_without_stop(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            for command in ("go wtime 0 btime 0", "go movetime 0", "go wtime 1 btime 1"):
                engine.send("position startpos")
                engine.send(command)
                engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    def test_searchmoves_restricts_root_with_options_in_either_order(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            for command, allowed in (
                    ("go depth 2 searchmoves a2a3", {"a2a3"}),
                    ("go nodes 1 searchmoves a2a3", {"a2a3"}),
                    ("go nodes 1", {"a2a3", "a2a4", "b2b3", "b2b4", "c2c3", "c2c4",
                                    "d2d3", "d2d4", "e2e3", "e2e4", "f2f3", "f2f4",
                                    "g2g3", "g2g4", "h2h3", "h2h4", "b1a3", "b1c3",
                                    "g1f3", "g1h3"}),
                    ("go searchmoves h2h3 depth 2", {"h2h3"}),
                    ("go searchmoves a2a3 h2h3 nodes 1000", {"a2a3", "h2h3"})):
                engine.send("position startpos")
                engine.send(command)
                self.assertIn(engine.wait_for("bestmove ").split()[1], allowed)
            engine.send("go searchmoves e2e5 depth 2")
            engine.wait_for("info string Invalid move:")
            engine.send("go depth 2 searchmoves")
            engine.wait_for("info string Invalid searchmoves:")
            engine.send("isready")
            engine.wait_for("readyok")
            engine.send("quit")
            engine.wait_for_exit()

    def test_book_and_zero_time_obey_searchmoves(self):
        with tempfile.TemporaryDirectory(prefix="qm2-book-") as directory:
            book = Path(directory) / "book.bin"
            # Known initial-position Polyglot key; the only book move is a2a3.
            book.write_bytes(struct.pack(">QHHI", 0x463b96181691fc9c, (8 << 6) | 16, 10, 0))
            with running_engine() as engine:
                engine.send(f"setoption name BookFile value {book}")
                for command, expected in (
                        ("go depth 2 searchmoves h2h3", "h2h3"),
                        ("go movetime 0 searchmoves a2a3", "a2a3")):
                    engine.send("position startpos")
                    engine.send(command)
                    self.assertEqual(engine.wait_for("bestmove ").split()[1], expected)
                engine.send("quit")
                engine.wait_for_exit()

    def test_tablebase_path_reconfiguration(self):
        with tempfile.TemporaryDirectory(prefix="qm2-tables-") as directory:
            tables = Path(directory) / "tables with spaces"
            shutil.copytree(Path(__file__).parent / "data" / "syzygy", tables)
            with running_engine() as engine:
                engine.send("uci")
                engine.wait_for("uciok")
                self.assertTrue(any(line.startswith("option name SyzygyPath ")
                                    for line in engine.transcript))
                engine.start_search()
                engine.send(f"setoption name SyzygyPath value {tables}")
                engine.wait_for_bestmove()
                engine.wait_for("info string Syzygy supports up to 3 pieces")
                engine.send("setoption name DrawContempt value 0")
                engine.send("position fen 7k/8/8/8/8/8/8/KR6 w - - 0 1\ngo depth 3")
                info = engine.wait_for("info depth 0 ")
                self.assertIn("score cp 28000", info)
                self.assertIn("tbhits 1", info)
                engine.wait_for_bestmove()
                engine.send("setoption name SyzygyPath value <empty>")
                engine.wait_for("info string Syzygy supports up to 0 pieces")
                engine.send("position fen 7k/8/8/8/8/8/8/KR6 w - - 0 1\ngo depth 1")
                info = engine.wait_for("info depth 1 ")
                self.assertNotIn("tbhits", info)
                engine.wait_for_bestmove()
                engine.send("quit")
                engine.wait_for_exit()

    def test_tablebase_infinite_and_ponder(self):
        tables = Path(__file__).parent / "data" / "syzygy"
        with running_engine() as engine:
            engine.send(f"setoption name SyzygyPath value {tables}")
            engine.wait_for("info string Syzygy supports up to 3 pieces")
            for command, finish in (("go infinite", "stop"), ("go ponder", "ponderhit")):
                engine.transcript.clear()
                engine.send("position fen 7k/8/8/8/8/8/8/KR6 w - - 0 1")
                engine.send(command)
                engine.wait_for("info depth 0 ")
                engine.send("isready")
                engine.wait_for("readyok")
                self.assertFalse(any(line.startswith("bestmove ") for line in engine.transcript))
                engine.send(finish)
                engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    def test_long_position_history(self):
        with running_engine() as engine:
            moves = "g1f3 g8f6 f3g1 f6g8 " * 80
            engine.send("position startpos moves " + moves)
            engine.send("go depth 1")
            engine.wait_for_bestmove()
            engine.send("position startpos\ngo depth 2")
            engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    def test_position_history_at_fifty_move_boundary(self):
        with running_engine() as engine:
            for halfmoves in (99, 100, 101, 150):
                engine.send(f"position fen 7k/8/8/8/8/8/8/K6R b - - {halfmoves} 1 moves h8g8")
                engine.send("isready")
                engine.wait_for("readyok")
                engine.send("go depth 1")
                engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    def test_stop_and_restart(self):
        with running_engine() as engine:
            for _ in range(3):
                engine.start_search()
                engine.send("stop")
                engine.wait_for_bestmove()
                engine.send("stop")
                engine.send("isready")
                engine.wait_for("readyok")
            engine.send("quit")
            engine.wait_for_exit()

    def test_replace_active_search(self):
        with running_engine() as engine:
            engine.start_search()
            engine.send("go infinite")
            engine.wait_for_bestmove()
            engine.wait_for("info depth 2 ")
            engine.send("go depth 2")
            engine.wait_for_bestmove()
            engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    def test_replace_search_during_startup(self):
        with running_engine() as engine:
            # Queue replacement before waiting for worker initialization or output.
            engine.send("position startpos\ngo infinite\ngo infinite\nstop\nisready\nquit")
            engine.wait_for("readyok")
            self.assertEqual(sum(line.startswith("bestmove ")
                                 for line in engine.transcript), 2)
            engine.wait_for_exit()

    def test_quit_during_search(self):
        with running_engine() as engine:
            engine.start_search()
            engine.send("quit")
            engine.wait_for_bestmove()
            engine.wait_for_exit()

    def test_ponderhit(self):
        with running_engine() as engine:
            engine.start_search("go ponder movetime 200")
            engine.send("ponderhit")
            engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    def test_resize_hash_during_search(self):
        with running_engine() as engine:
            engine.start_search()
            engine.send("setoption name Hash value 16")
            engine.send("isready")
            engine.wait_for_bestmove()
            engine.wait_for("readyok")
            engine.send("go depth 2")
            engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()


if __name__ == "__main__":
    unittest.main()
