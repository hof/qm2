"""Build draw.bin from the Lichess masters explorer: the lines where the moving side loses least
and draws most (MAXIMA.md 9.7)."""
import argparse
import heapq
import json
import os
from pathlib import Path
import time
import urllib.error
import urllib.parse
import urllib.request

import chess
import chess.polyglot

from . import learning

explorer_url = "https://explorer.lichess.ovh/masters"


def safety(white, draws, black, color):
    """Draw rate minus twice the loss rate, for the side playing color: boring and safe scores high."""
    games = white + draws + black
    if not games:
        return None
    losses = black if color == chess.WHITE else white
    return (draws - 2 * losses) / games


def lichess_token(root):
    """LICHESS_TOKEN from the environment, else from <project>/.env; never logged."""
    token = os.environ.get("LICHESS_TOKEN")
    env = Path(root) / ".env"
    if not token and env.is_file():
        for line in env.read_text(encoding="utf-8").splitlines():
            name, _, value = line.partition("=")
            if name.strip() == "LICHESS_TOKEN":
                token = value.strip().strip('"').strip("'")
    return token or None


class explorer:
    """Masters explorer queries, cached on disk by position, one request at a time."""

    def __init__(self, token, cache_dir, pause=1.0, on_line=print):
        self.token, self.cache_dir, self.pause, self.on_line = token, Path(cache_dir), pause, on_line
        self.requests = 0
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def __call__(self, board):
        epd = board.epd()
        cache = self.cache_dir / f"{chess.polyglot.zobrist_hash(board):016x}.json"
        if cache.is_file():
            data = json.loads(cache.read_text(encoding="utf-8"))
            if data.get("epd") == epd:
                return data["moves"]
        query = urllib.parse.urlencode({"fen": board.fen(), "moves": 12, "topGames": 0})
        request = urllib.request.Request(f"{explorer_url}?{query}",
                                         headers={"Authorization": f"Bearer {self.token}", "User-Agent": "MaxDrone"})
        for attempt in range(5):
            try:
                with urllib.request.urlopen(request, timeout=30) as response:
                    moves = json.load(response)["moves"]
                break
            except urllib.error.HTTPError as error:
                if error.code == 401:
                    raise RuntimeError("the Lichess explorer rejected LICHESS_TOKEN") from None
                if error.code != 429 or attempt == 4:
                    raise
                self.on_line("-- explorer rate limit; waiting 60 s")
                time.sleep(60)
        self.requests += 1
        time.sleep(self.pause)
        moves = [{"uci": move["uci"], "white": move["white"], "draws": move["draws"], "black": move["black"]}
                 for move in moves]
        temporary = cache.with_suffix(".tmp")
        temporary.write_text(json.dumps({"epd": epd, "moves": moves}), encoding="utf-8")
        temporary.replace(cache)
        return moves


def build(fetch, plies=16, min_games=50, margin=0.05, min_reply=0.05, min_reach=0.002, budget=2000):
    """{(key, code): weight} for both colours.

    Our moves: those within margin of the safest (by safety()), with at least min_games, weight 100
    for the safest down to 10 at the margin. Opponent moves: every reply played in at least min_reply
    of games. Positions are expanded most likely first while their reach is at least min_reach, up to
    budget positions per colour."""
    weights = {}
    for color in (chess.WHITE, chess.BLACK):
        queue, seen, counter, left = [(-1.0, 0, chess.Board())], set(), 1, budget
        while queue and left > 0:
            reach, _, board = heapq.heappop(queue)
            reach = -reach
            epd = board.epd()
            if epd in seen or board.ply() >= plies or reach < min_reach:
                continue
            seen.add(epd)
            moves = fetch(board)
            left -= 1
            total = sum(move["white"] + move["draws"] + move["black"] for move in moves)
            if board.turn == color:
                scored = []
                for move in moves:
                    games = move["white"] + move["draws"] + move["black"]
                    parsed = chess.Move.from_uci(move["uci"])
                    if games >= min_games and board.is_legal(parsed):
                        scored.append((safety(move["white"], move["draws"], move["black"], color), parsed))
                if not scored:
                    continue
                best = max(score for score, _ in scored)
                for score, move in scored:
                    if score < best - margin:
                        continue
                    weight = 10 + 90 * (1 - (best - score) / margin) if margin else 100
                    key = chess.polyglot.zobrist_hash(board)
                    weights[(key, learning.polyglot_code(board, move))] = round(weight)
                    child = board.copy(stack=False)
                    child.push(move)
                    heapq.heappush(queue, (-reach * weight / 100, counter, child))
                    counter += 1
            else:
                for move in moves:
                    games = move["white"] + move["draws"] + move["black"]
                    parsed = chess.Move.from_uci(move["uci"])
                    if total and games / total >= min_reply and games >= min_games and board.is_legal(parsed):
                        child = board.copy(stack=False)
                        child.push(parsed)
                        heapq.heappush(queue, (-reach * games / total, counter, child))
                        counter += 1
    return weights


def main():
    from .project import data_root, project_root
    root = data_root()
    parser = argparse.ArgumentParser(prog="maxima experience:draw", description=__doc__)
    parser.add_argument("--output", default=str(root / "draw.bin"), help="book to write (default: draw.bin)")
    parser.add_argument("--plies", type=int, default=16, help="depth of the book in plies (default: 16)")
    parser.add_argument("--min-games", type=int, default=50, help="master games a move needs (default: 50)")
    parser.add_argument("--margin", type=float, default=0.05,
                        help="keep our moves this close to the safest (default: 0.05)")
    parser.add_argument("--budget", type=int, default=2000,
                        help="most positions to query per colour (default: 2000)")
    parser.add_argument("--cache-dir", default=str(root / "experience" / "explorer"),
                        help="explorer answers by position (default: experience/explorer)")
    options = parser.parse_args()
    token = lichess_token(project_root())
    if not token:
        parser.error("set LICHESS_TOKEN (environment or .env); a token without scopes is enough")
    fetch = explorer(token, options.cache_dir)
    weights = build(fetch, options.plies, options.min_games, options.margin, budget=options.budget)
    count = learning.write_book(weights, options.output)
    print(f"{options.output}: {count} entries, {fetch.requests} new explorer queries")
    return 0
