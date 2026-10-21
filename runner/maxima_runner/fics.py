"""Play on FICS: timeseal transport, login, keepalive probes, and reconnects."""
import argparse
import asyncio
from contextlib import asynccontextmanager
import os
import platform
import re
import socket
import sys
import threading
import time

import chess
import chess.engine

from . import fics_game
from .project import engine_path, project_root
from .uci import open_engine

timeseal_key = b"Timestamp (FICS) v1.0 - programmed by Henrik Gram."
timeseal_ping = b"[G]\x00"
timeseal_pong = "\x029"
# FICS only decodes timeseal input after this hello line.
timeseal_hello = f"TIMESTAMP|maxima|{platform.system() or 'unknown'}|"
iac, will, wont, do, dont = 255, 251, 252, 253, 254
prompt = "fics% "
login_settings = (
    "set style 12", "set interface Maxima runner", "set seek 0", "set shout 0", "set cshout 0",
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


class engine_player:
    """Play our games with a UCI engine; searches run as tasks so server lines keep flowing."""

    def __init__(self, open_session, on_line, margin=5):
        self.open_session = open_session
        self.on_line = on_line
        self.margin = margin
        self.connection = None
        self.context = self.session = None
        self.game = self.history = self.token = None
        self.task = self.searched = self.retried = None

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
            self.cancel()
            self.on_line(f"-- game {self.game} over: {end['reason']} {end['result']}")
            self.game = self.history = None
        elif line.startswith("Illegal move") and self.game is not None and self.retried != self.searched:
            self.retried = self.searched
            await self.send("refresh")

    def on_board(self, position):
        if position.relation not in (fics_game.playing_my_move, fics_game.playing_their_move):
            return
        if position.game != self.game:
            history = fics_game.game_history(position)
            self.cancel()
            self.game, self.token, self.history = position.game, object(), history
            self.on_line(f"-- game {self.game}: {position.white} vs {position.black}, "
                         f"{position.initial_seconds // 60} {position.increment_seconds}")
        else:
            self.history.update(position)
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
        try:
            result = await self.session.play(board, limit, game=self.token, timeout=max(clock, 1) + self.margin)
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
                    await connection.send("quit")
                    return "quit"
                await connection.send(text)
                command = asyncio.ensure_future(commands.get())
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


def read_commands(loop, commands):
    def reader():
        for line in sys.stdin:
            loop.call_soon_threadsafe(commands.put_nowait, line.rstrip("\r\n"))
    threading.Thread(target=reader, daemon=True).start()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="freechess.org")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--handle", default="guest",
                        help="registered handle (password from FICS_PASSWORD), default guest")
    parser.add_argument("--no-timeseal", dest="timeseal", action="store_false",
                        help="send plain lines; the server then charges network lag to our clock")
    parser.add_argument("--probe-interval", type=float, default=300,
                        help="seconds of server silence before a liveness probe (default: 300)")
    parser.add_argument("--debug", metavar="FILE", help="append a wire transcript (password masked)")
    parser.add_argument("--engine", help="UCI engine to play with (default: the Release Maxima build)")
    parser.add_argument("--option", action="append", default=[], metavar="NAME=VALUE",
                        help="UCI option for the engine, repeatable (e.g. Hash=256)")
    parser.add_argument("--no-engine", action="store_true", help="connect without playing games")
    options = parser.parse_args()
    settings = {}
    for item in options.option:
        name, separator, value = item.partition("=")
        if not separator or not name.strip():
            parser.error(f"--option expects NAME=VALUE, got {item!r}")
        settings[name.strip()] = value.strip()
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
        player = None if options.no_engine else engine_player(engine_session, show)
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
