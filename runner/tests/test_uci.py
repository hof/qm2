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
