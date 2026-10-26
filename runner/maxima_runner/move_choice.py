"""MaxDrone's move choice, the same on every server: book and experience, then the engine while the online
tablebase is asked. Servers only send the move and explain it."""
import asyncio
import time

from .search_effort import reuse_monitor

import chess

from . import online_tablebase

tablebase_wait = 1.0  # seconds at most; the engine's move stands when the answer is later
draw_categories = ("draw", "cursed-win", "blessed-loss")  # the last two are draws under the fifty-move rule
engine_draw_value = -10  # the engine's DrawContempt default, in cp from our side


def opening_abort_position(board, move, info, kind, contempt):
    """The reply position eligible for an opening-stall abort, using our last move's score."""
    score = info.get("score")
    ours = score.pov(board.turn).score(mate_score=100000) if score is not None else None
    if kind != "book" and (ours is None or ours > (engine_draw_value if contempt is None else contempt)):
        return None
    reply = board.copy()
    reply.push(move)
    return reply.fen() if reply.fullmove_number <= 4 and not reply.is_game_over() else None


def opening_stall_wait(initial_seconds, minimum=60):
    """Allow at least a minute, or a fifth of the starting clock in longer games."""
    return max(minimum, initial_seconds / 5)


class tablebase_lookups:
    """Online tablebase lookups for one player, for positions beyond the engine's local tables (local_pieces);
    errors pause them for a minute."""

    def __init__(self, enabled, on_line, local_pieces=0):
        self.enabled, self.on_line, self.local_pieces = enabled, on_line, local_pieces
        self.pause = 0.0

    def ask(self, board):
        """Start a lookup with seven or fewer pieces; None when it does not apply."""
        if (not self.enabled or time.monotonic() < self.pause or not online_tablebase.covers(board)
                or chess.popcount(board.occupied) <= self.local_pieces):
            return None
        lookup = asyncio.get_running_loop().run_in_executor(None, online_tablebase.probe, board.copy(),
                                                            tablebase_wait)
        lookup.add_done_callback(self.failed)
        return lookup

    def failed(self, lookup):
        error = None if lookup.cancelled() else lookup.exception()
        if error is not None:
            # Offline, rate limited or slow: rest a minute rather than ask on every move.
            self.pause = time.monotonic() + 60
            self.on_line(f"-- online tablebase unavailable ({error or type(error).__name__}); searching")


async def tablebase_first(lookup, search):
    """The tablebase answer when it arrives before the engine's move, otherwise None."""
    if lookup is None:
        return None
    await asyncio.wait({lookup, search}, return_when=asyncio.FIRST_COMPLETED)
    if not lookup.done() or lookup.exception() is not None:
        return None
    return lookup.result()


async def choose_move(session, board, limit, *, opening=None, budget_ms=None, tablebase=None, ceiling=None, learning_nps=None, learning_margin=0.40, **search):
    """(move, info, kind) with kind "book", "tablebase" or "engine"; book and tablebase info carry "source".
    Engine failures propagate; search are engine_session.play keywords."""
    if opening is not None:
        found = opening.choose(board, budget_ms)
        if found is not None:
            move, reason = found
            if hasattr(session, "stop_pondering"):
                await session.stop_pondering()
            return move, {"source": reason}, "book"
    monitor = None
    if opening is not None and getattr(opening, "reuse", None) is not None:
        monitor = reuse_monitor(*opening.reuse, budget_ms,
                                getattr(opening, "learning_nps", learning_nps),
                                getattr(opening, "learning_margin", learning_margin))
        search["on_info"] = monitor
    lookup = tablebase.ask(board) if tablebase is not None else None
    if ceiling is not None:
        search["ceiling"] = ceiling
    searching = asyncio.ensure_future(session.play(board, limit, **search))
    try:
        answer = await tablebase_first(lookup, searching)
        if answer is not None:
            searching.cancel()
            await asyncio.wait({searching})
            if hasattr(session, "stop_pondering"):
                await session.stop_pondering()
            return answer["move"], {"source": tablebase_reason(answer), "tablebase": answer}, "tablebase"
        result = await searching
    except asyncio.CancelledError:
        searching.cancel()
        raise
    if monitor is not None and monitor.stopped and result.move == monitor.move:
        result.info["_experience"] = f"recalled d{monitor.saved[0]}"
    return result.move, result.info, "engine"


def accept_draw(board, info, contempt=None):
    """Whether to accept a draw offer, judged on our own choice for board: no mating material, a tablebase
    draw for us, a search that reached the tablebases and scores exactly zero with seven pieces or fewer, or
    a search that values the position no higher than a draw. The engine scores a draw as its DrawContempt
    (contempt, cp from our side), so the last case never accepts while we are better than that."""
    if board.is_insufficient_material():
        return True
    if "tablebase" in info:
        return info["tablebase"]["category"] in draw_categories
    score = info.get("score")
    if score is None or score.is_mate() or not info.get("depth"):
        return False
    ours = score.relative.score()
    if info.get("tbhits") and chess.popcount(board.occupied) <= online_tablebase.max_pieces and ours == 0:
        return True
    return ours <= (engine_draw_value if contempt is None else contempt)


def tablebase_reason(answer):
    distance = f" DTM {abs(answer['dtm'])}" if answer.get("dtm") else (
        f" DTZ {abs(answer['dtz'])}" if answer.get("dtz") else "")
    return f"tablebase: {answer['category']}{distance}"


def search_summary(board, info):
    """Our search for the live feed: scores from White's side; book and tablebase moves carry their reason."""
    if "tablebase" in info:
        answer = info["tablebase"]
        return {"tablebase": {"category": answer["category"], "dtz": answer.get("dtz"), "dtm": answer.get("dtm")}}
    score = info.get("score")
    if score is None:
        return {"book": info.get("source", "book")}
    white = score.white()
    pv, replay = [], board.copy()
    for move in info.get("pv", []):
        if not replay.is_legal(move):
            break
        pv.append(replay.san(move))
        replay.push(move)
    summary = {"depth": info.get("depth"), "seldepth": info.get("seldepth"), "nodes": info.get("nodes"),
               "nps": info.get("nps"), "time_ms": round(info["time"] * 1000) if "time" in info else None,
               "pv_san": pv}
    summary["score"] = {"mate": white.mate()} if white.is_mate() else {"cp": white.score()}
    return summary


def variation(board, moves, limit=10):
    """The engine's expected line in numbered SAN ("12...Rfe8 13. b3 b5"), up to limit moves or the first
    illegal one."""
    replay, legal = board.copy(), []
    for move in moves[:limit]:
        if not replay.is_legal(move):
            break
        legal.append(move)
        replay.push(move)
    return board.variation_san(legal) if legal else ""


def speed(nps):
    """Nodes per second the short way: 850nps, 500Knps, 1.2Mnps."""
    if nps >= 1_000_000:
        return f"{nps / 1_000_000:.1f}Mnps"
    if nps >= 1_000:
        return f"{nps / 1_000:.0f}Knps"
    return f"{nps}nps"


def describe(info, turn):
    """A searched move for the log: "d20 +0.35 480Knps"; the engine's own tablebase moves come without a
    search ("TB win")."""
    score = info.get("score")
    depth = info.get("depth", 0)
    if score is None:
        return "no score"
    relative = score.pov(turn)
    if not depth:
        if not info.get("tbhits"):
            return "no search"
        value = relative.score(mate_score=100000)
        return "TB " + ("win" if value > 0 else "loss" if value < 0 else "draw")
    value = f"#{relative.mate()}" if relative.is_mate() else f"{relative.score() / 100:+.2f}"
    return (f"d{depth} {value}" + (f" {speed(info['nps'])}" if info.get("nps") else "")
            + (f" ({info['_experience']})" if info.get("_experience") else ""))
