"""Run position suites through UCI, independently of engine internals."""
import argparse
import json
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time


move_pattern = re.compile(r"[a-h][1-8][a-h][1-8][qrbn]?")


def read_suite(path):
    try:
        import chess
    except ImportError as error:
        raise RuntimeError(
            "SAN/EPD parsing requires chess; install tools/requirements.txt "
            "in .tools/suites (see MAXIMA.md chapter 6.1)"
        ) from error
    positions = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        try:
            board = chess.Board()
            operations = board.set_epd(line.strip())
            if not board.is_valid():
                raise ValueError("invalid chess position")
            moves = operations.get("bm")
            if not isinstance(moves, list) or not moves or any(
                move not in board.legal_moves for move in moves
            ):
                raise ValueError("bm must contain one or more legal best moves")
            identifier = operations.get("id", str(len(positions) + 1))
            if not isinstance(identifier, str):
                raise ValueError("id must be a quoted string")
            positions.append({"id": identifier, "epd": line.strip(),
                              "fen": board.fen(en_passant="fen"),
                              "expected_moves": [move.uci() for move in moves],
                              "expected_san": [board.san(move) for move in moves]})
        except ValueError as error:
            raise ValueError(f"{path}:{number}: {error}") from error
    if not positions:
        raise ValueError("suite is empty")
    return positions


class engine_session:
    def __init__(self, command, directory):
        self.process = subprocess.Popen(command, cwd=directory, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, bufsize=1)
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        for line in self.process.stdout:
            self.lines.put((time.monotonic(), line.strip()))
        self.lines.put((time.monotonic(), None))

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def receive(self, deadline):
        if time.monotonic() >= deadline:
            raise RuntimeError("engine response timed out")
        try:
            timestamp, line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty:
            raise RuntimeError("engine response timed out")
        if line is None:
            raise RuntimeError("engine exited before completing UCI response")
        return timestamp, line

    def wait_for(self, marker, timeout):
        deadline = time.monotonic() + timeout
        lines = []
        while True:
            _, line = self.receive(deadline)
            lines.append(line)
            if line == marker:
                return lines

    def close(self):
        try:
            if self.process.poll() is None:
                self.send("quit")
                self.process.wait(timeout=2)
        except (OSError, subprocess.TimeoutExpired):
            self.process.kill()
        finally:
            self.process.wait()
            self.reader.join(timeout=2)
            self.process.stdin.close()
            self.process.stdout.close()


def parse_info(line):
    tokens = line.split()
    if not tokens or tokens[0] != "info" or "string" in tokens:
        return {}
    result = {}
    for name in ("depth", "seldepth", "nodes", "nps", "time", "tbhits"):
        if name in tokens:
            result[name] = int(tokens[tokens.index(name) + 1])
    if "score" in tokens:
        index = tokens.index("score")
        result["score"] = {"type": tokens[index + 1], "value": int(tokens[index + 2])}
        for bound in ("lowerbound", "upperbound"):
            if bound in tokens:
                result["score"]["bound"] = bound
    if "pv" in tokens:
        result["pv"] = tokens[tokens.index("pv") + 1:]
    if "multipv" in tokens and tokens[tokens.index("multipv") + 1] != "1":
        return {}
    return result


def run_position(command, position, budget, hash_mb, timeout):
    # Fresh processes guarantee cold caches, even without a Clear Hash option.
    # An empty working directory also excludes Maxima's automatic opening book.
    with tempfile.TemporaryDirectory(prefix="maxima-suite-") as directory:
        engine = engine_session(command, directory)
        try:
            engine.send("uci")
            handshake = engine.wait_for("uciok", timeout)
            options = {match.group(1) for line in handshake
                       for match in [re.match(r"option name (.+?) type ", line)] if match}
            settings = {"Hash": str(hash_mb), "Threads": "1", "OwnBook": "false",
                        "MultiPV": "1", "Ponder": "false", "DrawContempt": "0",
                        "SyzygyProbeLimit": "0"}
            applied = {name: value for name, value in settings.items() if name in options}
            for name, value in applied.items():
                engine.send(f"setoption name {name} value {value}")
            engine.send("ucinewgame")
            engine.send("isready")
            engine.wait_for("readyok", timeout)
            engine.send("position fen " + position["fen"])
            started = time.monotonic()
            engine.send("go " + budget)
            result = dict(position, first_expected_ms=None)
            deadline = started + timeout
            while True:
                timestamp, line = engine.receive(deadline)
                if line.startswith("bestmove "):
                    move = line.split()[1]
                    if move not in ("0000", "(none)") and not move_pattern.fullmatch(move):
                        raise RuntimeError(f"invalid bestmove response: {line}")
                    result.update(bestmove=move, solved=move in position["expected_moves"],
                                  elapsed_ms=round((timestamp - started) * 1000, 3))
                    break
                info = parse_info(line)
                result.update(info)
                pv = info.get("pv", [])
                if pv and pv[0] in position["expected_moves"] and result["first_expected_ms"] is None:
                    result["first_expected_ms"] = round((timestamp - started) * 1000, 3)
            return result, {"id": [line[3:] for line in handshake if line.startswith("id ")],
                            "options": applied}
        finally:
            engine.close()


def positive_int(value):
    value = int(value)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", default="bratko-kopec", help="bundled name or EPD file")
    parser.add_argument("--engine", default=str(root / "build/cpp26/release/src/maxima2"),
                        help="engine executable path")
    limits = parser.add_mutually_exclusive_group()
    limits.add_argument("--movetime", type=positive_int, help="milliseconds per position (default 1000)")
    limits.add_argument("--nodes", type=positive_int)
    parser.add_argument("--hash", type=positive_int, default=64, dest="hash_mb")
    parser.add_argument("--timeout", type=positive_int, default=30, help="response timeout in seconds")
    parser.add_argument("--json", type=Path, help="write full report")
    args = parser.parse_args()
    suite = root / "tests/data/suites/bratko-kopec.epd" if args.suite == "bratko-kopec" else Path(args.suite)
    budget = f"nodes {args.nodes}" if args.nodes else f"movetime {args.movetime or 1000}"
    try:
        positions = read_suite(suite)
        command = [str(Path(args.engine).resolve())]
        timeout = max(args.timeout, (args.movetime or 1000) / 1000 + 10)
        results = []
        for position in positions:
            result, metadata = run_position(command, position, budget, args.hash_mb, timeout)
            results.append(result)
            print(f"{position['id']:>6} {'ok' if result['solved'] else 'miss':4} "
                  f"{result['bestmove']:5} expected {','.join(position['expected_moves'])} "
                  f"depth {result.get('depth', '?')} nodes {result.get('nodes', '?')} "
                  f"time {result['elapsed_ms']:.0f}ms", flush=True)
        solved = sum(result["solved"] for result in results)
        report = {"suite": str(suite.resolve()), "engine": command[0], "engine_metadata": metadata,
                  "budget": budget, "cold_cache": True, "solved": solved,
                  "total": len(results), "results": results}
        if args.json:
            args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Solved {solved}/{len(results)} ({budget}, cold caches)")
        return 0
    except (OSError, ValueError, RuntimeError, IndexError) as error:
        print(f"Suite failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
