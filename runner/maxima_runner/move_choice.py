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
flag_keep_lead, flag_keep_moves = 1.25, 40  # once hunting, it lasts until clearly over: no flipping each move
flag_clock, flag_keep_clock = 30, 45  # their seconds below which a flag can be near; an engine's pace looks short at any clock
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
        """Start a lookup with seven or fewer pieces, or of the captures with eight; None when it does not apply."""
        pieces = chess.popcount(board.occupied)
        if (not self.enabled or time.monotonic() < self.pause or type(board) is not chess.Board
                or board.castling_rights or pieces > online_tablebase.max_pieces + 1
                or min(pieces, online_tablebase.max_pieces) <= self.local_pieces):
            return None
        probe = online_tablebase.probe if pieces <= online_tablebase.max_pieces else online_tablebase.capture_probe
        lookup = asyncio.get_running_loop().run_in_executor(None, probe, board.copy(), tablebase_wait)
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
    book_exit_move = 0
    if opening is not None:
        clock = limit.white_clock if board.turn else limit.black_clock
        increment = (limit.white_inc if board.turn else limit.black_inc) or 0
        opening.live_nps = getattr(session, "nps", None)
        found = opening.choose(board, None if clock is None else clock / 30 + increment)
        if found is not None:
            move, reason = found
            opening.book_seen = True
            if hasattr(session, "stop_pondering"):
                await session.stop_pondering()
            return move, {"source": reason}, "book"
        timed = (limit.time is None and limit.depth is None and limit.nodes is None
                 and clock is not None)
        if timed and getattr(opening, "book_seen", False):
            positions = getattr(opening, "book_exit_searches", [])
            fen = board.fen()
            book_exit_move = (positions.index(fen) + 1 if fen in positions
                              else len(positions) + 1 if len(positions) < 3 else 0)
        search["options"] = dict(search.get("options") or {}, BookExitMove=book_exit_move,
                                 TimeFactor=long_think_percent if opening.long_think else None)
    lookup = tablebase.ask(board) if tablebase is not None else None
    if ceiling is not None:
        search["ceiling"] = ceiling
    searching = asyncio.ensure_future(session.play(board, limit, **search))
    try:
        answer = await tablebase_first(lookup, searching)
        drawing = None
        if answer is not None and answer.get("capture") and answer["category"] != "win":
            drawing, answer = answer, None  # a drawing capture only replaces a search scoring below the draw
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
    if drawing is not None and below_draw(board, result.info, (search.get("options") or {}).get("DrawContempt")):
        if hasattr(session, "stop_pondering"):
            await session.stop_pondering()
        return drawing["move"], {"source": tablebase_reason(drawing), "tablebase": drawing}, "tablebase"
    if book_exit_move and result.move is not None:
        positions = getattr(opening, "book_exit_searches", [])
        if board.fen() not in positions:
            positions.append(board.fen())
        opening.book_exit_searches = positions
    return result.move, result.info, "engine"


def below_draw(board, info, contempt):
    """The search scores our move below a draw, which the engine values at its DrawContempt (cp from our side)."""
    score = info.get("score")
    return score is not None and (score.pov(board.turn).score(mate_score=100000)
                                  < (engine_draw_value if contempt is None else contempt))


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


def winning_on_time(ours, theirs, increment, bot, lead=time_win_lead):
    """Whether the game may still end on their flag: our clock, in seconds, far enough ahead of theirs, with an
    increment too small to save them. Bots live on any increment, humans on a few seconds."""
    if increment >= (time_win_bot_increment if bot else time_win_human_increment):
        return False
    extra = time_win_moves * increment
    return ours + extra > lead * (theirs + extra)


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


def flag_chance(ours, theirs, increment, bot, pace, hunting=False):
    """Whether their flag is a real chance: no increment, our clock far enough ahead (winning_on_time), and at
    their own recent pace fewer than flag_pace_moves moves left. A bot that moves in a tenth of a second has
    fifty moves in five seconds, so a small clock alone is not enough. An engine spends a share of its clock on
    each move, so its pace leaves some 25 moves at any clock: their clock must also be below flag_clock. While
    hunting, a smaller lead, more moves left and a higher clock keep it going, so a pace or clock near the line
    does not switch it every move."""
    lead, moves, floor = ((flag_keep_lead, flag_keep_moves, flag_keep_clock) if hunting
                          else (time_win_lead, flag_pace_moves, flag_clock))
    if increment or theirs >= floor or not winning_on_time(ours, theirs, increment, bot, lead):
        return False
    left = pace.moves_left(theirs)
    return left is not None and left < moves


class draw_offers:
    """Courtesy draw offers for one game, counting draw scores and tablebase draws."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.streak = self.made = 0

    def due(self, info, contempt=None, holding=False, board=None):
        """Count a drawn move; True at the next offer interval, unless holding delays it."""
        score = info.get("score")
        ours = None if score is None or score.is_mate() or not info.get("depth") else score.relative.score()
        tablebase = info.get("tablebase")
        drawn = (tablebase["category"] in draw_categories if tablebase is not None else
                 (ours == (engine_draw_value if contempt is None else contempt)
                  or (board is not None and info.get("tbhits") and score is not None
                      and not score.is_mate() and score.relative.score() == 0
                      and chess.popcount(board.occupied) <= online_tablebase.max_pieces)))
        if not drawn:
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
