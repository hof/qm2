"""Opening learning from MaxDrone's PGN games (MAXIMA.md 9.7): digests, player stats, and the multi-layer book."""
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
index_version = 2
eval_window = 4
result_share = 0.2
game_types = ("lightning", "blitz", "standard")


def expected_score(centipawns):
    """Win expectation of an eval: +1.00 is about 64%, +3.00 about 85%."""
    return 1 / (1 + 10 ** (-max(-2000, min(2000, centipawns)) / 400))


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
    """One game's learning record, or None when it is not one of our games.

    Every opening move gets two values from the mover's point of view: Maxima's eval just after it
    (the mean expected score of our next eval_window searched moves) and the game result."""
    headers = game.headers
    ours = [color for color, prefix in ((chess.WHITE, "White"), (chess.BLACK, "Black"))
            if headers.get(f"{prefix}Type") == "program" and headers.get(f"{prefix}Engine")]
    if len(ours) != 1 or game.errors or type(game.board()) is not chess.Board:
        return None
    our_color = ours[0]
    their_prefix = "Black" if our_color == chess.WHITE else "White"
    board = game.board()
    moves, evals, analyses = [], [], []
    for ply, node in enumerate(game.mainline()):
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        mover = board.turn
        moves.append([key, node.move.uci(), mover == our_color])
        evaluation, depth = node.eval(), node.eval_depth()
        if mover == our_color and evaluation is not None and depth:
            relative = evaluation.pov(mover)
            centipawns = relative.score(mate_score=2000)
            evals.append((ply, expected_score(centipawns)))
            if ply < horizon_plies:
                analyses.append([key, node.move.uci(), depth, centipawns, int(relative.is_mate()),
                                 round((node.emt() or 0) * 1000), board.halfmove_clock])
        board.push(node.move)
    outcome = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}.get(headers.get("Result"))
    score = None if outcome is None else (outcome if our_color == chess.WHITE else 1 - outcome)
    values = []
    for ply, (key, move, our_move) in enumerate(moves[:horizon_plies]):
        after = [value for at, value in evals if at >= ply][:eval_window]
        evaluated = round(sum(after) / len(after), 4) if after else None
        result = score
        if not our_move:
            evaluated = None if evaluated is None else round(1 - evaluated, 4)
            result = None if score is None else 1 - score
        if evaluated is not None or result is not None:
            values.append([key, move, evaluated, result])
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
        "score": score,
        "moves": [move[:2] for move in moves[:horizon_plies]],
        "values": values, "analyses": analyses,
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
            "evaluated": sum(any(value[2] is not None for value in record["values"]) for record in records),
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
    """A Polyglot book of our side's moves: each repertoire line adds weight 100 to its moves.

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
                    weights[entry] = weights.get(entry, 0) + 100
                board.push(node.move)
    return write_book(weights, book_path)


def write_book(weights, book_path):
    """Write {(zobrist key, polyglot move code): weight} as a sorted Polyglot book; returns the entries."""
    temporary = Path(book_path).with_suffix(".tmp")
    with open(temporary, "wb") as stream:
        for (key, code), weight in sorted(weights.items()):
            stream.write(struct.pack(">QHHI", key, code, max(1, min(int(weight), 0xffff)), 0))
    temporary.replace(book_path)
    return len(weights)


# Multi-layer book profiles (MAXIMA.md 9.7): percentages per layer; "search" is the share of games
# that leave the book once, at a random ply, so the engine can bring in new moves.
style_names = ("fire", "draw", "experiment")
layer_names = ("book",) + style_names + ("global", "group", "opponent")
layer_profiles = {
    "equal": {"book": 45, "fire": 0, "draw": 0, "experiment": 0, "global": 20, "group": 10, "opponent": 25,
              "search": 5},
    "weaker": {"book": 20, "fire": 25, "draw": 0, "experiment": 0, "global": 10, "group": 10, "opponent": 35,
               "search": 5},
    "stronger": {"book": 30, "fire": 0, "draw": 25, "experiment": 0, "global": 10, "group": 10, "opponent": 25,
                 "search": 5},
    "unrated": {"book": 30, "fire": 30, "draw": 0, "experiment": 15, "global": 25, "group": 0, "opponent": 0,
                "search": 10},
}


def style_share(contempt):
    """How far a rated game leans to the weaker or stronger profile: none within ±0.20, all at ±1.00."""
    if contempt is None:
        return 0.0
    return max(0.0, min(1.0, (abs(contempt) - 20) / 80))


def game_profile(contempt, rated, profiles=None):
    """Layer percentages for one game: unrated games (guests) use the unrated profile; rated games slide
    from equal toward weaker (fire) or stronger (draw) with the draw contempt."""
    profiles = profiles or layer_profiles
    if not rated:
        return dict(profiles["unrated"])
    side = "weaker" if contempt is not None and contempt < 0 else "stronger"
    share = style_share(contempt)
    return {name: (1 - share) * profiles["equal"].get(name, 0) + share * profiles[side].get(name, 0)
            for name in profiles["equal"]}


def learned_factor(rate):
    """The engine's experience factor: x1 at 50%, about x2.6 at 65%, x0.47 at 40%, never below x0.1."""
    return 0.1 + 0.9 * (2 * rate) ** 4


def per_move_budget_ms(clock_seconds, increment_seconds):
    """Roughly what the engine spends on a move: a thirtieth of the clock plus the increment."""
    return max(0.0, clock_seconds / 30 + increment_seconds) * 1000


class opening_book:
    """One game's multi-layer book (MAXIMA.md 9.7): book.bin, the style books fire, draw, and experiment,
    and the learned layers global, group, and opponent, each with a percentage from the game's profile."""

    def __init__(self, records, opponent=None, group=None, game_type=None, book_path=None, prior=8.0,
                 variety=1.0, supersede_depth=16, random_source=None, contempt=None, style_books=None,
                 rated=True, profiles=None):
        self.book_path = book_path
        self.random = random_source or random.Random()
        self.weights = game_profile(contempt, rated, profiles)
        self.paths = {"book": book_path}
        self.paths.update({name: path for name, path in (style_books or {}).items()
                           if name in style_names and self.weights.get(name) and path and Path(path).is_file()})
        self.names = dict({name: name for name in layer_names}, group=group or "group",
                          opponent=opponent or "opponent")
        # The search layer: some games leave the book once, at a random ply of ours.
        self.search_ply = None
        if self.random.random() * 100 < self.weights.get("search", 0):
            self.search_ply = self.random.randrange(horizon_plies)
        # JDrone's draw factor, tied to DrawContempt, for the result part: a draw is worth up to 0.95
        # against much stronger opponents and down to 0.05 against much weaker ones.
        self.draw_value = 0.5 if contempt is None else 0.5 + 0.45 * max(-1.0, min(1.0, contempt / 100))
        self.prior = prior
        self.variety = max(variety, 0.05)
        self.supersede_depth = supersede_depth
        self.layers = ({}, {}, {})  # global, group, opponent: (key, move) -> [value sum, games]
        self.totals = ({}, {}, {})  # the same per position: key -> games
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
            for key, move, evaluated, result in record["values"]:
                value = self.value(evaluated, result)
                for layer, layer_weight in layers:
                    entry = self.layers[layer].setdefault((key, move), [0.0, 0.0])
                    entry[0] += layer_weight * value
                    entry[1] += layer_weight
                    self.totals[layer][key] = self.totals[layer].get(key, 0.0) + layer_weight
            for key, move, depth, centipawns, mate, think_ms, clock in record["analyses"]:
                moves = self.analyses.setdefault(key, {})
                known = moves.get(move)
                if known is None or (think_ms, depth) > (known[3], known[0]):
                    moves[move] = [depth, centipawns, mate, think_ms, clock]

    def value(self, evaluated, result):
        """Maxima's eval is the main signal; the result adds what evals miss, such as time trouble."""
        if result == 0.5:
            result = self.draw_value
        if evaluated is None:
            return result
        if result is None:
            return evaluated
        return (1 - result_share) * evaluated + result_share * result

    def rates(self, key, move):
        """Per learned layer: (rate shrunk toward the layer below, games); global shrinks toward 50%."""
        rate, rates = 0.5, []
        for layer in self.layers:
            total, games = layer.get((key, move), (0.0, 0.0))
            rate = (total + self.prior * rate) / (games + self.prior)
            rates.append((rate, games))
        return rates

    def estimate(self, key, move):
        """Deepest shrunk rate and the games behind the move overall."""
        rates = self.rates(key, move)
        return rates[-1][0], rates[0][1]

    @staticmethod
    def read_book(path, board):
        """Move shares (summing to 1) from a Polyglot book without its rare moves (under a tenth of the
        heaviest) and without weights 0 and 1, which books use for known moves not to play; empty when
        absent or unreadable."""
        if not path:
            return {}
        weights = {}
        try:
            with chess.polyglot.open_reader(str(path)) as reader:
                for entry in reader.find_all(board, minimum_weight=2):
                    weights[entry.move] = weights.get(entry.move, 0) + entry.weight
        except (OSError, ValueError, IndexError):
            return {}
        heaviest = max(weights.values(), default=0)
        weights = {move: weight for move, weight in weights.items() if weight and weight >= heaviest / 10}
        total = sum(weights.values())
        return {move: weight / total for move, weight in weights.items()} if total else {}

    def superseded(self, analyses, move):
        """A clearly deeper search in this position preferred another move."""
        own = analyses.get(move.uci())
        depth = own[0] if own else 0
        return any(other != move.uci() and analysis[0] >= max(self.supersede_depth, depth + 2)
                   for other, analysis in analyses.items())

    def sample(self, rate, games):
        strength = (games + self.prior) / self.variety
        return self.random.betavariate(max(rate * strength, 1e-3), max((1 - rate) * strength, 1e-3))

    def distribution(self, board, key, analyses):
        """Each move's probability and the share each layer gave it, whether any book knew the position,
        and the chance to search instead: book.bin is the know-all base, so where it has no move, its
        percentage goes to the engine rather than to the other layers."""
        books = {name: self.read_book(path, board) for name, path in self.paths.items()}
        books = {name: moves for name, moves in books.items() if moves}
        known = set().union(*books.values()) if books else set()
        learned = {chess.Move.from_uci(move) for (position, move) in self.layers[0] if position == key}
        candidates = {move for move in known | learned
                      if board.is_legal(move) and not self.superseded(analyses, move)}
        if not candidates:
            return {}, False, 0.0
        in_book = bool(candidates & known)
        # The base the learned layers reweight: the books' mix, plus a share for moves only experience
        # knows (the opponent's good moves, engine novelties) that grows with their games.
        base = {}
        for move in candidates:
            share = sum(self.weights.get(name, 0) * moves.get(move, 0) for name, moves in books.items())
            games = self.layers[0].get((key, move.uci()), (0.0, 0.0))[1]
            base[move] = share / 100 if in_book else 1.0
            if in_book and move not in known:
                base[move] = 0.1 * games / (games + self.prior)
        total = sum(base.values())
        base = {move: weight / total for move, weight in base.items()}
        rates = {move: self.rates(key, move.uci()) for move in candidates}
        layers = dict(books)
        weights = {name: self.weights.get(name, 0) for name in layers}
        for index, name in enumerate(("global", "group", "opponent")):
            games = self.totals[index].get(key, 0.0)
            if not games or not self.weights.get(name):
                continue
            weighted = {move: base[move] * learned_factor(self.sample(*rates[move][index])) for move in candidates}
            total = sum(weighted.values())
            layers[name] = {move: weight / total for move, weight in weighted.items()}
            weights[name] = self.weights[name] * games / (games + self.prior)
        # Proven losers drop in every layer, book.bin included.
        damp = {move: 0.1 if rates[move][0][1] >= 4 and rates[move][-1][0] < 0.3 else 1.0 for move in candidates}
        shares = {move: {name: weights[name] * layers[name].get(move, 0) * damp[move] for name in layers}
                  for move in candidates}
        search = 0.0 if "book" in books else self.weights.get("book", 0)
        total = sum(sum(parts.values()) for parts in shares.values())
        if not total:
            return {}, in_book, 0.0
        total += search
        return ({move: (sum(parts.values()) / total, {name: part / total for name, part in parts.items()})
                 for move, parts in shares.items()}, in_book, search / total)

    def describe(self, probability, parts):
        """Label first (book or learned), then each layer's share of the choice, for tuning."""
        learned = sum(parts.get(name, 0) for name in ("global", "group", "opponent"))
        label = "learned" if learned > sum(parts.get(name, 0) for name in ("book",) + style_names) else "book"
        detail = ", ".join(f"{self.names[name]} {parts[name] / probability:.0%}" for name in layer_names
                           if parts.get(name, 0) >= 0.005 * probability)
        return f"{label} {probability:.0%}: {detail}"

    def choose(self, board, budget_ms):
        """(move, reason) to play without searching, or None to let the engine search."""
        if (type(board) is not chess.Board or board.ply() >= horizon_plies or board.halfmove_clock >= 90
                or board.is_repetition(2)):
            return None
        if self.search_ply is not None and board.ply() >= self.search_ply:
            self.search_ply = None
            return None
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        analyses = self.analyses.get(key, {})
        moves, in_book, search = self.distribution(board, key, analyses)
        if moves:
            ordered = sorted(moves, key=chess.Move.uci)
            move = self.random.choices(ordered + [None], [moves[move][0] for move in ordered] + [search])[0]
            if move is None:
                return None
            rate, games = self.estimate(key, move.uci())
            analysis = analyses.get(move.uci())
            # Out of book a learned move must have real evidence: several games scoring well, or an
            # analysis that took at least this game's budget.
            if in_book or (games >= 2 and rate >= 0.5) or (analysis and analysis[3] >= budget_ms):
                return move, self.describe(*moves[move])
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
                 profiles=None):
        self.experience_dir = Path(experience_dir)
        self.style_books = style_books
        self.profiles = profiles
        self.games_dir = Path(games_dir or self.experience_dir / "games" / "fics")
        self.book_path = book_path
        self.variety = variety
        self.records = load_index(self.games_dir, self.experience_dir / "index")

    def for_game(self, opponent, group, game_type, contempt=None, rated=True):
        return opening_book(self.records, opponent, group, game_type, self.book_path, variety=self.variety,
                            contempt=contempt, style_books=self.style_books, rated=rated, profiles=self.profiles)

    def add(self, game):
        record = digest(game)
        if record is not None:
            self.records.append(record)
        return record

    def refresh(self):
        """Index cache and player stats on disk; run off the event loop."""
        return build(self.experience_dir, self.games_dir)


def main():
    from .project import data_root
    parser = argparse.ArgumentParser(prog="maxima experience:build", description=__doc__)
    parser.add_argument("--experience-dir", default=str(data_root() / "experience"),
                        help="reads games/fics/*.pgn, writes index/ and players/ (default: experience)")
    options = parser.parse_args()
    summary = build(options.experience_dir)
    print(f"{summary['games']} games, {summary['evaluated']} with Maxima's evals, "
          f"{summary['players']} opponents")
    return 0


def repertoire_main():
    from .project import data_root, project_root
    parser = argparse.ArgumentParser(
        prog="maxima experience:repertoire",
        description="Build the style books of the multi-layer book (MAXIMA.md 9.7) from repertoire PGNs.")
    parser.add_argument("--books-dir", default=str(project_root() / "books"),
                        help="reads <style>.pgn (default: books/)")
    parser.add_argument("--output-dir", default=str(data_root()),
                        help="writes <style>.bin beside book.bin (default: the data directory)")
    parser.add_argument("--plies", type=int, default=30, help="moves per line to include (default: 30)")
    options = parser.parse_args()
    built = 0
    for style in ("fire", "draw", "experiment"):
        source = Path(options.books_dir) / f"{style}.pgn"
        if not source.is_file():
            continue
        target = Path(options.output_dir) / f"{style}.bin"
        print(f"{target}: {build_repertoire(source, target, options.plies)} entries from {source}")
        built += 1
    if not built:
        parser.error(f"no fire.pgn, draw.pgn or experiment.pgn in {options.books_dir}")
    return 0
