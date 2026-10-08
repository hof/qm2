import contextlib
import queue
import subprocess
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
