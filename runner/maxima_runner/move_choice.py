"""MaxDrone's move choice, the same on every server: book and experience, then the engine while the online
tablebase is asked. Servers only send the move and explain it."""
import asyncio
import time

import chess
import chess.engine

from . import online_tablebase

tablebase_wait = 1.0  # seconds at most; the engine's move stands when the answer is later
draw_categories = ("draw", "cursed-win", "blessed-loss")  # the last two are draws under the fifty-move rule
engine_draw_value = -10  # the engine's DrawContempt default, in cp from our side
long_think_percent = 175  # above 150 so the stored search can stand on its own next time
draw_offer_first, draw_offer_repeat = 4, 11  # our moves at the draw score; Lichess drops repeats within 20 plies
time_win_lead, time_win_moves = 1.5, 40  # our time over theirs, each with the increments of that many moves
time_win_bot_increment, time_win_human_increment = 1, 3  # seconds from which the opponent will not flag
flag_pace_moves, flag_pace_window = 30, 10  # a flag is a real chance when their recent pace leaves fewer moves
flag_draw_offset = -50  # ShortDrawOffset while it is: play on past quick draws; the engine floors them at -10 cp
draw_accept_lost = -150  # cp from our side: even while ahead on time, a lost position takes an offered draw


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


async def choose_move(session, board, limit, *, opening=None, tablebase=None, ceiling=None, **search):
    """(move, info, kind) with kind "book", "tablebase" or "engine"; book and tablebase info carry "source".
    Engine failures propagate; search are engine_session.play keywords."""
    if opening is not None:
        clock = limit.white_clock if board.turn else limit.black_clock
        increment = (limit.white_inc if board.turn else limit.black_inc) or 0
        opening.live_nps = getattr(session, "nps", None)
        found = opening.choose(board, None if clock is None else clock / 30 + increment)
        if found is not None:
            move, reason = found
            if hasattr(session, "stop_pondering"):
                await session.stop_pondering()
            return move, {"source": reason}, "book"
        search["options"] = dict(search.get("options") or {},
                                 TimeFactor=long_think_percent if opening.long_think else None)
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
    return result.move, result.info, "engine"


async def ponder_after_book(session, board, move, limit, opening, position, on_line, **search):
    """Think on the opponent's time while in book, so that leaving it starts with a filled hash.
    position gives the game's current FEN, None once it is over; search are ponder_book keywords."""
    after = board.copy()
    after.push(move)
    before = board.fen(), after.fen()
    waiting = lambda: position() in before
    ponder = chess.engine.Limit(
        white_clock=limit.white_clock + (limit.white_inc or 0), white_inc=limit.white_inc,
        black_clock=limit.black_clock + (limit.black_inc or 0), black_inc=limit.black_inc)
    if not waiting() or not hasattr(session, "ponder_book"):
        return
    reply = opening.predict(after)
    if reply is not None:
        ours = after.copy()
        ours.push(reply)
        if opening.book_applies(ours) and opening.predict(ours) is not None:
            return  # our answer to the likeliest reply is a book move too: nothing to think about
    try:
        await session.ponder_book(after, ponder, waiting, reply=reply, **search)
    except (asyncio.TimeoutError, AssertionError, RuntimeError, OSError, chess.engine.EngineError) as error:
        on_line(f"-- no pondering in book: {error or type(error).__name__}")


def accept_draw(board, info, contempt=None, ahead=False):
    """Whether to accept a draw offer, judged on our own choice for board: no mating material, or, unless
    we can still win on time (ahead), a tablebase draw for us, a search that reached the tablebases and scores exactly zero with seven pieces or fewer, or
    a search that values the position no higher than a draw. The engine scores a draw as its DrawContempt
    (contempt, cp from our side), so the last case never accepts while we are better than that. Ahead on time,
    a lost position (draw_accept_lost or a mate against us) still takes the draw, as a gentleman would."""
    if board.is_insufficient_material():
        return True
    score = info.get("score") if "tablebase" not in info else None
    if ahead:
        return score is not None and bool(info.get("depth")) and score.relative.score(mate_score=100000) <= draw_accept_lost
    if "tablebase" in info:
        return info["tablebase"]["category"] in draw_categories
    if score is None or score.is_mate() or not info.get("depth"):
        return False
    ours = score.relative.score()
    if info.get("tbhits") and chess.popcount(board.occupied) <= online_tablebase.max_pieces and ours == 0:
        return True
    return ours <= (engine_draw_value if contempt is None else contempt)


def winning_on_time(ours, theirs, increment, bot):
    """Whether the game may still end on their flag: our clock, in seconds, far enough ahead of theirs, with an
    increment too small to save them. Bots live on any increment, humans on a few seconds."""
    if increment >= (time_win_bot_increment if bot else time_win_human_increment):
        return False
    extra = time_win_moves * increment
    return ours + extra > time_win_lead * (theirs + extra)


class opponent_pace:
    """The opponent's seconds per move over their latest moves, from their clock at each of our turns."""

    def __init__(self):
        self.spent, self.last = [], None

    def record(self, ply, theirs, increment):
        """Their clock in seconds before our search at ply; a repeated ply, as on a retry, counts once."""
        if self.last is not None and self.last[0] == ply:
            return
        if self.last is not None:
            self.spent = (self.spent + [max(0.0, self.last[1] - theirs + increment)])[-flag_pace_window:]
        self.last = ply, theirs

    def moves_left(self, theirs):
        """Their moves left at that pace, or None before half the window is known."""
        if len(self.spent) < flag_pace_window // 2:
            return None
        pace = sum(self.spent) / len(self.spent)
        return theirs / pace if pace > 0 else None


def flag_chance(ours, theirs, increment, bot, pace):
    """Whether their flag is a real chance: no increment, our clock far enough ahead (winning_on_time), and at
    their own recent pace fewer than flag_pace_moves moves left. A bot that moves in a tenth of a second has
    fifty moves in five seconds, so a small clock alone is not enough."""
    if increment or not winning_on_time(ours, theirs, increment, bot):
        return False
    left = pace.moves_left(theirs)
    return left is not None and left < flag_pace_moves


class draw_offers:
    """Courtesy draw offers for one game, due once our searches keep scoring exactly the draw value."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.streak = self.made = 0

    def due(self, info, contempt=None, holding=False):
        """Count our move's search; True when it completes the gap before the next offer, which holding delays."""
        score = info.get("score")
        ours = None if score is None or score.is_mate() or not info.get("depth") else score.relative.score()
        if ours != (engine_draw_value if contempt is None else contempt):
            self.reset()
            return False
        self.streak += 1
        if holding or self.streak < (draw_offer_repeat if self.made else draw_offer_first):
            return False
        self.streak = 0
        self.made += 1
        return True


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
    """Shared searched-move description, marking only actual protocol ponder hits."""
    return ("pondered " if info.get("_ponder_hit") else "") + describe_search(info, turn)


def describe_search(info, turn):
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
    return f"d{depth} {value}" + (f" {speed(info['nps'])}" if info.get("nps") else "")
