"""Lichess bot: which challenges it accepts, owner commands, and one game through a fake Lichess server."""
import asyncio
import http.server
import json
from pathlib import Path
import queue
import sys
import tempfile
import threading
import unittest

from maxima_runner import connectors, lichess
from maxima_runner.uci import open_engine
from test_fics_game import fake_engine


def challenge(speed="blitz", variant="standard", clock=True):
    return {"id": "c1", "challenger": {"id": "someone", "name": "Someone", "rating": 1500}, "speed": speed,
            "variant": {"key": variant}, "timeControl": {"type": "clock" if clock else "unlimited"}, "rated": False}


def game_full(moves=""):
    return {"type": "gameFull", "id": "g1", "variant": {"key": "standard"}, "speed": "blitz", "rated": False,
            "white": {"id": "maximaengine", "name": "MaximaEngine", "title": "BOT", "rating": 2000},
            "black": {"id": "someone", "name": "Someone", "title": None, "rating": 1800},
            "initialFen": "startpos", "clock": {"initial": 180000, "increment": 2000},
            "state": {"type": "gameState", "moves": moves, "wtime": 180000, "btime": 180000, "winc": 2000,
                      "binc": 2000, "status": "started"}}


class stub_api:
    server = "https://lichess.org"

    def __init__(self):
        self.posts = []

    async def post(self, path, data=None):
        self.posts.append((path, data))
        return True


class challenge_tests(unittest.TestCase):
    def test_accepts_standard_clock_games_at_configured_speeds(self):
        bot = lichess.lichess_bot(stub_api(), None, lambda line: None, speeds=("blitz", "rapid"))
        self.assertIsNone(bot.decline_reason(challenge("blitz"), {"bot": True}))
        self.assertEqual(bot.decline_reason(challenge(variant="chess960"), {}), "standard")
        self.assertEqual(bot.decline_reason(challenge("bullet"), {}), "tooFast")
        self.assertEqual(bot.decline_reason(challenge("ultraBullet"), {"bot": False}), "tooFast")
        self.assertEqual(bot.decline_reason(challenge("correspondence", clock=False), {}), "tooSlow")
        bot.game = object()
        self.assertEqual(bot.decline_reason(challenge("blitz"), {}), "later")


class command_tests(unittest.IsolatedAsyncioTestCase):
    async def test_only_owners_steer_the_game(self):
        api, lines = stub_api(), []
        bot = lichess.lichess_bot(api, None, lines.append, owners=["SirHajewiet"])
        bot.id = "maximaengine"
        game = lichess.lichess_game(bot, "g1")
        await game.on_chat({"username": "Someone", "text": "!resign", "room": "player"})
        await game.on_chat({"username": "MaximaEngine", "text": "!resign", "room": "player"})
        self.assertEqual(api.posts, [])
        await game.on_chat({"username": "sirhajewiet", "text": "!resign", "room": "spectator"})
        await game.on_chat({"username": "SirHajewiet", "text": "!softquit", "room": "spectator"})
        self.assertEqual([path for path, _ in api.posts],
                         ["/api/bot/game/g1/resign", "/api/bot/game/g1/chat"])
        self.assertTrue(bot.stopping)
        self.assertTrue(any("refused" in line for line in lines))


class fake_lichess(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), handler)
        self.streams = {"/api/stream/event": queue.Queue(), "/api/bot/game/stream/g1": queue.Queue()}
        self.posts = []

    def close_streams(self):
        for lines in self.streams.values():
            lines.put(None)


class handler(http.server.BaseHTTPRequestHandler):
    def reply(self, body):
        data = json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/api/account":
            self.reply({"id": "maximaengine", "username": "MaximaEngine", "title": "BOT"})
            return
        lines = self.server.streams[self.path]
        self.send_response(200)
        self.end_headers()
        while (item := lines.get()) is not None:
            self.wfile.write(json.dumps(item).encode() + b"\n")
            self.wfile.flush()

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        self.server.posts.append((self.path, self.rfile.read(length).decode()))
        self.reply({"ok": True})

    def log_message(self, *unused):
        pass


class game_tests(unittest.IsolatedAsyncioTestCase):
    async def test_plays_saves_and_reports_a_game(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        script = Path(directory.name) / "engine.py"
        script.write_text(fake_engine)
        server = fake_lichess()
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        self.addCleanup(server.close_streams)
        events, seen = connectors.game_events(), []
        events.subscribe(seen.append)
        api = lichess.lichess_api("token", f"http://127.0.0.1:{server.server_port}", lambda line: None)
        session = lambda: open_engine([sys.executable, str(script), "play", str(Path(directory.name) / "log")],
                                      directory.name, timeout=5)
        bot = lichess.lichess_bot(api, session, lambda line: None, games=1, pgn_dir=Path(directory.name) / "games",
                                  events=events, online_tablebase=False)
        await bot.start()
        running = asyncio.ensure_future(bot.run(asyncio.Queue()))
        server.streams["/api/stream/event"].put({"type": "gameStart", "game": {"gameId": "g1"}})
        server.streams["/api/bot/game/stream/g1"].put(game_full())
        for _ in range(100):
            if ("/api/bot/game/g1/move/e2e4", "") in server.posts:
                break
            await asyncio.sleep(0.05)
        state = dict(game_full("e2e4 e7e5")["state"], status="resign", winner="white")
        server.streams["/api/bot/game/stream/g1"].put(state)
        self.assertEqual(await asyncio.wait_for(running, 10), 0)
        await bot.close()
        self.assertEqual([event["type"] for event in seen], ["game_start", "move", "move", "game_end"])
        self.assertEqual(seen[1]["search"]["score"], {"cp": 25})
        pgn = seen[-1]["pgn"]
        self.assertIn('[Event "Lichess casual blitz game"]', pgn)
        self.assertIn("1. e4", pgn)
        self.assertIn("1-0", pgn)
        self.assertEqual(len(list((Path(directory.name) / "games").glob("*.pgn"))), 1)


if __name__ == "__main__":
    unittest.main()
