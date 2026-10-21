"""Style12 parsing against a recorded FICS game, move history, and the engine player."""
import asyncio
from pathlib import Path
import sys
import tempfile
import unittest

import chess

from maxima_runner import fics, fics_game
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

    def test_ics_moves(self):
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("e2e4")), "e2e4")
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("e1g1")), "e1g1")
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("e7e8q")), "e7e8=Q")
        self.assertEqual(fics_game.ics_move(chess.Move.from_uci("a2a1n")), "a2a1=N")


fake_engine = '''
import sys, time
mode = sys.argv[1]
for line in sys.stdin:
    command = line.strip()
    if command == "uci":
        print("id name fics fixture")
        print("uciok", flush=True)
    elif command == "isready":
        print("readyok", flush=True)
    elif command.startswith("position"):
        with open(sys.argv[2], "a") as log:
            log.write(command + "\\n")
    elif command.startswith("go ") and mode == "play":
        time.sleep(0.05)
        print("info depth 3 score cp 25 nodes 100 pv e2e4")
        print("bestmove e2e4", flush=True)
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

    async def player(self, mode, margin=5):
        player = fics.engine_player(
            lambda: open_engine([sys.executable, str(self.script), mode, str(self.positions)],
                                self.directory.name, timeout=5),
            self.lines.append, margin=margin)
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

    async def test_plays_own_moves_only_once(self):
        player = await self.player("play")
        await player.handle(after_e4)
        self.assertIsNone(player.task)
        await player.handle(start.replace(" 17 GuestHJGC", " 18 GuestHJGC"))
        await player.handle(start.replace(" 17 GuestHJGC", " 18 GuestHJGC"))
        await self.settle(player)
        self.assertEqual(player.connection.sent, ["e2e4"])
        self.assertTrue(any(line.startswith("-- e4 d3 +0.25") for line in self.lines))
        self.assertEqual(self.positions.read_text().split("\n")[0], "position startpos")

    async def test_game_end_cancels_search(self):
        player = await self.player("hang")
        await player.handle(start)
        await asyncio.sleep(0.2)
        task = player.task
        await player.handle("{Game 17 (GuestHJGC vs. guestNinja) guestNinja resigns} 1-0")
        await asyncio.sleep(0.1)
        self.assertTrue(task.cancelled())
        self.assertIsNone(player.game)
        self.assertEqual(player.connection.sent, ["rematch"])

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
