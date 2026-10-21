"""FICS game state: style12 boards, move history, coordinate moves, and game endings."""
import re

import chess

style12_pattern = re.compile(
    r"<12> (?P<rows>(?:[pnbrqkPNBRQK-]{8} ){8})(?P<turn>[WB]) (?P<ep>-1|[0-7]) "
    r"(?P<castling>(?:[01] ){4})(?P<halfmove>\d+) (?P<game>\d+) (?P<white>[A-Za-z]+) (?P<black>[A-Za-z]+) "
    r"(?P<relation>-?[0-3]) (?P<initial>\d+) (?P<increment>\d+) \d+ \d+ "
    r"(?P<white_clock>-?\d+) (?P<black_clock>-?\d+) (?P<move_number>\d+) "
    r"(?P<verbose>\S+) \(\S+\) (?P<san>\S+)")
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

    def board(self):
        board = chess.Board(self.fen())
        if not board.is_valid():
            raise ValueError(f"invalid style12 position: {self.fen()}")
        return board


def same_position(board, position):
    return (board.board_fen() == position.fen().split()[0] and board.turn == position.turn
            and board.castling_xfen() == position.fen().split()[2])


class game_history:
    """Board with the game's moves, so the engine sees repetitions; resyncs on any mismatch."""

    def __init__(self, position):
        self.board = position.board()

    def update(self, position):
        """Apply the server's latest board; returns False when nothing changed."""
        if same_position(self.board, position):
            return False
        if position.san != "none":
            try:
                move = self.board.parse_san(position.san)
            except ValueError:
                move = None
            if move is not None:
                self.board.push(move)
                if same_position(self.board, position):
                    return True
        # Takebacks, missed updates, and resumed games restart from the server board.
        self.board = position.board()
        return True


def ics_move(move):
    """FICS wants promotions as e7e8=Q; other moves in coordinate form."""
    text = move.uci()
    if move.promotion:
        text = text[:4] + "=" + chess.piece_symbol(move.promotion).upper()
    return text
