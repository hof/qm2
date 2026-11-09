"""Opening learning from MaxDrone's PGN games (MAXIMA.md 9.2): digests, player stats, and the multi-layer book."""
import argparse
import collections
from datetime import datetime
import json
from pathlib import Path
import random
import re
import statistics
import struct
from time import monotonic

import chess
import chess.pgn
import chess.polyglot

from . import learning_store, online_tablebase
from .search_effort import analysis_rank, effort
from .variants import board_variant, experience_variants, learning_variant, read_pgn

search_plies = 40  # the search layer leaves the book at a random ply of the first 20 moves
# A game teaches only when it lasted 10 moves and Maxima searched enough moves to judge them: early
# resignations, aborts, disconnects, and book-only games carry no eval, only a result.
minimum_plies = 20
# Learning ends, past the opening's decided_plies, at our first eval this far from level: the game is won or lost.
decided_cp, decided_plies = 500, 40
eval_window = 4
drawn_evals = 3  # of those evals at the draw score: a drawn line, judged as a draw rather than a poor eval
result_share = 0.2
# Signature lines: after this many convincing wins in a row against one opponent with a move, it gets
# this share of the choice there; a win is convincing when our eval after the move stayed this good.
signature_wins = 3
signature_share = 0.7
signature_eval = 0.45
lost_damp = 0.03  # weight of a move this opponent has only beaten
revenge_plies = 24  # the opening of a lost game whose moves we play back at the opponent
mood_hours = 24  # how long one mood lasts in a running session
# A remembered move: an earlier search of this exact position. Clock/30 + increment overstates a move's
# real spend (archive median 0.66), and the saved search must beat that spend by this margin.
real_spend = 2 / 3
recall_margin = 1.5
recent_speeds = 200  # searches behind the speed estimate before the session has its own
older_contempt = -10  # the equal-rating value before games stored their DrawContempt tag; unknown ratings still use it
equal_contempt = 0  # the draw contempt between equal ratings, 4 Elo per centipawn away from it
fire_contempt = -25  # fire only below this draw contempt: the opponent is over 100 Elo weaker
game_types = ("lightning", "blitz", "standard")
lichess_speeds = {"bullet": "lightning", "blitz": "blitz", "rapid": "standard", "classical": "standard"}


def expected_score(centipawns):
    """Win expectation of an eval: +1.00 is about 64%, +3.00 about 85%."""
    return 1 / (1 + 10 ** (-max(-2000, min(2000, centipawns)) / 400))


def draw_contempt(our_rating, their_rating, window=100, default=equal_contempt, elo_per_cp=4):
    """DrawContempt in cp from our view: draws welcome against stronger players, avoided against weaker.

    None when a rating is unknown, so the engine keeps its own default."""
    if not window or our_rating is None or their_rating is None:
        return None
    return int(max(-window, min(window, default + round((their_rating - our_rating) / elo_per_cp))))


def color_contempt(contempt, white):
    """The draw score the engine plays with: White wants more than a draw, Black settles a little sooner.

    A fifth of the contempt's size, at least 2 cp, added so the direction stays right at any sign,
    within the engine's option range."""
    shift = max(2, round(abs(contempt) / 5))
    return max(-100, min(100, contempt - shift if white else contempt + shift))


def game_contempt(headers, our_color):
    """The draw score the engine searched with: the DrawContempt tag, or from the ratings in older games."""
    if headers.get("DrawContempt", "").lstrip("-").isdigit():
        return int(headers["DrawContempt"])
    ours, theirs = (headers.get(f"{prefix}Elo", "") for prefix in
                    (("White", "Black") if our_color == chess.WHITE else ("Black", "White")))
    contempt = draw_contempt(int(ours), int(theirs), default=older_contempt) \
        if ours.isdigit() and theirs.isdigit() else None
    return older_contempt if contempt is None else contempt


def game_type(event):
    """FICS type from the Event tag; Lichess speeds map to the nearest one."""
    words = event.lower().split()
    return next((lichess_speeds.get(word, word) for word in words if word in game_types or word in lichess_speeds),
                None)


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
    (the mean expected score of our next eval_window searched moves when most are below level, else the
    first one) and the game result."""
    headers = game.headers
    if headers.get("LearningIncomplete") == "true":
        return None
    ours = [color for color, prefix in ((chess.WHITE, "White"), (chess.BLACK, "Black"))
            if headers.get(f"{prefix}Type") == "program" and headers.get(f"{prefix}Engine")]
    if headers.get("LearningColor") in ("white", "black"):
        ours = [headers["LearningColor"] == "white"]
    if len(ours) != 1 or game.errors:
        return None
    variant = board_variant(game.board())
    our_color = ours[0]
    their_prefix = "Black" if our_color == chess.WHITE else "White"
    contempt = game_contempt(headers, our_color)
    board = game.board()
    moves, evals, analyses = [], [], []
    horizon = None
    for ply, node in enumerate(game.mainline()):
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        mover = board.turn
        moves.append([key, node.move.uci(), mover == our_color])
        evaluation, depth = node.eval(), node.eval_depth()
        if mover == our_color and evaluation is not None and depth:
            relative = evaluation.pov(mover)
            centipawns = relative.score(mate_score=2000)
            drawn_score = centipawns == contempt
            if horizon is None and ply >= decided_plies and abs(centipawns) >= decided_cp:
                horizon = ply + 1
            # The draw score is our contempt, not a judgement of the position: it counts as level.
            evals.append((ply, expected_score(0 if drawn_score else centipawns), drawn_score))
            nodes, nps, seconds = effort(node)
            analyses.append([key, node.move.uci(), depth, centipawns, int(relative.is_mate()),
                             round(seconds * 1000) if seconds is not None else None,
                             board.halfmove_clock, nodes, nps])
        board.push(node.move)
    if len(moves) < minimum_plies or len(evals) < eval_window and headers.get("LearningResults") != "true":
        return None
    if len(evals) < eval_window:
        if headers.get("Result", "*") == "*":
            return None
        evals, analyses = [], []
    outcome = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}.get(headers.get("Result"))
    score = None if outcome is None else (outcome if our_color == chess.WHITE else 1 - outcome)
    values, draws = [], []
    for ply, (key, move, our_move) in enumerate(moves[:horizon]):
        after = [entry for entry in evals if entry[0] >= ply][:eval_window]
        drawn = sum(entry[2] for entry in after) >= drawn_evals
        # A line we keep scoring below level is a bad one; otherwise only our first eval counts, so an
        # opponent's later blunder does not make the line look good.
        negative = sum(entry[1] < 0.5 for entry in after) >= min(drawn_evals, len(after))
        evaluated = None if not after or drawn else round(
            sum(entry[1] for entry in after) / len(after) if negative else after[0][1], 4)
        if drawn and our_move:
            draws.append([key, move])
        result = score
        if not our_move:
            evaluated = None if evaluated is None else round(1 - evaluated, 4)
            result = None if score is None else 1 - score
        if evaluated is not None or result is not None:
            values.append([key, move, evaluated, result])
    rating = headers.get(f"{their_prefix}Elo", "")
    return {
        "id": headers.get("LearningId"), "source": headers.get("LearningSource"), "variant": variant,
        "server": headers.get("LearningServer"),
        "date": headers.get("UTCDate", headers.get("Date", "")),
        "time": headers.get("UTCTime", ""),
        "white": headers.get("White", ""), "black": headers.get("Black", ""),
        "color": "white" if our_color == chess.WHITE else "black",
        "opponent": headers.get(their_prefix, ""),
        "group": headers.get("LearningGroup") or opponent_group(headers, not our_color),
        "title": headers.get(f"{their_prefix}Title") or None,
        "rating": int(rating) if rating.isdigit() else None,
        "type": game_type(headers.get("Event", "")),
        "time_control": headers.get("TimeControl", ""),
        "score": score,
        "moves": [move[:2] for move in moves[:horizon]],
        "values": values, "draws": draws, "analyses": analyses, "contempt": contempt,
    }


def player_stats(records, server=None):
    """Readable per-opponent stats from the actual results."""
    players = {}
    for record in records:
        if server is not None and record.get("server") != server:
            continue
        name = record["opponent"]
        if not name:
            continue
        key = f"{record['server']}/{name.lower()}" if server is None and record.get("server") else name.lower()
        stats = players.setdefault(key, {
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
    from urllib.parse import quote
    for key, stats in players.items():
        parts = key.split("/", 1)
        path = directory.joinpath(*(quote(part, safe="") for part in parts))
        path = path.parent / (path.name + ".json")
        learning_store.atomic_text(path, json.dumps(stats, indent=2, sort_keys=True) + "\n")
    return len(players)


def build(experience_dir, games_dir=None, variant="standard"):
    """Reconstruct all shared online learning from the complete PGN archive."""
    store = learning_store.shared_store(experience_dir, variant)
    if games_dir is not None and Path(games_dir).resolve() != store.games.resolve():
        store.sync(games_dir)
    summary = store.rebuild()
    records = store.load()
    return dict(summary, players=len(player_stats(records)),
                evaluated=sum(any(value[2] is not None for value in record["values"]) for record in records),
                built=datetime.now().isoformat(timespec="seconds"))


def polyglot_code(board, move):
    """Polyglot move encoding; castling is written as the king taking its own rook."""
    target = move.to_square
    if board.is_castling(move):
        target = chess.square(7 if chess.square_file(move.to_square) > 4 else 0, chess.square_rank(move.to_square))
    promotion = {chess.KNIGHT: 1, chess.BISHOP: 2, chess.ROOK: 3, chess.QUEEN: 4}.get(move.promotion, 0)
    return (chess.square_file(target) | chess.square_rank(target) << 3 | chess.square_file(move.from_square) << 6
            | chess.square_rank(move.from_square) << 9 | promotion << 12)


proof_weight = 0xffff  # a proven win: always played, never mixed with the other layers


def build_repertoire(pgn_paths, book_path, plies=30):
    """A Polyglot book of our side's moves from one PGN or several: each repertoire line adds weight 100 to
    its moves, or its [Weight] header.

    Games carry [Repertoire "white"] or [Repertoire "black"]; opponent moves are only the path. A line
    with a [Proof] header is a proven win: its moves get the proof weight."""
    weights = {}
    for pgn_path in [pgn_paths] if isinstance(pgn_paths, (str, Path)) else pgn_paths:
        add_repertoire(pgn_path, weights, plies)
    return write_book(weights, book_path)


def add_repertoire(pgn_path, weights, plies):
    with open(pgn_path, encoding="utf-8-sig") as stream:
        while True:
            game = read_pgn(stream)
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
                    weights[entry] = proof_weight if "Proof" in game.headers or weights.get(entry) == proof_weight \
                        else weights.get(entry, 0) + int(game.headers.get("Weight", 100))
                board.push(node.move)


def proven_wins(game, our_color):
    """(board, move, mate distance) of our moves that the engine scored as a mate for
    us: a losers mate score is a streak proof over every reply, so the move is a proven win."""
    board = game.board()
    for node in game.mainline():
        evaluation = node.eval()
        mate = evaluation.pov(our_color).mate() if evaluation is not None and board.turn == our_color else None
        if mate and mate > 0:
            yield board.copy(), node.move, mate
        board.push(node.move)


def games_proven_wins(games_dir):
    """Every proven win in the saved losers games below games_dir, as (board, move, mate distance)."""
    for path in sorted(Path(games_dir).rglob("*.pgn")) if Path(games_dir).is_dir() else ():
        with open(path, encoding="utf-8-sig") as stream:
            while (game := read_pgn(stream)) is not None:
                colour = game.headers.get("LearningColor")
                if game.headers.get("Variant") == "Royal losers" and colour in ("white", "black"):
                    yield from proven_wins(game, colour == "white")


def games_proof_entries(games_dir):
    """{(key, move code): proof weight} of every proven win in the saved losers games below games_dir."""
    return {(chess.polyglot.zobrist_hash(board), polyglot_code(board, move)): proof_weight
            for board, move, _ in games_proven_wins(games_dir)}


def book_proven_wins(game, our_color, book_path):
    """Add our proven wins of the game to the proof book; returns how many entries were new."""
    found = {(chess.polyglot.zobrist_hash(board), polyglot_code(board, move)): proof_weight
             for board, move, _ in proven_wins(game, our_color)}
    if not found:
        return 0
    weights = proven_entries(book_path, every=True)
    new = sum(weights.get(entry) != proof_weight for entry in found)
    if new:
        write_book({**weights, **found}, book_path)
    return new


def proven_entries(book_path, every=False):
    """{(key, move code): weight} of the book's proven entries, or of all its entries; empty when absent."""
    if not Path(book_path).is_file():
        return {}
    with chess.polyglot.open_reader(book_path) as reader:
        return {(entry.key, entry.raw_move): entry.weight for entry in reader
                if every or entry.weight == proof_weight}


def mood_sources(books_dir, output_dir):
    """(books/moods/<name>.pgn, <data>/moods/<name>.bin) for every mood repertoire."""
    return [(source, output_dir / "moods" / f"{source.stem}.bin")
            for source in sorted((books_dir / "moods").glob("*.pgn"))]


def write_book(weights, book_path):
    """Write {(zobrist key, polyglot move code): weight} as a sorted Polyglot book; returns the entries."""
    temporary = Path(book_path).with_suffix(".tmp")
    with open(temporary, "wb") as stream:
        for (key, code), weight in sorted(weights.items()):
            stream.write(struct.pack(">QHHI", key, code, max(1, min(int(weight), 0xffff)), 0))
    temporary.replace(book_path)
    return len(weights)


# Multi-layer book profiles (MAXIMA.md 9.2): percentages per layer; "search" is the share of games
# that leave the book once, at a random ply, so the engine can bring in new moves; "revenge" is the chance
# to play an opponent's winning line back at them.
style_names = ("fire", "draw", "experiment")
layer_names = ("book",) + style_names + ("mood", "global", "group", "opponent")
layer_profiles = {
    "equal": {"book": 45, "fire": 0, "draw": 0, "experiment": 0, "mood": 10, "global": 15, "group": 5,
              "opponent": 25, "search": 5, "revenge": 30, "repeat": 70},
    "weaker": {"book": 20, "fire": 25, "draw": 0, "experiment": 0, "mood": 10, "global": 5, "group": 5,
               "opponent": 35, "search": 5, "revenge": 40, "repeat": 70},
    "stronger": {"book": 20, "fire": 0, "draw": 35, "experiment": 0, "mood": 0, "global": 10, "group": 10,
                 "opponent": 25, "search": 5, "revenge": 15, "repeat": 70},
    "unrated": {"book": 30, "fire": 30, "draw": 0, "experiment": 10, "mood": 15, "global": 15, "group": 0,
                "opponent": 0, "search": 10, "revenge": 50, "repeat": 70},
}


def mood_names(directory):
    """The moods with a built book: <name>.bin in the moods directory."""
    return sorted(path.stem for path in Path(directory).glob("*.bin")) if directory else []


def pick_mood(setting, available, previous=None):
    """The next mood: none for "off", the configured name, or for "auto" a random one other than the last."""
    if setting == "off" or not available:
        return None
    if setting != "auto":
        return setting if setting in available else None
    return random.choice([name for name in available if name != previous] or available)


def style_share(contempt):
    """How far a rated game leans to the weaker or stronger profile: none within 20 Elo, all from 120 Elo."""
    if contempt is None:
        return 0.0
    return max(0.0, min(1.0, (abs(contempt - equal_contempt) - 5) / 25))


def game_profile(contempt, rated, profiles=None, bot=False):
    """Layer percentages for one game: unrated games (guests) use the unrated profile; rated games slide
    from equal toward weaker (fire) or stronger (draw) with the draw contempt. Rated games and bots get
    fire only when clearly weaker; its share then goes to the base book."""
    profiles = profiles or layer_profiles
    if rated:
        side = "weaker" if contempt is not None and contempt < 0 else "stronger"
        share = style_share(contempt)
        weights = {name: (1 - share) * profiles["equal"].get(name, 0) + share * profiles[side].get(name, 0)
                   for name in profiles["equal"]}
    else:
        weights = dict(profiles["unrated"])
    if (rated or bot) and (contempt is None or contempt >= fire_contempt):
        weights["book"] += weights.get("fire", 0)
        weights["fire"] = 0
    return weights


def learned_factor(rate):
    """The engine's experience factor: x1 at 50%, about x2.6 at 65%, x0.47 at 40%, never below x0.1."""
    return 0.1 + 0.9 * (2 * rate) ** 4


class opening_book:
    """One game's multi-layer book (MAXIMA.md 9.2): book.bin, the style books fire, draw, and experiment,
    the current mood, and the learned layers global, group, and opponent, each with a percentage from the
    game's profile. style_books may carry the mood's book under "mood"."""

    def __init__(self, records, opponent=None, group=None, game_type=None, book_path=None, prior=8.0,
                 variety=1.0, random_source=None, contempt=None, style_books=None,
                 rated=True, profiles=None, mood=None, server=None, variant="standard"):
        self.book_path = book_path
        self.mood = mood
        self.variant = learning_variant(variant)
        self.random = random_source or random.Random()
        self.weights = game_profile(contempt, rated, profiles, group == "engines")
        self.paths = {"book": book_path}
        # Deep book learning's moves (deep.bin beside book.bin) stand in where book.bin has none.
        self.deep_path = Path(book_path).with_name("deep.bin") if book_path and self.variant == "standard" else None
        self.deep_enough = True  # set per move by choose: deep.bin's weakest search beats our own
        self.paths.update({name: path for name, path in (style_books or {}).items()
                           if name in style_names + ("mood",) and self.weights.get(name) and path
                           and Path(path).is_file()})
        self.names = dict({name: name for name in layer_names}, group=group or "group",
                          opponent=opponent or "opponent")
        # The search layer: some games leave the book once, at a random ply of ours.
        self.search_ply = None
        self.deliberate = False  # this search was chosen to find new moves
        if self.random.random() * 100 < self.weights.get("search", 0):
            self.search_ply = self.random.randrange(search_plies)
        # JDrone's draw factor, tied to DrawContempt, for the result part: a draw is worth up to 0.95
        # against much stronger opponents and down to 0.05 against much weaker ones.
        self.draw_value = 0.5 if contempt is None else 0.5 + 0.45 * max(-1.0, min(1.0, contempt / 100))
        self.prior = prior
        self.variety = max(variety, 0.05)
        self.layers = ({}, {}, {})  # global, group, opponent: (key, move) -> [value sum, games]
        self.totals = ({}, {}, {})  # the same per position: key -> games
        self.results = ({}, {}, {})  # unweighted W/D/L only for moves played by Maxima
        self.choice_results = {}
        self.analyses = {}  # key -> strongest earlier search: [move, depth, cp, mate, ms, halfmove clock, nodes, nps]
        self.live_nps = None  # the engine session's latest search speed
        self.speeds = collections.deque(maxlen=recent_speeds)  # earlier games' speeds, for a fresh session
        self.long_think = False  # the position is known: search it longer, for a stronger record next time
        self.signatures = {}  # (key, move) -> convincing wins in a row against this opponent with our move
        self.revenge = {}  # colour -> {(key, move)}: the opponent's line from their last win, until we win with it
        self.comfort_scores = {}  # (key, move) -> [sum, games] of the first eval, halved per book move back
        self.draws = {}  # key -> {move: games}: our moves into lines we searched as drawn
        opponent = (opponent or "").lower()
        for record in records:
            if learning_variant(record.get("variant", "standard")) != self.variant:
                continue
            weight = 1.0 if record["type"] == game_type else 0.5
            # Our expected score from the rating gap in the game's contempt (4 Elo per cp). A clearly weaker
            # opponent's mistakes flatter our moves, so those games teach the shared layers less.
            contempt = record.get("contempt", 0)
            expected = 1 / (1 + 10 ** (contempt / 100))
            shared = weight * 2 / (1 + 10 ** (-min(0, contempt - fire_contempt) / 100))
            registered = record["group"] is not None
            layers = [(0, shared if registered else shared / 2)]
            if registered and group is not None and record["group"] == group:
                layers.append((1, shared))
            first = 0 if record.get("color") == "white" else 1
            own_moves = ({tuple(played) for played in record.get("moves", [])[first::2]}
                         if record.get("color") in ("white", "black") else set())
            ours = set()
            personal = server is None or record.get("server") == server
            if registered and personal and opponent and record["opponent"].lower() == opponent:
                layers.append((2, weight))
                first = 0 if record.get("color") == "white" else 1
                ours = {tuple(played) for played in record.get("moves", [])[first::2]}
            if personal and opponent and record["opponent"].lower() == opponent and record.get("color") in ("white", "black"):
                self.track_revenge(record)
            self.track_comfort(record)
            for key, move in record.get("draws", []):
                drawn = self.draws.setdefault(key, {})
                drawn[move] = drawn.get(move, 0) + 1
            self.track_analyses(record)
            counted = set()
            for key, move, evaluated, result in record["values"]:
                if (key, move) in ours:
                    convincing = result == 1.0 and (evaluated is None or evaluated >= signature_eval)
                    self.signatures[(key, move)] = self.signatures.get((key, move), 0) + 1 if convincing else 0
                value = self.value(evaluated, result, expected)
                for layer, layer_weight in layers:
                    entry = self.layers[layer].setdefault((key, move), [0.0, 0.0])
                    entry[0] += layer_weight * value
                    entry[1] += layer_weight
                    self.totals[layer][key] = self.totals[layer].get(key, 0.0) + layer_weight
                    if (key, move) in own_moves and result in (1.0, 0.5, 0.0) and (key, move) not in counted:
                        outcomes = self.results[layer].setdefault((key, move), [0, 0, 0])
                        outcomes[(1.0, 0.5, 0.0).index(result)] += 1
                if result is not None:
                    counted.add((key, move))

    def track_revenge(self, record):
        """A loss gives us the opponent's line for their colour; a win with that colour serves the revenge."""
        ours = chess.WHITE if record["color"] == "white" else chess.BLACK
        if record.get("score") == 0.0:
            first = 1 if ours == chess.WHITE else 0
            self.revenge[not ours] = {tuple(played) for played in record.get("moves", [])[:revenge_plies][first::2]}
        elif record.get("score") == 1.0:
            self.revenge.pop(ours, None)

    def track_comfort(self, record):
        """Our first searched eval judges the book line before it: the last book move fully, each earlier
        one half of the next, so early moves are not blamed for what came much later."""
        if record.get("color") not in ("white", "black") or not record.get("analyses"):
            return
        position, move, _, centipawns = record["analyses"][0][:4]
        played = [tuple(entry) for entry in record.get("moves", [])]
        if (position, move) in played:
            line = played[:played.index((position, move))][0 if record["color"] == "white" else 1::2]
            if line and list(line[-1]) in record.get("draws", []):
                return  # a drawn line: the draw layer learns it, comfort has nothing to judge
            for distance, entry in enumerate(reversed(line)):
                total = self.comfort_scores.setdefault(entry, [0.0, 0])
                total[0] += centipawns / 2 ** distance
                total[1] += 1

    def track_analyses(self, record):
        for saved in record.get("analyses", []):
            if len(saved) < 7:
                continue
            key, move, depth, centipawns, mate, think_ms, clock = saved[:7]
            nodes, nps = saved[7:9] if len(saved) >= 9 else (None, None)
            if nps:
                self.speeds.append(nps)
            known = self.analyses.get(key)
            rank = analysis_rank(depth, centipawns, mate, think_ms, nodes)
            if known is None or rank > analysis_rank(*known[1:5], known[6]):
                self.analyses[key] = [move, depth, centipawns, mate, think_ms, clock, nodes, nps]

    def value(self, evaluated, result, expected=0.5):
        """Maxima's eval is the main signal; the result adds what evals miss, such as time trouble. A result
        counts against the expected score: a win over a much weaker opponent says little, a draw with a
        much stronger one a lot."""
        if result == 0.5:
            result = self.draw_value
        if result is not None:
            result = max(0.0, min(1.0, result + 0.5 - expected))
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

    def proven_move(self, board):
        """The base book's proven winning move in board, or None."""
        if not self.book_path:
            return None
        try:
            with chess.polyglot.open_reader(str(self.book_path)) as reader:
                entry = max(reader.find_all(board, minimum_weight=proof_weight), key=lambda entry: entry.weight,
                            default=None)
        except (OSError, ValueError, IndexError, TypeError):
            return None
        return entry.move if entry is not None and board.is_legal(entry.move) else None

    def sample(self, rate, games):
        strength = (games + self.prior) / self.variety
        return self.random.betavariate(max(rate * strength, 1e-3), max((1 - rate) * strength, 1e-3))

    def distribution(self, board, key):
        """Each move's probability and the share each layer gave it, whether any book knew the position,
        and the chance to search instead: book.bin is the know-all base, so where it has no move, its
        percentage goes to the engine rather than to the other layers."""
        books = {name: self.read_book(path, board) for name, path in self.paths.items()}
        if self.deep_path is not None and self.deep_enough and not books.get("book") and self.deep_path.is_file():
            books["book"] = self.read_book(self.deep_path, board)
        drawn = self.draws.get(key) if self.weights.get("draw") else None
        if drawn:
            # Our own drawn lines join the draw book, trusted with their games where it knows the position.
            games = sum(drawn.values())
            book = books.get("draw", {})
            trust = games / (games + self.prior) if book else 1.0
            mixed = {move: (1 - trust) * share for move, share in book.items()}
            for move, count in drawn.items():
                move = chess.Move.from_uci(move)
                mixed[move] = mixed.get(move, 0) + trust * count / games
            books["draw"] = mixed
        books = {name: moves for name, moves in books.items() if moves}
        known = set().union(*books.values()) if books else set()
        learned = {chess.Move.from_uci(move) for (position, move) in self.layers[0] if position == key}
        candidates = {move for move in known | learned
                      if board.is_legal(move)}
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
        # Proven losers drop in every layer, book.bin included, after four games at a poor rate. Moves this
        # opponent has only beaten drop at once, below the untried moves until those have failed as well.
        # Comfort then moves every layer toward the lines Maxima understands.
        damp = {move: self.comfort(key, move) * (lost_damp if self.personal_score(key, move) == 0.0
                else 0.1 if rates[move][0][1] >= 4 and rates[move][-1][0] < 0.3 else 1.0) for move in candidates}
        # The mood keeps its share: a damped move yields to the mood's other lines, or stays if it has none.
        mood = {move: share * damp.get(move, 0) for move, share in layers.get("mood", {}).items()}
        kept = sum(mood.values())
        if kept:
            layers["mood"] = {move: share / kept for move, share in mood.items()}
        shares = {move: {name: weights[name] * layers[name].get(move, 0) * (1.0 if name == "mood" else damp[move])
                         for name in layers} for move in candidates}
        search = 0.0 if "book" in books else self.weights.get("book", 0)
        total = sum(sum(parts.values()) for parts in shares.values())
        if not total:
            return {}, in_book, 0.0
        total += search
        moves = {move: (sum(parts.values()) / total, {name: part / total for name, part in parts.items()})
                 for move, parts in shares.items()}
        search /= total
        signature = self.signature(key, moves)
        if signature is not None:
            # The signature line: same opening again, with some variety left.
            moves, search = self.boost(moves, search, signature, signature_share)
        repeat = self.repeat_move(key, moves) if signature is None else None
        if repeat is not None:
            # A good result against a stronger opponent: walk the same line again.
            moves, search = self.boost(moves, search, repeat, self.weights.get("repeat", 0) / 100)
        revenge = self.revenge_move(board, key, moves)
        if revenge is not None and revenge != signature and revenge != repeat:
            # Revenge: the line this opponent beat us with, played back at them.
            moves, search = self.boost(moves, search, revenge, self.weights.get("revenge", 0) / 100)
        return moves, in_book, search

    def personal_score(self, key, move):
        """Our score with the move against this opponent, a draw counting draw_value; None when untried."""
        wins, draws, losses = self.results[2].get((key, move.uci()), (0, 0, 0))
        games = wins + draws + losses
        return (wins + draws * self.draw_value) / games if games else None

    def comfort(self, key, move):
        """How well Maxima understands the move's lines: x0.1 at a score of -0.70, up to x2 from +1.00."""
        total, games = self.comfort_scores.get((key, move.uci()), (0.0, 1))
        score = total / games
        return max(0.1, 10 ** (score / 70)) if score < 0 else min(2.0, 1 + score / 100)

    def repeat_move(self, key, moves):
        """Our best move here that beat expectations against a stronger opponent, if any."""
        if self.draw_value <= 0.5:
            return None
        scores = {move: self.personal_score(key, move) or 0.0 for move, (probability, _) in moves.items()
                  if probability > 0}
        move = max(scores, key=lambda move: (scores[move], move.uci()), default=None)
        return move if move is not None and scores[move] > 0.5 else None

    @staticmethod
    def boost(moves, search, chosen, share):
        """Raise one move to share, scaling the other moves and the search chance down to make room."""
        probability = moves[chosen][0]
        if probability >= share or probability >= 1:
            return moves, search
        rest = (1 - share) / (1 - probability)
        boosted = {}
        for move, (old, parts) in moves.items():
            if move != chosen:
                boosted[move] = (old * rest, {name: part * rest for name, part in parts.items()})
            elif old > 0:
                boosted[move] = (share, {name: part * share / old for name, part in parts.items()})
            else:
                boosted[move] = (share, {"opponent": share})
        return boosted, search * rest

    def revenge_move(self, board, key, moves):
        """The opponent's own move from the line they beat us with, when it is a candidate here."""
        line = self.revenge.get(board.turn, set())
        return next((move for move in sorted(moves, key=chess.Move.uci) if (key, move.uci()) in line), None)

    def signature(self, key, moves):
        """Our move that beat this opponent convincingly several times in a row here, if any."""
        streaks = {move: self.signatures.get((key, move.uci()), 0) for move, (probability, _) in moves.items()
                   if probability > 0}
        move = max(streaks, key=lambda move: (streaks[move], move.uci()), default=None)
        return move if move is not None and streaks[move] >= signature_wins else None

    def describe(self, probability, parts):
        """Label first (book or learned), then each layer's share of the choice, for tuning."""
        learned = sum(parts.get(name, 0) for name in ("global", "group", "opponent"))
        label = "learned" if learned > sum(parts.get(name, 0) for name in ("book",) + style_names) else "book"
        detail = ", ".join(f"{self.names[name]} {parts[name] / probability:.0%}" for name in layer_names
                           if parts.get(name, 0) >= 0.005 * probability)
        return f"{label} {probability:.0%} selection: {detail}"

    def predict(self, board):
        """The likeliest book move in board for pondering, or None; the book's state stays as it is."""
        moves = self.distribution(board, f"{chess.polyglot.zobrist_hash(board):016x}")[0]
        return max(sorted(moves, key=chess.Move.uci), key=lambda move: moves[move][0], default=None)

    def remembered(self, board, seconds):
        """(move, reason) when an earlier, stronger search of this position stands; otherwise None, with
        long_think set for any position searched before, also where the line went badly."""
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        saved = self.analyses.get(key)
        if saved is None or self.variant != "standard" or saved[5] != board.halfmove_clock:
            return None
        text, depth, nodes = saved[0], saved[1], saved[6]
        nps = self.live_nps or (statistics.median(self.speeds) if self.speeds else None)
        move = chess.Move.from_uci(text)
        if not board.is_legal(move):
            return None
        strong = nodes and nps and seconds is not None and nodes >= recall_margin * nps * seconds * real_spend
        if not strong or self.estimate(key, text)[0] < 0.5:
            self.long_think = True
            return None
        self.why = ("remembered", 1.0, {"global": 1.0}, "global", depth, nodes)
        self.choice_results = {name: tuple(results[(key, text)])
                               for name, results in zip(("global", "group", "opponent"), self.results)
                               if (key, text) in results}
        return move, f"recalled d{depth}"

    def choose(self, board, seconds=None):
        """choose_book, then an earlier search of this position; seconds is clock/30 + increment."""
        self.long_think = self.deliberate = False
        self.deep_enough = self.deeper_than_search(seconds)
        found = self.choose_book(board)
        if found is None and not self.deliberate and self.book_applies(board):
            return self.remembered(board, seconds)
        return found

    def deeper_than_search(self, seconds):
        """Whether deep.bin's weakest search (deep.json) clearly beats what we would search now: a faster
        machine or a slower clock, as in correspondence chess, outgrows it."""
        if self.deep_path is None or seconds is None:
            return True
        try:
            nodes = json.loads(self.deep_path.with_name("deep.json").read_text(encoding="utf-8")).get("nodes")
        except (OSError, ValueError):
            nodes = None
        nps = self.live_nps or (statistics.median(self.speeds) if self.speeds else None)
        return not nodes or not nps or nodes >= recall_margin * nps * seconds * real_spend

    def book_applies(self, board):
        # Tablebase positions belong to the tablebases.
        return (learning_variant(board_variant(board)) == self.variant and not online_tablebase.covers(board)
                and board.halfmove_clock < 90 and not board.is_repetition(2))

    def choose_book(self, board):
        """(move, reason) to play without searching, or None to let the engine search.

        why keeps the last decision for observers: ("left book",), ("book" | "learned" | "signature" | "remembered" |
        "revenge", probability, parts, source).
        The source is the layer with
        the largest share of the move; ties go to the more specific layer."""
        self.why = None
        self.choice_results = {}
        if not self.book_applies(board):
            return None
        proven = self.proven_move(board)
        if proven is not None:
            self.why = ("proof", 1.0, {"book": 1.0}, "book")
            return proven, "proof: a proven win"
        if self.search_ply is not None and board.ply() >= self.search_ply:
            self.search_ply = None
            self.deliberate = True
            self.why = ("left book",)
            return None
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        moves, in_book, search = self.distribution(board, key)
        if moves:
            # A move under a tenth of the likeliest is never picked, as in the base book.
            top = max(probability for probability, _ in moves.values())
            ordered = sorted((move for move in moves if moves[move][0] >= top / 10), key=chess.Move.uci)
            move = self.random.choices(ordered + [None], [moves[move][0] for move in ordered] + [search])[0]
            if move is None:
                self.why = ("left book",)
                return None
            rate, games = self.estimate(key, move.uci())
            revenge = self.revenge_move(board, key, moves)
            if in_book or move == revenge or (games >= 2 and rate >= 0.5):
                reason = self.describe(*moves[move])
                kind = reason.split()[0]
                if move == self.signature(key, moves):
                    kind = "signature"
                    reason = "signature " + reason
                elif move == revenge:
                    kind = "revenge"
                    reason = "revenge " + reason
                parts = moves[move][1]
                source = max(parts, key=lambda name: (round(parts[name], 6), layer_names.index(name)))
                self.why = (kind,) + moves[move] + (source,)
                self.choice_results = {name: tuple(results[(key, move.uci())])
                                       for name, results in zip(("global", "group", "opponent"), self.results)
                                       if (key, move.uci()) in results}
                return move, reason

        return None


class learner:
    """MaxDrone's experience: digests kept in memory, a fresh opening_book per game."""

    def __init__(self, experience_dir, games_dir=None, book_path=None, variety=1.0, style_books=None,
                 profiles=None, mood="auto", moods_dir=None, server=None, variant="standard", notify=None):
        self.experience_dir = Path(experience_dir)
        self.variant = learning_variant(variant)
        self.mood, self.moods_dir = mood, moods_dir
        self.picked_mood, self.mood_picked_at = None, None
        self.style_books = style_books
        self.profiles = profiles
        self.games_dir = Path(games_dir or self.experience_dir / "games")
        self.server = learning_store.server_name(server) if server else None
        self.store = learning_store.shared_store(self.experience_dir, variant, notify)
        self.book_path = book_path
        self.variety = variety
        if self.games_dir.exists() and self.experience_dir.resolve() not in self.games_dir.resolve().parents and self.games_dir.resolve() != self.experience_dir.resolve():
            self.store.sync(self.games_dir)
        self.records = self.store.load()

    def today_mood(self, now=None):
        """The session's mood, picked at the first game and again once mood_hours have passed."""
        now = monotonic() if now is None else now
        if self.mood_picked_at is None or now - self.mood_picked_at >= mood_hours * 3600:
            self.picked_mood = pick_mood(self.mood, mood_names(self.moods_dir), self.picked_mood)
            self.mood_picked_at = now
        return self.picked_mood

    def for_game(self, opponent, group, game_type, contempt=None, rated=True):
        # Existing opening_book instances own their view; only the next game reloads.
        self.records = self.store.load()
        mood, books = self.today_mood(), dict(self.style_books or {})
        if mood:
            books["mood"] = Path(self.moods_dir) / f"{mood}.bin"
        book = opening_book(self.records, opponent, group, game_type, self.book_path, variety=self.variety,
                            contempt=contempt, style_books=books, rated=rated, profiles=self.profiles, mood=mood,
                            server=self.server, variant=self.variant)
        return book

    def add(self, game):
        record = self.store.add(game)
        self.records = self.store.load()
        # A proven win is a fact, so even a game too short to learn from books its proofs.
        colour = game.headers.get("LearningColor")
        if self.variant == "losers" and self.book_path and colour in ("white", "black"):
            book_proven_wins(game, colour == "white", self.book_path)
        return record

    def refresh(self):
        """Reload a published snapshot; existing opening books keep their own view."""
        self.records = self.store.load()
        return {"games": len(self.records)}


def main():
    from .project import data_root
    parser = argparse.ArgumentParser(prog="maxima experience:build", description=__doc__)
    parser.add_argument("--experience-dir", default=str(data_root() / "experience"),
                        help="rebuild shared index and player stats from all archived PGNs (default: experience)")
    parser.add_argument("--variant", choices=experience_variants, default="standard",
                        help="standard and numbered wild variants share the root; losers uses its subdirectory")
    options = parser.parse_args()
    root = Path(options.experience_dir)
    summary = build(root / "losers" if learning_variant(options.variant) == "losers" else root, variant=options.variant)
    print(f"{summary['games']} archived games, {summary['learned']} learned, {summary['evaluated']} with Maxima's evals, "
          f"{summary['players']} opponents")
    return 0


def sync_main():
    from .project import data_root
    parser = argparse.ArgumentParser(prog="maxima experience:sync",
                                     description="Upload PGNs into shared online learning; repeated imports count once.")
    parser.add_argument("source", type=Path, help="a PGN file or a folder searched recursively")
    parser.add_argument("destination", type=Path, nargs="?", default=data_root() / "experience",
                        help="shared experience root on the PVC (default: MAXIMA_DATA_DIR/experience)")
    parser.add_argument("--player", action="append", default=[], help="our PGN player name, repeatable")
    parser.add_argument("--source", choices=("fics", "lichess", "local"), dest="source_name",
                        help="source for games without provenance tags (otherwise inferred)")
    parser.add_argument("--group", choices=("humans", "bots", "masters"),
                        help="opponent group when uploading PGNs without account/type metadata")
    parser.add_argument("--replace", action="store_true", help="explicitly correct an archived final result")
    parser.add_argument("--variant", choices=experience_variants, default="standard",
                        help="standard and numbered wild variants share the destination; losers uses its subdirectory")
    options = parser.parse_args()
    root = options.destination / "losers" if learning_variant(options.variant) == "losers" else options.destination
    summary = learning_store.shared_store(root, options.variant).sync(options.source, options.player,
                                                                    options.source_name, options.replace, options.group)
    print(f"{summary['read']} read, {summary['added']} new, {summary['games']} archived, {summary['learned']} learnable")
    return 0


def repertoire_main():
    from .project import data_root, project_root
    parser = argparse.ArgumentParser(
        prog="maxima books:repertoire",
        description="Build the style books of the multi-layer book (MAXIMA.md 9.2) from repertoire PGNs.")
    parser.add_argument("--books-dir", default=str(project_root() / "books"),
                        help="reads <style>.pgn (default: books/)")
    parser.add_argument("--output-dir", default=str(data_root()),
                        help="writes <style>.bin beside book.bin (default: the data directory)")
    parser.add_argument("--plies", type=int, default=30, help="moves per line to include (default: 30)")
    parser.add_argument("--styles", nargs="+", choices=style_names + ("moods",), default=["experiment", "moods"],
                        help="books to build (default: experiment and moods; fire.bin and draw.bin normally "
                             "come from maxima init or books:fire|draw, which this would replace)")
    options = parser.parse_args()
    built = 0
    if "moods" in options.styles:
        for source, target in mood_sources(Path(options.books_dir), Path(options.output_dir)):
            target.parent.mkdir(parents=True, exist_ok=True)
            print(f"{target}: {build_repertoire(source, target, options.plies)} entries from {source}")
            built += 1
    for style in (style for style in options.styles if style != "moods"):
        source = Path(options.books_dir) / f"{style}.pgn"
        if not source.is_file():
            continue
        target = Path(options.output_dir) / f"{style}.bin"
        print(f"{target}: {build_repertoire(source, target, options.plies)} entries from {source}")
        built += 1
    if not built:
        parser.error(f"no repertoire PGNs for {', '.join(options.styles)} in {options.books_dir}")
    return 0
