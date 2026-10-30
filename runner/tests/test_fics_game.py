"""Style12 parsing against a recorded FICS game, move history, and the engine player."""
import asyncio
from datetime import datetime, timezone
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import chess
import chess.engine
import chess.pgn
import chess.polyglot

from maxima_runner import connectors, fics, fics_game, learning, move_choice, online_tablebase
from maxima_runner.uci import open_engine

observed = (Path(__file__).parent / "data" / "fics_observed_game.txt").read_text().splitlines()
start = ("<12> rnbqkbnr pppppppp -------- -------- -------- -------- PPPPPPPP RNBQKBNR W -1 1 1 1 1 0 17 "
         "GuestHJGC guestNinja 1 3 0 39 39 180000 180000 1 none (0:00.000) none 0 0 0")
after_e4 = ("<12> rnbqkbnr pppppppp -------- -------- ----P--- -------- PPPP-PPP RNBQKBNR B 4 1 1 1 1 0 17 "
            "GuestHJGC guestNinja -1 3 0 39 39 180000 180000 1 P/e2-e4 (0:00.000) e4 0 0 0")


class style12_tests(unittest.TestCase):
    def test_parses_recorded_game(self):
        boards = [fics_game.style12.parse(line) for line in observed if line.startswith("<12>")]
        self.assertEqual(len(boards), 16)
        first = boards[0]
        self.assertEqual((first.game, first.white, first.black, first.relation), (15, "GuestMCZL", "GuestVHRM", 0))
        self.assertEqual((first.initial_seconds, first.increment_seconds), (300, 0))
        self.assertEqual((first.white_ms, first.black_ms), (258712, 268143))
        self.assertEqual(first.castling, [True, False, True, True])
        self.assertEqual(first.fen(), "r3k2r/1p2bppp/p1np4/q2Qp3/4P3/8/PPPNBPPP/3RK2R w Kkq - 5 14")

    def test_start_and_en_passant_positions(self):
        self.assertEqual(fics_game.style12.parse(start).fen(), chess.STARTING_FEN)
        position = fics_game.style12.parse(after_e4)
        self.assertEqual(position.board(), chess.Board("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"))
        self.assertEqual(position.relation, fics_game.playing_their_move)

    def test_rejects_malformed_and_invalid_boards(self):
        self.assertIsNone(fics_game.style12.parse("<12> nonsense"))
        self.assertIsNone(fics_game.style12.parse(start.replace("GuestHJGC", "Guest-1")))
        no_kings = start.replace("RNBQKBNR W", "RNBQ-BNR W")
        with self.assertRaises(ValueError):
            fics_game.style12.parse(no_kings).board()

    def test_game_end_line(self):
        end = fics_game.game_end_pattern.match(observed[-1])
        self.assertEqual((end["game"], end["reason"], end["result"]), ("15", "GuestVHRM resigns", "1-0"))


class history_tests(unittest.TestCase):
    def test_follows_recorded_moves_without_resync(self):
        boards = [fics_game.style12.parse(line) for line in observed if line.startswith("<12>")]
        history = fics_game.game_history(boards[0])
        for number, position in enumerate(boards[1:], 1):
            self.assertTrue(history.update(position))
            self.assertEqual(len(history.board.move_stack), number)
            self.assertTrue(fics_game.same_position(history.board, position))
        self.assertIn(chess.Move.from_uci("e8c8"), history.board.move_stack)
        self.assertFalse(history.update(boards[-1]))

    def test_resyncs_on_takeback(self):
        history = fics_game.game_history(fics_game.style12.parse(start))
        history.update(fics_game.style12.parse(after_e4))
        self.assertEqual(len(history.board.move_stack), 1)
        self.assertTrue(history.update(fics_game.style12.parse(start)))
        self.assertEqual(history.board, chess.Board())
        self.assertEqual(history.board.move_stack, [])

    def test_takeback_keeps_earlier_moves(self):
        history = fics_game.game_history(fics_game.style12.parse(start))
        history.update(fics_game.style12.parse(after_e4))
        after_e5 = after_e4.replace("pppppppp -------- --------", "pppp-ppp -------- ----p---").replace(
            " B 4 ", " W 4 ").replace("-1 3 0", "1 3 0").replace("P/e2-e4 (0:00.000) e4", "P/e7-e5 (0:02.500) e5")
        self.assertEqual(history.update(fics_game.style12.parse(after_e5)), "move")
        self.assertEqual(history.update(fics_game.style12.parse(after_e4)), "takeback")
        self.assertEqual([move.uci() for move in history.board.move_stack], ["e2e4"])

    def style12_of(self, board, san):
        rows = " ".join(str(board).replace(" ", "").replace(".", "-").split("\n"))
        turn = "W" if board.turn else "B"
        return fics_game.style12.parse(f"<12> {rows} {turn} -1 1 1 1 1 0 1 A B 1 3 0 39 39 180000 180000 "
                                       f"{board.fullmove_number} N/g1-f3 (0:00.000) {san}")

    def test_repetition_is_not_a_takeback(self):
        server = chess.Board()
        history = fics_game.game_history(self.style12_of(server, "none"))
        for san in "Nf3 Nf6 Ng1 Ng8 Nf3 Nf6 Ng1 Ng8".split():
            server.push_san(san)
            self.assertEqual(history.update(self.style12_of(server, san)), "move")
        self.assertEqual(history.board.move_stack, server.move_stack)
        self.assertTrue(history.board.is_repetition(3))

    def test_takeback_of_a_repeated_position_finds_its_ply(self):
        server = chess.Board()
        history = fics_game.game_history(self.style12_of(server, "none"))
        for san in "Nf3 Nf6 Ng1 Ng8 Nf3 Nf6".split():
            server.push_san(san)
            history.update(self.style12_of(server, san))
        server.pop()
        self.assertEqual(history.update(self.style12_of(server, "none")), "takeback")
        self.assertEqual(history.board.move_stack, server.move_stack)

    def test_ics_moves(self):
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("e2e4")), "e2e4")
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("e1g1")), "e1g1")
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("e7e8q")), "e7e8=Q")
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("a2a1n")), "a2a1=N")


class record_tests(unittest.TestCase):
    def recorded_game(self):
        boards = [fics_game.style12.parse(line) for line in observed if line.startswith("<12>")]
        history = fics_game.game_history(boards[0])
        record = fics_game.game_record({"White": "GuestMCZL", "Black": "GuestVHRM", "WhiteEngine": "Maxima 2.0.0"},
                                       datetime(2026, 10, 21, 20, 5, 7, tzinfo=timezone.utc))
        for position in boards[1:]:
            before = history.board.copy()
            self.assertEqual(history.update(position), "move")
            mover_ms = position.black_ms if position.turn == chess.WHITE else position.white_ms
            record.on_move(history.board, mover_ms / 1000, position.elapsed)
            if before.ply() == boards[0].board().ply():
                info = {"depth": 16, "score": chess.engine.PovScore(chess.engine.Cp(42), chess.WHITE),
                        "pv": [history.board.peek(), chess.Move.from_uci("c6a5")], "nodes": 123456}
                record.on_search(before, history.board.peek(), info)
        return record.game(history.board, "1-0", "GuestVHRM resigns")

    def test_pgn_has_clocks_move_times_search_and_ending(self):
        game = self.recorded_game()
        text = str(game)
        self.assertEqual(game.headers["FEN"], "r3k2r/1p2bppp/p1np4/q2Qp3/4P3/8/PPPNBPPP/3RK2R w Kkq - 5 14")
        self.assertEqual((game.headers["UTCDate"], game.headers["UTCTime"]), ("2026.10.21", "20:05:07"))
        self.assertEqual((game.headers["Result"], game.headers["Termination"]), ("1-0", "normal"))
        first = game.next()
        self.assertEqual(first.eval().white().score(), 42)
        self.assertEqual(first.eval_depth(), 16)
        self.assertIn("[%pv Qxa5 Nxa5] [%nodes 123456]", first.comment)
        self.assertEqual(first.clock(), 250.867)
        self.assertIsNotNone(first.next().emt())
        self.assertTrue(text.rstrip().endswith("GuestVHRM resigns } 1-0"))
        self.assertEqual(len(list(game.mainline_moves())), 15)

    def test_saved_games_are_atomic_and_deduplicated(self):
        game = self.recorded_game()
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / "fics"
            path = fics_game.save_game(game, folder, datetime(2026, 10, 21, tzinfo=timezone.utc))
            fics_game.save_game(game, folder, datetime(2026, 10, 22, tzinfo=timezone.utc))
            self.assertEqual(path.stem, game.headers["LearningId"])
            with path.open() as stream:
                games = [chess.pgn.read_game(stream)]
                self.assertIsNone(chess.pgn.read_game(stream))
            self.assertEqual([str(item) for item in games], [str(game)])


fake_engine = '''
import sys, time
mode = sys.argv[1]
for line in sys.stdin:
    command = line.strip()
    if command == "uci":
        print("id name fics fixture")
        print("option name DrawContempt type spin default -10 min -100 max 100")
        print("uciok", flush=True)
    elif command == "isready":
        print("readyok", flush=True)
    elif command.startswith("position") or command.startswith("setoption"):
        with open(sys.argv[2], "a") as log:
            log.write(command + "\\n")
    elif command.startswith("go ") and mode == "play":
        time.sleep(0.05)
        print("info depth 3 score cp 25 nodes 100 pv e2e4")
        print("bestmove e2e4", flush=True)
    elif command == "stop" and mode == "think":
        print("bestmove e1e2", flush=True)
    elif command == "quit":
        break
'''


class fake_connection:
    def __init__(self):
        self.sent = []

    async def send(self, line, secret=False):
        self.sent.append(line)


class player_tests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.script = Path(self.directory.name) / "engine.py"
        self.script.write_text(fake_engine)
        self.positions = Path(self.directory.name) / "positions.log"
        self.lines = []

    async def player(self, mode, margin=5, **options):
        player = fics.engine_player(
            lambda: open_engine([sys.executable, str(self.script), mode, str(self.positions)],
                                self.directory.name, timeout=5),
            self.lines.append, margin=margin, **options)
        await player.start()
        self.addAsyncCleanup(player.close)
        player.connection = fake_connection()
        return player

    async def settle(self, player):
        for _ in range(100):
            if player.task is None or player.task.done():
                return
            await asyncio.sleep(0.05)
        self.fail("search did not finish")

    async def test_opponent_line_marks_actual_ponder_hit(self):
        from test_uci import ponder_engine
        self.script.write_text(ponder_engine.replace(
            "print('option name Ponder type check default false')",
            "print('option name Ponder type check default false')\n"
            "        print('option name OwnBook type check default true')"))
        player = fics.engine_player(
            lambda: open_engine([sys.executable, str(self.script), str(self.positions)],
                                self.directory.name, timeout=5),
            self.lines.append, online_tablebase=False)
        await player.start()
        await player.session.configure({'OwnBook': 'false'})
        self.addAsyncCleanup(player.close)
        player.connection = fake_connection()
        await player.handle(start)
        await self.settle(player)
        await player.handle(after_e4)
        reply = after_e4.replace("rnbqkbnr", "rnbqkb-r").replace(
            "pppppppp -------- --------", "pppppppp -------n --------").replace(
            " B 4 ", " W -1 ").replace("1 1 1 1 0 17", "1 1 1 1 1 17").replace(
            "-1 3 0", "1 3 0").replace("180000 180000 1", "180000 179000 2").replace(
            "P/e2-e4 (0:00.000) e4", "N/g8-h6 (0:01.000) Nh6")
        await player.handle(reply)
        await self.settle(player)
        opponents = [line for line in self.lines if line.startswith("1. ..Nh6 |")]
        self.assertEqual(len(opponents), 1)
        self.assertTrue(opponents[0].endswith(" | ponder hit"))
        self.assertTrue(any("(+" in line for line in self.lines))
        self.assertEqual(self.positions.read_text().splitlines().count("ponderhit"), 1)

    async def test_reseeks_only_after_idle_minutes(self):
        player = await self.player("play", policy=fics.play_policy(seeks=["3 0"], reseek_minutes=10))
        await player.reseek_when_idle()
        self.assertEqual(player.connection.sent, [])
        player.seeked -= 600
        await player.reseek_when_idle()
        self.assertEqual(player.connection.sent, ["unseek", "seek 3 0 u"])

    async def test_plays_own_moves_only_once(self):
        player = await self.player("play")
        await player.handle(after_e4)
        self.assertIsNone(player.task)
        await player.handle(start.replace(" 17 GuestHJGC", " 18 GuestHJGC"))
        await player.handle(start.replace(" 17 GuestHJGC", " 18 GuestHJGC"))
        await self.settle(player)
        self.assertEqual(player.connection.sent, ["e2e4"])
        self.assertTrue(any(line.startswith("1. e4 d3 +0.25 | ") and line.endswith(")") for line in self.lines))
        self.assertEqual(self.positions.read_text().split("\n")[0], "position startpos")
        self.assertNotIn("DrawContempt", self.positions.read_text())

    async def test_whispers_the_expected_line_when_asked(self):
        player = await self.player("play", policy=fics.play_policy(whisper=True))
        await player.handle(start)
        await self.settle(player)
        self.assertEqual(player.connection.sent[0], "e2e4")
        self.assertRegex(player.connection.sent[1], r"^whisper d3 \+0\.25 \d+\.\ds 1\. e4$")

    async def test_muted_account_stops_whispering(self):
        player = await self.player("play", policy=fics.play_policy(whisper=True))
        await player.handle("You are muted.")
        await player.handle(start)
        await self.settle(player)
        self.assertEqual(player.connection.sent, ["e2e4"])
        self.assertTrue(any("whispers off" in line for line in self.lines))

    async def test_follower_tells_are_paced_and_merged(self):
        player = await self.player("play")
        player.followers = ["hermen", "hof"]
        with patch.object(fics, "tell_gap", 0.05):
            for text in ("e4 book: base (base 100%)", "learned: 3 moves"):
                player.tell_followers(text)
            await asyncio.sleep(0)
            await player.handle("hof is not logged in.")
            await player.teller
        self.assertEqual(player.connection.sent, ["tell hermen e4 book: base (base 100%) | learned: 3 moves"])
        self.assertEqual(player.followers, ["hermen"])

    async def test_remote_control_needs_the_key(self):
        player = await self.player("play", policy=fics.play_policy(follow_key="s3cret"))
        player.name = "GuestCPKH"
        await player.handle("hermen tells you: wrong follow")
        await player.handle("hermen tells you: s3cret rm -rf")
        self.assertEqual(player.connection.sent, [])
        await player.handle("hermen(C) tells you: s3cret follow")
        self.assertEqual(player.followers, ["hermen"])
        await player.handle("hof tells you: s3cret !resign")
        self.assertEqual(player.connection.sent[-1], "tell hof No game to resign.")
        await player.handle(start)
        await self.settle(player)
        self.assertRegex(player.connection.sent[-1], r"^tell hermen d3 \+0\.25 \d+\.\ds 1\. e4$")
        await player.handle("hof tells you: s3cret !abort")
        self.assertEqual(player.connection.sent[-1], "abort")
        await player.handle("hermen tells you: unfollow")
        self.assertEqual(player.followers, [])
        await player.handle("hof tells you: s3cret softquit")
        self.assertTrue(player.stopping)
        self.assertFalse(player.restart)
        await player.handle("hof tells you: s3cret softrestart")
        self.assertTrue(player.restart)
        self.assertEqual(player.connection.sent[-1], "tell hof OK, I restart after this game.")

    def test_learning_report_says_what_was_kept(self):
        self.assertEqual(fics.learning_report(None, None, []),
                         ["not learned: under ten moves or too few searches"])
        game = chess.pgn.Game.from_board(chess.Board())
        for uci in ("e2e4", "e7e5", "f2f4"):
            game.end().add_main_variation(chess.Move.from_uci(uci))
        record = {"opponent": "John", "values": [1, 2, 3], "score": 0.0}
        lines = fics.learning_report(game, record, [record, dict(record, score=1.0)])
        self.assertEqual(lines, ["learned: 3 moves | vs John +1 =0 -1",
                                 "revenge filed: 1. e4 e5 2. f4"])

    def test_compact_search_limits_replies_and_preserves_board(self):
        board = chess.Board()
        fen = board.fen()
        moves = [chess.Move.from_uci(uci) for uci in ("e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6")]
        info = {"depth": 19, "score": chess.engine.PovScore(chess.engine.Cp(10), chess.WHITE),
                "nps": 543000, "pv": moves}
        prefix = "1. e4 d19 +0.10 | 543kN/s | 3.2s (4:56)"
        self.assertEqual(fics.console_search(board, moves[0], info, 3.2, 300),
                         prefix + " | PV: 1. ..e5 2. Nf3")
        self.assertEqual(fics.console_search(board, moves[0], info, 3.2, 300, 2),
                         prefix + " | PV: 1. ..e5 2. Nf3")
        self.assertEqual(fics.console_search(board, moves[0], info, 3.2, 300, 0), prefix)
        self.assertTrue(fics.console_search(board, moves[0], info, 3.2, 300, 6).endswith("3. Bb5 a6"))
        info["pv"] = moves[:2] + [moves[0]]
        self.assertEqual(fics.console_search(board, moves[0], info, 3.2, 300), prefix + " | PV: 1. ..e5")
        self.assertEqual(board.fen(), fen)

    def test_compact_console_matches_requested_layout(self):
        board = chess.Board()
        for san in ("e4", "e5", "Nf3", "Nc6", "Nc3"):
            board.push_san(san)
        move = board.parse_san("a6")
        self.assertEqual(fics.numbered_move(board, move) + " | " + fics.console_time(3.2, 294),
                         "3. ..a6 | 3.2s (4:54)")
        board.push(move)
        move = board.parse_san("Bc4")
        replay = board.copy()
        replay.push(move)
        info = {"depth": 23, "score": chess.engine.PovScore(chess.engine.Cp(483), chess.WHITE),
                "nps": 687000, "pv": [move, replay.parse_san("Qe7")]}
        self.assertEqual(fics.console_search(board, move, info, 4.6, 300),
                         "4. Bc4 d23 +4.83 | 687kN/s | 4.6s (4:55) | PV: 4. ..Qe7")
        self.assertIn(" | TB 12313 | ", fics.console_search(board, move, dict(info, tbhits=12313), 4.6, 300))

    def test_compact_console_time_with_missing_clocks(self):
        self.assertEqual(fics.console_time(3.2, 294), "3.2s (4:54)")
        self.assertEqual(fics.console_time(None, 294), "(4:54)")
        self.assertEqual(fics.console_time(None, None), "")
        self.assertEqual(fics.console_time(3.2, -1), "3.2s (0:00)")

    def test_compact_search_shows_only_hit_ponder_time(self):
        board = chess.Board()
        move = chess.Move.from_uci("e2e4")
        info = {"_ponder_hit": True, "_ponder_seconds": 5.5}
        self.assertIn(" | 2.3s (+5.5s) (4:57)", fics.console_search(board, move, info, 2.3, 300))
        info["_ponder_hit"] = False
        self.assertNotIn("(+", fics.console_search(board, move, info, 2.3, 300))
        self.assertEqual(fics.play_policy().pv_plies, 2)
        self.assertEqual(fics.default_settings["pv_plies"], 2)

    def test_whispers_explain_book_choices_simply(self):
        opening = learning.opening_book([], opponent="John", group="humans", mood="parham")
        parts = {"book": 0.2, "fire": 0.1, "mood": 0.15, "opponent": 0.05, "global": 0.001}
        self.assertEqual(fics.book_whisper("Qh5", ("book", 0.5, parts, "mood"), opening),
                         "Qh5 parham mood | 50% pick chance | support: base book, fire book, parham mood, vs John | learned: no results yet")
        self.assertEqual(fics.book_whisper("d3", ("book", 1.0, {"book": 1.0}, "book"), opening),
                         "d3 base book | 100% pick chance | learned: no results yet")
        self.assertTrue(fics.book_whisper("f4", ("revenge", 0.5, parts, "opponent"), opening).startswith(
            "f4 revenge vs John | 50% pick chance"))
        opening.choice_results = {"group": (23, 1, 1), "opponent": (2, 0, 1)}
        opening.names["group"] = "masters"
        self.assertEqual(fics.book_whisper("1...c6", ("book", 0.55, parts, "book"), opening, compact=True),
                         "1...c6 | BaseBook (pick 55%)")
        self.assertEqual([fics.speed(nps) for nps in (850, 512_345, 1_234_567)], ["850nps", "512Knps", "1.2Mnps"])
        info = {"depth": 18, "score": chess.engine.PovScore(chess.engine.Mate(5), chess.BLACK), "tbhits": 3}
        self.assertEqual(fics.search_whisper(info, chess.BLACK, "21...Qd5+ 22. Qd6", "out of book"),
                         "d18 #5 TB 21...Qd5+ 22. Qd6 (out of book)")

    def test_console_learning_only_shows_played_move_results(self):
        board = chess.Board()
        fen = board.fen()
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        opening = learning.opening_book([])
        opening.results[0].update({(key, "d2d4"): [35, 5, 0], (key, "e2e4"): [80, 0, 20],
                                   (key, "f2f4"): [1, 0, 0], (key, "g8f6"): [500, 0, 0],
                                   ("another_position", "c2c4"): [500, 0, 0], (key, "b2b4"): [0, 0, 0]})
        move = chess.Move.from_uci("d2d4")
        expected = "learn +35 =5 -0"
        self.assertEqual(fics.console_learning(board, move, opening), expected)
        self.assertIn(f" | {expected} | 0.3s (", fics.console_search(board, move, {}, 0.3, 180, opening=opening))
        opening.results[0][(key, "c2c4")] = [15, 0, 5]
        self.assertEqual(fics.console_learning(board, chess.Move.from_uci("f2f4"), opening),
                         "learn +1 =0 -0")
        self.assertEqual(fics.console_learning(board, chess.Move.from_uci("g1f3"), opening),
                         "")
        self.assertEqual(fics.console_learning(board, move, None), "")
        self.assertEqual(fics.console_learning(board, move, learning.opening_book([])), "")
        self.assertEqual(board.fen(), fen)

    def test_console_learning_includes_available_played_move_group_and_opponent(self):
        board = chess.Board()
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        move = chess.Move.from_uci("d2d4")
        opening = learning.opening_book([], group="engines", opponent="Kapablanca")
        opening.results[0][(key, "d2d4")] = [5, 80, 5]
        opening.results[0][(key, "e2e4")] = [20, 0, 0]
        opening.results[1][(key, "d2d4")] = [5, 77, 5]
        opening.results[2][(key, "d2d4")] = [0, 3, 0]
        # Alternatives' personal records must not be attributed to the played move.
        opening.results[2][(key, "e2e4")] = [20, 0, 0]
        base = "learn +5 =80 -5"
        for group, label in (("engines", "bots"), ("humans", "humans"), ("masters", "masters")):
            opening.names["group"] = group
            expected = f"{base} | {label} +5 =77 -5 | Kapablanca +0 =3 -0"
            self.assertEqual(fics.console_learning(board, move, opening), expected)
            self.assertIn(expected, fics.console_search(board, move, {}, 0.3, 180, opening=opening))
        opening.results[1].clear()
        self.assertEqual(fics.console_learning(board, move, opening), base + " | Kapablanca +0 =3 -0")
        opening.results[2][(key, "d2d4")] = [0, 0, 0]
        self.assertEqual(fics.console_learning(board, move, opening), base)
        self.assertNotIn("Kapablanca", fics.console_learning(board, chess.Move.from_uci("g1f3"), opening))

    def test_console_learning_results_are_maximas_for_either_colour(self):
        from test_learning import make_game

        for colour, win, loss in (("white", "1-0", "0-1"), ("black", "0-1", "1-0")):
            records = [learning.digest(make_game([0] * 10, ours=colour, result=result,
                       opponent_headers={"LearningGroup": "engines", "LearningServer": "fics"}))
                       for result in (win, win, loss)]
            for record in records:
                record["opponent"] = "Kapablanca"
            opening = learning.opening_book(records, group="engines", opponent="Kapablanca", server="fics")
            board = chess.Board()
            if colour == "black":
                board.push_uci("e2e4")
            move = chess.Move.from_uci("e2e4" if colour == "white" else "e7e5")
            self.assertEqual(fics.console_learning(board, move, opening),
                             "learn +2 =0 -1 | bots +2 =0 -1 | Kapablanca +2 =0 -1")

    async def test_game_events_follow_the_game(self):
        events = connectors.game_events()
        seen = []
        events.subscribe(seen.append)
        player = await self.player("play", events=events)
        await player.handle(start)
        await self.settle(player)
        await player.handle(after_e4)
        await player.handle("{Game 17 (GuestHJGC vs. guestNinja) guestNinja resigns} 1-0")
        self.assertEqual([event["type"] for event in seen], ["game_start", "move", "game_end"])
        start_event, move, end = seen
        self.assertEqual((start_event["our_color"], start_event["white"]), ("white", "GuestHJGC"))
        self.assertEqual((move["san"], move["by"], move["search"]["score"]), ("e4", "us", {"cp": 25}))
        self.assertIn("1. e4", end["pgn"])

    async def test_endgame_move_comes_from_the_online_tablebase(self):
        ending = ("<12> ----k--- -------- -------- -------- -------- -------- ----P--- R---K--- W -1 0 0 0 0 0 17 "
                  "GuestHJGC guestNinja 1 3 0 5 0 180000 180000 1 none (0:00.000) none 0 0 0")
        player = await self.player("think", online_tablebase=True)
        found = {"move": chess.Move.from_uci("a1a7"), "category": "win", "dtz": 15, "dtm": 15}
        with patch.object(online_tablebase, "probe", return_value=found):
            await player.handle(ending)
            await self.settle(player)
        self.assertEqual(player.connection.sent, ["a1a7"])
        self.assertTrue(any("tablebase: win DTM 15" in line for line in self.lines))

    async def test_unavailable_tablebase_leaves_the_move_to_the_engine(self):
        ending = ("<12> ----k--- -------- -------- -------- -------- -------- ----P--- R---K--- W -1 0 0 0 0 0 17 "
                  "GuestHJGC guestNinja 1 3 0 5 0 180000 180000 1 none (0:00.000) none 0 0 0")
        player = await self.player("play", online_tablebase=True)
        with patch.object(online_tablebase, "probe", side_effect=OSError("offline")) as probe:
            await player.handle(ending)
            await self.settle(player)
        self.assertEqual(player.connection.sent, ["e2e4"])
        self.assertGreater(player.tablebase.pause, 0)
        probe.assert_called_once()

    async def test_draw_offer_is_accepted_in_a_tablebase_draw(self):
        ending = ("<12> ----k--- -------- -------- -------- -------- -------- ----P--- R---K--- W -1 0 0 0 0 0 17 "
                  "GuestHJGC guestNinja 1 3 0 5 0 180000 180000 1 none (0:00.000) none 0 0 0")
        player = await self.player("think", online_tablebase=True)
        drawn = {"move": chess.Move.from_uci("a1a7"), "category": "draw", "dtz": 0, "dtm": None}
        with patch.object(online_tablebase, "probe", side_effect=lambda *unused: time.sleep(0.2) or drawn):
            await player.handle(ending)
            await player.handle("guestNinja offers you a draw.")
            await self.settle(player)
        self.assertEqual(player.connection.sent, ["draw", "a1a7"])

    async def test_engine_move_stands_when_the_tablebase_is_late(self):
        ending = ("<12> ----k--- -------- -------- -------- -------- -------- ----P--- R---K--- W -1 0 0 0 0 0 17 "
                  "GuestHJGC guestNinja 1 3 0 5 0 180000 180000 1 none (0:00.000) none 0 0 0")
        player = await self.player("play", online_tablebase=True)
        late = {"move": chess.Move.from_uci("a1a7"), "category": "win", "dtz": 15, "dtm": 15}
        with patch.object(online_tablebase, "probe", side_effect=lambda *unused: time.sleep(0.5) or late):
            await player.handle(ending)
            await self.settle(player)
        self.assertEqual(player.connection.sent, ["e2e4"])

    async def test_game_end_cancels_search(self):
        player = await self.player("hang")
        await player.handle(start)
        await asyncio.sleep(0.2)
        task = player.task
        await player.handle("{Game 17 (GuestHJGC vs. guestNinja) guestNinja resigns} 1-0")
        await asyncio.sleep(0.1)
        self.assertTrue(task.cancelled())
        self.assertIsNone(player.game)
        self.assertEqual(player.connection.sent, [])

    async def test_hung_engine_is_restarted_and_board_refreshed(self):
        player = await self.player("hang", margin=0.3)
        await player.handle(start.replace("180000 180000", "500 500"))
        await self.settle(player)
        self.assertEqual(player.connection.sent, ["refresh"])
        self.assertTrue(any("restarting" in line for line in self.lines))
        self.assertIsNotNone(player.session)

    async def test_illegal_move_reply_refreshes_once(self):
        player = await self.player("play")
        await player.handle(start)
        await self.settle(player)
        await player.handle("Illegal move (e2e4).")
        await player.handle("Illegal move (e2e4).")
        self.assertEqual(player.connection.sent, ["e2e4", "refresh"])

    async def test_aborts_when_the_opponent_stalls_before_a_first_move(self):
        after_e5 = ("<12> rnbqkbnr pppp-ppp -------- ----p--- ----P--- -------- PPPP-PPP RNBQKBNR W 4 1 1 1 1 0 17 "
                    "GuestHJGC guestNinja 1 3 0 39 39 180000 180000 2 P/e7-e5 (0:00.000) e5 0 0 0")
        folder = Path(self.directory.name) / "games"
        player = await self.player("hang", policy=fics.play_policy(first_move_wait=0.1), pgn_dir=folder)
        player.policy.opening_wait = lambda initial: 0.1
        await player.handle(after_e4)
        await player.handle(after_e5)
        await asyncio.sleep(0.3)
        self.assertNotIn("abort", player.connection.sent)
        player.cancel()
        await player.handle(after_e4.replace(" 17 GuestHJGC", " 18 GuestHJGC"))
        await asyncio.sleep(0.3)
        self.assertEqual(player.connection.sent, ["abort"])
        self.assertTrue(any("no first move" in line for line in self.lines))
        await player.handle("{Game 18 (GuestHJGC vs. guestNinja) Game aborted on move 1} *")
        self.assertEqual(list(folder.glob("*.pgn")), [])

    def test_opening_stall_eligibility_and_time(self):
        board = chess.Board()
        for move in ("d2d4", "f7f6"):
            board.push_uci(move)
        move = chess.Move.from_uci("c2c4")
        for cp, expected in ((-11, True), (-10, True), (-9, False)):
            info = {"score": chess.engine.PovScore(chess.engine.Cp(cp), chess.WHITE)}
            self.assertEqual(bool(move_choice.opening_abort_position(board, move, info, "engine", -10)), expected)
        self.assertIsNone(move_choice.opening_abort_position(board, move, {}, "engine", -10))
        self.assertIsNotNone(move_choice.opening_abort_position(board, move, {}, "book", -10))
        for fullmove, turn, expected in ((4, chess.WHITE, True), (4, chess.BLACK, False), (5, chess.WHITE, False)):
            board.fullmove_number, board.turn = fullmove, turn
            move = next(iter(board.legal_moves))
            self.assertEqual(bool(move_choice.opening_abort_position(board, move, {}, "book", -10)), expected)
        self.assertEqual([move_choice.opening_stall_wait(clock) for clock in (60, 180, 300, 600)],
                         [60, 60, 60, 120])

    async def test_opening_stall_timer_survives_clock_updates_and_stops_on_reply(self):
        player = await self.player("hang")
        await player.handle(after_e4)
        player.stop_waiting()
        player.history.board.push_uci("f7f6")
        player.history.board.push_uci("c2c4")
        player.abort_position = player.history.board.fen()
        player.policy.opening_wait = lambda initial: 0.01
        player.watch_first_move(fics_game.style12.parse(after_e4))
        timer = player.waiting
        player.watch_first_move(fics_game.style12.parse(after_e4))
        self.assertIs(player.waiting, timer)
        await timer
        self.assertEqual(player.connection.sent, ["abort"])
        player.watch_first_move(fics_game.style12.parse(after_e4))
        self.assertIs(player.waiting, timer)
        player.history.board.push_uci("e7e5")
        player.watch_first_move(fics_game.style12.parse(start))
        self.assertIsNone(player.waiting)

    async def test_played_game_is_saved_as_pgn(self):
        folder = Path(self.directory.name) / "games"
        player = await self.player("play", pgn_dir=folder)
        player.name = "GuestHJGC"
        await player.handle("Creating: GuestHJGC (++++) guestNinja (1500) unrated blitz 3 0")
        await player.handle("Finger of guestNinja(U):")
        await player.handle(start)
        await self.settle(player)
        await player.handle(after_e4.replace("180000 180000 1 P/e2-e4 (0:00.000)", "179900 180000 1 P/e2-e4 (0:00.100)"))
        await player.handle("{Game 17 (GuestHJGC vs. guestNinja) guestNinja resigns} 1-0")
        files = list(folder.rglob("*.pgn"))
        self.assertEqual(len(files), 1)
        self.assertEqual(files[0].parent, folder / 'fics')
        self.assertFalse(list(folder.glob('*.pgn')))
        with files[0].open() as stream:
            game = chess.pgn.read_game(stream)
        headers = game.headers
        self.assertEqual((headers["Event"], headers["Site"], headers["TimeControl"]),
                         ("FICS unrated blitz game", "freechess.org", "180+0"))
        self.assertEqual((headers["WhiteType"], headers["WhiteEngine"], headers["BlackType"], headers["BlackElo"]),
                         ("program", "fics fixture", "human", "1500"))
        self.assertNotIn("WhiteElo", headers)
        self.assertEqual((headers["WhiteAccount"], headers["BlackAccount"]), ("unregistered", "unregistered"))
        move = game.next()
        self.assertEqual((move.move.uci(), move.eval_depth(), move.clock(), move.emt()), ("e2e4", 3, 179.9, 0.1))
        self.assertTrue(any(line.startswith("-- saved game 17") for line in self.lines))

    async def test_learned_move_is_played_without_searching(self):
        folder = Path(self.directory.name) / "games"
        # Without book.bin in the profile, its share does not turn into searches.
        profiles = {name: dict(weights, book=0, search=0) for name, weights in learning.layer_profiles.items()}
        learner = learning.learner(Path(self.directory.name) / "experience", folder, profiles=profiles)
        key = f"{chess.polyglot.zobrist_hash(chess.Board()):016x}"
        learner.records = [{"opponent": "guestNinja", "group": "humans", "type": "blitz",
                            "color": "white", "moves": [[key, "d2d4"]],
                            "values": [[key, "d2d4", 0.44, 1.0]], "analyses": []}] * 6
        fixture_view = patch.object(learner.store, "load", return_value=learner.records)
        fixture_view.start()
        self.addCleanup(fixture_view.stop)
        player = await self.player("hang", pgn_dir=folder, learner=learner)
        player.name = "GuestHJGC"
        await player.handle("Creating: GuestHJGC (++++) guestNinja (1500) unrated blitz 3 0")
        await player.handle("Finger of guestNinja:")
        await player.handle(start)
        await self.settle(player)
        self.assertEqual(player.connection.sent[-1], "d2d4")
        self.assertTrue(any(line.startswith("1. d4 | learn (pick 100%) | learn +6 =0 -0 | ") and line.endswith(")")
                            for line in self.lines))
        self.assertEqual(player.opponent_group(), "humans")
        after_d4 = after_e4.replace("----P--- -------- PPPP-PPP", "---P---- -------- PPP-PPPP").replace(
            " B 4 ", " B 3 ").replace("P/e2-e4 (0:00.000) e4", "P/d2-d4 (0:00.000) d4")
        await player.handle(after_d4)
        await player.handle("{Game 17 (GuestHJGC vs. guestNinja) guestNinja resigns} 1-0")
        with next(folder.rglob("*.pgn")).open() as stream:
            self.assertEqual(chess.pgn.read_game(stream).next().comment.split()[0], "learned")
        self.assertEqual(len(learner.records), 6)  # one move teaches nothing

    async def test_draw_contempt_follows_the_rating_gap(self):
        self.assertEqual(fics.draw_contempt(1800, 1800), -10)
        self.assertEqual(fics.draw_contempt(1800, 2000), 40)
        self.assertEqual(fics.draw_contempt(1800, 2600), 100)
        self.assertEqual(fics.draw_contempt(2000, 1500), -100)
        self.assertIsNone(fics.draw_contempt(None, 1500))
        self.assertIsNone(fics.draw_contempt(1800, 1500, window=0))
        player = await self.player("play")
        player.name = "GuestHJGC"
        await player.handle("Creating: GuestHJGC (1700) guestNinja (1900) rated blitz 3 0")
        self.assertEqual(player.contempt, 40)
        await player.handle("Finger of guestNinja:")
        await player.handle(start)
        await self.settle(player)
        self.assertIn("setoption name DrawContempt value 32", self.positions.read_text())
        self.assertTrue(any(line.startswith("-- draw contempt +0.32 (1700 vs 1900)") for line in self.lines))
        await player.handle("Creating: guestNinja (1900) GuestHJGC (1700) rated blitz 3 0")
        self.assertEqual(player.contempt, 40)
        await player.handle("{Game 17 (GuestHJGC vs. guestNinja) guestNinja resigns} 1-0")
        await player.handle("Creating: GuestHJGC (++++) GuestOther (++++) unrated blitz 3 0")
        self.assertIsNone(player.contempt)
        await player.handle("Finger of GuestOther(U):")
        await player.handle(start.replace(" 17 GuestHJGC guestNinja", " 18 GuestHJGC GuestOther"))
        await self.settle(player)
        sent = self.positions.read_text()
        self.assertGreater(sent.rfind("setoption name DrawContempt value -10"),
                           sent.rfind("setoption name DrawContempt value 32"))

    async def test_white_wants_more_than_a_draw_at_any_sign(self):
        self.assertEqual([learning.color_contempt(18, white) for white in (True, False)], [14, 22])
        self.assertEqual([learning.color_contempt(-10, white) for white in (True, False)], [-12, -8])
        self.assertIsNone(learning.color_contempt(None, True))

    async def test_ignores_observed_games_and_invalid_boards(self):
        player = await self.player("play")
        for line in observed:
            await player.handle(line)
        await player.handle(start.replace("RNBQKBNR W", "RNBQ-BNR W"))
        self.assertIsNone(player.task)
        self.assertIsNone(player.game)
        self.assertEqual(player.connection.sent, [])
        self.assertTrue(any(line.startswith("-- ignored board") for line in self.lines))


if __name__ == "__main__":
    unittest.main()
