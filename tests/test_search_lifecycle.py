import contextlib
from pathlib import Path
import shutil
import signal
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
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"Timed out waiting for {prefix}: {self.transcript[-10:]}")
            try:
                line = self.lines.get(timeout=remaining)
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
    def test_search_result_provenance(self):
        for variant in ("standard", "losers"):
            for limit in (1, 2, 30, 1000):
                with self.subTest(variant=variant, limit=limit), running_engine() as engine:
                    engine.send("setoption name OwnBook value false")
                    engine.send(f"setoption name Wild value {variant}")
                    engine.send("position startpos")
                    engine.send(f"go nodes {limit}")
                    engine.wait_for_bestmove()
                    reports = [line.split() for line in engine.transcript
                               if line.startswith("info depth ")]
                    self.assertTrue(reports, engine.transcript)
                    for fields in reports:
                        self.assertLessEqual(int(fields[fields.index("nodes") + 1]), limit)
                    statuses = [line.split() for line in engine.transcript
                                if line.startswith("info string search_result ")]
                    self.assertEqual(len(statuses), 1, engine.transcript)
                    status = statuses[0]
                    final = reports[-1]
                    completed = int(status[status.index("completed_depth") + 1])
                    attempted = int(status[status.index("attempted_depth") + 1])
                    self.assertGreaterEqual(attempted, completed)
                    self.assertIn("interrupted 1", " ".join(status))
                    if limit == 1:
                        self.assertEqual(status[3], "fallback")
                        self.assertEqual(int(final[2]), 0)
                        self.assertNotIn("score", final)
                    elif status[3] == "partial":
                        # Interrupted fail-low work has a move, but no proven score.
                        if "score" in final:
                            self.assertIn("lowerbound", final)
                        else:
                            self.assertNotIn("lowerbound", final)
                        self.assertNotIn("upperbound", final)
                    elif status[3] == "complete":
                        self.assertEqual(int(final[2]), completed)
                        self.assertNotIn("lowerbound", final)
                        self.assertNotIn("upperbound", final)
                    engine.send("quit")
                    engine.wait_for_exit()

    def test_reported_nps_uses_elapsed_milliseconds(self):
        for variant in ("standard", "losers"):
            with self.subTest(variant=variant), running_engine() as engine:
                engine.send("setoption name OwnBook value false")
                engine.send(f"setoption name Wild value {variant}")
                engine.send("position startpos")
                engine.send("go depth 4")
                engine.wait_for_bestmove()
                reports = [line.split() for line in engine.transcript
                           if line.startswith("info depth ")]
                self.assertTrue(reports, engine.transcript)
                for fields in reports:
                    nodes = int(fields[fields.index("nodes") + 1])
                    elapsed = int(fields[fields.index("time") + 1])
                    nps = int(fields[fields.index("nps") + 1])
                    self.assertGreaterEqual(elapsed, 0)
                    self.assertEqual(nps, nodes * 1000 // max(1, elapsed), fields)
                engine.send("quit")
                engine.wait_for_exit()

    def test_evaluation_option_changes_match_cold_searches(self):
        fen = "r4rk1/1p2ppbp/p2pbnp1/q7/3BPPP1/2N2B2/PPP4P/R2Q1RK1 b - - 0 1"
        for names in (("KingAttackShelter",), ("KingAttackPieces",),
                      ("KingAttackShelter", "KingAttackPieces")):
            for before, after in ((0, 512), (512, 0)):
                with self.subTest(options=names, before=before, after=after), running_engine() as engine:
                    for name, value in (("Hash", "16"), ("OwnBook", "false"),
                                        ("DrawContempt", "0"), ("NullMove", "false"),
                                        ("BetaPruning", "false"), ("LMR", "false"),
                                        ("FutilityPruning", "false"),
                                        ("KingAttackShelter", "0"), ("KingAttackPieces", "0")):
                        engine.send(f"setoption name {name} value {value}")

                    def search_score():
                        start = len(engine.transcript)
                        engine.send(f"position fen {fen}")
                        engine.send("go depth 2")
                        engine.wait_for_bestmove()
                        lines = [line for line in engine.transcript[start:]
                                 if line.startswith("info depth 2 ")]
                        self.assertTrue(lines, engine.transcript[start:])
                        fields = lines[-1].split()
                        self.assertNotIn("lowerbound", fields)
                        self.assertNotIn("upperbound", fields)
                        index = fields.index("score")
                        return fields[index + 1:index + 3]

                    for name in names:
                        engine.send(f"setoption name {name} value {before}")
                    search_score()
                    for name in names:
                        engine.send(f"setoption name {name} value {after}")
                    warm = search_score()
                    engine.send("ucinewgame")
                    self.assertEqual(warm, search_score())
                    engine.send("quit")
                    engine.wait_for_exit()

    def test_evaluation_option_changes_stop_active_search(self):
        for name in ("KingAttackShelter", "KingAttackPieces"):
            with self.subTest(option=name), running_engine() as engine:
                engine.send("setoption name OwnBook value false")
                engine.start_search()
                engine.send(f"setoption name {name} value 512")
                engine.send("isready")
                engine.wait_for_bestmove()
                engine.wait_for("readyok")
                self.assertEqual(sum(line.startswith("bestmove ")
                                     for line in engine.transcript), 1)
                engine.send("go depth 2")
                engine.wait_for_bestmove()
                engine.send("isready")
                engine.wait_for("readyok")
                self.assertEqual(sum(line.startswith("bestmove ")
                                     for line in engine.transcript), 2)
                engine.send("quit")
                engine.wait_for_exit()

    def test_unsupported_pawn_counts_preserve_search_position(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            engine.send("position startpos moves g1f3 g8f6 f3g1 f6g8")
            for placement in ("7k/pppppppp/p7/8/8/P7/PPPPPPPP/K7",
                              "7k/pppppppp/8/8/8/P7/PPPPPPPP/K7",
                              "7k/pppppppp/p7/8/8/8/PPPPPPPP/K7"):
                engine.send(f"position fen {placement} w - - 0 1")
                engine.wait_for("info string Invalid FEN")
                engine.send(f"eval {placement} w - - 0 1")
                engine.wait_for("info string Invalid FEN")
                engine.send("eval")
                engine.send("go depth 1 searchmoves e2e4")
                self.assertEqual(engine.wait_for("bestmove ").split()[1], "e2e4")
            for position, move in (
                    ("7k/pppppppp/8/8/8/8/PPPPPPPP/K7 w - - 0 1", "a2a3"),
                    ("7k/Pppppppp/8/8/8/8/1PPPPPPP/K7 w - - 0 1", "a7a8q")):
                engine.send(f"position fen {position}")
                engine.send("eval")
                engine.send(f"go depth 1 searchmoves {move}")
                self.assertEqual(engine.wait_for("bestmove ").split()[1], move)
            engine.send("quit")
            engine.wait_for_exit()

    def test_losers_terminal_roots(self):
        positions = (
            "7k/7p/8/8/8/8/8/K7 w - - 0 1",  # king-only win
            "k7/8/8/8/8/8/7P/7K b - - 0 1",
            "7k/7p/8/8/8/8/8/K7 b - - 0 1",  # opponent already won
            "k7/8/8/8/8/8/7P/7K w - - 0 1",
            "7k/6Q1/5K2/8/8/8/p7/8 b - - 100 1",  # royal checkmate wins
            "8/P7/8/8/8/5k2/6q1/7K w - - 100 1",
            "7k/5K2/6Q1/8/8/p7/P7/8 b - - 100 1",  # stalemate wins
            "8/p7/P7/8/8/6q1/5k2/7K w - - 100 1",
        )
        with running_engine() as engine:
            engine.send("setoption name Wild value losers")
            # Leave book enabled: terminal rules must precede shortcuts.
            for position in positions:
                for command, finish in (("go depth 1", None),
                                        ("go infinite", "stop"),
                                        ("go ponder", "ponderhit")):
                    engine.transcript.clear()
                    engine.send(f"position fen {position}")
                    engine.send(command)
                    info = engine.wait_for("info depth 0 ")
                    self.assertIn(" score mate 0 ", info)
                    if finish:
                        time.sleep(0.05)
                        engine.send("isready")
                        engine.wait_for("readyok")
                        self.assertFalse(any(line.startswith("bestmove ")
                                             for line in engine.transcript))
                        engine.send(finish)
                    self.assertEqual(engine.wait_for("bestmove "), "bestmove 0000")
                    engine.send("isready")
                    engine.wait_for("readyok")
                    self.assertEqual(sum(line.startswith("bestmove ")
                                         for line in engine.transcript), 1)
            engine.send("quit")
            engine.wait_for_exit()

    def check_result_authorization(self, engine, position, info_prefix, expected_move,
                                   restrict_move=False, tablebase_result=False):
        commands = (
            ("go", "stop"),
            ("go ponder depth 1", "ponderhit"),
            ("go depth 1 ponder", "ponderhit"),
            ("go ponder nodes 1", "ponderhit"),
            ("go nodes 1 ponder", "stop"),
            ("go ponder movetime 0", "ponderhit"),
            ("go movetime 0 ponder", "stop"),
            ("go ponder wtime 0 btime 0 depth 1", "ponderhit"),
            ("go wtime 0 btime 0 depth 1 ponder", "stop"),
            ("go infinite depth 1", "stop"),
            ("go depth 1 infinite", "stop"),
            ("go infinite nodes 1", "stop"),
            ("go nodes 1 infinite", "stop"),
            ("go infinite movetime 0", "stop"),
            ("go movetime 0 infinite", "stop"),
            ("go infinite wtime 0 btime 0 depth 1", "stop"),
            ("go wtime 0 btime 0 depth 1 infinite", "stop"),
            ("go ponder infinite depth 1", "stop"),
            ("go depth 1 infinite ponder", "stop"),
        )
        if expected_move != "0000":
            commands += (("go searchmoves " + expected_move + " depth 1 ponder", "ponderhit"),)
        for command, finish in commands:
            with self.subTest(position=position, command=command):
                engine.transcript.clear()
                engine.send(position)
                if restrict_move:
                    command += " searchmoves " + expected_move
                engine.send(command)
                if tablebase_result and "nodes" not in command and "time" not in command:
                    info = engine.wait_for("info depth 0 ")
                    self.assertIn("tbhits 1", info)
                elif info_prefix:
                    engine.wait_for(info_prefix)
                # Give the worker time to finish bounded work and enter its wait.
                time.sleep(0.05)
                if "infinite" in command:
                    engine.send("ponderhit")
                engine.send("isready")
                engine.wait_for("readyok")
                self.assertFalse(any(line.startswith("bestmove ")
                                     for line in engine.transcript))
                engine.send(finish)
                self.assertEqual(engine.wait_for("bestmove ").split()[1], expected_move)
                engine.send("stop\nisready")
                engine.wait_for("readyok")
                self.assertEqual(sum(line.startswith("bestmove ")
                                     for line in engine.transcript), 1)
                if "depth 1" in command:
                    self.assertTrue(all(int(line.split()[2]) <= 1
                                        for line in engine.transcript
                                        if line.startswith("info depth ")))
        # Ordinary bounded requests must still publish without authorization.
        bounded_commands = ("go depth 1", "go nodes 1", "go movetime 0")
        if expected_move == "0000" or info_prefix or tablebase_result:
            bounded_commands += ("go depth 128",)
        for command in bounded_commands:
            if restrict_move:
                command += " searchmoves " + expected_move
            engine.send(position)
            engine.send(command)
            self.assertEqual(engine.wait_for("bestmove ").split()[1], expected_move)

    def test_bounded_result_authorization(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            # One legal move makes the expected result deterministic at every limit.
            self.check_result_authorization(
                engine, "position fen 8/8/8/8/8/2k5/2r5/K7 w - - 0 1",
                None, "a1b1")
            self.check_result_authorization(engine, "position startpos",
                                            None, "e2e4", restrict_move=True)
            for fen in ("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1",
                        "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"):
                self.check_result_authorization(engine, "position fen " + fen,
                                                None, "0000")
            engine.send("setoption name Wild value losers")
            for fen in ("7k/7p/8/8/8/8/8/K7 w - - 0 1",
                        "7k/6Q1/5K2/8/8/8/p7/8 b - - 100 1",
                        "7k/5K2/6Q1/8/8/p7/P7/8 b - - 100 1"):
                self.check_result_authorization(engine, "position fen " + fen,
                                                "info depth 0 ", "0000")

    def test_bounded_book_result_authorization(self):
        with tempfile.TemporaryDirectory(prefix="qm2-book-") as directory:
            book = Path(directory) / "book.bin"
            book.write_bytes(struct.pack(">QHHI", 0x463b96181691fc9c, (8 << 6) | 16, 10, 0))
            with running_engine() as engine:
                engine.send(f"setoption name BookFile value {book}")
                engine.send("position startpos")
                engine.send("go depth 1")
                self.assertEqual(engine.wait_for("bestmove ").split()[1], "a2a3")
                info = next(line.split() for line in engine.transcript
                            if line.startswith("info depth "))
                self.assertEqual(info[2], "0")
                self.assertNotIn("score", info)
                self.assertEqual(info[info.index("nodes") + 1], "0")
                self.assertTrue(any(line.startswith("info string search_result book ")
                                    for line in engine.transcript), engine.transcript)
                self.check_result_authorization(engine, "position startpos",
                                                "info depth 0 ", "a2a3")

    def test_bounded_tablebase_result_authorization(self):
        tables = Path(__file__).parent / "data" / "syzygy"
        with running_engine() as engine:
            engine.send(f"setoption name SyzygyPath value {tables}")
            engine.wait_for("info string Syzygy supports up to 3 pieces")
            self.check_result_authorization(
                engine, "position fen 7k/8/8/8/8/8/8/KR6 w - - 0 1",
                None, "b1g1", restrict_move=True, tablebase_result=True)

    def test_zero_time_returns_without_stop(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            for command in ("go wtime 0 btime 0", "go movetime 0", "go wtime 1 btime 1"):
                engine.send("position startpos")
                engine.send(command)
                engine.wait_for_bestmove()
            engine.send("quit")
            engine.wait_for_exit()

    @unittest.skipUnless(hasattr(signal, "SIGSTOP"), "requires process pause signals")
    def test_elapsed_deadline_after_process_pause(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            for command in ("go movetime 1000", "go wtime 25200 btime 25200"):
                engine.start_search(command)
                engine.process.send_signal(signal.SIGSTOP)
                try:
                    time.sleep(1.2)
                finally:
                    resumed = time.monotonic()
                    engine.process.send_signal(signal.SIGCONT)
                engine.wait_for_bestmove()
                self.assertLess(time.monotonic() - resumed, 0.5,
                                "expired search must finish promptly after resumption")
                info = [line for line in engine.transcript if line.startswith("info depth ")][-1]
                fields = info.split()
                self.assertGreaterEqual(int(fields[fields.index("time") + 1]), 1200)

    def test_ponderhit_after_elapsed_deadline(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            engine.start_search("go ponder movetime 200")
            time.sleep(0.3)
            engine.send("isready")
            engine.wait_for("readyok")
            self.assertFalse(any(line.startswith("bestmove ") for line in engine.transcript))
            started = time.monotonic()
            engine.send("ponderhit")
            engine.wait_for_bestmove()
            self.assertLess(time.monotonic() - started, 0.5)
            fields = [line for line in engine.transcript if line.startswith("info depth ")][-1].split()
            self.assertGreaterEqual(int(fields[fields.index("time") + 1]), 300)
            engine.send("quit")
            engine.wait_for_exit()

    def test_ordinary_time_budgets(self):
        # Keep the clock-mode allocation below the one-second response check;
        # 6000 ms with this increment can legitimately reserve 1050 ms.
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            for command in ("go movetime 200", "go wtime 4200 btime 4200 winc 100 binc 100"):
                started = time.monotonic()
                engine.start_search(command)
                engine.wait_for_bestmove()
                self.assertLess(time.monotonic() - started, 1.0)
            engine.send("quit")
            engine.wait_for_exit()

    def test_unsupported_chess960_is_not_advertised_or_enabled(self):
        with running_engine() as engine:
            engine.send("uci")
            engine.wait_for("uciok")
            self.assertFalse(any(line.startswith("option name UCI_Chess960 ")
                                 for line in engine.transcript))
            engine.send("setoption name OwnBook value false")
            for value in ("true", "1", "false", "nonsense"):
                start = len(engine.transcript)
                engine.send(f"setoption name UCI_Chess960 value {value}")
                engine.send("isready")
                engine.wait_for("readyok")
                self.assertIn("info string Unsupported option: UCI_Chess960",
                              engine.transcript[start:])
            # Ordinary castling remains available after rejected option requests.
            engine.send("position fen 4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1")
            engine.send("go depth 1 searchmoves e1g1")
            self.assertEqual(engine.wait_for("bestmove "), "bestmove e1g1")
            engine.send("quit")
            engine.wait_for_exit()

    def test_unsupported_mate_limits_do_not_start_search(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            engine.send("position startpos moves g1f3 g8f6 f3g1 f6g8")
            for command in ("go mate 1", "go mate", "go mate 0", "go mate nonsense",
                            "go depth 1 mate 2", "go mate 2 nodes 1",
                            "go searchmoves e2e4 mate 1 depth 1",
                            "go depth 1 searchmoves a2a3 mate 1 ponder",
                            "go infinite mate 1"):
                with self.subTest(command=command):
                    start = len(engine.transcript)
                    engine.send(command)
                    engine.send("isready")
                    engine.wait_for("readyok")
                    lines = engine.transcript[start:]
                    self.assertIn("info string Unsupported go limit: mate", lines)
                    self.assertFalse(any(line.startswith(("info depth ", "bestmove "))
                                         for line in lines))
            engine.send("go depth 1 searchmoves a2a3")
            self.assertEqual(engine.wait_for("bestmove "), "bestmove a2a3")
            engine.send("quit")
            engine.wait_for_exit()

    def test_unsupported_mate_stops_active_search_without_restarting(self):
        with running_engine() as engine:
            engine.send("setoption name OwnBook value false")
            engine.start_search()
            engine.send("go searchmoves e2e4 mate 1 depth 1")
            engine.send("isready")
            engine.wait_for("readyok")
            self.assertIn("info string Unsupported go limit: mate", engine.transcript)
            self.assertEqual(sum(line.startswith("bestmove ")
                                 for line in engine.transcript), 1)
            engine.send("go depth 1 searchmoves h2h3")
            self.assertEqual(engine.wait_for("bestmove "), "bestmove h2h3")
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
