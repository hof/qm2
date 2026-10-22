"""Play on FICS: timeseal transport, login, keepalive probes, and reconnects."""
import argparse
import asyncio
import json
from contextlib import asynccontextmanager
import os
from pathlib import Path
import platform
import random
import re
import socket
import sys
import threading
import time

import chess
import chess.engine

from . import fics_game, learning
from .project import engine_path, project_root
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
        self.received = self.started
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
finger_pattern = re.compile(r"^Finger of (?P<name>[A-Za-z]+)(?P<titles>(?:\([A-Z*]+\))*):")
playable_types = ("lightning", "blitz", "standard")
chess_titles = ("GM", "IM", "FM", "WGM", "WIM", "WFM", "CM", "WCM", "NM")
seek_pattern = re.compile(r"\d{1,3} \d{1,3}(?: [a-z0-9+-]+)*")
rated_word = re.compile(r"\b(?:r|u|rated|unrated)\b")
default_settings = {
    "host": "freechess.org", "port": 5000, "handle": "guest", "engine": None, "options": {},
    "seeks": ["3 0", "3 1", "5 0"], "seeks_at_once": 2, "accept": True, "rematches": 0, "games": None,
    "owner": None, "notes": [], "pgn_dir": None, "learning": True, "variety": 1.0,
    "contempt_window": 100,
}


def playable(game_type, minutes, increment):
    """Standard chess with a running clock; variants and untimed games are refused."""
    return game_type.lower() in playable_types and int(minutes) + int(increment) > 0


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
                 owner=None, notes=(), contempt_window=100):
        self.seeks = list(seeks)
        self.seeks_at_once = seeks_at_once
        self.accept = accept
        self.rematches = rematches
        self.games = games
        self.guest = guest
        self.owner = owner
        self.notes = list(notes)
        self.contempt_window = contempt_window

    def seek_commands(self):
        """A random draw from the pool; guests can only play unrated."""
        chosen = random.sample(self.seeks, min(self.seeks_at_once, len(self.seeks)))
        return [f"seek {spec}" + (" u" if self.guest and not rated_word.search(spec) else "") for spec in chosen]


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


class engine_player:
    """Play our games with a UCI engine; searches run as tasks so server lines keep flowing."""

    def __init__(self, open_session, on_line, margin=5, policy=None, finger_wait=2, pgn_dir=None,
                 site="freechess.org", learner=None):
        self.open_session = open_session
        self.learner = learner
        self.opening = None
        self.contempt = None
        self.fingered = False
        self.pgn_dir = pgn_dir
        self.site = site
        self.record = self.created = None
        self.opponent_unregistered = False
        self.on_line = on_line
        self.margin = margin
        self.policy = policy or play_policy()
        self.finger_wait = finger_wait
        self.connection = None
        self.context = self.session = None
        self.game = self.history = self.token = None
        self.task = self.searched = self.retried = None
        self.name = self.opponent = self.last_opponent = None
        self.opponent_ready = asyncio.Event()
        self.opponent_ready.set()
        self.streak = self.played = 0
        self.stopping = self.finished = False

    async def start(self):
        self.context = self.open_session()
        self.session = await self.context.__aenter__()

    async def stop_engine(self):
        if self.context is not None:
            context, self.context, self.session = self.context, None, None
            await context.__aexit__(None, None, None)

    async def close(self):
        self.cancel()
        await self.stop_engine()

    def cancel(self):
        if self.task is not None and not self.task.done():
            self.task.cancel()

    async def on_login(self, name):
        """A new session: any earlier game was forfeited (noescape), so start seeking again."""
        self.cancel()
        self.name = name
        self.game = self.history = None
        if not self.policy.guest:
            notes = finger_notes(*self.engine_facts(), self.policy.notes)
            for number, note in enumerate(notes, 1):
                await self.send(f"set {number} {note}")
        await self.seek()

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
        if self.stopping or self.game is not None or not self.policy.seeks:
            return
        await self.send("unseek")
        for command in self.policy.seek_commands():
            await self.send(command)

    async def stop(self):
        """Stop seeking; quit now when idle, or after the current game."""
        self.stopping = True
        await self.send("unseek")
        if self.game is None:
            self.finished = True
        else:
            self.on_line("-- stopping after this game; quit again to leave now")

    async def handle(self, line):
        position = fics_game.style12.parse(line) if line.startswith("<12> ") else None
        if position is not None:
            try:
                self.on_board(position)
            except ValueError as error:
                self.on_line(f"-- ignored board: {error}")
            return
        end = fics_game.game_end_pattern.match(line)
        if end and int(end["game"]) == self.game:
            await self.on_game_end(end)
        elif line.startswith("Illegal move") and self.game is not None and self.retried != self.searched:
            self.retried = self.searched
            await self.send("refresh")
        elif creating_pattern.match(line):
            await self.on_creating(creating_pattern.match(line))
        elif challenge_pattern.match(line):
            await self.on_challenge(challenge_pattern.match(line))
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
        if not playable(match["type"], match["time"], match["increment"]):
            self.on_line(f"-- aborting unsupported game: {match['type']} {match['time']} {match['increment']}")
            await self.send("abort")
            return
        await self.send(f"finger {name}")

    async def on_challenge(self, match):
        name = match["name"]
        if self.game is not None or self.stopping or not self.policy.accept:
            await self.send(f"decline {name}")
        elif not playable(match["type"], match["time"], match["increment"]):
            self.on_line(f"-- declining {match['type']} {match['time']} {match['increment']} from {name}")
            await self.send(f"decline {name}")
        else:
            await self.send(f"accept {name}")

    async def on_game_end(self, end):
        self.cancel()
        self.on_line(f"-- game {self.game} over: {end['reason']} {end['result']}")
        opponent = end["black"] if self.name and end["white"].lower() == self.name.lower() else end["white"]
        self.save(end)
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

    def headers(self, end):
        """PGN tags from Creating:, finger, and the engine; our side is a program, the other side as fingered."""
        created = self.created if self.created and {self.created["white"], self.created["black"]} == {
            end["white"], end["black"]} else {}
        headers = {"Event": f"FICS {created['rated']} {created['type']} game" if created else "FICS game",
                   "Site": self.site, "White": end["white"], "Black": end["black"]}
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
        return headers

    def save(self, end):
        if self.pgn_dir is None or self.record is None or self.history is None or not self.history.board.move_stack:
            return
        self.record.headers.update(self.headers(end))
        game = self.record.game(self.history.board, end["result"], end["reason"])
        try:
            path = fics_game.save_game(game, self.pgn_dir, self.record.started)
        except OSError as error:
            self.on_line(f"-- could not save game {self.game}: {error}")
            return
        self.on_line(f"-- saved game {self.game} to {path}")
        if self.learner is not None:
            self.learner.add(game)
            asyncio.get_event_loop().run_in_executor(None, self.refresh_experience)

    def refresh_experience(self):
        try:
            self.learner.refresh()
        except (OSError, ValueError) as error:
            self.on_line(f"-- could not refresh experience: {error}")

    def opponent_group(self):
        """Learning group from finger; None (global layer only) when unregistered or unknown."""
        if self.opponent is None or not self.fingered or self.opponent_unregistered:
            return None
        if self.opponent.is_engine:
            return "engines"
        return "masters" if self.opponent.title else "humans"

    def on_board(self, position):
        if position.relation not in (fics_game.playing_my_move, fics_game.playing_their_move):
            return
        if position.game != self.game:
            history = fics_game.game_history(position)
            self.cancel()
            self.game, self.token, self.history = position.game, object(), history
            self.record = fics_game.game_record({})
            self.opening = None
            self.on_line(f"-- game {self.game}: {position.white} vs {position.black}, "
                         f"{position.initial_seconds // 60} {position.increment_seconds}")
        elif self.history.update(position) == "move":
            mover_ms = position.black_ms if position.turn == chess.WHITE else position.white_ms
            self.record.on_move(self.history.board, mover_ms / 1000, position.elapsed)
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
        started = time.monotonic()
        if not self.opponent_ready.is_set():
            # The first search waits briefly for finger, so UCI_Opponent carries title and engine flag.
            try:
                await asyncio.wait_for(self.opponent_ready.wait(), self.finger_wait)
            except asyncio.TimeoutError:
                self.opponent_ready.set()
        if self.learner is not None:
            if self.opening is None:
                game_type = self.created["type"].lower() if self.created else None
                self.opening = self.learner.for_game(self.opponent.name if self.opponent else None,
                                                     self.opponent_group(), game_type, self.contempt)
            choice = self.opening.choose(board, learning.per_move_budget_ms(clock, position.increment_seconds))
            if choice is not None:
                move, reason = choice
                await self.send(fics_game.ics_move(move))
                if self.record is not None:
                    self.record.on_search(board, move, {"source": reason.split()[0]})
                self.on_line(f"-- {board.san(move)} {reason} {time.monotonic() - started:.1f}s")
                return
        try:
            result = await self.session.play(board, limit, game=self.token, opponent=self.opponent,
                                             timeout=max(clock, 1) + self.margin,
                                             options={"DrawContempt": self.contempt})
        except (asyncio.TimeoutError, RuntimeError, OSError, chess.engine.EngineError) as error:
            self.on_line(f"-- engine failed ({error or type(error).__name__}); restarting it")
            await self.stop_engine()
            await self.start()
            await self.send("refresh")
            return
        if result.move is None or not board.is_legal(result.move):
            self.on_line(f"-- engine returned no legal move in {board.fen()}")
            return
        await self.send(fics_game.ics_move(result.move))
        if self.record is not None:
            self.record.on_search(board, result.move, result.info)
        self.on_line(f"-- {board.san(result.move)} {describe(result.info, board.turn)} "
                     f"{time.monotonic() - started:.1f}s")


def describe(info, turn):
    score = info.get("score")
    depth = info.get("depth", 0)
    if score is None or not depth:
        return "book"
    relative = score.pov(turn)
    value = f"#{relative.mate()}" if relative.is_mate() else f"{relative.score() / 100:+.2f}"
    return f"d{depth} {value}"


async def serve(connection, commands, on_line, probe_interval=300, probe_timeout=60, player=None):
    """Relay server lines and operator commands until quit; probe the server when it is quiet."""
    probe_sent = None
    command = asyncio.ensure_future(commands.get())
    try:
        while True:
            line = asyncio.ensure_future(connection.next(None))
            done, _ = await asyncio.wait({line, command}, timeout=1,
                                         return_when=asyncio.FIRST_COMPLETED)
            if line in done:
                text = line.result()
                if text is not None:
                    on_line(text)
                    if player is not None:
                        await player.handle(text)
            else:
                line.cancel()
            if command in done:
                text = command.result()
                if text.strip().lower() in ("quit", "exit"):
                    if player is None or player.stopping or player.game is None:
                        await connection.send("quit")
                        return "quit"
                    await player.stop()
                else:
                    await connection.send(text)
                command = asyncio.ensure_future(commands.get())
            if player is not None and player.finished:
                await connection.send("quit")
                return "quit"
            now = time.monotonic()
            if probe_sent is not None and connection.received > probe_sent:
                probe_sent = None
            if probe_sent is None and now - connection.received > probe_interval:
                await connection.send("date")
                probe_sent = now
            elif probe_sent is not None and now - probe_sent > probe_timeout:
                raise ConnectionError("server stopped responding")
    finally:
        command.cancel()


async def run_client(options, commands, on_line, backoff=backoff_seconds, transcript=None, player=None):
    """Keep a FICS session alive with reconnect backoff until the operator quits."""
    attempt = 0
    while True:
        connection = None
        try:
            connection = await ics_connection.open(options.host, options.port, options.timeseal, transcript)
            name = await login(connection, options.handle, options.password, on_line=on_line)
            on_line(f"-- logged in as {name}")
            attempt = 0
            if player is not None:
                player.connection = connection
                await player.on_login(name)
            if await serve(connection, commands, on_line, options.probe_interval, player=player) == "quit":
                return 0
        except (ConnectionError, OSError, asyncio.TimeoutError) as error:
            delay = backoff[min(attempt, len(backoff) - 1)]
            attempt += 1
            on_line(f"-- connection lost ({error or type(error).__name__}); reconnecting in {delay}s")
            await asyncio.sleep(delay)
        finally:
            if connection is not None:
                await connection.close()


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
             "pgn_dir": str, "learning": bool, "variety": (int, float), "contempt_window": int}
    for key, value in loaded.items():
        if key not in types:
            raise ValueError(f"{path}: unknown setting {key!r}")
        kinds = types[key] if isinstance(types[key], tuple) else (types[key],)
        if value is not None and (not isinstance(value, kinds) or isinstance(value, bool) and bool not in kinds):
            expected = " or ".join(kind.__name__ for kind in (types[key] if isinstance(types[key], tuple)
                                                                 else (types[key],)))
            raise ValueError(f"{path}: {key} must be {expected}")
        if key == "seeks" and not all(isinstance(spec, str) for spec in value):
            raise ValueError(f"{path}: seeks must be strings like '3 0'")
        if key == "owner" and isinstance(value, list) and not all(isinstance(name, str) for name in value):
            raise ValueError(f"{path}: owner must be a handle or a list of handles")
        if key == "notes" and (len(value) > 7 or not all(isinstance(note, str) for note in value)):
            raise ValueError(f"{path}: notes must be at most 7 strings (notes 1-3 are generated)")
        settings[key] = value
    settings["options"] = {str(name): str(value) for name, value in (settings["options"] or {}).items()}
    return settings


def read_commands(loop, commands):
    def reader():
        for line in sys.stdin:
            loop.call_soon_threadsafe(commands.put_nowait, line.rstrip("\r\n"))
    threading.Thread(target=reader, daemon=True).start()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", metavar="FILE",
                        help="JSON settings (keys: host, port, handle, engine, options, seeks, seeks_at_once, "
                             "accept, rematches, games, owner, notes, pgn_dir, learning, variety, contempt_window); "
                             "flags override it")
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    parser.add_argument("--handle", help="registered handle (password from FICS_PASSWORD), default guest")
    parser.add_argument("--no-timeseal", dest="timeseal", action="store_false",
                        help="send plain lines; the server then charges network lag to our clock")
    parser.add_argument("--probe-interval", type=float, default=300,
                        help="seconds of server silence before a liveness probe (default: 300)")
    parser.add_argument("--debug", metavar="FILE", help="append a wire transcript (password masked)")
    parser.add_argument("--engine", help="UCI engine to play with (default: the Release Maxima build)")
    parser.add_argument("--option", action="append", default=[], metavar="NAME=VALUE",
                        help="UCI option for the engine, repeatable (e.g. Hash=256)")
    parser.add_argument("--no-engine", action="store_true", help="connect without playing games")
    parser.add_argument("--seek", action="append", metavar="SPEC",
                        help='seek pool entry, repeatable (e.g. "3 0"; default pool 3 0, 3 1, 5 0)')
    parser.add_argument("--seeks-at-once", type=int, help="seeks posted at a time, drawn from the pool (default: 2)")
    parser.add_argument("--no-seek", action="store_true", help="do not seek; only accept challenges")
    parser.add_argument("--no-accept", action="store_true", help="decline all challenges")
    parser.add_argument("--rematches", type=int, help="rematches in a row per opponent (default: 0)")
    parser.add_argument("--games", type=int, help="quit after this many games")
    parser.add_argument("--pgn-dir", help="directory for monthly PGN files (default: experience/games/fics)")
    parser.add_argument("--no-pgn", action="store_true", help="do not save games")
    parser.add_argument("--no-learning", action="store_true",
                        help="let the engine use its own book instead of MaxDrone's experience")
    parser.add_argument("--variety", type=float,
                        help="opening variety: above 1 explores more, below 1 repeats winners (default: 1)")
    options = parser.parse_args()
    try:
        configured = load_settings(options.config)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    flags = {"host": options.host, "port": options.port, "handle": options.handle, "engine": options.engine,
             "seeks": options.seek, "seeks_at_once": options.seeks_at_once, "rematches": options.rematches,
             "games": options.games, "pgn_dir": options.pgn_dir, "variety": options.variety}
    configured.update({key: value for key, value in flags.items() if value is not None})
    if options.no_seek:
        configured["seeks"] = []
    if options.no_accept:
        configured["accept"] = False
    if options.no_learning or options.no_pgn:
        configured["learning"] = False
    for name in ("host", "port", "handle", "engine"):
        setattr(options, name, configured[name])
    settings = dict(configured["options"])
    if configured["learning"]:
        settings.setdefault("OwnBook", "false")  # MaxDrone chooses book and learned moves (MAXIMA.md 9.7)
    for item in options.option:
        name, separator, value = item.partition("=")
        if not separator or not name.strip():
            parser.error(f"--option expects NAME=VALUE, got {item!r}")
        settings[name.strip()] = value.strip()
    for spec in configured["seeks"]:
        if not seek_pattern.fullmatch(spec):
            parser.error(f"seek spec must look like '3 0' or '5 2 u', got {spec!r}")
    policy = play_policy(configured["seeks"], configured["seeks_at_once"], configured["accept"],
                         configured["rematches"], configured["games"], guest=options.handle == "guest",
                         owner=configured["owner"], notes=configured["notes"],
                         contempt_window=configured["contempt_window"])
    pgn_dir = None if options.no_pgn else Path(configured["pgn_dir"] or project_root() / "experience" / "games" / "fics")
    options.password = os.environ.get("FICS_PASSWORD") if options.handle != "guest" else None
    if not valid_handle(options.handle):
        parser.error("FICS handles have 3 to 17 letters, or use guest")
    transcript = None
    if options.debug:
        stream = open(options.debug, "a", encoding="utf-8")

        def transcript(text):
            stream.write(time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d} {text}\n")
            stream.flush()

    def show(line):
        print(line, flush=True)

    @asynccontextmanager
    async def engine_session():
        # The project root resolves the engine's relative eval.nnue and book.bin defaults.
        async with open_engine([str(options.engine or engine_path())], str(project_root())) as session:
            await session.configure(settings)
            yield session

    async def session():
        commands = asyncio.Queue()
        read_commands(asyncio.get_event_loop(), commands)
        learner = None
        if configured["learning"] and not options.no_engine:
            # index/ and players/ sit beside games/<server>/, so a custom --pgn-dir keeps them together
            learner = learning.learner(pgn_dir.parent.parent, pgn_dir, project_root() / "book.bin",
                                       configured["variety"])
            show(f"-- experience: {len(learner.records)} games")
        player = None if options.no_engine else engine_player(engine_session, show, policy=policy,
                                                                  pgn_dir=pgn_dir, site=options.host, learner=learner)
        try:
            if player is not None:
                await player.start()
            return await run_client(options, commands, show, transcript=transcript, player=player)
        finally:
            if player is not None:
                await player.close()

    return asyncio.run(session())


if __name__ == "__main__":
    try:
        sys.exit(main())
    except login_error as error:
        raise SystemExit(f"Login failed: {error}")
