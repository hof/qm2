"""Lichess bot: which challenges it accepts, owner commands, and one game through a fake Lichess server."""
import asyncio
import http.client
import http.server
import json
from pathlib import Path
import queue
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import chess
import chess.engine

from maxima_runner import connectors, lichess
from maxima_runner.uci import open_engine
from test_fics_game import fake_engine
from test_uci import ponder_engine


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

    def test_declines_clocks_without_time_to_cover_the_lag(self):
        bot = lichess.lichess_bot(stub_api(), None, lambda line: None)
        offered = challenge("bullet")
        offered["timeControl"].update(limit=60, increment=0)
        self.assertEqual(bot.decline_reason(offered, {}), "tooFast")
        offered["timeControl"].update(limit=60, increment=1)
        self.assertIsNone(bot.decline_reason(offered, {}))

    def test_declines_clocks_over_the_maximum(self):
        bot = lichess.lichess_bot(stub_api(), None, lambda line: None, speeds=("rapid", "classical"), max_clock=30)
        offered = challenge("classical")
        offered["timeControl"].update(limit=1800, increment=0)
        self.assertIsNone(bot.decline_reason(offered, {}))
        offered["timeControl"].update(limit=1800, increment=1)
        self.assertEqual(bot.decline_reason(offered, {}), "tooSlow")


class command_tests(unittest.IsolatedAsyncioTestCase):
    def game(self):
        api, lines = stub_api(), []
        bot = lichess.lichess_bot(api, None, lines.append, owners=["SirHajewiet"])
        bot.id = "maximaengine"
        return api, lines, bot, lichess.lichess_game(bot, "g1")

    async def say(self, game, name, text, room="spectator"):
        await game.on_chat({"username": name, "text": text, "room": room})
        await asyncio.sleep(0)  # chat replies are posted in the background

    async def test_only_owners_steer_the_game(self):
        api, lines, bot, game = self.game()
        await self.say(game, "Someone", "!resign", "player")
        await self.say(game, "MaximaEngine", "!resign", "player")
        self.assertEqual(api.posts, [])
        await self.say(game, "sirhajewiet", "!resign")
        await self.say(game, "SirHajewiet", "!softquit")
        self.assertEqual([path for path, _ in api.posts], ["/api/bot/game/g1/resign", "/api/bot/game/g1/chat"])
        self.assertTrue(bot.stopping)
        self.assertTrue(any("refused" in line for line in lines))

    async def test_anyone_may_ask_but_eval_stays_with_spectators(self):
        api, lines, bot, game = self.game()
        game.last_eval = "e4 d20 +0.30"
        await self.say(game, "Someone", "!eval", "player")
        game.answered = 0
        await self.say(game, "Someone", "!eval")
        await self.say(game, "Someone", "!name")  # within the gap: ignored
        replies = [data["text"] for _, data in api.posts]
        self.assertEqual(replies, ["I share that with spectators only.", "e4 d20 +0.30"])

    async def test_book_details_are_separate_readable_chat_lines(self):
        api, lines, bot, game = self.game()
        game.last_book = ("5...Nc6 base book | 55% pick chance | support: base book, fire book"
                          " | learned: masters +23 =1 -1")
        await self.say(game, "Someone", "!book")
        self.assertEqual([data["text"] for _, data in api.posts],
                         ["5...Nc6 base book | 55% pick chance", "support: base book, fire book",
                          "learned: masters +23 =1 -1"])

    async def test_console_answers_every_command(self):
        api, lines, bot, game = self.game()
        self.assertFalse(await bot.console("help"))
        self.assertFalse(await bot.console("resign"))
        self.assertEqual(api.posts, [])
        bot.game = game
        self.assertFalse(await bot.console("!resign"))
        self.assertFalse(await bot.console("softquit"))
        self.assertFalse(await bot.console("bogus"))
        self.assertTrue(await bot.console("quit"))
        self.assertEqual([path for path, _ in api.posts], ["/api/bot/game/g1/resign"])
        self.assertTrue(bot.stopping)
        self.assertEqual(len(lines), 5)


class team_tests(unittest.IsolatedAsyncioTestCase):
    async def test_joins_a_team_with_its_case_and_message(self):
        api, lines = stub_api(), []
        bot = lichess.lichess_bot(api, None, lines.append)
        await bot.console("team Bot-Battle please let this bot play in your team")
        self.assertEqual(api.posts, [("/team/Bot-Battle/join", {"message": "please let this bot play in your team"})])
        await bot.console("team")
        self.assertEqual(len(api.posts), 1)

    async def test_reports_a_missing_scope(self):
        class scoped_api(stub_api):
            token = "secret"

            async def call(self, method, path, data=None):
                return {"secret": {"scopes": "bot:play"}}

        bot = lichess.lichess_bot(scoped_api(), None, lambda line: None)
        self.assertEqual(await bot.missing_scopes(("bot:play", "team:write")), ["team:write"])


class tournament_api(stub_api):
    def __init__(self, info=None, joins=True):
        super().__init__()
        self.info, self.joins = {} if info is None else info, joins

    async def call(self, method, path, data=None):
        return self.info

    async def post(self, path, data=None):
        await super().post(path, data)
        return self.joins or not path.endswith("/join")


class tournament_tests(unittest.IsolatedAsyncioTestCase):
    def bot(self, api=None, **settings):
        self.lines = []
        return lichess.lichess_bot(api or tournament_api(), None, self.lines.append, challenge_idle=1, **settings)

    def paths(self, bot):
        return [path for path, _ in bot.api.posts]

    async def test_declines_every_challenge_and_sends_none(self):
        bot = self.bot()
        await bot.join_tournament("AbCd")
        self.assertTrue(bot.in_tournament)
        self.assertEqual(bot.decline_reason(challenge(variant="chess960"), {}), "generic")
        await bot.on_challenge(challenge(), {})
        self.assertEqual(bot.api.posts, [("/api/challenge/c1/decline", {"reason": "generic"})])
        bot.set_challenging(True)
        bot.matchmaker.idle_since = bot.matchmaker.next_try = 0
        await bot.matchmaker.tick(bot)
        await bot.matchmaker.challenge(bot, "Other", "other", "blitz", 180, 0)
        await bot.matchmaker.rematch(bot, "Other", "other", "blitz", 180, 0, delay=0)
        self.assertEqual(len(bot.api.posts), 1)

    async def test_entering_cancels_our_challenge(self):
        bot = self.bot()
        bot.matchmaker.pending = ("c2", "other", "blitz", time.monotonic())
        await bot.join_tournament("AbCd")
        self.assertEqual(self.paths(bot), ["/api/challenge/c2/cancel"])
        self.assertIsNone(bot.matchmaker.pending)

    async def test_a_challenge_sent_while_joining_is_cancelled(self):
        bot = self.bot()
        await bot.join_tournament("AbCd")
        bot.matchmaker.pending = ("c2", "other", "blitz", time.monotonic())
        await bot.matchmaker.tick(bot)
        self.assertEqual(self.paths(bot), ["/api/challenge/c2/cancel"])
        self.assertEqual(bot.matchmaker.resting, {})

    async def test_joins_only_when_no_game_can_overlap_and_again_after_a_game(self):
        bot = self.bot()
        await bot.join_tournament("AbCd", "Secret")
        bot.game = object()
        await bot.tournament_tick()
        self.assertEqual(bot.api.posts, [])
        bot.game = None
        bot.accepted = ("c1", time.monotonic())
        await bot.tournament_tick()
        self.assertEqual(bot.api.posts, [])
        bot.accepted = (None, 0.0)
        bot.next_tournament_call = 0.0
        await bot.tournament_tick()
        self.assertEqual(bot.api.posts, [("/api/tournament/AbCd/join", {"pairMeAsap": "true", "password": "Secret"})])
        await bot.tournament_tick()  # within the gap
        self.assertEqual(len(bot.api.posts), 1)
        bot.ended()
        await bot.tournament_tick()
        self.assertEqual(len(bot.api.posts), 2)

    async def test_a_failed_first_join_ends_it_and_a_failed_repeat_does_not(self):
        bot = self.bot(tournament_api(joins=False))
        await bot.join_tournament("AbCd")
        await bot.tournament_tick()
        self.assertFalse(bot.in_tournament)
        bot = self.bot(tournament_api(joins=True))
        await bot.join_tournament("AbCd")
        await bot.tournament_tick()
        bot.api.joins, bot.next_tournament_call = False, 0.0
        await bot.tournament_tick()
        self.assertTrue(bot.in_tournament)

    async def test_ends_when_finished_past_its_schedule_closed_to_bots_or_a_variant(self):
        for info in ({"isFinished": True}, {"secondsToFinish": -lichess.tournament_grace}, {"botsAllowed": False},
                     {"variant": "atomic"}):
            bot = self.bot(tournament_api(info))
            await bot.join_tournament("AbCd")
            await bot.tournament_tick()
            self.assertFalse(bot.in_tournament, info)
            self.assertFalse(any(path.endswith("/join") for path in self.paths(bot)))
            self.assertIsNone(bot.decline_reason(challenge(), {"bot": True}))

    async def test_silence_from_lichess_cannot_hold_it_forever(self):
        api = tournament_api()
        api.info = None
        bot = self.bot(api)
        await bot.join_tournament("AbCd")
        bot.tournament_ends = 0
        await bot.tournament_tick()
        self.assertFalse(bot.in_tournament)

    async def test_stopping_withdraws_even_during_a_game(self):
        bot = self.bot(tournament="AbCd")
        await bot.tournament_tick()
        bot.game = object()
        bot.stopping = True
        await bot.tournament_tick()
        self.assertEqual(self.paths(bot)[-1], "/api/tournament/AbCd/withdraw")
        self.assertFalse(bot.in_tournament)

    async def test_console_keeps_the_ids_case(self):
        bot = self.bot()
        await bot.console("tournament AbCd")
        await bot.console("tournament Other")
        self.assertEqual(bot.tournament, "AbCd")
        await bot.console("leave")
        self.assertFalse(bot.in_tournament)
        self.assertEqual(bot.api.posts, [])  # never joined, so nothing to withdraw from


class stub_session:
    async def play(self, board, limit, **unused):
        move = next(iter(board.legal_moves))
        return chess.engine.PlayResult(move, None, {"score": chess.engine.PovScore(chess.engine.Cp(0), board.turn),
                                                    "depth": 20, "tbhits": 3})


class ponder_tests(unittest.IsolatedAsyncioTestCase):
    async def test_stream_reports_prediction_hit_and_miss(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / "engine.py", Path(directory) / "commands"
            script.write_text(ponder_engine)
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                api, lines = stub_api(), []
                bot = lichess.lichess_bot(api, None, lines.append, online_tablebase=False)
                bot.id, bot.session = "maximaengine", session
                game = lichess.lichess_game(bot, "g1")
                game.setup(game_full())
                state = game_full()["state"]
                await game.on_state(state)
                await game.searching
                self.assertNotIn("pondered", game.last_eval)
                await game.on_state(dict(state, moves="e2e4 g8h6"))
                await game.searching
                self.assertIn("pondered", game.last_eval)
                self.assertFalse(any("pondered" in line for line in lines))
                self.assertTrue(any(line.startswith("1. ..Nh6 |") and line.endswith(" | ponder hit") for line in lines))
                self.assertTrue(any("(+" in line for line in lines))
                self.assertEqual(bot.pv_plies, 2)
                replay = game.board.copy()
                our_move = chess.Move.from_uci(api.posts[-1][0].rsplit("/", 1)[-1])
                replay.push(our_move)
                replay.push(list(replay.legal_moves)[-1])
                await game.on_state(dict(state, moves=" ".join(move.uci() for move in replay.move_stack)))
                await game.searching
                self.assertNotIn("pondered", game.last_eval)
                self.assertEqual(log.read_text().splitlines().count("ponderhit"), 1)
                self.assertEqual(sum(line.endswith(" | ponder hit") for line in lines), 1)
                await session.stop_pondering()
                game.stop_waiting()


class draw_tests(unittest.IsolatedAsyncioTestCase):
    async def test_stalled_opening_requests_abort_once_and_cancels_on_reply(self):
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None, online_tablebase=False)
        bot.id = "maximaengine"
        game = lichess.lichess_game(bot, "g1")
        game.setup(game_full())
        for move in ("d2d4", "f7f6", "c2c4"):
            game.board.push_uci(move)
        game.abort_position = game.board.fen()
        with patch.object(lichess.move_choice, "opening_stall_wait", return_value=0.01):
            game.watch_opening()
            timer = game.waiting
            game.watch_opening()
            self.assertIs(game.waiting, timer)
            await timer
            game.watch_opening()
            self.assertEqual(sum(path.endswith("/abort") for path, _ in api.posts), 1)
            game.stop_waiting()
            game.watch_opening()
            timer = game.waiting
            game.board.push_uci("e7e5")
            game.watch_opening()
            await asyncio.sleep(0.02)
            self.assertTrue(timer.cancelled())
            self.assertEqual(sum(path.endswith("/abort") for path, _ in api.posts), 1)

    async def test_spectators_get_the_mood_and_once_the_book_odds(self):
        class book:
            mood, why = "kings_indian", None
            results = ({}, {}, {})
            mood, choice_results = "kings_indian", {}
            names = {"opponent": "Someone", "group": "engines", "global": "global"}

            def choose(self, board):
                move = chess.Move.from_uci("e2e4")
                self.why = ("book", 0.5, {"book": 0.3, "fire": 0.2}, "book")
                return move, "book 50%"

        class learner:
            records, server = [], "lichess.org"

            def for_game(self, *_):
                return book()

            def expect(self, *_):
                pass

        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None, online_tablebase=False)
        bot.id, bot.session, bot.learner = "maximaengine", stub_session(), learner()
        game = lichess.lichess_game(bot, "g1")
        full = game_full()
        game.setup(full)
        await game.on_state(full["state"])
        await game.searching
        await asyncio.sleep(0.01)
        chats = [data["text"] for path, data in api.posts if path.endswith("/chat") and data["room"] == "spectator"]
        self.assertEqual(chats, ["Hi! I'm Maxima, running on MaxDrone. Mood: Kings Indian. Type !help for available commands.",
                                 "1. e4 Book: Base 60%, Fire 40% (pick 50%)."])

    async def test_draw_offer_is_accepted_in_a_tablebase_draw(self):
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None, online_tablebase=False)
        bot.id, bot.session = "maximaengine", stub_session()
        game = lichess.lichess_game(bot, "g1")
        full = game_full()
        full["initialFen"] = "8/8/8/4k3/8/8/2r5/R3K3 w - - 0 1"
        game.setup(full)
        await game.on_state(dict(full["state"], bdraw=True))
        await game.searching
        actions = [path for path, _ in api.posts if not path.endswith("/chat")]
        greetings = [data["text"] for path, data in api.posts if path.endswith("/chat")]
        self.assertEqual(len(set(greetings)), len(greetings))  # Lichess drops a repeated line
        self.assertTrue(greetings and not any("/" in text for text in greetings))  # Lichess blanks lines with links
        self.assertEqual(actions[0], "/api/bot/game/g1/draw/yes")
        self.assertTrue(actions[1].startswith("/api/bot/game/g1/move/"))


class fake_lichess(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), handler)
        self.streams = {"/api/stream/event": queue.Queue(), "/api/bot/game/stream/g1": queue.Queue()}
        self.posts = []
        self.tournament = {"secondsToFinish": 600}

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
        if self.path.startswith("/api/tournament/"):
            self.reply(self.server.tournament)
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
        self.assertEqual(len(list((Path(directory.name) / "games").rglob("*.pgn"))), 1)
        self.assertFalse(list((Path(directory.name) / 'games').glob('*.pgn')))
        self.assertEqual(len(list((Path(directory.name) / 'games' / 'lichess').glob('*.pgn'))), 1)


if __name__ == "__main__":
    unittest.main()


class tournament_stream_tests(unittest.IsolatedAsyncioTestCase):
    async def test_a_tournament_over_http_from_join_to_withdraw_free_end(self):
        server = fake_lichess()
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        self.addCleanup(server.close_streams)
        api = lichess.lichess_api("token", f"http://127.0.0.1:{server.server_address[1]}", lambda line: None)
        bot = lichess.lichess_bot(api, None, lambda line: None, tournament="AbCd")
        bot.id = "maximaengine"
        commands = asyncio.Queue()
        running = asyncio.ensure_future(bot.run(commands))

        async def until(condition):
            for _ in range(200):
                if condition():
                    return
                await asyncio.sleep(0.05)
            self.fail("timed out")

        await until(lambda: ("/api/tournament/AbCd/join", "pairMeAsap=true") in server.posts)
        server.streams["/api/stream/event"].put({"type": "challenge", "challenge": challenge(), "compat": {}})
        await until(lambda: ("/api/challenge/c1/decline", "reason=generic") in server.posts)
        server.tournament, bot.next_tournament_call = {"isFinished": True}, 0.0
        await until(lambda: not bot.in_tournament)
        self.assertIsNone(bot.decline_reason(challenge(), {"bot": True}))
        await commands.put("quit")
        await asyncio.wait_for(running, 5)
        self.assertFalse(any(path.endswith("/withdraw") for path, _ in server.posts))


class chat_line_tests(unittest.TestCase):
    def test_spectators_see_the_book_layers_and_what_we_learned(self):
        board = chess.Board()
        board.push_uci("e2e4")
        move = chess.Move.from_uci("e7e5")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        opening = lichess.learning.opening_book([], opponent="Ferrite", group="engines", mood="traxler")
        opening.why = ("book", 0.8, {"book": 0.5, "draw": 0.28, "global": 0.02}, "book")
        self.assertEqual(lichess.book_tell(board, move, "1...e5", opening), "1...e5 Book: Base 62%, Draw 35% (pick 80%).")
        opening.why = ("book", 1.0, {"book": 1.0}, "book")
        opening.results[0][(key, "e7e5")] = [9, 1, 3]
        opening.results[1][(key, "e7e5")] = [0, 2, 1]
        opening.results[2][(key, "e7e5")] = [0, 1, 0]
        self.assertEqual(lichess.book_tell(board, move, "1...e5", opening),
                         "1...e5 Book: Base 100%. Learn: +9 =1 -3, bots +0 =2 -1, vs Ferrite +0 =1 -0.")


class matchmaker_tests(unittest.IsolatedAsyncioTestCase):
    async def test_challenges_a_fitting_bot_after_idle_time_and_rests_after_a_decline(self):
        bots = [{"id": "near", "username": "Near", "perfs": {"blitz": {"rating": 2900}}},
                {"id": "far", "username": "Far", "perfs": {"blitz": {"rating": 2000}}},
                {"id": "last", "username": "Last", "perfs": {"blitz": {"rating": 2950}}}]

        class api:
            calls = []

            async def call(self, method, path, data=None):
                self.calls.append((method, path, data))
                return {"perfs": {"blitz": {"rating": 2924}}} if path == "/api/account" else {"id": "c1"}

            async def stream(self, path):
                for item in bots:
                    yield item

        bot = lichess.lichess_bot(api(), None, lambda line: None, speeds=("blitz",), challenge_idle=30)
        bot.id, maker = "maximaengine", bot.matchmaker
        maker.busy("Last")
        await maker.tick(bot)
        self.assertEqual(api.calls, [])  # not idle long enough
        maker.idle_since = maker.next_try = time.monotonic() - 31 * 60
        await maker.tick(bot)
        method, path, data = api.calls[-1]
        self.assertEqual((method, path, data["rated"]), ("POST", "/api/challenge/Near", "true"))
        self.assertIn((data["clock.limit"], data["clock.increment"]), lichess.challenge_clocks["blitz"])
        maker.declined({"id": "c1", "declineReasonKey": "later"})
        self.assertIsNone(maker.pending)
        maker.next_try = 0
        calls = len(api.calls)
        await maker.tick(bot)
        self.assertFalse(any(path.startswith("/api/challenge/") for _, path, _ in api.calls[calls:]))


    async def test_asks_a_bot_that_wants_casual_games_for_casual_games(self):
        bots = [{"id": "casual", "username": "Casual", "perfs": {"blitz": {"rating": 2900}}}]
        maker = lichess.matchmaker(30, 300, ("blitz",), lambda line: None)
        self.assertTrue(maker.pick(bots, {"blitz": 2900}, "us", 0)[-1])
        maker.pending = ("c1", "casual", "blitz", 0)
        maker.declined({"id": "c1", "declineReasonKey": "casual"})
        self.assertFalse(maker.pick(bots, {"blitz": 2900}, "us", 0)[-1])
        maker.pending = ("c2", "casual", "blitz", 0)
        maker.declined({"id": "c2", "declineReasonKey": "casual"})
        self.assertIsNone(maker.pick(bots, {"blitz": 2900}, "us", time.monotonic()))

    async def test_a_game_against_someone_else_cancels_our_open_challenge(self):
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None)
        bot.id = "maximaengine"
        for opponent, cancels in (("other", 1), ("arasanx", 0)):
            api.posts.clear()
            bot.game = None
            bot.matchmaker.pending = ("c1", "arasanx", "blitz", time.monotonic())
            with patch.object(lichess.lichess_game, "start"):
                await bot.on_event({"type": "gameStart", "game": {"gameId": "g1", "opponent": {"id": opponent}}})
            self.assertEqual(len([path for path, _ in api.posts if path == "/api/challenge/c1/cancel"]), cancels)
            self.assertIsNone(bot.matchmaker.pending)

    def test_never_picks_a_bot_with_a_skipped_word_in_its_name(self):
        maker = lichess.matchmaker(30, 300, ("blitz",), lambda line: None, skip=("Odds",))
        bots = [{"id": "leelarookodds", "perfs": {"blitz": {"rating": 2800}}}]
        self.assertIsNone(maker.pick(bots, {"blitz": 2800}, "me", 0))

    async def test_a_decline_or_no_answer_rests_the_bot_at_every_speed(self):
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None, speeds=("blitz", "rapid"), challenge_idle=30)
        maker = bot.matchmaker
        maker.pending = ("c1", "leela", "blitz", 0)
        maker.declined({"id": "c1", "declineReasonKey": "later"})
        self.assertEqual(set(maker.resting), {("leela", "blitz"), ("leela", "rapid")})
        maker.pending = ("c2", "weiawaga", "blitz", 0)
        await maker.tick(bot)
        self.assertIn(("weiawaga", "rapid"), maker.resting)

    async def test_leaves_a_limited_bot_alone_at_every_speed_for_the_wait_lichess_names(self):
        class api:
            limit_seconds = 1000

            async def call(self, method, path, data=None):
                return None

        bot = lichess.lichess_bot(api(), None, lambda line: None, speeds=("blitz", "rapid"), challenge_idle=30)
        await bot.matchmaker.challenge(bot, "Odonata", "odonata", "blitz", 180, 0)
        self.assertIsNone(bot.matchmaker.pending)
        now = time.monotonic()
        for speed in ("blitz", "rapid"):
            self.assertGreater(bot.matchmaker.resting[("odonata", speed)], now + 900)

    async def test_reconnects_once_when_the_server_dropped_the_connection(self):
        class dropped:
            def request(self, *_):
                raise http.client.RemoteDisconnected("gone")

            def close(self):
                pass

        class working(dropped):
            def request(self, *_):
                pass

            def getresponse(self):
                class response:
                    status = 200

                    def read(self):
                        return b"{}"

                return response()

        api = lichess.lichess_api("token", on_line=lambda line: None)
        connections = [dropped(), working()]
        api.connection = lambda: connections.pop(0)
        self.assertEqual(api.exchange("POST", "/api/bot/game/g1/move/e2e4"), (200, b"{}"))

    async def test_startup_waits_out_an_outage_but_not_a_bad_token(self):
        async def run(*statuses):
            api = lichess.lichess_api("token", on_line=lambda line: None)
            answers = iter(statuses)

            async def call(method, path, data=None):
                api.status = next(answers)
                return {"id": "m", "username": "M", "title": "BOT"} if api.status == 200 else None

            api.call = call
            bot = lichess.lichess_bot(api, None, lambda line: None)
            bot.start_engine = bot.start_events = None
            with unittest.mock.patch("asyncio.sleep", unittest.mock.AsyncMock()):
                await bot.start()

        with self.assertRaisesRegex(RuntimeError, "rejected"):
            await run(502, 401)
        with self.assertRaisesRegex(RuntimeError, "not answering"):
            await run(*[502] * lichess.login_attempts)

    async def test_reads_the_wait_from_a_refused_call(self):
        body = json.dumps({"error": "x" * 300, "ratelimit": {"key": "bot.vsBot.day", "seconds": 24430}}).encode()

        api = lichess.lichess_api("token", on_line=lambda line: None)
        api.exchange = lambda *_: (400, body)
        self.assertIsNone(await api.call("POST", "/api/challenge/Odonata"))
        self.assertEqual(api.limit_seconds, 24430)


class command_line_tests(unittest.TestCase):
    def test_book_display_labels_the_moves_chance_and_learned_results(self):
        opening = lichess.learning.opening_book([], group="engines", opponent="Ferrite")
        opening.choice_results = {"global": (23, 1, 1), "group": (10, 0, 1)}
        self.assertEqual(lichess.book_whisper("5...Nc6", ("book", 0.5, {"book": 0.475, "fire": 0.025}, "book"), opening),
                         "5...Nc6 base book | 50% pick chance | support: base book, fire book | learned: learn +23 =1 -1; engines +10 =0 -1")
        opening.choice_results = {}
        self.assertEqual(lichess.book_whisper("1...e5", ("book", 0.43, {"fire": 0.43}, "fire"), opening, compact=True),
                         "1...e5 | Book (43%): Fire")


class rematch_tests(unittest.IsolatedAsyncioTestCase):
    async def test_offers_one_rematch_to_a_bot(self):
        class api:
            calls = []

            sent = []

            async def call(self, method, path, data=None):
                self.calls.append(path)
                self.sent.append(data)
                return {"id": "r1"}

        bot = lichess.lichess_bot(api(), None, lambda line: None, challenge_idle=30)
        bot.matchmaker.busy("Ferrite")
        await bot.matchmaker.rematch(bot, "Ferrite", "ferrite", "blitz", 180, 2, rated=False, delay=0)
        self.assertEqual(api.calls, ["/api/challenge/Ferrite"])
        self.assertEqual(api.sent[-1]["rated"], "false")  # the same terms as the game
        bot.matchmaker.busy("Ferrite")  # the rematch was played
        await bot.matchmaker.rematch(bot, "Ferrite", "ferrite", "blitz", 180, 2, delay=0)
        self.assertEqual(len(api.calls), 1)
