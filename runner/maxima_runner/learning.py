"""Opening learning from MaxDrone's PGN games (MAXIMA.md 9.7): per-game digests and player stats."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import random
import re
import struct

import chess
import chess.pgn
import chess.polyglot

horizon_plies = 40
index_version = 1
game_types = ("lightning", "blitz", "standard")


def bucket(centipawns):
    """JDrone's score bands from the mover's point of view: -3 (lost) to 3 (won)."""
    for limit, value in ((300, 3), (150, 2), (50, 1)):
        if centipawns >= limit:
            return value
    for limit, value in ((-50, 0), (-150, -1), (-300, -2)):
        if centipawns > limit:
            return value
    return -3


def find_decision(scores, plies):
    """Where Maxima's evals settled: (result, last credited ply, final band), or (None, None, None).

    scores holds (ply, band) for our searched moves in order; plies is the game length.
    Ported from JDrone's LearnBook.findResultChain.
    """
    draw_chain = win_chain = loss_chain = 0
    previous, score_drop = 3, False
    for ply, band in scores:
        score_drop = band + 1 < previous
        if band == -3:
            win_chain, draw_chain, loss_chain = 0, 0, loss_chain + 3
        elif band == -2:
            win_chain, draw_chain, loss_chain = 0, 0, loss_chain + 1
        elif band == -1:
            win_chain, draw_chain, loss_chain = 0, draw_chain + 1, loss_chain + 1
        elif band in (0, 1):
            win_chain, draw_chain, loss_chain = 0, draw_chain + 1, 0
        else:
            win_chain, draw_chain, loss_chain = win_chain + 1, 0, 0
        previous = band
        near_end = ply >= plies - 2
        if draw_chain == 0 and win_chain == 0 and (loss_chain >= 6 or (loss_chain >= 2 and near_end)):
            return "loss", ply - 1, band
        if loss_chain == 0 and win_chain == 0 and (
                (band == 0 and draw_chain >= 12) or (draw_chain > 0 and near_end and ply >= 12)):
            return "draw", ply - 6, band
        if win_chain > 0 and loss_chain == 0 and draw_chain == 0 and near_end and not score_drop:
            return "win", ply, band
    return None, None, None


def game_type(event):
    words = event.lower().split()
    return next((word for word in words if word in game_types), None)


def opponent_group(headers, color):
    """engines (C account), masters (titled), humans; None for unregistered logins."""
    prefix = "White" if color == chess.WHITE else "Black"
    if headers.get(f"{prefix}Account") == "unregistered":
        return None
    if headers.get(f"{prefix}Type") == "program":
        return "engines"
    if headers.get(f"{prefix}Title"):
        return "masters"
    return "humans"


def digest(game):
    """One game's learning record, or None when it is not one of our games."""
    headers = game.headers
    ours = [color for color, prefix in ((chess.WHITE, "White"), (chess.BLACK, "Black"))
            if headers.get(f"{prefix}Type") == "program" and headers.get(f"{prefix}Engine")]
    if len(ours) != 1 or game.errors or type(game.board()) is not chess.Board:
        return None
    our_color = ours[0]
    their_prefix = "Black" if our_color == chess.WHITE else "White"
    board = game.board()
    moves, scores, analyses = [], [], []
    for ply, node in enumerate(game.mainline()):
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        mover = board.turn
        moves.append([key, node.move.uci(), mover == our_color])
        evaluation, depth = node.eval(), node.eval_depth()
        if mover == our_color and evaluation is not None and depth:
            relative = evaluation.pov(mover)
            centipawns = relative.score(mate_score=100000)
            scores.append((ply, bucket(centipawns)))
            if ply < horizon_plies:
                analyses.append([key, node.move.uci(), depth, centipawns, int(relative.is_mate()),
                                 round((node.emt() or 0) * 1000), board.halfmove_clock])
        board.push(node.move)
    result, end, final = find_decision(scores, len(moves))
    credited = []
    if result is not None:
        bands = dict(scores)
        strong = 0
        for ply, (key, move, our_move) in enumerate(moves[:min(end + 1, horizon_plies)]):
            if final >= 3 and bands.get(ply, -9) >= 3:
                strong += 1
                if strong > 2:
                    continue  # easy wins: later moves of the game get no credit
            if result == "draw":
                credited.append([key, move, 0.5])
            elif result == "loss":
                credited.append([key, move, 0.0 if our_move else 1.0])
            elif our_move:
                credited.append([key, move, 1.0])
    outcome = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}.get(headers.get("Result"))
    rating = headers.get(f"{their_prefix}Elo", "")
    return {
        "date": headers.get("UTCDate", headers.get("Date", "")),
        "time": headers.get("UTCTime", ""),
        "white": headers.get("White", ""), "black": headers.get("Black", ""),
        "color": "white" if our_color == chess.WHITE else "black",
        "opponent": headers.get(their_prefix, ""),
        "group": opponent_group(headers, not our_color),
        "title": headers.get(f"{their_prefix}Title") or None,
        "rating": int(rating) if rating.isdigit() else None,
        "type": game_type(headers.get("Event", "")),
        "time_control": headers.get("TimeControl", ""),
        "score": None if outcome is None else (outcome if our_color == chess.WHITE else 1 - outcome),
        "decision": result, "decided_at": end,
        "moves": [move[:2] for move in moves[:horizon_plies]],
        "credited": credited, "analyses": analyses,
    }


def digest_file(path):
    with open(path, encoding="utf-8-sig") as stream:
        while True:
            game = chess.pgn.read_game(stream)
            if game is None:
                return
            record = digest(game)
            if record is not None:
                yield record


def load_index(games_dir, index_dir):
    """Digests of every game; a PGN file is re-read only when its size or mtime changed."""
    index_dir.mkdir(parents=True, exist_ok=True)
    records = []
    for path in sorted(Path(games_dir).glob("*.pgn")):
        stat = path.stat()
        cache = index_dir / f"{path.stem}.json"
        stamp = {"version": index_version, "size": stat.st_size, "mtime_ns": stat.st_mtime_ns}
        cached = None
        if cache.is_file():
            try:
                cached = json.loads(cache.read_text(encoding="utf-8"))
            except ValueError:
                cached = None
        if cached is None or cached.get("stamp") != stamp:
            cached = {"stamp": stamp, "games": list(digest_file(path))}
            temporary = cache.with_suffix(".tmp")
            temporary.write_text(json.dumps(cached), encoding="utf-8")
            temporary.replace(cache)
        records.extend(cached["games"])
    return records


def player_stats(records):
    """Readable per-opponent stats from the actual results."""
    players = {}
    for record in records:
        name = record["opponent"]
        if not name:
            continue
        stats = players.setdefault(name.lower(), {
            "handle": name, "games": 0, "wins": 0, "draws": 0, "losses": 0, "by_type": {},
            "group": None, "title": None, "rating": None, "last_game": ""})
        if record["score"] is None:
            continue
        outcome = {1.0: "wins", 0.5: "draws", 0.0: "losses"}[record["score"]]
        stats["games"] += 1
        stats[outcome] += 1
        kind = stats["by_type"].setdefault(record["type"] or "other", {"games": 0, "wins": 0, "draws": 0,
                                                                        "losses": 0})
        kind["games"] += 1
        kind[outcome] += 1
        stamp = f"{record['date']} {record['time']}"
        if stamp >= stats["last_game"]:
            stats.update(last_game=stamp, group=record["group"], title=record["title"],
                         rating=record["rating"] or stats["rating"], handle=name)
    return players


def write_player_stats(players, directory):
    directory.mkdir(parents=True, exist_ok=True)
    for key, stats in players.items():
        if not re.fullmatch(r"[a-z]{1,20}", key):
            continue
        path = directory / f"{key}.json"
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(stats, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(path)
    return len(players)


def build(experience_dir, games_dir=None):
    """Refresh the index cache and player stats from experience/games/fics."""
    experience_dir = Path(experience_dir)
    records = load_index(games_dir or experience_dir / "games" / "fics", experience_dir / "index")
    count = write_player_stats(player_stats(records), experience_dir / "players")
    return {"games": len(records), "players": count,
            "decided": sum(record["decision"] is not None for record in records),
            "built": datetime.now().isoformat(timespec="seconds")}


def polyglot_code(board, move):
    """Polyglot move encoding; castling is written as the king taking its own rook."""
    target = move.to_square
    if board.is_castling(move):
        target = chess.square(7 if chess.square_file(move.to_square) > 4 else 0, chess.square_rank(move.to_square))
    promotion = {chess.KNIGHT: 1, chess.BISHOP: 2, chess.ROOK: 3, chess.QUEEN: 4}.get(move.promotion, 0)
    return (chess.square_file(target) | chess.square_rank(target) << 3 | chess.square_file(move.from_square) << 6
            | chess.square_rank(move.from_square) << 9 | promotion << 12)


def build_repertoire(pgn_path, book_path, plies=30):
    """A Polyglot book of our side's moves: each repertoire line adds weight 1 to its moves.

    Games carry [Repertoire "white"] or [Repertoire "black"]; opponent moves are only the path."""
    weights = {}
    with open(pgn_path, encoding="utf-8-sig") as stream:
        while True:
            game = chess.pgn.read_game(stream)
            if game is None:
                break
            side = game.headers.get("Repertoire")
            if game.errors or side not in ("white", "black"):
                raise ValueError(f"{pgn_path}: bad repertoire line {game.headers.get('Event', '?')!r}")
            ours = chess.WHITE if side == "white" else chess.BLACK
            board = game.board()
            for node in list(game.mainline())[:plies]:
                if board.turn == ours:
                    entry = (chess.polyglot.zobrist_hash(board), polyglot_code(board, node.move))
                    weights[entry] = weights.get(entry, 0) + 1
                board.push(node.move)
    temporary = Path(book_path).with_suffix(".tmp")
    with open(temporary, "wb") as stream:
        for (key, code), weight in sorted(weights.items()):
            stream.write(struct.pack(">QHHI", key, code, min(weight, 0xffff), 0))
    temporary.replace(book_path)
    return len(weights)


def style_share(contempt):
    """How much of the style book to blend in: none within ±0.20, all at ±1.00."""
    if contempt is None:
        return 0.0
    return max(0.0, min(1.0, (abs(contempt) - 20) / 80))


def choose_style(contempt, style_books, unrated_share=0.5):
    """(style, share) for this game's multi-layer book.

    Unrated opponents (guests) are for experiments: experiment.bin, else fire.bin, at unrated_share.
    Rated ones follow the draw contempt: fire.bin against weaker, solid.bin against stronger."""
    available = {style for style, path in (style_books or {}).items() if path and Path(path).is_file()}
    if contempt is None:
        style = next((style for style in ("experiment", "fire") if style in available), None)
        return style, unrated_share if style else 0.0
    style = "fire" if contempt < 0 else "solid"
    return style, style_share(contempt) if style in available else 0.0


def per_move_budget_ms(clock_seconds, increment_seconds):
    """Roughly what the engine spends on a move: a thirtieth of the clock plus the increment."""
    return max(0.0, clock_seconds / 30 + increment_seconds) * 1000


class opening_book:
    """One game's view of the experience: layered results, analyses, and book.bin (MAXIMA.md 9.7)."""

    def __init__(self, records, opponent=None, group=None, game_type=None, book_path=None, prior=8.0,
                 variety=1.0, supersede_depth=16, random_source=None, contempt=None, style_books=None,
                 unrated_share=0.5):
        self.book_path = book_path
        # Multi-layer book: a style book blended over book.bin (MAXIMA.md 9.7).
        self.style, self.style_share = choose_style(contempt, style_books, unrated_share)
        self.style_path = (style_books or {}).get(self.style)
        # JDrone's draw factor, tied to DrawContempt: a draw is worth up to 0.95 against much stronger
        # opponents and down to 0.05 against much weaker ones.
        self.draw_value = 0.5 if contempt is None else 0.5 + 0.45 * max(-1.0, min(1.0, contempt / 100))
        self.prior = prior
        self.variety = max(variety, 0.05)
        self.supersede_depth = supersede_depth
        self.random = random_source or random.Random()
        self.layers = ({}, {}, {})  # global, group, opponent: (key, move) -> [wins, draws, games]
        self.analyses = {}  # key -> {move: [depth, centipawns, mate, think_ms, halfmove clock]}
        opponent = (opponent or "").lower()
        for record in records:
            weight = 1.0 if record["type"] == game_type else 0.5
            registered = record["group"] is not None
            layers = [(0, weight if registered else weight / 2)]
            if registered and group is not None and record["group"] == group:
                layers.append((1, weight))
            if registered and opponent and record["opponent"].lower() == opponent:
                layers.append((2, weight))
            for key, move, value in record["credited"]:
                for layer, layer_weight in layers:
                    entry = self.layers[layer].setdefault((key, move), [0.0, 0.0, 0.0])
                    entry[0] += layer_weight * (value == 1.0)
                    entry[1] += layer_weight * (value == 0.5)
                    entry[2] += layer_weight
            for key, move, depth, centipawns, mate, think_ms, clock in record["analyses"]:
                moves = self.analyses.setdefault(key, {})
                known = moves.get(move)
                if known is None or (think_ms, depth) > (known[3], known[0]):
                    moves[move] = [depth, centipawns, mate, think_ms, clock]

    def estimate(self, key, move):
        """Score rate shrunk global -> group -> opponent, its posterior strength, and the games behind it."""
        rate, games = 0.5, 0.0
        for layer in self.layers:
            wins, draws, count = layer.get((key, move), (0.0, 0.0, 0.0))
            rate = (wins + self.draw_value * draws + self.prior * rate) / (count + self.prior)
            games = count if count else games
        return rate, self.prior + games, self.layers[0].get((key, move), (0.0, 0.0, 0.0))[2]

    @staticmethod
    def read_book(path, board):
        """Move shares (summing to 1) from a Polyglot book; empty when absent or unreadable."""
        if not path:
            return {}
        weights = {}
        try:
            with chess.polyglot.open_reader(str(path)) as reader:
                for entry in reader.find_all(board):
                    weights[entry.move] = weights.get(entry.move, 0) + entry.weight
        except (OSError, ValueError, IndexError):
            return {}
        total = sum(weights.values())
        return {move: weight / total for move, weight in weights.items()} if total else {}

    def book_moves(self, board):
        main = self.read_book(self.book_path, board)
        style = self.read_book(self.style_path, board) if self.style_share > 0 else {}
        if not style:
            return main
        share = self.style_share if main else 1.0
        return {move: (1 - share) * main.get(move, 0) + share * style.get(move, 0) for move in set(main) | set(style)}

    def superseded(self, analyses, move):
        """A clearly deeper search in this position preferred another move."""
        own = analyses.get(move.uci())
        depth = own[0] if own else 0
        return any(other != move.uci() and analysis[0] >= max(self.supersede_depth, depth + 2)
                   for other, analysis in analyses.items())

    def choose(self, board, budget_ms):
        """(move, reason) to play without searching, or None to let the engine search."""
        if (type(board) is not chess.Board or board.ply() >= horizon_plies or board.halfmove_clock >= 90
                or board.is_repetition(2)):
            return None
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        analyses = self.analyses.get(key, {})
        book = {move: weight for move, weight in self.book_moves(board).items() if board.is_legal(move)}
        heaviest = max(book.values(), default=0)
        candidates = {move for move, weight in book.items() if weight >= heaviest / 10}
        candidates |= {chess.Move.from_uci(move) for (position, move) in self.layers[0] if position == key}
        candidates = [move for move in candidates if board.is_legal(move) and not self.superseded(analyses, move)]
        evidence = {move: self.estimate(key, move.uci()) for move in candidates}
        if any(games for _, _, games in evidence.values()):
            samples = {}
            for move, (rate, strength, _) in evidence.items():
                alpha = max(rate * strength / self.variety, 1e-3)
                beta = max((1 - rate) * strength / self.variety, 1e-3)
                samples[move] = self.random.betavariate(alpha, beta)
            move = max(sorted(samples, key=chess.Move.uci), key=samples.get)
            rate, _, games = evidence[move]
            if samples[move] >= 0.5 or move in book:
                return move, f"learned {rate:.0%} of {games:g}"
        weighted = sorted((move for move in candidates if move in book), key=chess.Move.uci)
        if weighted:
            move = self.random.choices(weighted, [book[move] for move in weighted])[0]
            style = self.read_book(self.style_path, board) if self.style_share > 0 else {}
            return move, f"book {self.style} {self.style_share:.0%}" if move in style else "book"
        if analyses:
            text, (depth, centipawns, mate, think_ms, clock) = max(analyses.items(),
                                                                   key=lambda item: (item[1][3], item[1][0]))
            move = chess.Move.from_uci(text)
            if think_ms >= budget_ms and clock == board.halfmove_clock and board.is_legal(move):
                return move, f"recalled d{depth} {think_ms / 1000:.1f}s"
        return None


class learner:
    """MaxDrone's experience: digests kept in memory, a fresh opening_book per game."""

    def __init__(self, experience_dir, games_dir=None, book_path=None, variety=1.0, style_books=None,
                 unrated_share=0.5):
        self.experience_dir = Path(experience_dir)
        self.style_books = style_books
        self.unrated_share = unrated_share
        self.games_dir = Path(games_dir or self.experience_dir / "games" / "fics")
        self.book_path = book_path
        self.variety = variety
        self.records = load_index(self.games_dir, self.experience_dir / "index")

    def for_game(self, opponent, group, game_type, contempt=None):
        return opening_book(self.records, opponent, group, game_type, self.book_path, variety=self.variety,
                            contempt=contempt, style_books=self.style_books, unrated_share=self.unrated_share)

    def add(self, game):
        record = digest(game)
        if record is not None:
            self.records.append(record)
        return record

    def refresh(self):
        """Index cache and player stats on disk; run off the event loop."""
        return build(self.experience_dir, self.games_dir)


def main():
    from .project import project_root
    parser = argparse.ArgumentParser(prog="maxima experience:build", description=__doc__)
    parser.add_argument("--experience-dir", default=str(project_root() / "experience"),
                        help="reads games/fics/*.pgn, writes index/ and players/ (default: experience)")
    options = parser.parse_args()
    summary = build(options.experience_dir)
    print(f"{summary['games']} games, {summary['decided']} with a detected decision, "
          f"{summary['players']} opponents")
    return 0


def repertoire_main():
    from .project import project_root
    parser = argparse.ArgumentParser(
        prog="maxima experience:repertoire",
        description="Build the style books of the multi-layer book (MAXIMA.md 9.7) from repertoire PGNs.")
    parser.add_argument("--books-dir", default=str(project_root() / "books"),
                        help="reads <style>.pgn (default: books/)")
    parser.add_argument("--output-dir", default=str(project_root()),
                        help="writes <style>.bin beside book.bin (default: project root)")
    parser.add_argument("--plies", type=int, default=30, help="moves per line to include (default: 30)")
    options = parser.parse_args()
    built = 0
    for style in ("fire", "solid", "experiment"):
        source = Path(options.books_dir) / f"{style}.pgn"
        if not source.is_file():
            continue
        target = Path(options.output_dir) / f"{style}.bin"
        print(f"{target}: {build_repertoire(source, target, options.plies)} entries from {source}")
        built += 1
    if not built:
        parser.error(f"no fire.pgn, solid.pgn or experiment.pgn in {options.books_dir}")
    return 0
