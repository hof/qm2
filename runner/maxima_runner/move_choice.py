"""MaxDrone's move choice, the same on every server: book and experience, then the engine while the online
tablebase is asked. Servers only send the move and explain it."""
import asyncio
import time

from . import online_tablebase

tablebase_wait = 1.0  # seconds at most; the engine's move stands when the answer is later


class tablebase_lookups:
    """Online tablebase lookups for one player; errors pause them for a minute."""

    def __init__(self, enabled, on_line):
        self.enabled, self.on_line = enabled, on_line
        self.pause = 0.0

    def ask(self, board):
        """Start a lookup with seven or fewer pieces; None when it does not apply."""
        if not self.enabled or time.monotonic() < self.pause or not online_tablebase.covers(board):
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


async def choose_move(session, board, limit, *, opening=None, budget_ms=None, tablebase=None, **search):
    """(move, info, kind) with kind "book", "tablebase" or "engine"; book and tablebase info carry "source".
    Engine failures propagate; search are engine_session.play keywords."""
    if opening is not None:
        found = opening.choose(board, budget_ms)
        if found is not None:
            move, reason = found
            return move, {"source": reason}, "book"
    lookup = tablebase.ask(board) if tablebase is not None else None
    searching = asyncio.ensure_future(session.play(board, limit, **search))
    try:
        answer = await tablebase_first(lookup, searching)
        if answer is not None:
            searching.cancel()
            await asyncio.wait({searching})
            return answer["move"], {"source": tablebase_reason(answer), "tablebase": answer}, "tablebase"
        result = await searching
    except asyncio.CancelledError:
        searching.cancel()
        raise
    return result.move, result.info, "engine"


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
    score = info.get("score")
    depth = info.get("depth", 0)
    if score is None or not depth:
        return "book"
    relative = score.pov(turn)
    value = f"#{relative.mate()}" if relative.is_mate() else f"{relative.score() / 100:+.2f}"
    return f"d{depth} {value}" + (f" {speed(info['nps'])}" if info.get("nps") else "")
