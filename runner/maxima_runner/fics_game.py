"""FICS game state: style12 boards, move history, coordinate moves, game endings, and PGN records."""
from datetime import datetime, timezone
import re

import chess
import chess.pgn

from .variants import variant_game

style12_pattern = re.compile(
    r"<12> (?P<rows>(?:[pnbrqkPNBRQK-]{8} ){8})(?P<turn>[WB]) (?P<ep>-1|[0-7]) "
    r"(?P<castling>(?:[01] ){4})(?P<halfmove>\d+) (?P<game>\d+) (?P<white>[A-Za-z]+) (?P<black>[A-Za-z]+) "
    r"(?P<relation>-?[0-3]) (?P<initial>\d+) (?P<increment>\d+) \d+ \d+ "
    r"(?P<white_clock>-?\d+) (?P<black_clock>-?\d+) (?P<move_number>\d+) "
    r"(?P<verbose>\S+) \((?P<elapsed>[\d:.]+)\) (?P<san>\S+)")
game_end_pattern = re.compile(
    r"^\{Game (?P<game>\d+) \((?P<white>[A-Za-z]+) vs\. (?P<black>[A-Za-z]+)\) (?P<reason>[^}]*)\} "
    r"(?P<result>1-0|0-1|1/2-1/2|\*)")

# style12 relation values
playing_my_move = 1
playing_their_move = -1


class style12:
    """One validated style12 board; clocks are milliseconds (the client sets iset ms 1)."""

    def __init__(self, match):
        self.rows = match["rows"].split()
        self.turn = chess.WHITE if match["turn"] == "W" else chess.BLACK
        self.ep_file = int(match["ep"])
        self.castling = [flag == "1" for flag in match["castling"].split()]
        self.halfmove = int(match["halfmove"])
        self.game = int(match["game"])
        self.white = match["white"]
        self.black = match["black"]
        self.relation = int(match["relation"])
        self.initial_seconds = int(match["initial"]) * 60
        self.increment_seconds = int(match["increment"])
        self.white_ms = int(match["white_clock"])
        self.black_ms = int(match["black_clock"])
        self.move_number = int(match["move_number"])
        self.verbose = match["verbose"]
        self.elapsed = elapsed_seconds(match["elapsed"])
        self.san = match["san"]

    @classmethod
    def parse(cls, line):
        match = style12_pattern.search(line)
        return cls(match) if match else None

    def fen(self):
        placement = "/".join(re.sub("-+", lambda run: str(len(run.group(0))), row) for row in self.rows)
        castling = "".join(flag for flag, allowed in zip("KQkq", self.castling) if allowed) or "-"
        ep = "-"
        if self.ep_file >= 0:
            ep = "abcdefgh"[self.ep_file] + ("6" if self.turn == chess.WHITE else "3")
        side = "w" if self.turn == chess.WHITE else "b"
        return f"{placement} {side} {castling} {ep} {self.halfmove} {self.move_number}"

    def board(self, board_type=chess.Board):
        board = board_type(self.fen())
        if not board.is_valid():
            raise ValueError(f"invalid style12 position: {self.fen()}")
        return board


def elapsed_seconds(text):
    """FICS move time 'm:ss.fff' (or 'h:mm:ss.fff') in seconds."""
    seconds = 0.0
    for part in text.split(":"):
        seconds = seconds * 60 + float(part)
    return seconds


def same_position(board, position):
    return (board.board_fen() == position.fen().split()[0] and board.turn == position.turn
            and board.castling_xfen() == position.fen().split()[2])


class game_history:
    """Board with the game's moves, so the engine sees repetitions; resyncs on any mismatch."""

    def __init__(self, position, board_type=chess.Board):
        self.board_type = board_type
        self.board = position.board(board_type)

    def update(self, position):
        """Apply the server's latest board: 'move', 'takeback', 'reset', or False when nothing changed."""
        if same_position(self.board, position):
            return False
        replay = self.board.copy()
        while replay.move_stack:
            replay.pop()
            if same_position(replay, position):
                # A takeback keeps the earlier moves, so the PGN stays complete.
                self.board = replay
                return "takeback"
        if position.san != "none":
            try:
                move = self.board.parse_san(position.san)
            except ValueError:
                move = None
            if move is not None:
                self.board.push(move)
                if same_position(self.board, position):
                    return "move"
        # Takebacks, missed updates, and resumed games restart from the server board.
        self.board = position.board(self.board_type)
        return "reset"


termination_of = (("forfeits on time", "time forfeit"), ("forfeits by disconnection", "abandoned"),
                  ("adjudicat", "adjudication"))


class game_record:
    """Clock and search annotations by ply; the PGN is built from the final history so takebacks stay consistent."""

    def __init__(self, headers, started=None):
        self.started = started or datetime.now(timezone.utc)
        self.headers = dict(headers)
        self.clocks = {}
        self.searches = {}

    def on_move(self, board, clock_seconds, elapsed):
        """The server applied board.peek(); the mover's clock is now clock_seconds."""
        self.clocks[board.ply() - 1] = (board.peek(), clock_seconds, elapsed)

    def on_search(self, board, move, info):
        """Our engine chose move in board (before the move)."""
        self.searches[board.ply()] = (move, info)

    def game(self, board, result, reason):
        game = variant_game.from_board(board)
        when = self.started
        game.headers.update({"Date": when.astimezone().strftime("%Y.%m.%d"), "Round": "-", "UTCDate": when.strftime("%Y.%m.%d"),
                             "UTCTime": when.strftime("%H:%M:%S")})
        game.headers.update(self.headers)
        game.headers["Result"] = result
        game.headers["Termination"] = next((name for text, name in termination_of if text in reason), "normal")
        replay = game.board()
        for node in game.mainline():
            ply = replay.ply()
            search = self.searches.get(ply)
            if search is not None and search[0] == node.move:
                annotate_search(node, replay, search[1])
            clock = self.clocks.get(ply)
            if clock is not None and clock[0] == node.move:
                node.set_clock(clock[1])
                node.set_emt(clock[2])
            replay.push(node.move)
        end = game.end()
        end.comment = " ".join(part for part in (end.comment, reason) if part)
        return game


def annotate_search(node, board, info):
    score, depth = info.get("score"), info.get("depth")
    if score is None or not depth:
        node.comment = info.get("source") or ("tablebase" if info.get("tbhits") else "")
        return
    node.set_eval(score, depth)
    pv, replay = [], board.copy()
    for move in info.get("pv", [])[:12]:
        if not replay.is_legal(move):
            break
        pv.append(replay.san(move))
        replay.push(move)
    extras = [f"[%pv {' '.join(pv)}]"] if pv else []
    if info.get("nodes"):
        extras.append(f"[%nodes {info['nodes']}]")
    for name, key in (("nps", "nps"), ("searchtime", "_search_seconds")):
        if info.get(key) is not None:
            value = f"{info[key]:.9f}" if name == "searchtime" else str(info[key])
            extras.append(f"[%{name} {value}]")
    if info.get("_experience"):
        extras.append(info["_experience"])
    node.comment = " ".join([node.comment] + extras)


def save_game(game, directory, when):
    """Atomically archive a complete, identified game; replaying its save is idempotent."""
    from .learning_store import archive_game, prepare_game
    prepare_game(game)
    return archive_game(game, directory)[0]


def ics_move(move):
    """FICS wants promotions as e7e8=Q; other moves in coordinate form."""
    text = move.uci()
    if move.promotion:
        text = text[:4] + "=" + chess.piece_symbol(move.promotion).upper()
    return text
