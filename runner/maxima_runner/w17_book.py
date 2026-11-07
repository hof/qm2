"""The losers opening book: our searched move in every position of our first moves, against every reply.

Breadth-first by our move number, for both colours: our turn gets one engine search, the opponent's turn
expands every legal reply, transpositions merged. Results stream into a PGN, one line per position, so a
stopped run resumes; ./maxima init merges it into book_w17.bin. A search runs at least the given seconds,
never stops before depth 1 is complete, and books the move of its last completed depth; a proven mate
stops it at once and is booked as a proof. A position with one legal move is booked without a search.
"""

import argparse
import queue
import subprocess
import threading
import time
from pathlib import Path

import chess
import chess.pgn
import chess.polyglot

from . import learning, project
from .variants import read_pgn, royal_losers_board, variant_game


def start_engine(path, hash_mb):
    engine = subprocess.Popen([str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, text=True, bufsize=1)
    send(engine, "uci", "setoption name Wild value losers", "setoption name OwnBook value false",
         "setoption name Threads value 1", f"setoption name Hash value {hash_mb}", "isready")
    for line in engine.stdout:
        if line.strip() == "readyok":
            break
    return engine


def send(engine, *commands):
    engine.stdin.write("".join(command + "\n" for command in commands))
    engine.stdin.flush()


def search(engine, fen, seconds):
    """(move uci, depth, score, pv) of the last completed depth; score is 'mate N' or 'cp N'."""
    best, stopped, lock = None, False, threading.Lock()

    def stop():
        nonlocal stopped
        with lock:
            if best is not None and not stopped:
                stopped = True
                send(engine, "stop")

    send(engine, f"position fen {fen}", "go infinite")
    timer = threading.Timer(seconds, stop)
    timer.start()
    started = time.monotonic()
    for line in engine.stdout:
        words = line.split()
        if words[:1] == ["bestmove"]:
            break
        exact = (words[:2] == ["info", "depth"] and "score" in words and "pv" in words
                 and "lowerbound" not in words and "upperbound" not in words)
        if not exact or stopped:
            continue
        at = words.index("score")
        with lock:
            best = (words[words.index("pv") + 1], int(words[2]), f"{words[at + 1]} {words[at + 2]}",
                    words[words.index("pv") + 1:])
        if (words[at + 1] == "mate" and int(words[at + 2]) > 0) or time.monotonic() - started >= seconds:
            stop()
    timer.cancel()
    return best


def load(path):
    """{zobrist key: [move uci]} of the positions already booked in path."""
    done = {}
    if Path(path).is_file():
        with open(path, encoding="utf-8") as stream:
            while (game := read_pgn(stream)) is not None:
                moves = list(game.mainline_moves())
                booked = done.setdefault(chess.polyglot.zobrist_hash(game.board()), [])
                if moves and moves[0].uci() not in booked:
                    booked.append(moves[0].uci())
    return done


def entry(board, line, colour, move, found, kind="forced"):
    """One PGN line: the position as FEN, our move, and the search that chose it (else kind: forced, choice)."""
    game = variant_game()
    for name in ("Site", "Date", "Round", "White", "Black", "Result"):
        del game.headers[name]
    game.headers["Event"] = royal_losers_board().variation_san(line) if line else "start"
    game.headers["Variant"] = "Royal losers"
    game.headers["Repertoire"] = colour
    if found is None:
        game.headers["Search"] = kind
    elif found[2].startswith("mate ") and int(found[2].split()[1]) > 0:
        game.headers["Proof"] = found[2]
    else:
        game.headers["Search"] = f"depth {found[1]} {found[2]}"
    game.setup(board)
    node = game.add_variation(chess.Move.from_uci(move))
    if found is not None:
        node.comment = " ".join(found[3][:8])
    return str(game) + "\n\n"


def replies(positions):
    """{key: (board, line)} after every legal move in positions that does not end the game."""
    level = {}
    for board, line in positions:
        for move in board.legal_moves:
            child = board.copy()
            child.push(move)
            if not child.is_game_over():
                level.setdefault(chess.polyglot.zobrist_hash(child), (child, line + [move]))
    return level


def build(output, our_moves, seconds, workers, engine_path, hash_mb, log=print, first=()):
    """Book our first our_moves moves as both colours; returns the number of new entries.

    first: white's first moves to book side by side instead of a searched one, so play can learn between them."""
    done, lock, written = load(output), threading.Lock(), 0

    def record(board, line, colour, move, found, kind="forced"):
        nonlocal written
        with lock, open(output, "a", encoding="utf-8") as stream:
            stream.write(entry(board, line, colour, move, found, kind))
            written += 1

    start = royal_losers_board()
    for move in first:
        uci = start.parse_san(move).uci()
        if uci not in done.setdefault(chess.polyglot.zobrist_hash(start), []):
            done[chess.polyglot.zobrist_hash(start)].append(uci)
            record(start, [], "white", uci, None, "choice")

    def book(colour, number, level):
        """Our move in every position of level: [(board after it, line)]."""
        tasks, moves = queue.Queue(), {}
        for key, (board, line) in level.items():
            if done.get(key):
                moves[key] = done[key]
            elif board.legal_moves.count() == 1:
                moves[key] = [next(iter(board.legal_moves)).uci()]
                record(board, line, colour, moves[key][0], None)
            else:
                tasks.put(key)
        total, searched, failures = tasks.qsize(), [0], []
        log(f"-- {colour}, our move {number}: {len(level)} positions, {total} to search")

        def work():
            engine = start_engine(engine_path, hash_mb)
            try:
                search_all(engine)
            except Exception as error:  # a failed worker must stop the run, not leave positions unbooked
                failures.append(error)
            finally:
                send(engine, "quit")
                engine.wait()
                engine.stdin.close()
                engine.stdout.close()

        def search_all(engine):
            while True:
                try:
                    key = tasks.get_nowait()
                except queue.Empty:
                    return
                board, line = level[key]
                found = search(engine, board.fen(), seconds)
                moves[key] = [found[0]]
                record(board, line, colour, found[0], found)
                with lock:
                    searched[0] += 1
                    log(f"{searched[0]:5}/{total} {royal_losers_board().variation_san(line) or 'start':48} "
                        f"{board.san(chess.Move.from_uci(found[0])):7} d{found[1]} {found[2]}")

        threads = [threading.Thread(target=work) for _ in range(min(workers, total))]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        if failures:
            raise RuntimeError(f"a book worker failed: {failures[0]!r}") from failures[0]
        after = []
        for key, (board, line) in level.items():
            for move in map(chess.Move.from_uci, moves[key]):
                child = board.copy()
                child.push(move)
                if not child.is_game_over():
                    after.append((child, line + [move]))
        return after

    for colour in ("white", "black"):
        start = royal_losers_board()
        level = {chess.polyglot.zobrist_hash(start): (start, [])}
        if colour == "black":
            level = replies([(start, [])])
        for number in range(1, our_moves + 1):
            after = book(colour, number, level)
            level = replies(after)
    return written


def export_proofs(games_dir, proofs_path, known_paths, log=print):
    """Append the proven wins of the saved games that no book PGN holds yet to proofs_path; returns how many."""
    known = {}
    for path in known_paths:
        for key, moves in load(path).items():
            known.setdefault(key, set()).update(moves)
    written = 0
    text = Path(proofs_path).read_text(encoding="utf-8") if Path(proofs_path).is_file() else ""
    with open(proofs_path, "a", encoding="utf-8") as stream:
        if text and not text.endswith("\n\n"):
            stream.write("\n" if text.endswith("\n") else "\n\n")  # a PGN game needs its blank line
        for board, move, mate in learning.games_proven_wins(games_dir):
            key = chess.polyglot.zobrist_hash(board)
            if move.uci() in known.get(key, ()):
                continue
            known.setdefault(key, set()).add(move.uci())
            colour = "white" if board.turn else "black"
            stream.write(entry(board, board.move_stack, colour, move.uci(), (move.uci(), 0, f"mate {mate}", [])))
            written += 1
            log(f"{royal_losers_board().variation_san(board.move_stack)} {board.san(move)}: mate {mate}")
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(prog="maxima books:w17", description=__doc__)
    parser.add_argument("--moves", type=int, default=2, help="our first moves to book per colour (default 2)")
    parser.add_argument("--seconds", type=float, default=60, help="minimum search per position (default 60)")
    parser.add_argument("--workers", type=int, default=4, help="engines searching side by side (default 4)")
    parser.add_argument("--hash", type=int, default=256, help="hash per engine in MB (default 256)")
    parser.add_argument("--engine", type=Path, default=None, help="engine binary (default: the release build)")
    parser.add_argument("--first", default="", help="white's first moves to book side by side, e.g. a3,h3,c4,f4")
    parser.add_argument("--export-proofs", action="store_true",
                        help="only add the proven wins of the saved games to books/w17.pgn, without searching")
    parser.add_argument("--output", type=Path, default=project.project_root() / "books/w17_search.pgn")
    options = parser.parse_args(argv)
    if options.export_proofs:
        proofs = project.project_root() / "books/w17.pgn"
        written = export_proofs(project.data_root() / "experience/losers/games", proofs, [proofs, options.output],
                                log=lambda line: print(line, flush=True))
        print(f"-- {written} proven wins added to {proofs}", flush=True)
        return 0
    engine = options.engine or project.engine_path()
    if not engine.is_file():
        raise ValueError(f"no engine at {engine}: ./maxima build")
    written = build(options.output, options.moves, options.seconds, options.workers, engine, options.hash,
                    log=lambda line: print(line, flush=True), first=[move for move in options.first.split(",") if move])
    print(f"-- {written} new entries in {options.output}; ./maxima init builds book_w17.bin", flush=True)
    return 0
