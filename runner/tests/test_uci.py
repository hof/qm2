"""Async responsiveness, history preservation, and process lifecycle checks."""
import asyncio
import sys
import tempfile
from pathlib import Path
import unittest

import chess
import chess.engine

from maxima_runner.uci import open_engine


fake_engine = '''
import sys, time
for line in sys.stdin:
    command = line.strip()
    if command == 'uci':
        print('id name async fixture')
        print('uciok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command.startswith('go '):
        time.sleep(0.1)
        print('info depth 2 score cp 12 nodes 8 pv e2e4')
        print('bestmove e2e4', flush=True)
    elif command == 'quit':
        break
'''


class uci_tests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.script = Path(self.directory.name) / 'engine.py'
        self.script.write_text(fake_engine)
        self.command = [sys.executable, str(self.script)]

    async def test_search_leaves_event_loop_responsive(self):
        async with open_engine(self.command, self.directory.name, 2) as engine:
            protocol = engine.protocol
            task = asyncio.create_task(engine.search(chess.Board(), chess.engine.Limit(nodes=8)))
            await asyncio.sleep(0.02)
            self.assertFalse(task.done())
            best, elapsed_ms = await task
            self.assertEqual(best.move.uci(), 'e2e4')
            self.assertGreater(elapsed_ms, 0)
        self.assertTrue(protocol.returncode.done())
        self.assertEqual(protocol.returncode.result(), 0)

    async def test_hung_search_is_stopped_and_reaped(self):
        self.script.write_text(fake_engine.replace('time.sleep(0.1)', 'time.sleep(30)'))
        with self.assertRaisesRegex(RuntimeError, 'timed out'):
            async with open_engine(self.command, self.directory.name, 0.2) as engine:
                protocol = engine.protocol
                await engine.search(chess.Board(), chess.engine.Limit(nodes=8))
        self.assertTrue(protocol.returncode.done())

    async def test_search_cancellation_reaps_process(self):
        self.script.write_text(fake_engine.replace('time.sleep(0.1)', 'time.sleep(30)'))

        async def search():
            async with open_engine(self.command, self.directory.name, 0.2) as engine:
                protocols.append(engine.protocol)
                started.set()
                await engine.search(chess.Board(), chess.engine.Limit(nodes=8))

        protocols = []
        started = asyncio.Event()
        task = asyncio.create_task(search())
        await asyncio.wait_for(started.wait(), 2)
        await asyncio.sleep(0.02)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await asyncio.wait_for(task, 2)
        self.assertTrue(protocols[0].returncode.done())

    async def test_invalid_bestmove_fails_and_reaps(self):
        for move in ('e2e5', 'garbage'):
            self.script.write_text(fake_engine.replace('bestmove e2e4', 'bestmove ' + move))
            # python-chess 1.11.2 logs malformed bestmove errors after analysis
            # has started; our deadline must still fail and reap the process.
            loop = asyncio.get_running_loop()
            errors = []
            previous = loop.get_exception_handler()
            loop.set_exception_handler(lambda loop, context: errors.append(context))
            try:
                with self.assertRaises(RuntimeError):
                    async with open_engine(self.command, self.directory.name, 0.2) as engine:
                        protocol = engine.protocol
                        await engine.search(chess.Board(), chess.engine.Limit(nodes=8))
            finally:
                loop.set_exception_handler(previous)
            self.assertTrue(any(isinstance(error.get('exception'), chess.engine.EngineError)
                                for error in errors))
            self.assertTrue(protocol.returncode.done())

    async def test_invalid_position_is_rejected_before_engine_start(self):
        from maxima_runner.run_suite import run_position
        with self.assertRaisesRegex(ValueError, 'invalid chess position'):
            await run_position(['/nonexistent-engine'],
                               {'fen': '8/8/8/8/8/8/8/8 w - - 0 1'}, 'nodes 8', 32, 1)

    async def test_position_history_and_new_game_identity(self):
        transcript = Path(self.directory.name) / 'commands.txt'
        self.script.write_text(fake_engine.replace(
            'command = line.strip()',
            'command = line.strip()\n    with open(sys.argv[1], "a") as log: log.write(command + "\\n")'))
        async with open_engine(self.command + [str(transcript)], self.directory.name, 2) as engine:
            game = object()
            board = chess.Board()
            await engine.search(board, chess.engine.Limit(nodes=8), game=game)
            for move in ('g1f3', 'g8f6', 'f3g1', 'f6g8'):
                board.push_uci(move)
            await engine.search(board, chess.engine.Limit(nodes=8), game=game)
            await engine.search(board, chess.engine.Limit(nodes=8), game=object())
        lines = transcript.read_text().splitlines()
        self.assertIn('position startpos moves g1f3 g8f6 f3g1 f6g8', lines)
        self.assertEqual(lines.count('ucinewgame'), 2)


class variant_session_tests(unittest.IsolatedAsyncioTestCase):
    async def test_reject_unsupported_variant_before_analysis(self):
        from unittest.mock import AsyncMock
        from types import SimpleNamespace
        from maxima_runner.uci import engine_session
        from maxima_runner.variants import royal_losers_board
        import chess.variant
        protocol = SimpleNamespace(options={}, analysis=AsyncMock())
        session = engine_session(protocol, 1)
        with self.assertRaisesRegex(ValueError, "Wild=losers"):
            await session.search(royal_losers_board(), chess.engine.Limit(depth=1))
        with self.assertRaisesRegex(ValueError, "unsupported board variant"):
            await session.search(chess.variant.SuicideBoard(), chess.engine.Limit(depth=1))
        protocol.analysis.assert_not_called()

    async def test_real_engine_switches_variants_and_preserves_history(self):
        import os
        from maxima_runner.variants import royal_losers_board
        engine = os.environ.get("MAXIMA_ENGINE")
        if not engine:
            self.skipTest("MAXIMA_ENGINE is unset")
        async with open_engine([engine], None, 5) as session:
            await session.configure({"OwnBook": False, "UseExperience": False})
            fen = "4k3/7p/8/8/8/8/p7/R3K3 w - - 0 1"
            for board in (royal_losers_board(fen), chess.Board(fen), royal_losers_board()):
                if board.board_fen() == chess.Board().board_fen():
                    for move in ("e2e4", "d7d5", "e4d5"):
                        board.push_uci(move)
                before = (board.fen(), list(board.move_stack))
                best, _ = await session.search(board, chess.engine.Limit(depth=2))
                self.assertIn(best.move, board.legal_moves)
                self.assertEqual((board.fen(), board.move_stack), before)
                expected = "losers" if type(board) is royal_losers_board else "standard"
                self.assertEqual(session.protocol.config["Wild"], expected)


ponder_engine = '''
import sys
import chess
board = chess.Board()
first = True
for line in sys.stdin:
    command = line.strip()
    with open(sys.argv[1], 'a') as log:
        log.write(command + '\\n')
    if command == 'uci':
        print('option name Ponder type check default false')
        print('uciok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command.startswith('position '):
        fields = command.split()
        if fields[1] == 'startpos':
            board = chess.Board()
        else:
            end = fields.index('moves') if 'moves' in fields else len(fields)
            board = chess.Board(' '.join(fields[2:end]))
        if 'moves' in fields:
            for move in fields[fields.index('moves') + 1:]:
                board.push_uci(move)
    elif command.startswith('go ponder'):
        pass
    elif command.startswith('go ') or command in ('ponderhit', 'stop'):
        move = chess.Move.from_uci('e2e4') if first else next(iter(board.legal_moves))
        first = False
        replay = board.copy()
        replay.push(move)
        reply = next(iter(replay.legal_moves))
        print('info depth 18 score cp 12 nodes 100 pv', move.uci())
        print('bestmove', move.uci(), 'ponder', reply.uci(), flush=True)
    elif command == 'quit':
        break
'''


class ponder_tests(unittest.IsolatedAsyncioTestCase):
    async def test_string_configuration_preserves_ponder_hits(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
            script.write_text(ponder_engine.replace(
                "print('option name Ponder type check default false')",
                "print('option name Ponder type check default false')\n"
                "        print('option name OwnBook type check default true')\n"
                "        print('option name Hash type spin default 256 min 1 max 1024')"))
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                await session.configure({'OwnBook': 'false', 'Hash': '16'})
                board, game = chess.Board(), object()
                limit = chess.engine.Limit(white_clock=60, black_clock=60)
                first = await session.play(board, limit, game=game)
                board.push(first.move)
                board.push(first.ponder)
                hit = await session.play(board, limit, game=game)
                self.assertTrue(hit.info['_ponder_hit'])
                self.assertEqual(log.read_text().splitlines().count('ponderhit'), 1)

    async def test_completed_ponder_keeps_score_and_miss_discards_it(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
            fixture = ponder_engine.replace(
                "elif command.startswith('go ponder'):\n        pass",
                "elif command.startswith('go ponder'):\n        print('info depth 23 score cp 77 nodes 300 pv', next(iter(board.legal_moves)).uci(), flush=True)")
            fixture = fixture.replace(
                "print('info depth 18 score cp 12 nodes 100 pv', move.uci())",
                "if command != 'ponderhit':\n            print('info depth 18 score cp 12 nodes 100 pv', move.uci())")
            script.write_text(fixture)
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                game, board = object(), chess.Board()
                limit = chess.engine.Limit(white_clock=60, black_clock=60)
                first = await session.play(board, limit, game=game)
                self.assertFalse(first.info["_ponder_hit"])
                self.assertEqual(first.info['score'].relative.score(), 12)
                board.push(first.move)
                board.push(first.ponder)
                await asyncio.sleep(0.03)
                starts = []
                hit = await session.play(board, limit, game=game, on_search_started=starts.append)
                self.assertEqual(starts, [True])
                self.assertGreaterEqual(hit.info["_ponder_seconds"], 0.03)
                self.assertTrue(hit.info["_ponder_hit"])
                self.assertEqual(hit.info['score'].relative.score(), 77)
                self.assertEqual(hit.info['_resignation']['depth'], 23)
                self.assertEqual(hit.info['_resignation']['score'].relative.score(), 77)
                board.push(hit.move)
                board.push(next(move for move in board.legal_moves if move != hit.ponder))
                miss = await session.play(board, limit, game=game)
                self.assertFalse(miss.info["_ponder_hit"])
                self.assertIsNone(miss.info["_ponder_seconds"])
                self.assertEqual(miss.info['score'].relative.score(), 12)
                self.assertEqual(miss.info['_resignation']['depth'], 18)
                await session.stop_pondering()

    async def test_prediction_hit_miss_game_change_and_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
            script.write_text(ponder_engine)
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                game, board = object(), chess.Board()
                limit = chess.engine.Limit(white_clock=60, black_clock=60)
                result = await session.play(board, limit, game=game)
                self.assertFalse(result.info["_ponder_hit"])
                self.assertEqual(result.info['_resignation']['depth'], 18)
                board.push(result.move)
                board.push(result.ponder)
                hit = await session.play(board, limit, game=game)
                self.assertTrue(hit.info["_ponder_hit"])
                self.assertEqual(log.read_text().splitlines().count('ponderhit'), 1)
                board.push(hit.move)
                board.push(next(move for move in board.legal_moves if move != hit.ponder))
                miss = await session.play(board, limit, game=game)
                self.assertFalse(miss.info["_ponder_hit"])
                self.assertIn('stop', log.read_text().splitlines())
                board.push(miss.move)
                board.push(miss.ponder)
                changed = await session.play(board, limit, game=object())
                self.assertFalse(changed.info["_ponder_hit"])
                await session.stop_pondering()
                lines = log.read_text().splitlines()
                self.assertEqual(lines.count('ponderhit'), 1)
                self.assertEqual(lines.count('ucinewgame'), 2)
                self.assertEqual(sum(line.startswith('go ponder ') for line in lines), 4)
                self.assertEqual(lines[-1], 'isready')

    async def test_restart_discards_ponder_hit_and_preserves_background_info(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / "engine.py", Path(directory) / "commands"
            fixture = ponder_engine.replace(
                "elif command.startswith('go ponder'):\n        pass",
                "elif command.startswith('go ponder'):\n        print('info depth 23 score cp 77 nodes 9000 time 1000 pv', next(iter(board.legal_moves)).uci(), flush=True)")
            fixture = fixture.replace(
                "print('info depth 18 score cp 12 nodes 100 pv', move.uci())",
                "if command != 'ponderhit':\n            print('info depth 18 score cp 12 nodes 100 pv', move.uci())")
            script.write_text(fixture)
            limit = chess.engine.Limit(white_clock=60, black_clock=60)
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                board, game = chess.Board(), object()
                first = await session.play(board, limit, game=game)
                board.push(first.move)
                board.push(first.ponder)
                await asyncio.sleep(.03)
                hit = await session.play(board, limit, game=game)
                self.assertTrue(hit.info["_ponder_hit"])
                self.assertEqual(hit.info["depth"], 23)
                await asyncio.sleep(.03)
                self.assertTrue(hit.info["_ponder_hit"])
                self.assertTrue(session.protocol.ponder_hit)
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                fresh = await session.play(chess.Board(), limit)
                self.assertFalse(fresh.info["_ponder_hit"])


timed_engine = """
import sys, time
for line in sys.stdin:
    command = line.strip()
    with open(sys.argv[1], 'a') as log:
        log.write(command + '\\n')
    if command == 'uci':
        print('uciok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command.startswith('go '):
        print('info depth 10 score cp 12 nodes 100 time 100 pv e2e4', flush=True)
        time.sleep(.11)
        print('info depth 11 score cp 13 nodes 200 nps 1000 time 200 pv e2e4', flush=True)
    elif command == 'stop':
        print('info depth 12 score cp 14 nodes 210 nps 1000 time 210 pv FINAL', flush=True)
        print('bestmove FINAL', flush=True)
    elif command == 'quit':
        break
"""


class search_lifecycle_tests(unittest.IsolatedAsyncioTestCase):
    async def test_saved_analysis_cannot_stop_search_before_ceiling(self):
        from maxima_runner.move_choice import choose_move
        from types import SimpleNamespace
        for final in ("e2e4", "d2d4"):
            with self.subTest(final=final), tempfile.TemporaryDirectory() as directory:
                script, log = Path(directory) / "engine.py", Path(directory) / "commands"
                script.write_text(timed_engine.replace("FINAL", final))
                opening = SimpleNamespace(reuse=None)
                def choose(board):
                    opening.reuse = (chess.Move.from_uci("e2e4"), [25, 30, False, 60000, 0, 1400, 1000])
                opening.choose = choose
                async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                    move, info, kind = await choose_move(session, chess.Board(),
                        chess.engine.Limit(white_clock=30, black_clock=30),
                        opening=opening, ceiling=.35)
                    self.assertEqual(move.uci(), final)
                    self.assertEqual(kind, "engine")
                    self.assertEqual(info["depth"], 12)
                    self.assertEqual(info["nodes"], 210)
                    self.assertEqual(info["time"], .210)
                    self.assertGreaterEqual(info["_search_seconds"], .3)
                    self.assertNotIn("_experience", info)
                    self.assertEqual(log.read_text().splitlines().count("stop"), 1)

    async def test_cancellation_stops_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / "engine.py", Path(directory) / "commands"
            script.write_text(timed_engine.replace("FINAL", "e2e4"))
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                task = asyncio.create_task(session.play(chess.Board(), chess.engine.Limit(time=10)))
                await asyncio.sleep(.03)
                task.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await task
                await session.protocol.ping()
                self.assertIn("stop", log.read_text().splitlines())

    async def test_tablebase_cancels_search_and_keeps_tablebase_result(self):
        from maxima_runner.move_choice import choose_move
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / "engine.py", Path(directory) / "commands"
            script.write_text(timed_engine.replace("FINAL", "e2e4"))
            opening = SimpleNamespace(reuse=None)
            def choose(board):
                opening.reuse = (chess.Move.from_uci("e2e4"), [25, 30, False, 60000, 0, 1400, 1000])
            opening.choose = choose
            async def lookup():
                await asyncio.sleep(.03)
                return {"move": chess.Move.from_uci("d2d4"), "category": "win"}
            tablebase = SimpleNamespace(ask=lambda board: asyncio.create_task(lookup()))
            async with open_engine([sys.executable, str(script), str(log)], directory, 2) as session:
                move, info, kind = await choose_move(session, chess.Board(),
                    chess.engine.Limit(time=10), opening=opening, tablebase=tablebase)
                self.assertEqual(move.uci(), "d2d4")
                self.assertEqual(kind, "tablebase")
                self.assertNotIn("_experience", info)
                await session.protocol.ping()
                self.assertEqual(log.read_text().splitlines().count("stop"), 1)
