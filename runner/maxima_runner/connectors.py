"""Connectors around a game source: a neutral event stream, a WebSocket live feed and a PGN post.

Game sources (the FICS client today) call events.emit(); nothing here knows about a server.
The event format is described in MAXIMA.md chapter 11."""
import asyncio
import base64
import hashlib
import json
import struct
import urllib.request

feed_version = 1
websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class game_events:
    """Fan out events to listeners and keep the current game, so late viewers can catch up."""

    def __init__(self):
        self.listeners = []
        self.game = None

    def subscribe(self, listener):
        self.listeners.append(listener)

    def emit(self, kind, **fields):
        event = {"v": feed_version, "type": kind, **fields}
        if kind == "game_start":
            self.game = {"start": event, "moves": []}
        elif kind == "move" and self.game is not None:
            self.game["moves"].append(event)
        elif kind == "position" and self.game is not None:
            self.game["moves"] = self.game["moves"][:event["ply"]]
        elif kind == "game_end":
            self.game = None
        for listener in list(self.listeners):
            listener(event)
        return event


def frame(text, opcode=0x1):
    """One unmasked server frame (RFC 6455 5.2)."""
    payload = text.encode() if isinstance(text, str) else text
    size = len(payload)
    if size < 126:
        header = struct.pack("!BB", 0x80 | opcode, size)
    elif size < 1 << 16:
        header = struct.pack("!BBH", 0x80 | opcode, 126, size)
    else:
        header = struct.pack("!BBQ", 0x80 | opcode, 127, size)
    return header + payload


async def read_frame(reader):
    """Opcode and unmasked payload of one client frame; clients must mask (RFC 6455 5.3)."""
    first, second = await reader.readexactly(2)
    size = second & 0x7F
    if size == 126:
        size = struct.unpack("!H", await reader.readexactly(2))[0]
    elif size == 127:
        size = struct.unpack("!Q", await reader.readexactly(8))[0]
    if size > 1 << 16:
        raise ValueError("client frame too large")
    mask = await reader.readexactly(4) if second & 0x80 else b"\0\0\0\0"
    payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(await reader.readexactly(size)))
    return first & 0x0F, payload


def accept_key(key):
    return base64.b64encode(hashlib.sha1((key + websocket_guid).encode()).digest()).decode()


class websocket_feed:
    """Serve every event as one JSON text message to any number of WebSocket viewers."""

    def __init__(self, events, host, port, about=None, on_line=print, backlog=256):
        self.events, self.host, self.port = events, host, port
        self.about = about or (lambda: {})
        self.on_line = on_line
        self.backlog = backlog
        self.clients = {}
        self.server = None
        events.subscribe(self.publish)

    async def start(self):
        self.server = await asyncio.start_server(self.serve, self.host, self.port)
        self.port = self.server.sockets[0].getsockname()[1]
        self.on_line(f"-- live feed on ws://{self.host}:{self.port}/")

    async def close(self):
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
        for writer in list(self.clients.values()):
            writer.close()

    def hello(self):
        return {"v": feed_version, "type": "hello", **self.about(), "game": self.events.game}

    def publish(self, event):
        text = json.dumps(event)
        for queue, writer in list(self.clients.items()):
            if queue.full():
                # A viewer that cannot keep up is dropped rather than slowing the game.
                del self.clients[queue]
                writer.close()
            else:
                queue.put_nowait(text)

    async def handshake(self, reader, writer):
        request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10)
        headers = {}
        for line in request.decode("latin-1").split("\r\n")[1:]:
            name, separator, value = line.partition(":")
            if separator:
                headers[name.strip().lower()] = value.strip()
        key = headers.get("sec-websocket-key")
        if headers.get("upgrade", "").lower() != "websocket" or not key:
            writer.write(b"HTTP/1.1 426 Upgrade Required\r\nSec-WebSocket-Version: 13\r\n"
                         b"Content-Length: 0\r\nConnection: close\r\n\r\n")
            return False
        writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      f"Sec-WebSocket-Accept: {accept_key(key)}\r\n\r\n").encode())
        return True

    async def serve(self, reader, writer):
        queue = asyncio.Queue(self.backlog)
        try:
            if not await self.handshake(reader, writer):
                return
            queue.put_nowait(json.dumps(self.hello()))
            self.clients[queue] = writer
            listener = asyncio.ensure_future(self.listen(reader, writer))
            while True:
                text = await queue.get()
                if text is None:
                    break
                writer.write(frame(text))
                await writer.drain()
            listener.cancel()
            writer.write(frame(b"", 0x8))
        except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, asyncio.TimeoutError,
                ConnectionError, OSError, ValueError):
            pass
        finally:
            self.clients.pop(queue, None)
            writer.close()

    async def listen(self, reader, writer):
        """Answer pings and close with the viewer; viewers have nothing else to say."""
        try:
            while True:
                opcode, payload = await read_frame(reader)
                if opcode == 0x8:
                    break
                if opcode == 0x9:
                    writer.write(frame(payload, 0xA))
        except (asyncio.IncompleteReadError, ConnectionError, OSError, ValueError):
            pass
        writer.close()


def pgn_poster(url, token=None, on_line=print):
    """Listener that POSTs each finished game as JSON; failures are logged, the local PGN remains."""
    def post(event):
        if event["type"] != "game_end" or not event.get("pgn"):
            return
        body = json.dumps({name: event.get(name) for name in
                           ("v", "source", "game", "result", "reason", "headers", "pgn")}).encode()
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        request = urllib.request.Request(url, body, headers, method="POST")

        def send():
            try:
                urllib.request.urlopen(request, timeout=10).close()
                on_line(f"-- posted game {event.get('game')} to {url}")
            except (OSError, ValueError) as error:
                on_line(f"-- could not post game {event.get('game')} to {url}: {error}")

        try:
            asyncio.get_running_loop().run_in_executor(None, send)
        except RuntimeError:
            send()
    return post
