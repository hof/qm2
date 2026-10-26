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
                self.assertEqual(first.info['score'].relative.score(), 12)
                board.push(first.move)
                board.push(first.ponder)
                await asyncio.sleep(0.03)
                hit = await session.play(board, limit, game=game)
                self.assertEqual(hit.info['score'].relative.score(), 77)
                self.assertEqual(hit.info['_resignation']['depth'], 23)
                self.assertEqual(hit.info['_resignation']['score'].relative.score(), 77)
                board.push(hit.move)
                board.push(next(move for move in board.legal_moves if move != hit.ponder))
                miss = await session.play(board, limit, game=game)
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
                self.assertEqual(result.info['_resignation']['depth'], 18)
                board.push(result.move)
                board.push(result.ponder)
                hit = await session.play(board, limit, game=game)
                self.assertEqual(log.read_text().splitlines().count('ponderhit'), 1)
                board.push(hit.move)
                board.push(next(move for move in board.legal_moves if move != hit.ponder))
                miss = await session.play(board, limit, game=game)
                self.assertIn('stop', log.read_text().splitlines())
                board.push(miss.move)
                board.push(miss.ponder)
                await session.play(board, limit, game=object())
                await session.stop_pondering()
                lines = log.read_text().splitlines()
                self.assertEqual(lines.count('ponderhit'), 1)
                self.assertEqual(lines.count('ucinewgame'), 2)
                self.assertEqual(sum(line.startswith('go ponder ') for line in lines), 4)
                self.assertEqual(lines[-1], 'isready')
