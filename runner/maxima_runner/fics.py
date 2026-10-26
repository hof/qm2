"""Play on FICS: timeseal transport, login, keepalive probes, and reconnects."""
import asyncio
import json
from contextlib import asynccontextmanager
import os
from pathlib import Path
import platform
import random
import hmac
import re
import socket
import sys
import threading
import time

import chess
import chess.engine
from .variants import royal_losers_board, wild_boards

from . import connectors, fics_game, learning, move_choice, online_tablebase, resignation, online_shutdown
from .move_choice import describe, search_summary, speed, variation
from .project import data_root, engine_path, project_root
from .uci import open_engine

timeseal_key = b"Timestamp (FICS) v1.0 - programmed by Henrik Gram."
timeseal_ping = b"[G]\x00"
timeseal_pong = "\x029"
# FICS only decodes timeseal input after this hello line.
client_name = "MaxDrone"
timeseal_hello = f"TIMESTAMP|{client_name}|{platform.system() or 'unknown'}|"
iac, will, wont, do, dont = 255, 251, 252, 253, 254
prompt = "fics% "
login_settings = (
    "set style 12", f"set interface {client_name}", "set seek 0", "set shout 0", "set cshout 0",
    "set bell 0", "set autoflag 1", "set noescape 1", "set width 240", "iset nowrap 1", "iset ms 1",
)
backoff_seconds = (5, 10, 30, 60)
closed = object()


class login_error(ValueError):
    pass


def timeseal_encode(line, timestamp, offset):
    """Encode one line without newline; the final byte tells the server the key offset."""
    buffer = bytearray(line) + b"\x18" + str(timestamp).encode() + b"\x19"
    buffer += b"1" * (12 - len(buffer) % 12)
    for block in range(0, len(buffer), 12):
        for first, second in ((0, 11), (2, 9), (4, 7)):
            buffer[block + first], buffer[block + second] = buffer[block + second], buffer[block + first]
    encoded = bytearray((((byte | 0x80) ^ timeseal_key[(index + offset) % len(timeseal_key)]) - 32) & 0xff
                        for index, byte in enumerate(buffer))
    encoded.append(0x80 | offset)
    return bytes(encoded)


def clean_line(text):
    text = text.replace("\x00", "").strip("\r")
    while text.startswith(prompt):
        text = text[len(prompt):]
    return text


class line_buffer:
    """Split server output into lines, removing telnet negotiation and timeseal pings."""

    def __init__(self):
        self.raw = bytearray()
        self.text = bytearray()

    def feed(self, data):
        self.raw += data
        index = 0
        while index < len(self.raw):
            byte = self.raw[index]
            if byte != iac:
                self.text.append(byte)
                index += 1
                continue
            if index + 1 >= len(self.raw):
                break
            command = self.raw[index + 1]
            if command == iac:
                self.text.append(iac)
                index += 2
            elif command in (will, wont, do, dont):
                if index + 2 >= len(self.raw):
                    break
                index += 3
            else:
                index += 2
        del self.raw[:index]
        pings = self.text.count(timeseal_ping)
        self.text = self.text.replace(timeseal_ping, b"")
        *complete, rest = self.text.split(b"\n")
        self.text = bytearray(rest)
        lines = []
        for raw_line in complete:
            line = clean_line(raw_line.decode("latin-1"))
            if line == "[G]":
                pings += 1
            elif line:
                lines.append(line)
        return lines, pings

    @property
    def partial(self):
        return clean_line(self.text.decode("latin-1"))

    def clear_partial(self):
        self.text.clear()


class ics_connection:
    def __init__(self, reader, writer, timeseal, transcript=None):
        self.reader = reader
        self.writer = writer
        self.timeseal = timeseal
        self.transcript = transcript
        self.started = time.monotonic()
        self.received = self.sent = self.started
        self.buffer = line_buffer()
        self.queue = asyncio.Queue()
        self.lock = asyncio.Lock()
        self.pump = asyncio.ensure_future(self._pump())

    @classmethod
    async def open(cls, host, port, timeseal=True, transcript=None, timeout=30):
        reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), timeout)
        tcp = writer.get_extra_info("socket")
        if tcp is not None:
            tcp.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        if transcript:
            transcript(f"-- connected to {host}:{port}{' with timeseal' if timeseal else ''}")
        connection = cls(reader, writer, timeseal, transcript)
        if timeseal:
            await connection.send(timeseal_hello)
        return connection

    def encode(self, line):
        data = line.encode("latin-1")
        if not self.timeseal:
            return data + b"\n"
        timestamp = int((time.monotonic() - self.started) * 1000)
        return timeseal_encode(data, timestamp, timestamp % len(timeseal_key)) + b"\n"

    async def send(self, line, secret=False):
        if "\n" in line or "\r" in line:
            raise ValueError("ICS commands must be single lines")
        if self.transcript:
            self.transcript("-> " + ("********" if secret else repr(line)))
        async with self.lock:
            self.writer.write(self.encode(line))
            await self.writer.drain()
        self.sent = time.monotonic()

    async def _pump(self):
        try:
            while True:
                data = await self.reader.read(65536)
                if not data:
                    break
                self.received = time.monotonic()
                lines, pings = self.buffer.feed(data)
                for line in lines:
                    if self.transcript:
                        self.transcript("<- " + repr(line))
                    self.queue.put_nowait(line)
                for _ in range(pings):
                    if self.transcript:
                        self.transcript("-> <timeseal ping reply>")
                    async with self.lock:
                        self.writer.write(self.encode(timeseal_pong))
                        await self.writer.drain()
                if self.buffer.partial:
                    self.queue.put_nowait(None)
        except OSError as error:
            if self.transcript:
                self.transcript(f"-- connection error: {error}")
        finally:
            self.queue.put_nowait(closed)

    async def next(self, timeout):
        """Return the next server line, or None when only the unterminated prompt changed."""
        item = await asyncio.wait_for(self.queue.get(), timeout)
        if item is closed:
            self.queue.put_nowait(closed)
            raise ConnectionError("server closed the connection")
        return item

    async def expect(self, patterns, timeout, on_line=None, prompts=()):
        """Wait for a line matching patterns, or unterminated text matching prompts."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise asyncio.TimeoutError()
            line = await self.next(remaining)
            if line is None:
                for pattern in prompts:
                    match = pattern.search(self.buffer.partial)
                    if match:
                        self.buffer.clear_partial()
                        return match
                continue
            if on_line is not None:
                on_line(line)
            for pattern in patterns:
                match = pattern.search(line)
                if match:
                    return match

    async def close(self):
        self.pump.cancel()
        try:
            await self.pump
        except asyncio.CancelledError:
            pass
        self.writer.close()
        try:
            await asyncio.wait_for(self.writer.wait_closed(), 5)
        except (OSError, asyncio.TimeoutError):
            pass


login_prompt = re.compile(r"login: ?$")
password_prompt = re.compile(r"password: ?$")
guest_name = re.compile(r'Press return to enter the server as "([A-Za-z]+)":')
session_start = re.compile(r"\*\*\*\* Starting FICS session as ([A-Za-z]+)")
login_rejected = re.compile(r"\*\*\*\* Invalid password! \*\*\*\*|^Sorry, .*")


def valid_handle(handle):
    return handle == "guest" or re.fullmatch(r"[A-Za-z]{3,17}", handle) is not None


async def login(connection, handle, password=None, timeout=60, on_line=None):
    """Log in and apply session settings; returns the server's name for this session."""
    if not valid_handle(handle):
        raise login_error("FICS handles have 3 to 17 letters")
    if handle != "guest" and not password:
        raise login_error(f"{handle} needs a password")
    await connection.expect([], timeout, on_line, prompts=[login_prompt])
    await connection.send(handle)
    match = await connection.expect([guest_name, login_rejected], timeout, on_line, prompts=[password_prompt])
    if match.re is login_rejected:
        raise login_error(match.group(0))
    if match.re is guest_name:
        if password:
            raise login_error(f"{handle} is not a registered FICS name")
        await connection.send("")
    else:
        await connection.send(password, secret=True)
    match = await connection.expect([session_start, login_rejected], timeout, on_line)
    if match.re is login_rejected:
        raise login_error(match.group(0))
    for setting in login_settings:
        await connection.send(setting)
    return match.group(1)


creating_pattern = re.compile(
    r"^Creating: (?P<white>[A-Za-z]+) \((?P<white_rating>[^)]*)\) (?P<black>[A-Za-z]+) "
    r"\((?P<black_rating>[^)]*)\) (?P<rated>rated|unrated) (?P<type>.+?) (?P<time>\d+) (?P<increment>\d+)"
    r"(?: \(adjourned\))?$")
challenge_pattern = re.compile(
    r"^Challenge: (?P<name>[A-Za-z]+) \([^)]*\)(?: \[(?:white|black)\])? [A-Za-z]+ \([^)]*\) "
    r"(?P<rated>rated|unrated) (?P<type>.+?) (?P<time>\d+) (?P<increment>\d+)(?: \(adjourned\))?\.?$")
tell_pattern = re.compile(r"^(?P<name>[A-Za-z]{3,17})(?:\([A-Z*]+\))* tells you: (?P<text>.*)$")
max_followers = 5
# FICS queues a guest's tells sent too fast ("Your communication has been queued"); we pace them ourselves and
# merge what piles up per follower, so followers never fall behind the game.
tell_gap = 2.0
gone_pattern = re.compile(r"^(?P<name>[A-Za-z]{3,17}) is not logged in\.$")
remote_commands = ("follow", "softquit", "softrestart", "help", "!abort", "!resign", "!draw", "seek-pool")
# Exit status after softrestart (EX_TEMPFAIL): a supervisor loop starts MaxDrone again with the latest code.
restart_status = 75
draw_offer_pattern = re.compile(r"^(?:Your opponent|[A-Za-z]+) offers you a draw\.")
finger_pattern = re.compile(r"^Finger of (?P<name>[A-Za-z]+)(?P<titles>(?:\([A-Z*]+\))*):")
playable_types = ("lightning", "blitz", "standard")
wild_types = tuple(wild_boards)
chess_titles = ("GM", "IM", "FM", "WGM", "WIM", "WFM", "CM", "WCM", "NM")
seek_pattern = re.compile(r"\d{1,3} \d{1,3}(?: [a-z0-9+-]+)*")
rated_word = re.compile(r"\b(?:r|u|rated|unrated)\b")
default_settings = {
    "resignation": resignation.validate_settings({}),
    "shutdown_grace": 15,
    "host": "freechess.org", "port": 5000, "handle": "guest", "engine": None, "options": {},
    "seeks": ["3 0", "3 1", "5 0"], "seeks_at_once": 2, "accept": True, "rematches": 0, "games": None,
    "owner": None, "notes": [], "pgn_dir": None, "learning": True, "variety": 1.0,
    "contempt_window": 100, "layer_profiles": {}, "style_books": {}, "first_move_wait": 60,
    "whisper": False, "mood": "auto", "live_feed": None, "pgn_post": None,
    "online_tablebase": True, "reseek_minutes": 10,
    "losers": False, "losers_seeks": [], "losers_seeks_at_once": 1,
}


def playable(game_type, minutes, increment, losers=False):
    """Supported rules and a running clock."""
    supported = game_type.lower() in playable_types + wild_types or losers and game_type.lower() == "losers"
    return supported and int(minutes) + int(increment) > 0


def parse_seek_pool(text):
    """Validate the complete replacement before changing the running policy."""
    pool = []
    for entry in text.split(","):
        spec = " ".join(entry.lower().split())
        words = spec.split()
        allowed = {"r", "u", "rated", "unrated", "white", "black", "random", "losers",
                   *(kind.replace("wild/", "w") for kind in wild_types)}
        if not seek_pattern.fullmatch(spec) or int(words[0]) + int(words[1]) == 0:
            raise ValueError("use comma-separated time controls, e.g. 3 0, 5 1 losers")
        if any(word not in allowed for word in words[2:]):
            raise ValueError("unsupported seek option or variant")
        variants = [word for word in words[2:] if word == "losers" or word.startswith("w") and word != "white"]
        if len(variants) > 1:
            raise ValueError("one variant per seek")
        if spec not in pool:
            pool.append(spec)
    if len(pool) > 32:
        raise ValueError("at most 32 seeks")
    return pool


def rating_of(text):
    return int(text) if text.strip().isdigit() else None


def draw_contempt(our_rating, their_rating, window=100, default=-10, elo_per_cp=4):
    """DrawContempt in cp from our view: draws welcome against stronger players, avoided against weaker.

    None when a rating is unknown, so the engine keeps its own default."""
    if not window or our_rating is None or their_rating is None:
        return None
    return int(max(-window, min(window, default + round((their_rating - our_rating) / elo_per_cp))))


class play_policy:
    """When to seek, accept, rematch, and stop; mirrors JDrone's seek pool and rematch limit."""

    def __init__(self, seeks=(), seeks_at_once=2, accept=True, rematches=0, games=None, guest=True,
                 owner=None, notes=(), contempt_window=100, first_move_wait=60,
                 whisper=False, follow_key=None, reseek_minutes=10, losers=False,
                 losers_seeks=(), losers_seeks_at_once=1):
        self.seek_pool = None
        self.seeks = list(seeks)
        self.losers_seeks = list(losers_seeks)
        self.losers = losers or bool(self.losers_seeks)
        self.losers_seeks_at_once = losers_seeks_at_once
        self.seeks_at_once = seeks_at_once
        self.accept = accept
        self.rematches = rematches
        self.games = games
        self.guest = guest
        self.owner = owner
        self.notes = list(notes)
        self.contempt_window = contempt_window
        self.first_move_wait = first_move_wait
        self.whisper = whisper
        self.follow_key = follow_key  # "tell <us> <key> <command>" steers us remotely (on_tell)
        self.reseek_seconds = reseek_minutes * 60  # 0: seeks stay as posted while idle

    def opening_wait(self, initial_seconds):
        """Seconds the opponent gets for their first move: a fifth of the clock, at least 15, at most first_move_wait."""
        return min(self.first_move_wait, max(15, initial_seconds / 5))

    def seek_commands(self):
        """A random draw from the pool; guests can only play unrated."""
        chosen = random.sample(self.seeks, min(self.seeks_at_once, len(self.seeks)))
        chosen += [f"{spec} losers" for spec in random.sample(
            self.losers_seeks, min(self.losers_seeks_at_once, len(self.losers_seeks)))]
        if self.seek_pool is not None:
            count = max(self.seeks_at_once, self.losers_seeks_at_once, 1)
            chosen = random.sample(self.seek_pool, min(count, len(self.seek_pool)))
        commands = []
        for spec in chosen:
            if self.guest:
                spec = rated_word.sub("u", spec) if rated_word.search(spec) else spec + " u"
            commands.append(f"seek {spec}")
        return commands


def cpu_model():
    model = platform.processor()
    try:
        with open("/proc/cpuinfo", encoding="utf-8") as stream:
            model = next((line.split(":", 1)[1] for line in stream if line.startswith("model name")), model)
    except OSError:
        pass
    model = re.sub(r"\((?:R|TM|tm)\)|\bCPU\b|@.*$|\d+(?:th|rd|nd|st) Gen\b", " ", model or "")
    return " ".join(model.split()) or platform.machine() or "unknown CPU"


def machine_description(threads, hash_mb):
    """What the engine uses, not the whole machine: CPU model, its Threads and Hash, and the OS."""
    hash_text = f", Hash {hash_mb} MB" if hash_mb else ""
    return f"{cpu_model()}, {threads} thread{'s' if int(threads) != 1 else ''}{hash_text}, {platform.system()}"


def short_engine_name(name):
    """'Maxima 2.0.0-branch-commit-Release' becomes 'Maxima 2.0.0'; other names stay as they are."""
    if not name:
        return "UCI engine"
    match = re.match(r"(.*?\d+(?:\.\d+)+)[-+ ]", name + " ")
    return match.group(1) if match else name


def finger_notes(engine_name, owners, machine, extra=()):
    """Notes 1-3: engine, owners, machine (mandatory for FICS computer accounts); then extra lines."""
    notes = [f"{short_engine_name(engine_name)} on {client_name}"]
    if owners:
        owners = [owners] if isinstance(owners, str) else list(owners)
        notes.append("Owner: " + (" and ".join(owners) if len(owners) < 3
                                  else ", ".join(owners[:-1]) + " and " + owners[-1]))
    notes.append(f"Hardware: {machine}")
    notes.extend(extra)
    return [" ".join(note.split()) for note in notes[:10]]


mood_names = {"kings_indian": "King's Indian", "parham": "Parham Attack (Qh5!)"}


def mood_title(mood):
    """books/moods/kings_indian.pgn -> the King's Indian."""
    return "the " + mood_names.get(mood, mood.replace("_", " ").title())


whisper_plies = 6


def clock_text(seconds):
    """Clock as m:ss."""
    minutes, rest = divmod(int(seconds), 60)
    return f"{minutes}:{rest:02d}"


def book_whisper(san, why, opening):
    """A book move for observers: the layer that picked it, then the mix, e.g. "Nc6 book: fire (base 39%,
    fire 39%, surprise 10%, experience 12%)"."""
    kind = why[0]
    opponent = opening.names["opponent"]
    if kind == "recalled":
        return f"{san} recalled d{why[1]} {why[2]:.0f}s"
    probability, parts, source = why[1], why[2], why[3]
    words = {"book": "base", "fire": "fire", "draw": "draw", "experiment": "surprise", "global": "experience",
             "group": opening.names["group"], "opponent": f"vs {opponent}",
             "mood": f"{(opening.mood or '').replace('_', ' ')} mood"}
    mix = ", ".join(f"{words[name]} {parts[name] / probability:.0%}" for name in learning.layer_names
                    if parts.get(name, 0) >= 0.05 * probability)
    lead = {"signature": f"signature vs {opponent}", "revenge": f"revenge vs {opponent}"}.get(kind, words[source])
    return f"{san} book: {lead} ({mix})"


def learning_report(game, record, records):
    """Plain lines for the owner after a game: what Maxima keeps from it, and the score against this
    opponent; a loss also files their opening as a revenge line."""
    if record is None:
        return ["not learned: under ten moves or too few searches"]
    opponent = record["opponent"]
    moves = sum(1 for _ in record["values"])
    played = [other["score"] for other in records if other["opponent"].lower() == opponent.lower()
              and other.get("server") == record.get("server")]
    wins, draws, losses = played.count(1.0), played.count(0.5), played.count(0.0)
    lines = [f"learned: {moves} moves | vs {opponent} +{wins} ={draws} -{losses}"]
    if record["score"] == 0.0:
        board, line = game.board(), list(game.mainline_moves())[:learning.revenge_plies // 2]
        lines.append(f"revenge filed: {board.variation_san(line)}")
    return lines


def search_whisper(info, turn, line, note=None, seconds=None):
    """A searched move for observers: "d15 +4.52 TB 9. Bg2 d5 10. d4 Nbd7 (out of book)"; the line starts
    with our move, already cut at whisper_plies."""
    score, depth = info.get("score"), info.get("depth", 0)
    parts = []
    if score is not None and depth:
        relative = score.pov(turn)
        parts += [f"d{depth}", f"#{relative.mate()}" if relative.is_mate() else f"{relative.score() / 100:+.2f}"]
    if info.get("tbhits"):
        parts.append("TB")
    if seconds is not None:
        parts.append(f"{seconds:.1f}s")
    if info.get("nps"):
        parts.append(speed(info["nps"]))
    parts += line.split()
    if note:
        parts.append(f"({note})")
    return " ".join(parts)


class engine_player:
    """Play our games with a UCI engine; searches run as tasks so server lines keep flowing."""

    def __init__(self, open_session, on_line, margin=5, policy=None, finger_wait=2, pgn_dir=None,
                 site="freechess.org", learner=None, events=None, online_tablebase=False, local_pieces=0,
                 resignation_settings=None, losers_learner=None):
        self.loss = resignation.loss_policy(resignation_settings)
        self.latest_position = None
        self.position_received_at = None
        self.open_session = open_session
        self.events = events
        self.our_color = None
        self.learner = learner
        self.losers_learner = losers_learner
        self.wild_learners = {}
        self.tablebase = move_choice.tablebase_lookups(online_tablebase, on_line, local_pieces)
        self.opening = None
        self.from_book = self.draw_offered = False
        self.announced_mood = None
        self.contempt = None
        self.fingered = False
        self.pgn_dir = pgn_dir
        self.site = site
        self.record = self.created = None
        self.opponent_unregistered = False
        self.on_line = on_line
        self.margin = margin
        self.policy = policy or play_policy()
        self.whispering = self.policy.whisper
        self.followers = []
        self.outbox, self.teller = {}, None
        self.finger_wait = finger_wait
        self.connection = None
        self.context = self.session = None
        self.game = self.history = self.token = None
        self.task = self.searched = self.retried = self.waiting = None
        self.name = self.opponent = self.last_opponent = None
        self.opponent_ready = asyncio.Event()
        self.opponent_ready.set()
        self.streak = self.played = 0
        self.stopping = self.finished = self.restart = False
        self.seeked = time.monotonic()

    async def start(self):
        self.context = self.open_session()
        self.session = await self.context.__aenter__()
        if self.policy.losers:
            try:
                self.session.variant_settings(royal_losers_board())
            except ValueError:
                await self.stop_engine()
                raise

    @property
    def is_losers(self):
        return self.history is not None and type(self.history.board) is royal_losers_board

    @property
    def is_wild(self):
        return bool(self.created and self.created["type"].lower() in wild_types)

    @property
    def game_learner(self):
        if self.is_wild:
            kind = self.created["type"].lower()
            if self.learner is not None and kind not in self.wild_learners:
                # Each ruleset owns its PGNs, experience and optional Polyglot book.
                root = self.learner.experience_dir / kind
                self.wild_learners[kind] = learning.learner(
                    root, self.game_pgn_dir, root / "book.bin", self.learner.variety,
                    mood="off", server=self.site, variant=kind)
            return self.wild_learners.get(kind)
        return self.losers_learner if self.is_losers else self.learner

    @property
    def game_pgn_dir(self):
        if self.pgn_dir is not None and self.is_wild:
            return self.pgn_dir.parent / "wild" / self.created["type"].split("/")[1] / self.pgn_dir.name
        if self.pgn_dir is None or not self.is_losers:
            return self.pgn_dir
        return self.pgn_dir.parent / "losers" / self.pgn_dir.name

    async def stop_engine(self):
        if self.context is not None:
            context, self.context, self.session = self.context, None, None
            await context.__aexit__(None, None, None)

    async def close(self):
        self.cancel()
        if self.teller is not None:
            self.teller.cancel()
        await self.stop_engine()

    def cancel(self):
        if self.task is not None and not self.task.done():
            self.task.cancel()
        self.stop_waiting()

    def stop_waiting(self):
        if self.waiting is not None:
            self.waiting.cancel()
            self.waiting = None

    def watch_first_move(self, position):
        """FICS aborts without agreement until both sides have moved, so an opponent who lets the opening
        wait pass before their first move does not hold us up."""
        if position.relation != fics_game.playing_their_move or self.history.board.ply() >= 2:
            self.stop_waiting()
        elif self.waiting is None:
            wait = self.policy.opening_wait(position.initial_seconds)
            self.waiting = asyncio.ensure_future(self.abort_unless_moved(self.game, self.history.board.ply(), wait))

    async def abort_unless_moved(self, game, ply, wait):
        await asyncio.sleep(wait)
        if self.game == game and self.history is not None and self.history.board.ply() == ply:
            self.on_line(f"-- aborting game {game}: no first move from the opponent in {wait:.0f}s")
            await self.send("abort")

    async def on_login(self, name):
        """A new session: any earlier game was forfeited (noescape), so start seeking again."""
        self.cancel()
        self.loss.reset()
        self.name = name
        self.game = self.history = None
        self.announced_mood = None
        if self.policy.losers:
            await self.send("set f3 losers")
        await self.announce()
        await self.seek()

    async def on_tell(self, found):
        """Remote control by tell: "<key> follow|softquit|softrestart|help|!abort|!resign|!draw", and "unfollow". Wrong
        keys get no answer; the ! commands are a fixed list, never raw server commands."""
        name, words = found["name"], found["text"].split()
        if words == ["unfollow"]:
            if name in self.followers:
                self.followers.remove(name)
                await self.send(f"tell {name} Stopped. Thanks for following!")
            return
        key = self.policy.follow_key
        owners = self.policy.owner or []
        owners = [owners] if isinstance(owners, str) else owners
        authorized = name.lower() in {owner.lower() for owner in owners}
        if key and words and hmac.compare_digest(words[0].encode(), key.encode()):
            authorized = True
            words = words[1:]
        if not words or words[0] not in remote_commands:
            return
        if not authorized:
            self.on_line(f"-- remote command refused for {name}")
            return
        command = words[0]
        if command == "seek-pool":
            try:
                pool = parse_seek_pool(" ".join(words[1:]))
                if any("losers" in spec.split() for spec in pool) and self.session is not None:
                    self.session.variant_settings(royal_losers_board())
            except ValueError as error:
                await self.send(f"tell {name} Invalid seek pool: {error}")
                return
            if any("losers" in spec.split() for spec in pool):
                self.policy.losers = True
                await self.send("set f3 losers")
            self.policy.seek_pool = pool
            if self.game is None:
                await self.seek()
            await self.send(f"tell {name} Seek pool set: {', '.join(pool)}. Used after every game.")
            return
        if len(words) != 1:
            return
        self.on_line(f"-- {name}: {command}")
        if command == "follow":
            if name not in self.followers:
                self.followers = (self.followers + [name])[-max_followers:]
            await self.send(f"tell {name} Hi! I will tell you why I play each move. 'tell {self.name} unfollow' stops.")
        elif command in ("softquit", "softrestart"):
            self.restart = command == "softrestart"
            await self.stop()
            await self.send(f"tell {name} OK, I {'restart' if self.restart else 'stop'} after this game.")
        elif command == "help":
            await self.send(f"tell {name} Commands after the key: {', '.join(remote_commands)}; and unfollow.")
        elif self.game is None:
            await self.send(f"tell {name} No game to {command[1:]}.")
        else:
            await self.send(command[1:])

    async def explain(self, text):
        """A move's explanation to observers (whisper) and to followers (tell)."""
        if self.whispering:
            await self.send(f"whisper {text}")
        self.tell_followers(text)

    def tell_followers(self, text):
        """Queue text for each follower; deliver sends one tell per tell_gap, merging what waits."""
        for name in self.followers:
            self.outbox.setdefault(name, []).append(text)
        if self.outbox and (self.teller is None or self.teller.done()):
            self.teller = asyncio.ensure_future(self.deliver())

    async def deliver(self):
        while self.outbox:
            name = next(iter(self.outbox))
            texts = self.outbox.pop(name)
            if name in self.followers:
                await self.send(f"tell {name} {' | '.join(texts)}")
                await asyncio.sleep(tell_gap)

    async def announce(self):
        """Finger notes for registered accounts, with the week's mood after the configured notes."""
        if self.policy.guest:
            return
        mood = self.learner.today_mood() if self.learner is not None else None
        if self.announced_mood is not None and mood == self.announced_mood:
            return
        extra = list(self.policy.notes) + ([f"Mood this week: {mood_title(mood)}"] if mood else [])
        for number, note in enumerate(finger_notes(*self.engine_facts(), extra), 1):
            await self.send(f"set {number} {note}")
        self.announced_mood = mood or ""

    def engine_facts(self):
        """Engine name, owners, and machine for the finger notes, from the running engine's settings."""
        protocol = self.session.protocol if self.session is not None else None

        def setting(name, fallback):
            if protocol is None:
                return fallback
            option = protocol.options.get(name)
            return protocol.config.get(name, option.default if option is not None else fallback)

        name = protocol.id.get("name") if protocol is not None else None
        return name, self.policy.owner, machine_description(setting("Threads", 1), setting("Hash", None))

    async def seek(self):
        self.seeked = time.monotonic()
        if self.stopping or self.game is not None or not (self.policy.seek_pool or self.policy.seeks or self.policy.losers_seeks):
            return
        await self.send("unseek")
        for command in self.policy.seek_commands():
            await self.send(command)

    async def reseek_when_idle(self):
        """A fresh draw from the seek pool after reseek_minutes without a game: other time controls get
        their turn and new players see the seeks announced."""
        if self.policy.reseek_seconds and self.game is None and \
                time.monotonic() - self.seeked >= self.policy.reseek_seconds:
            await self.seek()

    async def stop(self):
        """Stop seeking; quit now when idle, or after the current game."""
        self.stopping = True
        await self.send("unseek")
        action = "restarting" if self.restart else "stopping"
        if self.game is None:
            self.finished = True
            self.on_line(f"-- {action}")
        else:
            self.on_line(f"-- {action} after this game; quit again to leave now")

    async def handle(self, line):
        position = fics_game.style12.parse(line) if line.startswith("<12> ") else None
        if position is not None:
            try:
                previous_token = self.token
                self.on_board(position)
                if self.token is not previous_token and position.relation == fics_game.playing_their_move \
                        and self.session is not None and hasattr(self.session, "stop_pondering"):
                    await self.session.stop_pondering()
            except ValueError as error:
                self.on_line(f"-- ignored board: {error}")
            return
        end = fics_game.game_end_pattern.match(line)
        if end and int(end["game"]) == self.game:
            await self.on_game_end(end)
        elif line.startswith("Illegal move") and self.game is not None and self.retried != self.searched:
            self.retried = self.searched
            await self.send("refresh")
        elif draw_offer_pattern.match(line) and self.game is not None:
            self.draw_offered = True
            self.on_line("-- draw offered; judged on our move")
        elif creating_pattern.match(line):
            await self.on_creating(creating_pattern.match(line))
        elif challenge_pattern.match(line):
            await self.on_challenge(challenge_pattern.match(line))
        elif (gone := gone_pattern.match(line.strip())) and gone["name"] in self.followers:
            name = gone["name"]
            self.followers.remove(name)
            self.outbox.pop(name, None)
            self.on_line(f"-- {name} left; no longer followed")
        elif tell_pattern.match(line):
            await self.on_tell(tell_pattern.match(line))
        elif line.strip() == "You are muted." and self.whispering:
            self.whispering = False
            self.on_line("-- whispers off: FICS mutes this account (guests cannot whisper; register one)")
        elif finger_pattern.match(line) and self.opponent is not None:
            found = finger_pattern.match(line)
            if found["name"].lower() == self.opponent.name.lower():
                titles = re.findall(r"\(([A-Z*]+)\)", found["titles"])
                title = next((title for title in titles if title in chess_titles), None)
                self.opponent = chess.engine.Opponent(self.opponent.name, title, self.opponent.rating, "C" in titles)
                self.opponent_unregistered = "U" in titles
                self.fingered = True
                self.opponent_ready.set()

    async def on_creating(self, match):
        we_are_white = self.name is not None and match["white"].lower() == self.name.lower()
        name, rating = ((match["black"], match["black_rating"]) if we_are_white
                        else (match["white"], match["white_rating"]))
        self.opponent = chess.engine.Opponent(name, None, rating_of(rating), False)
        self.opponent_ready.clear()
        self.opponent_unregistered = self.fingered = False
        self.created = match.groupdict()
        our_rating = match["white_rating"] if we_are_white else match["black_rating"]
        self.contempt = draw_contempt(rating_of(our_rating), rating_of(rating), self.policy.contempt_window)
        if self.contempt is not None:
            self.on_line(f"-- draw contempt {self.contempt / 100:+.2f} ({rating_of(our_rating)} vs {rating_of(rating)})")
        if not playable(match["type"], match["time"], match["increment"], self.policy.losers):
            self.on_line(f"-- aborting unsupported game: {match['type']} {match['time']} {match['increment']}")
            await self.send("abort")
            return
        await self.send(f"finger {name}")

    async def on_challenge(self, match):
        name = match["name"]
        if self.game is not None or self.stopping or not self.policy.accept:
            await self.send(f"decline {name}")
        elif not playable(match["type"], match["time"], match["increment"], self.policy.losers):
            self.on_line(f"-- declining {match['type']} {match['time']} {match['increment']} from {name}")
            await self.send(f"decline {name}")
        else:
            await self.send(f"accept {name}")

    async def on_game_end(self, end):
        self.cancel()
        if self.session is not None and hasattr(self.session, "stop_pondering"):
            await self.session.stop_pondering()
        self.on_line(f"-- game {self.game} over: {end['reason']} {end['result']}")
        opponent = end["black"] if self.name and end["white"].lower() == self.name.lower() else end["white"]
        game = self.finished_game(end)
        self.save(game)
        self.emit("game_end", game=self.game, result=end["result"], reason=end["reason"],
                  headers=dict(game.headers) if game else None, pgn=str(game) if game else None)
        self.game = self.history = self.opponent = self.record = self.created = self.opening = self.contempt = None
        self.opponent_ready.set()
        self.played += 1
        self.streak = self.streak + 1 if opponent == self.last_opponent else 1
        self.last_opponent = opponent
        if self.policy.games is not None and self.played >= self.policy.games:
            self.on_line(f"-- played {self.played} games; stopping")
            self.stopping = True
        if self.stopping:
            await self.send("unseek")
            self.finished = True
            return
        if self.streak <= self.policy.rematches:
            await self.send("rematch")
        await self.seek()

    def save_interrupted(self):
        if self.game is None or self.history is None or self.record is None or self.latest_position is None:
            return
        end = {"white": self.latest_position.white, "black": self.latest_position.black}
        self.record.headers.update(self.headers(end))
        game = self.record.game(self.history.board, "*", "client shutdown before server result")
        online_shutdown.save_interrupted(game, self.game_pgn_dir, self.on_line)

    def headers(self, end):
        """PGN tags from Creating:, finger, and the engine; our side is a program, the other side as fingered."""
        created = self.created if self.created and {self.created["white"], self.created["black"]} == {
            end["white"], end["black"]} else {}
        headers = {"Event": f"FICS {created['rated']} {created['type']} game" if created else "FICS game",
                   "Site": self.site, "White": end["white"], "Black": end["black"],
                   "LearningSource": "fics", "LearningServer": self.site,
                   "LearningColor": "white" if self.our_color == chess.WHITE else "black",
                   "GameId": str(self.game)}
        if created:
            headers["TimeControl"] = f"{int(created['time']) * 60}+{created['increment']}"
        engine_name = self.session.protocol.id.get("name") if self.session is not None else None
        for color in ("White", "Black"):
            name = end[color.lower()]
            rating = rating_of(created.get(f"{color.lower()}_rating", ""))
            if rating:
                headers[f"{color}Elo"] = str(rating)
            if self.name and name.lower() == self.name.lower():
                headers[f"{color}Type"] = "program"
                if self.policy.guest:
                    headers[f"{color}Account"] = "unregistered"
                if engine_name:
                    headers[f"{color}Engine"] = engine_name
            elif self.opponent is not None and self.opponent.name.lower() == name.lower():
                if self.opponent.title:
                    headers[f"{color}Title"] = self.opponent.title
                if self.opponent_ready.is_set():
                    headers[f"{color}Type"] = "program" if self.opponent.is_engine else "human"
                if self.opponent_unregistered:
                    headers[f"{color}Account"] = "unregistered"
        if self.opening is not None and self.opening.mood:
            headers["Mood"] = self.opening.mood
        return headers

    def finished_game(self, end):
        """The annotated PGN game, or None when nothing was played."""
        if self.record is None or self.history is None or not self.history.board.move_stack:
            return None
        if end["result"] == "*" and self.history.board.ply() < 2:
            return None  # aborted before both sides moved: nothing to learn
        self.record.headers.update(self.headers(end))
        return self.record.game(self.history.board, end["result"], end["reason"])

    def save(self, game):
        if self.pgn_dir is None or game is None:
            return
        try:
            path = fics_game.save_game(game, self.game_pgn_dir, self.record.started)
        except (OSError, ValueError) as error:
            self.on_line(f"-- could not save game {self.game}: {error}")
            return
        self.on_line(f"-- saved game {self.game} to {path}")
        learner = self.game_learner
        if learner is not None:
            try:
                record = learner.add(game)
            except (OSError, ValueError) as error:
                self.on_line(f"-- PGN saved; shared learning update failed: {error}")
                return
            for text in learning_report(game, record, learner.records):
                self.on_line(f"-- {text}")
                self.tell_followers(text)
        if game.headers.get('Result') != '*':
            from .monthly_pgn import archive_saved
            paths = [path]
            if learner is not None and getattr(learner, 'experience_dir', None) is not None:
                paths.append(learner.experience_dir / 'games' / (game.headers['LearningId'] + '.pgn'))
            archive_saved(paths, self.on_line)

    def opponent_group(self):
        """Learning group from finger; None (global layer only) when unregistered or unknown."""
        if self.opponent is None or not self.fingered or self.opponent_unregistered:
            return None
        if self.opponent.is_engine:
            return "engines"
        return "masters" if self.opponent.title else "humans"

    def emit(self, kind, **fields):
        if self.events is not None:
            self.events.emit(kind, source="fics", **fields)

    def on_board(self, position):
        if position.relation not in (fics_game.playing_my_move, fics_game.playing_their_move):
            return
        if position.game != self.game:
            created = self.created if self.created and {self.created["white"], self.created["black"]} == {
                position.white, position.black} else None
            if created and not playable(created["type"], created["time"], created["increment"], self.policy.losers):
                return
            # Style12 has no variant field. With mixed rules, wait for Creating:
            # rather than silently treating a resumed losers board as standard.
            if self.policy.losers and created is None:
                raise ValueError("mixed-variant game needs Creating: metadata")
            kind = created["type"].lower() if created else "standard"
            board_type = royal_losers_board if kind == "losers" else wild_boards.get(kind, chess.Board)
            history = fics_game.game_history(position, board_type)
            self.cancel()
            self.game, self.token, self.history = position.game, object(), history
            self.loss.reset()
            self.record = fics_game.game_record({})
            self.opening = None
            self.from_book = self.draw_offered = False
            self.our_color = position.turn if position.relation == fics_game.playing_my_move else not position.turn
            self.on_line(f"-- game {self.game}: {position.white} vs {position.black}, "
                         f"{position.initial_seconds // 60} {position.increment_seconds}")
            self.emit_start(position)
        else:
            change = self.history.update(position)
            if change == "move":
                mover_ms = position.black_ms if position.turn == chess.WHITE else position.white_ms
                self.record.on_move(self.history.board, mover_ms / 1000, position.elapsed)
                self.emit_move(position)
                if position.turn == self.our_color:
                    before = self.history.board.copy()
                    move = before.pop()
                    self.loss.on_reply(before, move, position.elapsed)
                    name = position.black if position.turn == chess.WHITE else position.white
                    self.on_line(f"-- {before.variation_san([move])} by {name}, {clock_text(mover_ms / 1000)} left")
            elif change:
                self.loss.reset()
                self.cancel()
                self.token = object()
                self.emit("position", game=self.game, ply=self.history.board.ply(), fen=self.history.board.fen(),
                          reason=change, clock_ms={"white": position.white_ms, "black": position.black_ms})
        self.latest_position = position
        self.position_received_at = time.monotonic()
        self.watch_first_move(position)
        if position.relation != fics_game.playing_my_move:
            return
        board = self.history.board
        key = (self.game, board.fen(), len(board.move_stack))
        if self.task is not None and not self.task.done() and self.searched == key:
            return
        self.cancel()
        self.searched = key
        self.task = asyncio.ensure_future(self.play(position, board.copy()))
        self.task.add_done_callback(self.report)

    def emit_start(self, position):
        created = self.created if self.created and {self.created["white"], self.created["black"]} == {
            position.white, position.black} else {}
        self.emit("game_start", game=self.game, white=position.white, black=position.black,
                  white_elo=rating_of(created.get("white_rating", "")) or None,
                  black_elo=rating_of(created.get("black_rating", "")) or None,
                  initial_seconds=position.initial_seconds, increment_seconds=position.increment_seconds,
                  rated=created.get("rated") == "rated" if created else None,
                  variant=created.get("type") if created else None,
                  our_color="white" if self.our_color == chess.WHITE else "black",
                  fen=self.history.board.fen(), clock_ms={"white": position.white_ms, "black": position.black_ms})

    def emit_move(self, position):
        board = self.history.board
        move = board.peek()
        before = board.copy()
        before.pop()
        search = self.record.searches.get(before.ply())
        self.emit("move", game=self.game, ply=board.ply(), san=before.san(move), uci=move.uci(), fen=board.fen(),
                  by="us" if before.turn == self.our_color else "them",
                  clock_ms={"white": position.white_ms, "black": position.black_ms},
                  search=search_summary(before, search[1]) if search and search[0] == move else None)

    def report(self, task):
        if not task.cancelled() and task.exception() is not None:
            self.on_line(f"-- move task failed: {task.exception()!r}")

    async def send(self, line):
        try:
            await self.connection.send(line)
        except (ConnectionError, OSError) as error:
            self.on_line(f"-- could not send {line!r}: {error}")

    async def play(self, position, board):
        clock = (position.white_ms if board.turn == chess.WHITE else position.black_ms) / 1000
        limit = chess.engine.Limit(white_clock=position.white_ms / 1000, black_clock=position.black_ms / 1000,
                                   white_inc=position.increment_seconds, black_inc=position.increment_seconds)
        token = self.token
        started = time.monotonic()
        if self.loss.sent:
            return
        if not self.opponent_ready.is_set():
            # The first search waits briefly for finger, so UCI_Opponent carries title and engine flag.
            try:
                await asyncio.wait_for(self.opponent_ready.wait(), self.finger_wait)
            except asyncio.TimeoutError:
                self.opponent_ready.set()
        learner = self.game_learner
        if learner is not None and self.opening is None:
            await self.announce()  # a new week may bring a new mood
            game_type = self.created["type"].lower() if self.created else None
            rated = bool(self.created) and self.created["rated"] == "rated"
            self.opening = learner.for_game(self.opponent.name if self.opponent else None,
                                                 self.opponent_group(), game_type, self.contempt, rated)
        try:
            search_options = {"DrawContempt": self.contempt}
            if self.is_losers or self.is_wild:
                search_options["OwnBook"] = False
            ceiling = None if self.is_losers else self.loss.ceiling(
                (False if self.created.get("rated") == "unrated" else None) if self.created else None)
            move, info, kind = await move_choice.choose_move(
                self.session, board, limit, opening=self.opening,
                ceiling=ceiling,
                budget_ms=learning.per_move_budget_ms(clock, position.increment_seconds), tablebase=self.tablebase,
                game=self.token, opponent=self.opponent, timeout=max(clock, 1) + self.margin,
                options=search_options)
        except (asyncio.TimeoutError, RuntimeError, OSError, chess.engine.EngineError) as error:
            self.on_line(f"-- engine failed ({error or type(error).__name__}); restarting it")
            await self.stop_engine()
            await self.start()
            await self.send("refresh")
            return
        if self.token is not token or self.history is None or self.history.board.fen() != board.fen():
            return
        if move is None or not board.is_legal(move):
            self.on_line(f"-- engine returned no legal move in {board.fen()}")
            return
        fresh = self.loss.on_search(board, move, info, kind) if not self.is_losers else False
        latest = self.latest_position
        received_at = self.position_received_at if self.position_received_at is not None else started
        created = self.created or {}
        our_tag = "white" if self.our_color == chess.WHITE else "black"
        evidence = self.loss.decision(
            our_rating=rating_of(created.get(f"{our_tag}_rating", "")),
            their_rating=self.opponent.rating if self.opponent else None,
            bot=True if self.opponent and self.opponent.is_engine else None,
            speed=created.get("type"),
            ours=max(0, (latest.white_ms if self.our_color else latest.black_ms) / 1000 - (time.monotonic() - received_at)),
            theirs=(latest.black_ms if self.our_color else latest.white_ms) / 1000,
            increment=latest.increment_seconds, info=info, board=board) if fresh and not self.is_losers else None
        if evidence is not None:
            self.loss.sent = True
            self.on_line(f"-- resigning game {self.game}: {json.dumps(evidence)}")
            await self.send("resign")
            return
        if self.draw_offered and move_choice.accept_draw(board, info, self.contempt):
            # Accept, and move too: a withdrawn offer must not leave our clock running.
            self.on_line("-- accepting the draw offer")
            await self.send("draw")
        self.draw_offered = False
        await self.send(fics_game.ics_move(move))
        if self.record is not None:
            self.record.on_search(board, move, info)
        seconds, numbered = time.monotonic() - started, board.variation_san([move])
        left = f", {clock_text(max(0.0, clock - seconds))} left"
        explaining = self.whispering or self.followers
        if kind != "engine":
            book = kind == "book" and self.opening is not None and self.opening.why
            source = book_whisper(numbered, self.opening.why, self.opening) if book else f"{numbered} {info['source']}"
            self.on_line(f"-- {source} {seconds:.1f}s{left}")
            if explaining and kind == "book" and self.opening.why:
                await self.explain(book_whisper(board.variation_san([move]), self.opening.why, self.opening))
            elif explaining and kind == "tablebase":
                await self.explain(f"{board.variation_san([move])} {info['source']}")
            self.from_book = kind == "book"
            return
        pv = variation(board, info.get("pv", []))
        self.on_line(f"-- {numbered} {describe(info, board.turn)} {seconds:.1f}s{left}" + (f" pv {pv}" if pv else ""))
        if explaining:
            note = None
            if self.opening is not None and self.opening.why == ("left book",):
                note = "own idea"
            elif self.from_book:
                note = "out of book"
            line = variation(board, info.get("pv", [])[:whisper_plies]) or board.variation_san([move])
            await self.explain(search_whisper(info, board.turn, line, note, seconds))
        self.from_book = False


async def serve(connection, commands, on_line, probe_interval=300, probe_timeout=60, player=None):
    """Relay server lines and operator commands until quit; probe when either side is quiet, which
    checks the link and keeps FICS from logging us off after an hour without input."""
    probe_sent = None
    command = asyncio.ensure_future(commands.get())
    line = None
    try:
        while True:
            line = asyncio.ensure_future(connection.next(None))
            done, _ = await asyncio.wait({line, command}, timeout=1,
                                         return_when=asyncio.FIRST_COMPLETED)
            if line in done:
                text = line.result()
                if text is not None:
                    if player is None or not text.startswith("<12> "):
                        on_line(text)  # boards are noise once moves are logged
                    if player is not None:
                        await player.handle(text)
            else:
                line.cancel()
            if command in done:
                text = command.result()
                word = text.strip().lower()
                if word in ("quit", "exit") and (player is None or player.stopping):
                    await connection.send("quit")
                    return "quit"
                if word in ("quit", "exit", "softquit", "softrestart") and player is not None:
                    if word in ("softquit", "softrestart"):
                        player.restart = word == "softrestart"
                    await player.stop()
                elif word == "help":
                    on_line("-- console: quit (after the game, again to leave now), softquit, softrestart, help; "
                            "anything else goes to FICS (help <topic> for its help)")
                else:
                    await connection.send(text)
                command = asyncio.ensure_future(commands.get())
            if player is not None and player.finished:
                await connection.send("quit")
                return "quit"
            if player is not None:
                await player.reseek_when_idle()
            now = time.monotonic()
            if probe_sent is not None and connection.received > probe_sent:
                probe_sent = None
            if probe_sent is None and now - min(connection.received, connection.sent) > probe_interval:
                await connection.send("date")
                probe_sent = now
            elif probe_sent is not None and now - probe_sent > probe_timeout:
                raise ConnectionError("server stopped responding")
    finally:
        command.cancel()
        if line is not None:
            line.cancel()
        await asyncio.gather(*(task for task in (command, line) if task is not None), return_exceptions=True)


async def run_client(options, commands, on_line, backoff=backoff_seconds, transcript=None, player=None):
    """Keep a FICS session alive with reconnect backoff until the operator quits."""
    attempt = 0
    while True:
        if player is not None and player.stopping:
            return 0
        connection = None
        try:
            connection = await ics_connection.open(options.host, options.port, options.timeseal, transcript)
            if player is not None and player.stopping:
                return 0
            name = await login(connection, options.handle, options.password, on_line=on_line)
            if player is not None and player.stopping:
                return 0
            on_line(f"-- logged in as {name}")
            attempt = 0
            if player is not None:
                player.connection = connection
                await player.on_login(name)
            if await serve(connection, commands, on_line, options.probe_interval, player=player) == "quit":
                return 0
        except (ConnectionError, OSError, asyncio.TimeoutError) as error:
            if player is not None and player.stopping:
                return 0
            delay = backoff[min(attempt, len(backoff) - 1)]
            attempt += 1
            on_line(f"-- connection lost ({error or type(error).__name__}); reconnecting in {delay}s")
            await asyncio.sleep(delay)
        finally:
            if connection is not None:
                await connection.close()


def config_path(path=None):
    """An explicit file wins; otherwise use the first local/data/project .fics.json."""
    if path is not None:
        return Path(path).expanduser()
    return next((root / ".fics.json" for root in (Path.cwd(), data_root(), project_root())
                 if (root / ".fics.json").exists()), None)


def load_settings(path):
    """Defaults merged with an optional JSON file; unknown keys and wrong types are errors."""
    settings = dict(default_settings)
    if path is None:
        return settings
    with open(path, encoding="utf-8") as stream:
        loaded = json.load(stream)
    if not isinstance(loaded, dict):
        raise ValueError(f"{path}: expected a JSON object")
    types = {"host": str, "port": int, "handle": str, "engine": str, "options": dict, "seeks": list,
             "seeks_at_once": int, "accept": bool, "rematches": int, "games": int, "owner": (str, list), "notes": list,
             "pgn_dir": str, "learning": bool, "variety": (int, float), "contempt_window": int,
             "layer_profiles": dict, "style_books": dict, "first_move_wait": (int, float), "whisper": bool,
             "mood": str, "live_feed": str, "pgn_post": str,
             "online_tablebase": bool, "reseek_minutes": (int, float),
             "losers": bool, "losers_seeks": list, "losers_seeks_at_once": int}
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
            expected = " or ".join(kind.__name__ for kind in (types[key] if isinstance(types[key], tuple)
                                                                 else (types[key],)))
            raise ValueError(f"{path}: {key} must be {expected}")
        if key in ("seeks", "losers_seeks") and (value is None or not all(isinstance(spec, str) for spec in value)):
            raise ValueError(f"{path}: seeks must be strings like '3 0'")
        if key in ("seeks_at_once", "losers_seeks_at_once") and (value is None or value < 0):
            raise ValueError(f"{path}: {key} must be a nonnegative integer")
        if key == "losers" and value is None:
            raise ValueError(f"{path}: losers must be bool")
        if key == "owner" and isinstance(value, list) and not all(isinstance(name, str) for name in value):
            raise ValueError(f"{path}: owner must be a handle or a list of handles")
        if key == "notes" and (len(value) > 7 or not all(isinstance(note, str) for note in value)):
            raise ValueError(f"{path}: notes must be at most 7 strings (notes 1-3 are generated)")
        if key == "mood" and not re.fullmatch(r"[a-z0-9_]{1,32}", value):
            raise ValueError(f"{path}: mood is auto, off, or a name from books/moods")
        if key == "reseek_minutes" and (value is None or value < 0):
            raise ValueError(f"{path}: reseek_minutes must be 0 (off) or more")
        if key == "layer_profiles":
            value = layer_profiles(value, path)
        if key == "style_books" and not all(name in learning.style_names and isinstance(book, str)
                                            for name, book in value.items()):
            raise ValueError(f"{path}: style_books maps {', '.join(learning.style_names)} to Polyglot files")
        settings[key] = value
    settings["options"] = {str(name): str(value) for name, value in (settings["options"] or {}).items()}
    return settings


def layer_profiles(overrides, path):
    """The multi-layer book's default profiles with the configured percentages merged in."""
    profiles = {name: dict(weights) for name, weights in learning.layer_profiles.items()}
    for name, weights in (overrides or {}).items():
        if name not in profiles or not isinstance(weights, dict):
            raise ValueError(f"{path}: layer_profiles has profiles {', '.join(profiles)}")
        for layer, weight in weights.items():
            if layer not in profiles[name] or isinstance(weight, bool) or not isinstance(weight, (int, float)) \
                    or weight < 0:
                raise ValueError(f"{path}: layer_profiles.{name} takes non-negative "
                                 f"{', '.join(profiles[name])}")
            profiles[name][layer] = weight
    return profiles


def style_books(configured):
    """Style book paths: <style>.bin beside book.bin unless the config names a file (relative to the
    project root), e.g. {"fire": "ph-gambitbook.bin"}."""
    books = {name: data_root() / f"{name}.bin" for name in learning.style_names}
    books.update({name: data_root() / Path(path).expanduser() for name, path in (configured or {}).items()})
    return books


def read_commands(loop, commands):
    def reader():
        for line in sys.stdin:
            loop.call_soon_threadsafe(commands.put_nowait, line.rstrip("\r\n"))
    threading.Thread(target=reader, daemon=True).start()


def run(options, parser):
    """Play with parsed options; parser reports configuration errors."""
    try:
        options.config = config_path(options.config)
        configured = load_settings(options.config)
        if options.shutdown_grace is not None:
            configured["shutdown_grace"] = online_shutdown.grace_seconds(options.shutdown_grace)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    flags = {"host": options.host, "port": options.port, "handle": options.handle, "engine": options.engine,
             "seeks": options.seek, "seeks_at_once": options.seeks_at_once, "rematches": options.rematches,
             "games": options.games, "pgn_dir": options.pgn_dir, "variety": options.variety,
             "live_feed": options.live_feed, "pgn_post": options.pgn_post,
             "losers": options.losers, "losers_seeks": options.losers_seek,
             "losers_seeks_at_once": options.losers_seeks_at_once}
    configured.update({key: value for key, value in flags.items() if value is not None})
    configured["losers"] = configured["losers"] or bool(configured["losers_seeks"])
    if options.no_seek:
        configured["seeks"] = []
        configured["losers_seeks"] = []
    if options.no_accept:
        configured["accept"] = False
    if options.whisper:
        configured["whisper"] = True
    if options.no_learning or options.no_pgn:
        configured["learning"] = False
    if options.no_online_tablebase:
        configured["online_tablebase"] = False
    for name in ("host", "port", "handle", "engine"):
        setattr(options, name, configured[name])
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
    local_pieces = online_tablebase.local_pieces(settings.get("SyzygyPath"))
    feed_address = None
    if configured["live_feed"]:
        host, _, port = configured["live_feed"].rpartition(":")
        if not port.isdigit():
            parser.error(f"live feed must look like 8765 or 127.0.0.1:8765, got {configured['live_feed']!r}")
        feed_address = (host or "127.0.0.1", int(port))
    if configured["pgn_post"] and not re.match(r"https?://", configured["pgn_post"]):
        parser.error(f"PGN post needs an http(s) URL, got {configured['pgn_post']!r}")
    for name in ("seeks_at_once", "losers_seeks_at_once"):
        if type(configured[name]) is not int or configured[name] < 0:
            parser.error(f"{name} must be a nonnegative integer")
    for spec in configured["seeks"] + configured["losers_seeks"]:
        if not seek_pattern.fullmatch(spec):
            parser.error(f"seek spec must look like '3 0' or '5 2 u', got {spec!r}")
        if any(word in ("losers", "suicide", "wild", "atomic", "crazyhouse", "zh") or re.fullmatch(r"w\d+", word)
               for word in spec.split()[2:]):
            parser.error("seek pools contain time controls and seek options; use losers_seeks or --losers-seek for losers")
    policy = play_policy(configured["seeks"], configured["seeks_at_once"], configured["accept"],
                         configured["rematches"], configured["games"], guest=options.handle == "guest",
                         owner=configured["owner"], notes=configured["notes"],
                         contempt_window=configured["contempt_window"],
                         first_move_wait=configured["first_move_wait"], whisper=configured["whisper"],
                         follow_key=os.environ.get("MAXDRONE_KEY") or None,
                         reseek_minutes=configured["reseek_minutes"], losers=configured["losers"],
                         losers_seeks=configured["losers_seeks"], losers_seeks_at_once=configured["losers_seeks_at_once"])
    redact = (lambda text: text.replace(policy.follow_key, "********")) if policy.follow_key else (lambda text: text)
    pgn_dir = None if options.no_pgn else Path(configured["pgn_dir"] or data_root() / "experience" / "games")
    options.password = os.environ.get("FICS_PASSWORD") if options.handle != "guest" else None
    if not valid_handle(options.handle):
        parser.error("FICS handles have 3 to 17 letters, or use guest")
    transcript = None
    if options.debug:
        stream = open(options.debug, "a", encoding="utf-8")

        def transcript(text):
            stream.write(time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d} {redact(text)}\n")
            stream.flush()

    def show(line):
        print(redact(line), flush=True)

    if options.config is not None:
        show(f"-- configuration: {options.config}")

    @asynccontextmanager
    async def engine_session():
        # The data directory resolves the engine's relative eval.nnue and book.bin defaults.
        async with open_engine([str(options.engine or engine_path())], str(data_root())) as session:
            await session.configure(settings)
            yield session

    async def play_session(controller, commands, state):
        read_commands(asyncio.get_event_loop(), commands)
        learner = None
        losers_learner = None
        if configured["learning"] and not options.no_engine:
            learner = learning.learner(data_root() / "experience", pgn_dir, data_root() / "book.bin",
                                       configured["variety"],
                                       style_books(configured["style_books"]),
                                       configured["layer_profiles"] or None, configured["mood"],
                                       data_root() / "moods", server=options.host)
            show(f"-- experience: {len(learner.records)} games")
            if policy.losers:
                losers_games = pgn_dir.parent / "losers" / pgn_dir.name
                losers_learner = learning.learner(data_root() / "experience" / "losers", losers_games,
                                                 variety=configured["variety"], mood="off", server=options.host,
                                                 variant="losers")
                show(f"-- losers experience: {len(losers_learner.records)} games")
        events = connectors.game_events() if feed_address or configured["pgn_post"] else None
        player = None if options.no_engine else engine_player(engine_session, show, policy=policy,
                                                                  pgn_dir=pgn_dir, site=options.host, learner=learner,
                                                                  losers_learner=losers_learner,
                                                                  events=events,
                                                                  online_tablebase=configured["online_tablebase"],
                                                                  local_pieces=local_pieces,
                                                                  resignation_settings=configured["resignation"])
        state["player"] = player
        if controller.requested and player is not None:
            player.stopping = True
        feed = None
        if feed_address:
            def about():
                engine = player.session.protocol.id.get("name") if player and player.session else None
                return {"source": "fics", "server": options.host, "handle": player.name if player else None,
                        "engine": engine}
            feed = connectors.websocket_feed(events, *feed_address, about=about, on_line=show)
        if configured["pgn_post"]:
            events.subscribe(connectors.pgn_poster(configured["pgn_post"],
                                                   os.environ.get("MAXDRONE_POST_TOKEN") or None, show))
        try:
            if feed is not None:
                await feed.start()
            if controller.requested:
                return 0
            if player is not None:
                await player.start()
            status = await run_client(options, commands, show, transcript=transcript, player=player)
            return restart_status if status == 0 and player is not None and player.restart else status
        finally:
            if player is not None:
                if controller.requested:
                    player.save_interrupted()
                await player.close()
            if feed is not None:
                await feed.close()

    async def session():
        commands, state = asyncio.Queue(), {}
        controller = online_shutdown.shutdown_controller(show, configured["shutdown_grace"])

        def stop():
            player = state.get("player")
            if player is not None:
                player.stopping, player.restart = True, False
            commands.put_nowait("softquit" if player is not None else "quit")
            return player is not None and player.game is not None

        return await controller.run(play_session(controller, commands, state), stop)

    return asyncio.run(session())


if __name__ == "__main__":
    from .fics_cli import main
    try:
        sys.exit(main())
    except login_error as error:
        raise SystemExit(f"Login failed: {error}")
