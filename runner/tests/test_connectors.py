"""Connectors: event fan-out, the WebSocket live feed and the PGN post."""
import asyncio
import base64
import http.server
import json
import os
import struct
import threading
import unittest

from maxima_runner import connectors


async def read_text(reader):
    opcode, payload = await connectors.read_frame(reader)
    return opcode, json.loads(payload) if opcode == 0x1 else payload


def masked(opcode, payload=b""):
    mask = os.urandom(4)
    body = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
    return struct.pack("!BB", 0x80 | opcode, 0x80 | len(payload)) + mask + body


class events_tests(unittest.TestCase):
    def test_snapshot_follows_the_game(self):
        events = connectors.game_events()
        seen = []
        events.subscribe(seen.append)
        events.emit("game_start", game=7, fen="start")
        events.emit("move", game=7, ply=1, san="e4")
        events.emit("move", game=7, ply=2, san="e5")
        events.emit("position", game=7, ply=1, fen="after e4")
        self.assertEqual([move["san"] for move in events.game["moves"]], ["e4"])
        events.emit("game_end", game=7, result="1-0")
        self.assertIsNone(events.game)
        self.assertEqual([event["type"] for event in seen], ["game_start", "move", "move", "position", "game_end"])
        self.assertTrue(all(event["v"] == connectors.feed_version for event in seen))


class feed_tests(unittest.IsolatedAsyncioTestCase):
    async def test_viewer_gets_hello_with_game_then_events(self):
        events = connectors.game_events()
        feed = connectors.websocket_feed(events, "127.0.0.1", 0, about=lambda: {"source": "test"},
                                         on_line=lambda text: None)
        await feed.start()
        self.addAsyncCleanup(feed.close)
        events.emit("game_start", game=3, fen="start")
        reader, writer = await asyncio.open_connection("127.0.0.1", feed.port)
        key = base64.b64encode(os.urandom(16)).decode()
        writer.write((f"GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        response = (await reader.readuntil(b"\r\n\r\n")).decode()
        self.assertIn("101", response.split("\r\n")[0])
        self.assertIn(connectors.accept_key(key), response)
        opcode, hello = await read_text(reader)
        self.assertEqual((hello["type"], hello["source"], hello["game"]["start"]["game"]), ("hello", "test", 3))
        events.emit("move", game=3, ply=1, san="d4")
        self.assertEqual((await read_text(reader))[1]["san"], "d4")
        writer.write(masked(0x9, b"hi"))
        self.assertEqual(await connectors.read_frame(reader), (0xA, b"hi"))
        writer.write(masked(0x8))
        writer.close()

    async def test_plain_http_is_refused(self):
        feed = connectors.websocket_feed(connectors.game_events(), "127.0.0.1", 0, on_line=lambda text: None)
        await feed.start()
        self.addAsyncCleanup(feed.close)
        reader, writer = await asyncio.open_connection("127.0.0.1", feed.port)
        writer.write(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"426", await reader.readline())
        writer.close()


class post_tests(unittest.TestCase):
    def test_finished_game_is_posted_as_json(self):
        received = []

        class handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                received.append((self.headers["Authorization"], json.loads(body)))
                self.send_response(204)
                self.end_headers()

            def log_message(self, *unused):
                pass

        server = http.server.HTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        lines = []
        post = connectors.pgn_poster(f"http://127.0.0.1:{server.server_port}/games", "secret", lines.append)
        events = connectors.game_events()
        events.subscribe(post)
        events.emit("move", game=5, ply=1, san="e4")
        events.emit("game_end", game=5, source="fics", result="1-0", reason="mate", headers={}, pgn="1. e4 1-0")
        self.assertEqual(len(received), 1)
        self.assertEqual(received[0][0], "Bearer secret")
        self.assertEqual((received[0][1]["game"], received[0][1]["pgn"]), (5, "1. e4 1-0"))
        self.assertIn("posted game 5", lines[0])


if __name__ == "__main__":
    unittest.main()
