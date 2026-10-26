"""Royal FICS losers: compulsory legal captures, with orthodox king safety.

Rules: https://www.freechess.org/Help/HelpFiles/losers.html
This is not python-chess's suicide/antichess variant.
"""
import chess
import chess.pgn


class royal_losers_board(chess.Board):
    aliases = ["Royal losers", "FICS losers"]
    # Maxima uses Wild=losers; its position transport remains orthodox UCI.
    uci_variant = "chess"
    xboard_variant = "losers"

    def _king_only(self, color):
        return self.occupied_co[color] == self.kings & self.occupied_co[color]

    def is_variant_end(self):
        return self._king_only(self.turn) or self._king_only(not self.turn)

    def is_variant_win(self):
        return self._king_only(self.turn) or (
            not self.is_variant_end() and not any(super().generate_legal_moves()))

    def is_variant_loss(self):
        return not self._king_only(self.turn) and self._king_only(not self.turn)

    def has_insufficient_material(self, color):
        return False

    def generate_legal_moves(self, from_mask=chess.BB_ALL, to_mask=chess.BB_ALL):
        # Capture priority is global, even when the caller requests a subset.
        moves = list(super().generate_legal_moves())
        captures = [move for move in moves if self.is_capture(move)]
        for move in captures or moves:
            if from_mask & chess.BB_SQUARES[move.from_square] and to_mask & chess.BB_SQUARES[move.to_square]:
                yield move

    def generate_legal_captures(self, from_mask=chess.BB_ALL, to_mask=chess.BB_ALL):
        return (move for move in self.generate_legal_moves(from_mask, to_mask)
                if self.is_capture(move))

    def is_legal(self, move):
        return move in self.generate_legal_moves()


class orthodox_wild_board(chess.Board):
    """Orthodox moves from a FICS wild starting position, with variant provenance."""
    uci_variant = "chess"


wild_boards = {
    name: type("wild_" + name.split("/")[1] + "_board", (orthodox_wild_board,),
               {"wild_type": name, "aliases": ["FICS " + name], "xboard_variant": name})
    for name in ("wild/2", "wild/3", "wild/4", "wild/5", "wild/8", "wild/8a")
}
experience_variants = ("standard", "losers", *wild_boards)


def board_variant(board):
    if type(board) is royal_losers_board:
        return "losers"
    if isinstance(board, orthodox_wild_board) and not board.chess960:
        return board.wild_type
    if type(board) is chess.Board and not board.chess960:
        return "standard"
    raise ValueError("unsupported board variant")


class variant_headers(chess.pgn.Headers):
    def variant(self):
        if self.get("Variant") == "Royal losers":
            return royal_losers_board
        for name, board_type in wild_boards.items():
            if self.get("Variant") == "FICS " + name:
                return board_type
        return super().variant()


class variant_game(chess.pgn.Game):
    """Royal-losers PGNs without modifying python-chess's global registry."""

    def __init__(self, headers=None):
        super().__init__(headers)
        self.headers = variant_headers(self.headers)


def read_pgn(stream):
    return chess.pgn.read_game(stream, Visitor=lambda: chess.pgn.GameBuilder(Game=variant_game))
