"""MaximaEngine on Lichess: accept challenges and play them through the Bot API, with MaxDrone's book,
learning, online tablebase and connectors (MAXIMA.md 10.3)."""
import asyncio
from contextlib import asynccontextmanager
import json
from pathlib import Path
import random
import signal
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

import chess
import chess.engine

from . import connectors, fics_game, learning, move_choice, online_tablebase, resignation, online_shutdown
from .fics import book_whisper, chess_titles, clock_text, draw_contempt, layer_profiles, learning_report, read_commands, \
    restart_status, style_books
from .move_choice import describe, search_summary, variation
from .project import data_root, engine_path, env_value
from .uci import open_engine

server_url = "https://lichess.org"
speeds = ("bullet", "blitz", "rapid", "classical")
speed_rank = {name: rank for rank, name in enumerate(("ultraBullet",) + speeds + ("correspondence",))}
stream_timeout = 30  # seconds of silence before a stream counts as dead; Lichess sends keepalives every 7 s
rate_limit_pause = 60  # Lichess asks clients to wait a full minute after HTTP 429
owner_commands = ("softquit", "softrestart", "resign", "abort", "draw", "challenge", "nochallenge")
console_commands = ("help", "quit") + owner_commands
public_commands = ("help", "name", "eval", "stats", "book")
public_gap = 5  # seconds between answers to anyone's commands, so chat spam cannot exhaust the rate limit
credits = "Maxima, a chess engine by hof and hajewiet"  # no links: Lichess blanks chat lines with URLs
# Each room gets its own wording: Lichess silently drops a line equal to one of our previous two.
player_greeting = "Hi, I'm Maxima running on MaxDrone. Good luck!"
greeting = "Hi! I'm Maxima, running on MaxDrone. Type !help for available commands."
player_farewells = ("Good game, thanks for playing!", "Thanks for the game!")
# Our own challenges after idle time: (limit, increment) seconds per speed and each speed's share.
challenge_clocks = {"bullet": ((60, 0), (60, 1), (120, 1)), "blitz": ((180, 0), (180, 2), (300, 0), (300, 3)),
                    "rapid": ((600, 0), (600, 5), (900, 10))}
challenge_shares = {"bullet": 30, "blitz": 50, "rapid": 20}
challenge_wait = 60  # seconds for an answer before we cancel, and between attempts
challenge_rest = 24 * 3600  # seconds a bot is left alone for a speed after it declined
rematch_delay = 5  # seconds after a game before we offer a bot one rematch
book_layers = {"book": "base", "global": "all games"}  # chat names; the others keep their layer name
group_labels = {"engines": "bots"}
reasons = {"mate": "checkmate", "resign": "resignation", "stalemate": "stalemate", "timeout": "opponent left",
           "draw": "draw", "outoftime": "time forfeit", "cheat": "cheat detected", "noStart": "no first move",
           "aborted": "aborted", "unknownFinish": "unknown finish", "variantEnd": "variant end"}
terminations = {"outoftime": "time forfeit", "timeout": "abandoned", "noStart": "abandoned", "aborted": "abandoned"}
default_settings = {
    "resignation": resignation.validate_settings({}),
    "shutdown_grace": 15,
    "engine": None, "options": {}, "speeds": ["bullet", "blitz", "rapid"], "accept": True, "games": None,
    "pgn_dir": None, "learning": True, "variety": 1.0, "contempt_window": 100, "layer_profiles": {},
    "style_books": {}, "mood": "auto", "live_feed": None, "pgn_post": None, "online_tablebase": True,
    "challenge_idle": 30, "challenge_window": 300,
}


class lichess_api:
    """Blocking urllib calls run in threads; ndjson streams are read line by line by a thread."""

    def __init__(self, token, server=server_url, on_line=print):
        self.token, self.server, self.on_line = token, server.rstrip("/"), on_line
        self.paused_until = 0.0

    def open(self, method, path, data=None, timeout=10):
        body = urllib.parse.urlencode(data).encode() if data is not None else (b"" if method == "POST" else None)
        request = urllib.request.Request(self.server + path, body, method=method,
                                         headers={"Authorization": f"Bearer {self.token}",
                                                  "User-Agent": "MaximaEngine (https://github.com/hof/qm2)"})
        try:
            return urllib.request.urlopen(request, timeout=timeout)
        except urllib.error.HTTPError as error:
            if error.code == 429:
                self.paused_until = time.monotonic() + rate_limit_pause
            raise

    async def wait_for_limit(self):
        remaining = self.paused_until - time.monotonic()
        if remaining > 0:
            self.on_line(f"-- rate limited by Lichess; waiting {remaining:.0f}s")
            await asyncio.sleep(remaining)

    async def call(self, method, path, data=None):
        """Parsed JSON, or None after logging the failure."""
        await self.wait_for_limit()

        def send():
            with self.open(method, path, data) as response:
                return json.loads(response.read() or b"null")

        try:
            return await asyncio.get_running_loop().run_in_executor(None, send)
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")[:200]
            self.on_line(f"-- Lichess {method} {path}: HTTP {error.code} {detail}")
        except (OSError, ValueError) as error:
            self.on_line(f"-- Lichess {method} {path}: {error}")
        return None

    async def post(self, path, data=None):
        return await self.call("POST", path, data) is not None

    async def stream(self, path):
        """Each JSON line of an ndjson stream; ends when the server closes it, raises OSError when it fails."""
        await self.wait_for_limit()
        loop = asyncio.get_running_loop()
        lines = asyncio.Queue()
        stop = threading.Event()

        def put(item):
            try:
                loop.call_soon_threadsafe(lines.put_nowait, item)
            except RuntimeError:
                pass  # the loop is gone

        def reader():
            try:
                with self.open("GET", path, timeout=stream_timeout) as response:
                    for raw in response:
                        if stop.is_set():
                            return
                        if raw.strip():
                            put(json.loads(raw))
            except (OSError, ValueError) as error:
                put(error)
            finally:
                put(None)

        threading.Thread(target=reader, daemon=True).start()
        try:
            while True:
                item = await lines.get()
                if item is None:
                    return
                if isinstance(item, Exception):
                    raise OSError(str(item) or type(item).__name__)
                yield item
        finally:
            stop.set()


class matchmaker:
    """Challenge another online bot after idle minutes: rated, a random clock weighted by speed, opponents
    within window rating points, never the last opponent or a bot that declined that speed recently."""

    def __init__(self, idle_minutes, window, speeds, on_line, choose=random):
        self.idle, self.window, self.on_line, self.random = idle_minutes * 60, window, on_line, choose
        self.speeds = [speed for speed in challenge_clocks if speed in speeds]
        self.enabled = bool(idle_minutes) and bool(self.speeds)
        self.idle_since = self.next_try = time.monotonic()
        self.challenging = False
        self.pending = None  # (challenge id, opponent id, speed, sent)
        self.resting = {}  # (opponent id, speed) -> monotonic time until which we leave it alone
        self.last_opponent = None
        self.streak = 0  # games in a row against last_opponent

    def busy(self, opponent=None):
        """A game started or ended: the idle time starts again."""
        self.idle_since = time.monotonic()
        self.pending = None
        if opponent:
            opponent = opponent.lower()
            self.streak = self.streak + 1 if opponent == self.last_opponent else 1
            self.last_opponent = opponent

    async def rematch(self, bot, name, opponent, speed, limit, increment, rated=True, delay=rematch_delay):
        """Offer a bot one rematch with the same clock after a few seconds, unless the game was a rematch
        already, something else started, or the bot declined that speed recently."""
        await asyncio.sleep(delay)
        now = time.monotonic()
        if (not self.enabled or self.streak >= 2 or self.pending is not None or bot.game is not None
                or bot.stopping or bot.accepted[0] or self.resting.get((opponent, speed), 0) > now
                or bot.games is not None and bot.played >= bot.games):
            return
        await self.challenge(bot, name, opponent, speed, limit, increment, rated, "rematch")

    async def challenge(self, bot, name, opponent, speed, limit, increment, rated=True, why="challenging"):
        if bot.stopping:
            return
        self.challenging = True
        try:
            answer = await bot.api.call("POST", f"/api/challenge/{name}", {
                "rated": "true" if rated else "false", "clock.limit": limit, "clock.increment": increment,
                "color": "random", "variant": "standard"})
            challenge_id = ((answer or {}).get("challenge") or answer or {}).get("id")
            if challenge_id:
                self.pending = (challenge_id, opponent, speed, time.monotonic())
                self.on_line(f"-- {why} {name} ({speed} {limit / 60:g}+{increment}, {'rated' if rated else 'casual'})")
        finally:
            self.challenging = False

    def due(self, now):
        return (self.enabled and self.pending is None and now >= self.next_try
                and now - self.idle_since >= self.idle)

    def pick(self, bots, ratings, own_id, now):
        """(name, opponent id, speed, limit, increment) for a challenge, or None without a fitting bot."""
        speed = self.random.choices(self.speeds, [challenge_shares[name] for name in self.speeds])[0]
        ours = ratings.get(speed)
        if ours is None:
            return None
        fitting = [bot for bot in bots if bot.get("id") not in (own_id, self.last_opponent)
                   and not bot.get("disabled") and not bot.get("tosViolation")
                   and self.resting.get((bot.get("id"), speed), 0) <= now
                   and abs(bot.get("perfs", {}).get(speed, {}).get("rating", ours + self.window + 1) - ours)
                   <= self.window]
        if not fitting:
            return None
        bot = self.random.choice(fitting)
        limit, increment = self.random.choice(challenge_clocks[speed])
        return bot.get("username", bot["id"]), bot["id"], speed, limit, increment

    async def tick(self, bot):
        """Send one challenge when due; cancel an unanswered one after challenge_wait seconds."""
        now = time.monotonic()
        if self.pending is not None:
            challenge_id, opponent, speed, sent = self.pending
            if now - sent >= challenge_wait:
                self.pending, self.next_try = None, now + challenge_wait
                self.resting[(opponent, speed)] = now + challenge_rest
                self.on_line(f"-- no answer from {opponent}; challenge cancelled")
                await bot.api.post(f"/api/challenge/{challenge_id}/cancel")
            return
        if not self.due(now) or bot.game is not None or bot.stopping or bot.accepted[0]:
            return
        self.next_try = now + challenge_wait
        account = await bot.api.call("GET", "/api/account")
        bots = [line async for line in bot.api.stream("/api/bot/online?nb=100")]
        ratings = {speed: perf["rating"] for speed, perf in (account or {}).get("perfs", {}).items()
                   if isinstance(perf, dict) and "rating" in perf}
        choice = self.pick(bots, ratings, bot.id, now)
        if choice is None:
            return
        await self.challenge(bot, *choice)

    def declined(self, challenge):
        """Our challenge was declined: leave that bot alone for that speed."""
        if self.pending is not None and self.pending[0] == challenge.get("id"):
            _, opponent, speed, _ = self.pending
            now = time.monotonic()
            self.pending, self.next_try = None, now + challenge_wait
            self.resting[(opponent, speed)] = now + challenge_rest
            self.on_line(f"-- {opponent} declined: {challenge.get('declineReasonKey') or challenge.get('declineReason')}")


class lichess_bot:
    """Accept challenges one game at a time and play them; owners steer it from the game chat."""

    def __init__(self, api, open_session, on_line, *, speeds=("bullet", "blitz", "rapid"), accept=True, games=None,
                 owners=(), learner=None, pgn_dir=None, events=None, online_tablebase=True, contempt_window=100,
                 margin=5, local_pieces=0, challenge_idle=0, challenge_window=300, resignation_settings=None):
        self.resignation_settings = resignation.validate_settings(
            {} if resignation_settings is None else resignation_settings)
        self.api, self.open_session, self.on_line = api, open_session, on_line
        self.speeds, self.accept, self.games = tuple(speeds), accept, games
        self.owners = {name.lower() for name in owners}
        self.learner, self.pgn_dir, self.events = learner, pgn_dir, events
        self.tablebase = move_choice.tablebase_lookups(online_tablebase, on_line, local_pieces)
        self.contempt_window, self.margin = contempt_window, margin
        self.context = self.session = None
        self.id = self.name = None
        self.game = None
        self.accepted = (None, 0.0)  # a challenge accepted whose game has not started yet
        self.played = 0
        self.stopping = self.restart = False
        self.matchmaker = matchmaker(challenge_idle, challenge_window, self.speeds, on_line)

    @property
    def finished(self):
        return (self.stopping and self.game is None and not self.matchmaker.challenging
                and self.matchmaker.pending is None
                and (self.accepted[0] is None or time.monotonic() - self.accepted[1] >= 20))

    async def start(self):
        account = await self.api.call("GET", "/api/account")
        if account is None:
            raise RuntimeError("Lichess rejected LICHESS_BOT_TOKEN")
        if account.get("title") != "BOT":
            raise RuntimeError(f"{account.get('username')} is not a bot account (MAXIMA.md 10.3)")
        self.id, self.name = account["id"], account["username"]
        if self.stopping:
            return
        await self.start_engine()
        self.on_line(f"-- logged in as {self.name}; accepting {', '.join(self.speeds)}"
                     + (f"; owners {', '.join(sorted(self.owners))}" if self.owners else "; no owners for commands"))
        self.on_line("-- type help for console commands")

    async def start_engine(self):
        self.context = self.open_session()
        self.session = await self.context.__aenter__()

    async def stop_engine(self):
        if self.context is not None:
            context, self.context, self.session = self.context, None, None
            await context.__aexit__(None, None, None)

    async def close(self):
        if self.game is not None:
            self.game.cancel()
        await self.stop_engine()

    def soft_quit(self, restart=False):
        self.stopping, self.restart = True, restart
        action = "restarting" if restart else "stopping"
        self.on_line(f"-- {action} after this game; quit again to stop now" if self.game is not None
                     else f"-- {action}")

    def set_challenging(self, enabled):
        self.matchmaker.enabled = enabled and bool(self.matchmaker.speeds)
        return "challenge other bots when idle" if self.matchmaker.enabled else "only accept challenges"

    async def console(self, text):
        """Run a console command (a leading ! is optional); True stops at once (quit while stopping)."""
        words = text.strip().lstrip("!").lower().split()
        if not words:
            return False
        command = "quit" if words[0] in ("exit", "softquit") else words[0]
        if command in ("quit", "softrestart"):
            if self.stopping and command == "quit":
                return True
            self.soft_quit(restart=command == "softrestart")
        elif command in ("challenge", "nochallenge"):
            self.on_line(f"-- {self.set_challenging(command == 'challenge')}")
        elif command in ("resign", "abort", "draw"):
            if self.game is None:
                self.on_line(f"-- no game to {command}")
            elif await self.api.post(self.game.path("draw/yes" if command == "draw" else command)):
                self.on_line(f"-- {command} sent")
            else:
                self.on_line(f"-- could not {command}")
        elif command == "help":
            self.on_line("-- commands: " + ", ".join(console_commands) + "; quit stops after the game")
        else:
            self.on_line(f"-- unknown command {words[0]!r}; type help")
        return False

    async def run(self, commands):
        """Follow the event stream until stopped; console quit stops after the game, a second quit at once."""
        events = asyncio.ensure_future(self.follow_events())
        command = asyncio.ensure_future(commands.get())
        try:
            while not self.finished:
                if self.stopping and self.matchmaker.pending is not None:
                    await self.cancel_challenge()
                done, _ = await asyncio.wait({events, command}, timeout=1, return_when=asyncio.FIRST_COMPLETED)
                if events in done:
                    events.result()
                if not self.stopping and (self.games is None or self.played < self.games):
                    try:
                        await self.matchmaker.tick(self)
                    except OSError as error:
                        self.on_line(f"-- could not look for an opponent: {error}")
                if command in done:
                    if await self.console(command.result()):
                        break
                    command = asyncio.ensure_future(commands.get())
            return restart_status if self.restart else 0
        finally:
            events.cancel()
            command.cancel()
            await asyncio.gather(events, command, return_exceptions=True)
            if self.stopping and self.matchmaker.pending is not None:
                try:
                    await asyncio.wait_for(self.cancel_challenge(), 2)
                except asyncio.TimeoutError:
                    self.on_line("-- outgoing challenge cancellation timed out")

    async def cancel_challenge(self):
        pending = self.matchmaker.pending
        if pending is not None and await self.api.post(f"/api/challenge/{pending[0]}/cancel"):
            if self.matchmaker.pending == pending:
                self.matchmaker.pending = None

    async def follow_events(self):
        attempt = 0
        while True:
            try:
                async for event in self.api.stream("/api/stream/event"):
                    attempt = 0
                    try:
                        await self.on_event(event)
                    except (KeyError, TypeError, ValueError) as error:
                        self.on_line(f"-- ignored event {event.get('type')}: {error!r}")
                self.on_line("-- event stream closed by Lichess; reconnecting")
            except OSError as error:
                self.on_line(f"-- event stream failed ({error}); reconnecting")
            await asyncio.sleep(min(60, 2 ** attempt))
            attempt += 1

    async def on_event(self, event):
        kind = event.get("type")
        if kind == "challenge":
            await self.on_challenge(event["challenge"], event.get("compat", {}))
        elif kind == "challengeCanceled" and event["challenge"]["id"] == self.accepted[0]:
            self.accepted = (None, 0.0)
        elif kind == "challengeDeclined":
            self.matchmaker.declined(event["challenge"])
        elif kind == "gameStart":
            game_id = event["game"]["gameId"]
            if self.accepted[0] == game_id:
                self.accepted = (None, 0.0)
            if self.game is None:
                self.matchmaker.busy((event["game"].get("opponent") or {}).get("id"))
                self.game = lichess_game(self, game_id)
                self.game.start()
            elif self.game.id != game_id:
                self.on_line(f"-- ignoring game {game_id}: already playing {self.game.id}")
        elif kind == "gameFinish" and self.game is not None and self.game.id == event["game"]["gameId"]:
            game = event["game"]
            await self.game.finish(game.get("status", {}).get("name", "unknownFinish"), game.get("winner"))

    def decline_reason(self, challenge, compat):
        """Lichess's own decline reasons, or None to accept."""
        if challenge["variant"]["key"] != "standard":
            return "standard"
        speed = challenge["speed"]
        if (speed not in self.speeds or challenge.get("timeControl", {}).get("type") != "clock"
                or compat.get("bot") is False):
            allowed = sorted(speed_rank[name] for name in self.speeds)
            rank = speed_rank.get(speed, len(speed_rank))
            return "tooFast" if rank < allowed[0] else "tooSlow" if rank > allowed[-1] else "timeControl"
        accepted, when = self.accepted
        if not self.accept or self.stopping or self.game is not None or accepted and time.monotonic() - when < 20:
            return "later"
        if self.games is not None and self.played >= self.games:
            return "later"
        return None

    async def on_challenge(self, challenge, compat):
        challenger = challenge.get("challenger") or {}
        if challenger.get("id") == self.id:
            return  # our own challenge
        name, clock = challenger.get("name", "?"), challenge.get("timeControl", {})
        about = (f"{name} ({challenger.get('rating', '?')}) {challenge['speed']} "
                 f"{clock.get('show', '')} {'rated' if challenge.get('rated') else 'casual'}").replace("  ", " ")
        reason = self.decline_reason(challenge, compat)
        if reason is None:
            self.accepted = (challenge["id"], time.monotonic())
            self.on_line(f"-- accepting {about}")
            if not await self.api.post(f"/api/challenge/{challenge['id']}/accept"):
                self.accepted = (None, 0.0)
        else:
            self.on_line(f"-- declining {about}: {reason}")
            await self.api.post(f"/api/challenge/{challenge['id']}/decline", {"reason": reason})

    def ended(self):
        self.game = None
        self.matchmaker.busy()
        self.played += 1
        if self.games is not None and self.played >= self.games:
            self.on_line(f"-- played {self.played} games; stopping")
            self.stopping = True


class lichess_game:
    """One game: follow its stream, keep the board, record and events, and play our moves."""

    def __init__(self, bot, game_id):
        self.loss = resignation.loss_policy(bot.resignation_settings)
        self.generation = object()
        self.state = None
        self.state_received_at = None
        self.reply_clock = None
        self.bot, self.id = bot, game_id
        self.initial = self.board = self.record = self.color = self.opponent = None
        self.full = self.opening = self.contempt = None
        self.task = self.searching = None
        self.searched = None
        self.clocks = {}
        self.last_eval = self.last_book = self.left_book = None
        self.answered = 0.0
        self.over = self.draw_offered = False

    def start(self):
        self.task = asyncio.ensure_future(self.follow())

    def cancel(self):
        for task in (self.searching, self.task):
            if task is not None and not task.done():
                task.cancel()

    def path(self, action):
        return f"/api/bot/game/{self.id}/{action}"

    async def follow(self):
        failures = 0
        while not self.over:
            try:
                async for event in self.bot.api.stream(f"/api/bot/game/stream/{self.id}"):
                    failures = 0
                    try:
                        await self.on_event(event)
                    except (KeyError, TypeError, ValueError) as error:
                        self.bot.on_line(f"-- game {self.id}: ignored {event.get('type')}: {error!r}")
                    if self.over:
                        return
            except OSError as error:
                self.bot.on_line(f"-- game {self.id} stream failed ({error}); reconnecting")
            self.loss.reset()
            self.generation = object()
            if self.searching is not None:
                self.searching.cancel()
            self.searched = None
            self.state = None
            if self.bot.session is not None and hasattr(self.bot.session, "stop_pondering"):
                await self.bot.session.stop_pondering()
            self.reply_clock = None
            failures += 1
            if failures > 8:
                await self.finish("unknownFinish", None)
                return
            await asyncio.sleep(min(30, 2 ** failures))

    async def on_event(self, event):
        kind = event.get("type")
        if kind == "gameFull":
            if self.full is None:
                self.setup(event)
            await self.on_state(event["state"])
        elif kind == "gameState" and self.full is not None:
            await self.on_state(event)
        elif kind == "chatLine":
            await self.on_chat(event)
        elif kind == "opponentGone" and event.get("gone") and event.get("claimWinInSeconds") == 0:
            self.bot.on_line(f"-- game {self.id}: opponent left; claiming the win")
            await self.bot.api.post(self.path("claim-victory"))

    def player(self, color):
        return self.full["white" if color == chess.WHITE else "black"]

    def setup(self, full):
        bot = self.bot
        self.full = full
        fen = full.get("initialFen", "startpos")
        self.initial = chess.Board() if fen == "startpos" else chess.Board(fen)
        self.board = self.initial.copy()
        self.color = chess.WHITE if full["white"].get("id") == bot.id else chess.BLACK
        them = self.player(not self.color)
        title = them.get("title")
        self.opponent = chess.engine.Opponent(them.get("name"), title if title in chess_titles else None,
                                              them.get("rating"), title == "BOT")
        self.contempt = draw_contempt(self.player(self.color).get("rating"), them.get("rating"), bot.contempt_window)
        self.record = fics_game.game_record({})
        if bot.learner is not None:
            group = "engines" if title == "BOT" else "masters" if title in chess_titles else "humans"
            self.opening = bot.learner.for_game(them.get("name"), group, learning.lichess_speeds.get(full["speed"]),
                                                self.contempt, full.get("rated", False))
            stats = learning.player_stats(bot.learner.records, bot.learner.server).get((them.get("name") or "").lower(), {})
            history = tuple(stats.get(outcome, 0) for outcome in ("wins", "draws", "losses"))
        else:
            history = None
        self.chat("player", player_greeting)
        self.chat("spectator", " ".join(filter(None, (greeting, record_line(history, them.get("name") or "you")))))
        clock = full.get("clock") or {}
        bot.on_line(f"-- game {self.id}: {full['white'].get('name')} vs {full['black'].get('name')}, "
                    f"{full['speed']} {'rated' if full.get('rated') else 'casual'} {bot.api.server}/{self.id}")
        self.emit("game_start", white=full["white"].get("name"), black=full["black"].get("name"),
                  white_elo=full["white"].get("rating"), black_elo=full["black"].get("rating"),
                  initial_seconds=clock.get("initial", 0) // 1000, increment_seconds=clock.get("increment", 0) // 1000,
                  rated=full.get("rated"), variant=full["speed"],
                  our_color="white" if self.color == chess.WHITE else "black", fen=self.board.fen(),
                  clock_ms={"white": full["state"].get("wtime"), "black": full["state"].get("btime")})

    def emit(self, kind, **fields):
        if self.bot.events is not None:
            self.bot.events.emit(kind, source="lichess", game=self.id, **fields)

    async def on_state(self, state):
        if self.over:
            return
        clock_ms = {"white": state.get("wtime"), "black": state.get("btime")}
        replay = self.initial.copy()
        for uci in state.get("moves", "").split():
            replay.push_uci(uci)
        old, new = self.board.move_stack, replay.move_stack
        previous_state = self.state
        self.state = dict(state)
        self.state_received_at = time.monotonic()
        if previous_state is not None and previous_state.get("moves", "") == state.get("moves", ""):
            side = "b" if self.color == chess.WHITE else "w"
            if any(previous_state.get(f"{side}{field}") != state.get(f"{side}{field}") for field in ("time", "inc")):
                # A donation/clock adjustment without a move invalidates timing.
                self.reply_clock = None
        common = next((index for index, (a, b) in enumerate(zip(old, new)) if a != b), min(len(old), len(new)))
        if common < len(old):
            self.loss.reset()
            self.generation = object()
            if self.searching is not None:
                self.searching.cancel()
            self.searched = None
            self.reply_clock = None
            self.board = replay
            if self.bot.session is not None and hasattr(self.bot.session, "stop_pondering"):
                await self.bot.session.stop_pondering()
            self.emit("position", ply=replay.ply(), fen=replay.fen(), reason="takeback", clock_ms=clock_ms)
        else:
            added = len(new) - len(old)
            for move in new[len(old):]:
                before = self.board.copy()
                self.board.push(move)
                if before.turn != self.color:
                    elapsed = None
                    side = "w" if before.turn == chess.WHITE else "b"
                    if added == 1 and self.reply_clock is not None:
                        prior, previous_inc = self.reply_clock
                        now, inc = state.get(f"{side}time"), state.get(f"{side}inc")
                        if prior is not None and now is not None and inc is not None and inc == previous_inc:
                            elapsed = (prior - now + inc) / 1000
                    self.loss.on_reply(before, move, elapsed)
                    self.reply_clock = None
                elif added == 1:
                    side = "b" if self.color == chess.WHITE else "w"
                    self.reply_clock = (state.get(f"{side}time"), state.get(f"{side}inc"))
                else:
                    self.reply_clock = None
                self.on_move(before, move, state, clock_ms)
        if state.get("status") not in ("created", "started"):
            await self.finish(state.get("status", "unknownFinish"), state.get("winner"))
            return
        self.draw_offered = bool(state.get("wdraw" if self.color == chess.BLACK else "bdraw"))
        if state.get("wtakeback" if self.color == chess.BLACK else "btakeback"):
            await self.bot.api.post(self.path("takeback/no"))
        key = len(self.board.move_stack)
        if self.board.turn == self.color and not self.board.is_game_over() and self.searched != key:
            self.searched = key
            self.searching = asyncio.ensure_future(self.play(self.board.copy(), state))
            self.searching.add_done_callback(self.report)

    def on_move(self, before, move, state, clock_ms):
        mover = "white" if before.turn == chess.WHITE else "black"
        remaining = clock_ms[mover]
        if remaining is not None:
            previous = self.clocks.get(mover)
            increment = state.get("winc" if mover == "white" else "binc", 0)
            elapsed = max(0, previous - remaining + increment) / 1000 if previous is not None else 0
            self.record.on_move(self.board, remaining / 1000, elapsed)
            self.clocks[mover] = remaining
        if before.turn != self.color:
            left = f", {clock_text(remaining / 1000)} left" if remaining is not None else ""
            name = self.player(not self.color).get("name") or "them"
            self.bot.on_line(f"-- {before.variation_san([move])} by {name}{left}")
        search = self.record.searches.get(before.ply())
        self.emit("move", ply=self.board.ply(), san=before.san(move), uci=move.uci(), fen=self.board.fen(),
                  by="us" if before.turn == self.color else "them", clock_ms=clock_ms,
                  search=search_summary(before, search[1]) if search and search[0] == move else None)

    def report(self, task):
        if not task.cancelled() and task.exception() is not None:
            self.bot.on_line(f"-- move task failed: {task.exception()!r}")

    async def play(self, board, state, retry=True):
        bot = self.bot
        generation = self.generation
        if self.over or self.loss.sent:
            return
        ours = "w" if board.turn == chess.WHITE else "b"
        clock, increment = state[f"{ours}time"] / 1000, state[f"{ours}inc"] / 1000
        limit = chess.engine.Limit(white_clock=state["wtime"] / 1000, black_clock=state["btime"] / 1000,
                                   white_inc=state["winc"] / 1000, black_inc=state["binc"] / 1000)
        started = time.monotonic()
        try:
            move, info, kind = await move_choice.choose_move(
                bot.session, board, limit, opening=self.opening,
                ceiling=self.loss.ceiling(self.full.get("rated")),
                budget_ms=learning.per_move_budget_ms(clock, increment), tablebase=bot.tablebase, game=(self.id, self.generation),
                opponent=self.opponent, timeout=max(clock, 1) + bot.margin, options={"DrawContempt": self.contempt})
        except (asyncio.TimeoutError, RuntimeError, OSError, chess.engine.EngineError) as error:
            bot.on_line(f"-- engine failed ({error or type(error).__name__}); restarting it")
            await bot.stop_engine()
            await bot.start_engine()
            if retry:
                await self.play(board, state, retry=False)
            return
        if self.over or self.generation is not generation or self.board.fen() != board.fen():
            return
        if move is None or not board.is_legal(move):
            bot.on_line(f"-- engine returned no legal move in {board.fen()}")
            return
        fresh = self.loss.on_search(board, move, info, kind)
        latest = self.state or state
        received_at = self.state_received_at if self.state_received_at is not None else started
        their_side = "b" if self.color == chess.WHITE else "w"
        evidence = self.loss.decision(
            our_rating=self.player(self.color).get("rating"), their_rating=self.player(not self.color).get("rating"),
            bot=True if self.player(not self.color).get("title") == "BOT" else None, speed=self.full.get("speed"),
            ours=max(0, latest[f"{ours}time"] / 1000 - (time.monotonic() - received_at)),
            theirs=latest.get(f"{their_side}time", 0) / 1000,
            increment=latest[f"{their_side}inc"] / 1000 if f"{their_side}inc" in latest else None,
            info=info, board=board) if fresh else None
        if evidence is not None:
            self.loss.sent = True
            bot.on_line(f"-- resigning game {self.id}: {json.dumps(evidence)}")
            await bot.api.post(self.path("resign"))
            return
        if self.draw_offered and move_choice.accept_draw(board, info, self.contempt):
            # Accept, and move too: a withdrawn offer must not leave our clock running.
            bot.on_line(f"-- game {self.id}: accepting the draw offer")
            await bot.api.post(self.path("draw/yes"))
        # Recorded before sending: the stream may report the move before the POST returns.
        self.record.on_search(board, move, info)
        if not await bot.api.post(self.path(f"move/{move.uci()}")):
            return
        seconds, numbered = time.monotonic() - started, board.variation_san([move])
        left = f", {clock_text(max(0.0, clock - seconds))} left"
        why = self.opening.why if kind == "book" and self.opening is not None else None
        if why and why[0] in ("book", "learned", "signature", "revenge"):
            self.last_book, self.left_book = book_line(numbered, why, self.opening.names), None
        elif self.last_book and self.left_book is None:
            self.left_book = numbered
        if kind == "engine":
            pv = variation(board, info.get("pv", []))
            self.last_eval = f"{board.san(move)} {describe(info, board.turn)}" + (f", expecting {pv}" if pv else "")
            bot.on_line(f"-- {numbered} {describe(info, board.turn)} {seconds:.1f}s{left}" + (f" pv {pv}" if pv else ""))
        else:
            self.last_eval = f"{board.san(move)} {info['source']}"
            source = book_whisper(numbered, why, self.opening, compact=True) if why else f"{numbered} {info['source']}"
            bot.on_line(f"-- {source} {seconds:.1f}s{left}")

    def stats_line(self):
        """Our record against the opponent and on Lichess, from the experience."""
        learner = self.bot.learner
        if learner is None:
            return "No stats: learning is off."
        name = self.player(not self.color).get("name") or "?"
        stats = learning.player_stats(learner.records, learner.server).get(name.lower(), {})
        scores = [record["score"] for record in learner.records if record["score"] is not None
                  and record.get("server") == learner.server]
        overall = f"Lichess: {len(scores)} games, W/D/L {scores.count(1.0)}/{scores.count(0.5)}/{scores.count(0.0)}."
        if not stats.get("games"):
            return f"First game against {name}. {overall}"
        return (f"Vs {name}: {stats['games']} game{'s' if stats['games'] != 1 else ''}, "
                f"W/D/L {stats['wins']}/{stats['draws']}/{stats['losses']}. {overall}")

    def chat(self, room, *texts):
        """Post lines to a chat room in order without holding up the game; failures are only logged."""
        async def post():
            for text in texts:
                await self.bot.api.post(self.path("chat"), {"room": room, "text": text})
        asyncio.ensure_future(post())

    async def on_chat(self, event):
        """Commands typed in either chat room: !help, !name and !eval for anyone; owners may also
        !softquit, !softrestart, !resign, !abort and !draw."""
        bot = self.bot
        name, text, room = event.get("username", ""), event.get("text", "").strip(), event.get("room", "player")
        if name.lower() in (bot.id, "lichess") or not text.startswith("!"):
            return
        command = text[1:].split()[0].lower() if len(text) > 1 else ""
        owner = name.lower() in bot.owners
        if command in public_commands:
            now = time.monotonic()
            if not owner and now - self.answered < public_gap:
                return
            self.answered = now
            if command == "help":
                listed = public_commands + (owner_commands if owner else ())
                self.chat(room, "Commands: " + ", ".join(f"!{name}" for name in listed))
            elif command == "name":
                engine = bot.session.protocol.id.get("name") if bot.session is not None else "Maxima"
                self.chat(room, f"{engine}. {credits}")
            elif room != "spectator":
                self.chat(room, "I share that with spectators only.")
            elif command == "stats":
                self.chat(room, self.stats_line())
            elif command == "book":
                self.chat(room, f"Out of book since {self.left_book}." if self.left_book
                          else self.last_book or "No book move yet.")
            else:
                self.chat(room, self.last_eval or "No search yet.")
            return
        if command not in owner_commands:
            return
        if not owner:
            bot.on_line(f"-- command {text!r} from {name} refused: not an owner")
            return
        bot.on_line(f"-- {name}: {command}")
        if command in ("challenge", "nochallenge"):
            self.chat(room, f"OK, I {bot.set_challenging(command == 'challenge')}.")
        elif command in ("softquit", "softrestart"):
            bot.soft_quit(restart=command == "softrestart")
            self.chat(room, f"OK, I {'restart' if bot.restart else 'stop'} after this game.")
        else:
            path = "draw/yes" if command == "draw" else command
            if not await bot.api.post(self.path(path)):
                self.chat(room, f"Could not {command}.")

    def save_interrupted(self):
        if self.over or self.full is None or self.record is None or self.board is None:
            return
        self.record.headers.update(self.headers())
        game = self.record.game(self.board, "*", "client shutdown before server result")
        online_shutdown.save_interrupted(game, self.bot.pgn_dir, self.bot.on_line)

    async def finish(self, status, winner):
        """Save, learn and report once, whichever of the game stream and the event stream ends it first."""
        if self.over:
            return
        self.over = True
        if self.bot.session is not None and hasattr(self.bot.session, "stop_pondering"):
            await self.bot.session.stop_pondering()
        if self.searching is not None and not self.searching.done():
            self.searching.cancel()
        bot = self.bot
        if winner in ("white", "black"):
            result = "1-0" if winner == "white" else "0-1"
        elif status in ("aborted", "noStart", "unknownFinish"):
            result = "*"
        else:
            result = "1/2-1/2"
        reason = reasons.get(status, status)
        bot.on_line(f"-- game {self.id} over: {reason} {result}")
        if status not in ("aborted", "noStart"):
            self.chat("player", random.choice(player_farewells))
            self.chat("spectator", "Thanks for watching!")
        game = self.pgn(result, reason, status)
        if game is not None and bot.pgn_dir is not None:
            self.save(game)
        self.emit("game_end", result=result, reason=reason, headers=dict(game.headers) if game else None,
                  pgn=str(game) if game else None)
        bot.ended()
        them, clock = self.player(not self.color) if self.full else {}, (self.full or {}).get("clock") or {}
        if status not in ("aborted", "noStart") and them.get("title") == "BOT" and clock and them.get("id"):
            asyncio.ensure_future(bot.matchmaker.rematch(bot, them.get("name") or them["id"], them["id"],
                                                         self.full["speed"], clock["initial"] // 1000,
                                                         clock["increment"] // 1000, bool(self.full.get("rated"))))
        if self.task is not None and self.task is not asyncio.current_task():
            self.task.cancel()  # ended by the event stream; the game stream has nothing more to say

    def pgn(self, result, reason, status):
        if self.full is None or not self.board.move_stack or result == "*" and self.board.ply() < 2:
            return None
        self.record.headers.update(self.headers())
        game = self.record.game(self.board, result, reason)
        game.headers["Termination"] = terminations.get(status, "normal")
        return game

    def headers(self):
        full, bot = self.full, self.bot
        clock = full.get("clock") or {}
        headers = {"Event": f"Lichess {'rated' if full.get('rated') else 'casual'} {full['speed']} game",
                   "Site": f"{bot.api.server}/{self.id}", "White": full["white"].get("name", "?"),
                   "Black": full["black"].get("name", "?"), "LearningSource": "lichess",
                   "LearningServer": learning.learning_store.server_name(bot.api.server),
                   "LearningColor": "white" if self.color == chess.WHITE else "black", "GameId": self.id}
        if clock:
            headers["TimeControl"] = f"{clock['initial'] // 1000}+{clock['increment'] // 1000}"
        engine_name = bot.session.protocol.id.get("name") if bot.session is not None else None
        for color in (chess.WHITE, chess.BLACK):
            tag, player = "White" if color == chess.WHITE else "Black", self.player(color)
            if player.get("rating"):
                headers[f"{tag}Elo"] = str(player["rating"])
            if player.get("title"):
                headers[f"{tag}Title"] = player["title"]
            headers[f"{tag}Type"] = "program" if color == self.color or player.get("title") == "BOT" else "human"
            if color == self.color and engine_name:
                headers[f"{tag}Engine"] = engine_name
        if self.opening is not None and self.opening.mood:
            headers["Mood"] = self.opening.mood
        return headers

    def save(self, game):
        bot = self.bot
        try:
            path = fics_game.save_game(game, bot.pgn_dir, self.record.started)
        except (OSError, ValueError) as error:
            bot.on_line(f"-- could not save game {self.id}: {error}")
            return
        bot.on_line(f"-- saved game {self.id} to {path}")
        if bot.learner is not None:
            try:
                record = bot.learner.add(game)
            except (OSError, ValueError) as error:
                bot.on_line(f"-- PGN saved; shared learning update failed: {error}")
                return
            for text in learning_report(game, record, bot.learner.records):
                bot.on_line(f"-- {text}")
        if game.headers.get('Result') != '*':
            from .monthly_pgn import archive_saved
            paths = [path]
            if bot.learner is not None and getattr(bot.learner, 'experience_dir', None) is not None:
                paths.append(bot.learner.experience_dir / 'games' / (game.headers['LearningId'] + '.pgn'))
            archive_saved(paths, bot.on_line)


def book_line(move, why, names):
    """ "5...Nc6 book: base 95%, fire 5%" from a book choice (kind, probability, parts, source)."""
    kind, probability, parts = why[0], why[1], why[2]

    def label(layer):
        if layer == "group":
            return f"vs {group_labels.get(names['group'], names['group'])}"
        if layer == "opponent":
            return f"vs {names['opponent']}"
        return book_layers.get(layer, layer)
    shares = [(label(layer), share / probability) for layer, share in parts.items() if share >= 0.005 * probability]
    detail = ", ".join(f"{name} {share:.0%}" for name, share in sorted(shares, key=lambda item: -item[1]))
    return f"{move} {'from book' if kind == 'book' else kind}: {detail}"


def record_line(history, name):
    """Compact spectator record from (wins, draws, losses), omitted before a first game."""
    if not history or not sum(history):
        return None
    wins, draws, losses = history
    return f"vs {name}: +{wins} ={draws} -{losses}."


def load_settings(path):
    """Defaults merged with an optional JSON file; unknown keys and wrong types are errors."""
    settings = dict(default_settings)
    if path is None:
        return settings
    with open(path, encoding="utf-8") as stream:
        loaded = json.load(stream)
    if not isinstance(loaded, dict):
        raise ValueError(f"{path}: expected a JSON object")
    types = {"engine": str, "options": dict, "speeds": list, "accept": bool, "games": int, "pgn_dir": str,
             "learning": bool, "variety": (int, float), "contempt_window": int, "layer_profiles": dict,
             "style_books": dict, "mood": str, "live_feed": str, "pgn_post": str, "online_tablebase": bool,
             "challenge_idle": int, "challenge_window": int}
    types["resignation"] = dict
    types["shutdown_grace"] = (int, float)
    for key, value in loaded.items():
        if key == "shutdown_grace":
            value = online_shutdown.grace_seconds(value)
        if key == "resignation":
            value = resignation.validate_settings(value)
        if key not in types:
            raise ValueError(f"{path}: unknown setting {key!r}")
        kinds = types[key] if isinstance(types[key], tuple) else (types[key],)
        if value is not None and (not isinstance(value, kinds) or isinstance(value, bool) and bool not in kinds):
            raise ValueError(f"{path}: {key} must be {' or '.join(kind.__name__ for kind in kinds)}")
        if key == "speeds" and (not value or not all(speed in speeds for speed in value)):
            raise ValueError(f"{path}: speeds are {', '.join(speeds)}")
        if key == "layer_profiles":
            value = layer_profiles(value, path)
        settings[key] = value
    settings["options"] = {str(name): str(value) for name, value in (settings["options"] or {}).items()}
    return settings


def run(options, parser):
    """Play with parsed options; parser reports configuration errors."""
    try:
        configured = load_settings(options.config)
        if options.shutdown_grace is not None:
            configured["shutdown_grace"] = online_shutdown.grace_seconds(options.shutdown_grace)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    flags = {"engine": options.engine, "speeds": options.speed, "games": options.games, "pgn_dir": options.pgn_dir,
             "variety": options.variety, "live_feed": options.live_feed, "pgn_post": options.pgn_post}
    configured.update({key: value for key, value in flags.items() if value is not None})
    if options.no_accept:
        configured["accept"] = False
    if options.no_learning or options.no_pgn:
        configured["learning"] = False
    if options.no_online_tablebase:
        configured["online_tablebase"] = False
    if options.challenge_idle is not None:
        configured["challenge_idle"] = options.challenge_idle
    if options.no_challenge or options.no_accept:
        configured["challenge_idle"] = 0
    token = env_value("LICHESS_BOT_TOKEN")
    if not token:
        parser.error("set LICHESS_BOT_TOKEN (environment or .env) to the bot account's token (MAXIMA.md 10.3)")
    owners = [name.strip() for name in (env_value("LICHESS_OWNERS") or "").split(",") if name.strip()]
    settings = dict(configured["options"])
    if configured["learning"]:
        settings.setdefault("OwnBook", "false")  # MaxDrone chooses book and learned moves (MAXIMA.md 9.7)
    tables = data_root() / "tablebases" / "syzygy"
    if tables.is_dir():
        settings.setdefault("SyzygyPath", str(tables))
    for item in options.option:
        name, separator, value = item.partition("=")
        if not separator or not name.strip():
            parser.error(f"--option expects NAME=VALUE, got {item!r}")
        settings[name.strip()] = value.strip()
    feed_address = None
    if configured["live_feed"]:
        host, _, port = configured["live_feed"].rpartition(":")
        if not port.isdigit():
            parser.error(f"live feed must look like 8765 or 127.0.0.1:8765, got {configured['live_feed']!r}")
        feed_address = (host or "127.0.0.1", int(port))
    if configured["pgn_post"] and not configured["pgn_post"].startswith(("http://", "https://")):
        parser.error(f"PGN post needs an http(s) URL, got {configured['pgn_post']!r}")
    pgn_dir = None if options.no_pgn else Path(configured["pgn_dir"] or data_root() / "experience" / "games")

    def show(line):
        print(line, flush=True)

    @asynccontextmanager
    async def engine_session():
        # The data directory resolves the engine's relative eval.nnue and book.bin defaults.
        async with open_engine([str(configured["engine"] or engine_path())], str(data_root())) as session:
            await session.configure(settings)
            yield session

    async def play_session(controller, commands, state):
        loop = asyncio.get_running_loop()
        read_commands(loop, commands)
        learner = None
        if configured["learning"]:
            learner = learning.learner(data_root() / "experience", pgn_dir, data_root() / "book.bin", configured["variety"],
                                       style_books(configured["style_books"]), configured["layer_profiles"] or None,
                                       configured["mood"], data_root() / "moods", server=options.server)
            show(f"-- experience: {len(learner.records)} games")
        events = connectors.game_events() if feed_address or configured["pgn_post"] else None
        api = lichess_api(token, options.server, show)
        bot = lichess_bot(api, engine_session, show, speeds=configured["speeds"], accept=configured["accept"],
                          games=configured["games"], owners=owners, learner=learner, pgn_dir=pgn_dir, events=events,
                          online_tablebase=configured["online_tablebase"],
                          contempt_window=configured["contempt_window"],
                          challenge_idle=configured["challenge_idle"] or 0,
                          challenge_window=configured["challenge_window"],
                          resignation_settings=configured["resignation"],
                          local_pieces=online_tablebase.local_pieces(settings.get("SyzygyPath")))
        state["bot"] = bot
        if controller.requested:
            bot.soft_quit()
        feed = None
        if feed_address:
            def about():
                engine = bot.session.protocol.id.get("name") if bot.session else None
                return {"source": "lichess", "server": api.server, "handle": bot.name, "engine": engine}
            feed = connectors.websocket_feed(events, *feed_address, about=about, on_line=show)
        if configured["pgn_post"]:
            events.subscribe(connectors.pgn_poster(configured["pgn_post"], env_value("MAXDRONE_POST_TOKEN"), show))
        try:
            loop.add_signal_handler(signal.SIGUSR1, bot.soft_quit)
        except (NotImplementedError, AttributeError):
            pass  # no SIGUSR1 on this platform; console quit and chat commands still work
        try:
            if controller.requested:
                return 0
            if feed is not None:
                await feed.start()
            await bot.start()
            return await bot.run(commands)
        finally:
            if controller.requested and bot.game is not None:
                bot.game.save_interrupted()
            await bot.close()
            if feed is not None:
                await feed.close()

    async def session():
        commands, state = asyncio.Queue(), {}
        controller = online_shutdown.shutdown_controller(show, configured["shutdown_grace"])

        def stop():
            bot = state.get("bot")
            if bot is not None:
                bot.soft_quit()
                bot.matchmaker.enabled = False
            return bot is not None and (bot.game is not None or bot.accepted[0] is not None
                                       or bot.matchmaker.pending is not None or bot.matchmaker.challenging)

        return await controller.run(play_session(controller, commands, state), stop)

    return asyncio.run(session())
