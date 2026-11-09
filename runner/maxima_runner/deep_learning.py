"""Deep book learning: search our first out-of-book positions at classical depth and book the good moves.

A position gets up to three moves: the best, then the best of the rest (UCI searchmoves without the moves
found), while each stays within the margin of the best. A singular move is booked alone. The moves go to
deep.bin beside book.bin, which the opening book reads where book.bin has no move. deep.json beside it
says how deep the book is: its weakest search in nodes and depth, so a runner on faster hardware or a
slower clock can tell whether the book still beats its own search."""
import argparse
import asyncio
import collections
import json
import time
from pathlib import Path

import chess
import chess.engine
import chess.polyglot

from . import learning, learning_store, master_books

classical_seconds = 180  # a classical move: 120 minutes for 40 moves
margin_cp = 30  # a second or third move is booked within this of the best
most_moves = 3


def deep_path(book_path):
    return Path(book_path).with_name("deep.bin")


def meta_path(book_path):
    return Path(book_path).with_name("deep.json")


def read_meta(book_path):
    try:
        return json.loads(meta_path(book_path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def note_depth(book_path, searches, seconds, engine_name):
    """Keep the book's weakest search in deep.json: [(nodes, depth)] of one position's searches."""
    meta = read_meta(book_path)
    nodes, depth = min(entry[0] for entry in searches), min(entry[1] for entry in searches)
    meta.update(nodes=min(meta.get("nodes", nodes), nodes), depth=min(meta.get("depth", depth), depth),
                seconds=min(meta.get("seconds", seconds), seconds), engine=engine_name,
                positions=meta.get("positions", 0) + 1)
    temporary = meta_path(book_path).with_suffix(".tmp")
    temporary.write_text(json.dumps(meta, indent=1) + "\n", encoding="utf-8")
    temporary.replace(meta_path(book_path))


def known_keys(path):
    """Positions deep.bin already holds."""
    return {key for key, _ in master_books.read_entries(path)} if Path(path).is_file() else set()


def first_searched(record):
    """The board of our first searched move in a standard game record, or None."""
    analyses, moves = record.get("analyses"), record.get("moves")
    if learning.learning_variant(record.get("variant", "standard")) != "standard" or not analyses or not moves:
        return None
    target, board = analyses[0][0], chess.Board()
    for key, uci in moves:
        if key == target:
            return board
        if key != f"{chess.polyglot.zobrist_hash(board):016x}":
            return None  # not from the standard start
        board.push(chess.Move.from_uci(uci))
    return None


def queue(records, known=()):
    """Positions to learn: our first searched positions not yet in deep.bin, the most frequent first,
    then those we judged worst."""
    counts, boards, scores = collections.Counter(), {}, {}
    for record in records:
        board = first_searched(record)
        if board is None:
            continue
        key = chess.polyglot.zobrist_hash(board)
        if key in known:
            continue
        counts[key] += 1
        boards[key] = board
        scores[key] = min(scores.get(key, record["analyses"][0][3]), record["analyses"][0][3])
    return [boards[key] for key in sorted(counts, key=lambda key: (-counts[key], scores[key]))]


async def good_moves(engine, board, seconds=classical_seconds, margin=margin_cp, most=most_moves, efforts=None):
    """[(move, centipawns)] best first: each further move is the best of the rest, kept within margin.
    efforts, when given, collects (nodes, depth) of each search."""
    found, legal = [], list(board.legal_moves)
    while len(found) < most:
        rest = [move for move in legal if move not in [move for move, _ in found]]
        if not rest:
            break
        info = await engine.analyse(board, chess.engine.Limit(time=seconds), root_moves=rest if found else None)
        if not info.get("pv") or "score" not in info:
            break
        move, score = info["pv"][0], info["score"].relative.score(mate_score=100000)
        if efforts is not None:
            efforts.append((info.get("nodes", 0), info.get("depth", 0)))
        if found and score < found[0][1] - margin:
            break
        found.append((move, score))
        if len(rest) == 1:
            break
    # A later, separate search may score a little above the first.
    return sorted(found, key=lambda entry: -entry[1])


def book_moves(path, board, moves, margin=margin_cp):
    """Add the position's moves to deep.bin: the best weighs 100, a move at the margin 10."""
    entries = master_books.read_entries(path) if Path(path).is_file() else {}
    key, best = chess.polyglot.zobrist_hash(board), moves[0][1]
    for move, score in moves:
        weight = 10 + 90 * (1 - (best - score) / margin) if margin else 100
        entries[(key, learning.polyglot_code(board, move))] = round(weight)
    learning.write_book(entries, path)


async def learn(engine_path, book_path, records, minutes=None, positions=None, seconds=classical_seconds,
                margin=margin_cp, report=print):
    """Learn queued positions until the time or position budget runs out; returns the positions learned."""
    path = deep_path(book_path)
    todo = queue(records, known_keys(path))[:positions]
    deadline = time.monotonic() + minutes * 60 if minutes else None
    learned = 0
    if not todo:
        return 0
    _, engine = await chess.engine.popen_uci(str(engine_path))
    try:
        await engine.configure({name: value for name, value in (("OwnBook", False),)
                                if name in engine.options})
        for board in todo:
            if deadline is not None and time.monotonic() + most_moves * seconds > deadline:
                break
            efforts = []
            moves = await good_moves(engine, board, seconds, margin, efforts=efforts)
            if moves:
                book_moves(path, board, moves, margin)
                note_depth(book_path, efforts, seconds, engine.id.get("name", "?"))
                learned += 1
                report(f"deep book: {board.fen()} -> " + ", ".join(
                    f"{board.san(move)} {score / 100:+.2f}" for move, score in moves))
    finally:
        await engine.quit()
    return learned


def main():
    from .project import data_root, engine_path
    parser = argparse.ArgumentParser(prog="maxima books:deep", description=__doc__)
    parser.add_argument("--minutes", type=float, help="stop starting positions after this long (default: all)")
    parser.add_argument("--positions", type=int, help="learn at most this many positions")
    parser.add_argument("--seconds", type=float, default=classical_seconds,
                        help=f"search time per move found (default: {classical_seconds})")
    parser.add_argument("--margin", type=int, default=margin_cp,
                        help=f"book further moves within this many centipawns (default: {margin_cp})")
    options = parser.parse_args()
    records = learning_store.shared_store(data_root() / "experience", "standard").load()
    print(f"{len(queue(records, known_keys(deep_path(data_root() / 'book.bin'))))} positions to learn")
    learned = asyncio.run(learn(engine_path(), data_root() / "book.bin", records, options.minutes,
                                options.positions, options.seconds, options.margin))
    print(f"{learned} positions learned into {deep_path(data_root() / 'book.bin')}")
    return 0
