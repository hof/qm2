"""Online tablebase lookups: perfect play with up to seven pieces from tablebase.lichess.ovh.

A game source asks before searching; any failure (offline, slow, unknown position) means the
engine searches as usual. See MAXIMA.md 11.4."""
import json
import urllib.parse
import urllib.request

import chess

url = "https://tablebase.lichess.ovh/standard"
max_pieces = 7
user_agent = "MaxDrone (Maxima chess engine; https://github.com/hof/qm2)"


def covers(board):
    """Standard positions with at most seven pieces and no castling rights are in the tables."""
    return (type(board) is chess.Board and chess.popcount(board.occupied) <= max_pieces
            and not board.castling_rights)


def probe(board, timeout=2.0):
    """The best move and the position's category, or None when the tables do not answer."""
    if not covers(board):
        return None
    request = urllib.request.Request(f"{url}?{urllib.parse.urlencode({'fen': board.fen()})}",
                                     headers={"User-Agent": user_agent})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        answer = json.load(response)
    moves = answer.get("moves") or []
    if answer.get("category") in (None, "unknown") or not moves:
        return None
    move = chess.Move.from_uci(moves[0]["uci"])
    if move not in board.legal_moves:
        return None
    return {"move": move, "category": answer["category"], "dtz": answer.get("dtz"), "dtm": answer.get("dtm")}
