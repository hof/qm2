"""Run position suites through UCI, independently of engine internals."""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import sys
import tempfile

from .project import engine_path, project_root
from .match_options import (engine_profile, option_assignment, validated_settings,
                            verify_models)


def read_suite(path):
    try:
        import chess
    except ImportError as error:
        raise RuntimeError(
            "SAN/EPD parsing requires chess; install the runner package "
            "(see MAXIMA.md chapter 6.1)"
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


def report_info(info):
    """Preserve the existing JSON fields while using python-chess parsed values."""
    result = {name: info[name] for name in
              ("depth", "seldepth", "nodes", "nps", "tbhits") if name in info}
    if "time" in info:
        result["time"] = round(info["time"] * 1000)
    if "score" in info:
        score = info["score"].relative
        result["score"] = {"type": "mate" if score.is_mate() else "cp",
                           "value": score.mate() if score.is_mate() else score.score()}
        for bound in ("lowerbound", "upperbound"):
            if info.get(bound):
                result["score"]["bound"] = bound
    if "pv" in info:
        result["pv"] = [move.uci() for move in info["pv"]]
    return result


async def run_position(command, position, budget, hash_mb, timeout, syzygy_path=None, profile=None):
    import chess
    import chess.engine
    from .uci import open_engine

    board = chess.Board(position["fen"])
    if not board.is_valid():
        raise ValueError("invalid chess position")
    kind, value = budget.split()
    value = positive_int(value)
    if kind == "nodes":
        limit = chess.engine.Limit(nodes=value)
    elif kind == "movetime":
        limit = chess.engine.Limit(time=value / 1000)
    else:
        raise ValueError("unsupported search budget")
    profile = profile if profile is not None else engine_profile([], [])
    verify_models(profile)
    # Fresh processes guarantee cold caches and exclude automatic opening books.
    with tempfile.TemporaryDirectory(prefix="maxima-suite-") as directory:
        async with open_engine(command, directory, timeout) as engine:
            settings = {"Hash": hash_mb, "Threads": 1, "OwnBook": False,
                        "MultiPV": 1, "Ponder": False, "DrawContempt": 0,
                        "UCI_AnalyseMode": False, "SyzygyProbeLimit": 0}
            if syzygy_path is not None:
                if "SyzygyPath" not in engine.protocol.options:
                    raise RuntimeError("engine does not advertise SyzygyPath")
                settings.update(SyzygyPath=str(Path(syzygy_path).resolve()),
                                SyzygyProbeLimit=5)
            applied = await engine.configure(validated_settings(engine.protocol.options, settings, profile))
            result = dict(position, first_expected_ms=None, tbhits=0)

            def observe(info, elapsed_ms):
                result.update(report_info(info))
                pv = info.get("pv", [])
                if (pv and pv[0].uci() in position["expected_moves"]
                        and result["first_expected_ms"] is None):
                    result["first_expected_ms"] = round(elapsed_ms, 3)

            best, elapsed_ms = await engine.search(board, limit, on_info=observe)
            move = best.move.uci() if best.move is not None else "(none)"
            result.update(bestmove=move, solved=move in position["expected_moves"],
                          elapsed_ms=round(elapsed_ms, 3))
            verify_models(profile)
            return result, {"id": engine.identity, "options": applied, "models": profile["models"]}


def positive_int(value):
    value = int(value)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return value


def main():
    root = project_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", default="modern_2026",
                        help="bundled name (default modern_2026) or EPD file")
    parser.add_argument("--engine", default=str(engine_path(root)),
                        help="engine executable path")
    parser.add_argument("--option", action="append", type=option_assignment, default=[],
                        metavar="NAME=VALUE", help="repeatable evaluator/search option")
    parser.add_argument("--model", action="append", type=option_assignment, default=[],
                        metavar="OPTION=PATH", help="set and hash an external model file")
    limits = parser.add_mutually_exclusive_group()
    limits.add_argument("--movetime", type=positive_int, help="milliseconds per position (default 1000)")
    limits.add_argument("--nodes", type=positive_int)
    parser.add_argument("--hash", type=positive_int, default=64, dest="hash_mb")
    parser.add_argument("--syzygy-path", type=Path,
                        help="enable local tablebases (up to five pieces); disabled by default")
    parser.add_argument("--timeout", type=positive_int, default=30, help="response timeout in seconds")
    parser.add_argument("--json", type=Path, help="write full report")
    args = parser.parse_args()
    bundled = {"modern_2026", "bratko-kopec"}
    suite = (Path(__file__).parent / "data/suites" / f"{args.suite}.epd"
             if args.suite in bundled else Path(args.suite))
    budget = f"nodes {args.nodes}" if args.nodes else f"movetime {args.movetime or 1000}"
    try:
        profile = engine_profile(args.option, args.model)
        suite_hash = hashlib.sha256(suite.read_bytes()).hexdigest()
        positions = read_suite(suite)
        if args.syzygy_path is not None:
            args.syzygy_path = args.syzygy_path.resolve()
            if not args.syzygy_path.is_dir() or not any(args.syzygy_path.glob("*.rtbw")):
                raise ValueError("Syzygy path must be a directory containing WDL (.rtbw) tables")
        command = [str(Path(args.engine).resolve())]
        engine_hash = hashlib.sha256(Path(command[0]).read_bytes()).hexdigest()
        timeout = max(args.timeout, (args.movetime or 1000) / 1000 + 10)
        results = []
        for position in positions:
            result, metadata = asyncio.run(run_position(
                command, position, budget, args.hash_mb, timeout, args.syzygy_path, profile))
            results.append(result)
            print(f"{position['id']:>6} {'ok' if result['solved'] else 'miss':4} "
                  f"{result['bestmove']:5} expected {','.join(position['expected_moves'])} "
                  f"depth {result.get('depth', '?')} nodes {result.get('nodes', '?')} "
                  f"time {result['elapsed_ms']:.0f}ms tbhits {result['tbhits']}", flush=True)
        solved = sum(result["solved"] for result in results)
        if (hashlib.sha256(suite.read_bytes()).hexdigest() != suite_hash
                or hashlib.sha256(Path(command[0]).read_bytes()).hexdigest() != engine_hash):
            raise ValueError("suite or engine changed during the run")
        report = {"suite": str(suite.resolve()), "suite_sha256": suite_hash,
                  "engine": command[0], "engine_sha256": engine_hash,
                  "engine_metadata": metadata,
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
