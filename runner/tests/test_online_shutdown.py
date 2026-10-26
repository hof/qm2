"""Bounded signal draining, incomplete PGNs, and real online CLI/process cleanup."""
import asyncio
import io
import json
import os
from pathlib import Path
import signal
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import chess.pgn

from maxima_runner import fics, learning, learning_store, lichess, online_shutdown
from test_fics import fake_server, guest_login
from test_fics_game import start, after_e4
from test_learning import make_game
from test_lichess import fake_lichess, game_full, stub_api


class controller_tests(unittest.IsolatedAsyncioTestCase):
    async def test_idle_signal_does_not_wait_for_drain_and_restores_handlers(self):
        ready, cleaned = asyncio.Event(), asyncio.Event()
        messages, stops = [], []
        controller = online_shutdown.shutdown_controller(messages.append, grace=10)
        before = {number: signal.getsignal(number) for number in (signal.SIGTERM, signal.SIGINT)}

        async def session():
            try:
                ready.set()
                await asyncio.Event().wait()
            finally:
                cleaned.set()

        task = asyncio.create_task(controller.run(session(), lambda: stops.append(True) or False))
        await ready.wait()
        controller.notify('SIGTERM', lambda: stops.append(True) or False)
        self.assertEqual(await asyncio.wait_for(task, 1), 0)
        self.assertTrue(cleaned.is_set())
        self.assertEqual(stops, [True])
        self.assertEqual({number: signal.getsignal(number) for number in before}, before)

    async def test_current_game_finishes_during_grace(self):
        ready, finish = asyncio.Event(), asyncio.Event()
        controller = online_shutdown.shutdown_controller(lambda line: None, grace=1)

        async def session():
            ready.set()
            await finish.wait()
            return 0

        task = asyncio.create_task(controller.run(session(), lambda: None))
        await ready.wait()
        controller.notify('SIGTERM', lambda: None)
        await asyncio.sleep(0.01)
        self.assertFalse(task.done())
        finish.set()
        self.assertEqual(await asyncio.wait_for(task, 1), 0)

    async def test_expiry_and_second_signal_cancel_once_then_cleanup(self):
        for second_signal in (False, True):
            ready = asyncio.Event()
            cancelled = []
            controller = online_shutdown.shutdown_controller(lambda line: None, grace=0.04 if not second_signal else 10)

            async def session():
                try:
                    ready.set()
                    await asyncio.Event().wait()
                finally:
                    cancelled.append(True)
                    await asyncio.sleep(0.01)

            task = asyncio.create_task(controller.run(session(), lambda: True))
            await ready.wait()
            controller.notify('SIGTERM', lambda: True)
            if second_signal:
                controller.notify('SIGINT', lambda: self.fail('second signal repeated stop'))
            self.assertEqual(await asyncio.wait_for(task, 1), 0)
            self.assertEqual(cancelled, [True])

    async def test_normal_errors_remain_errors(self):
        controller = online_shutdown.shutdown_controller(lambda line: None)

        async def session():
            raise RuntimeError('fixture error')

        with self.assertRaisesRegex(RuntimeError, 'fixture error'):
            await controller.run(session(), lambda: None)


class interrupted_tests(unittest.TestCase):
    def test_partial_archive_never_teaches_or_conflicts_with_complete_game(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = make_game([0] * 10)
            complete = chess.pgn.read_game(io.StringIO(str(game)))
            messages = []
            online_shutdown.save_interrupted(game, root / 'games', messages.append)
            online_shutdown.save_interrupted(game, root / 'games', messages.append)
            partials = list((root / 'games' / 'interrupted').glob('*.pgn'))
            self.assertEqual(len(partials), 1)
            partial = learning_store.read_games(partials[0])[0]
            self.assertEqual(partial.headers['Result'], '*')
            self.assertEqual(partial.headers['Termination'], 'unterminated')
            self.assertIsNone(learning.digest(partial))
            store = learning_store.shared_store(root)
            store.add(complete)
            self.assertEqual(store.rebuild(), {'games': 2, 'learned': 1})
            self.assertEqual(len(store.load()), 1)
            self.assertTrue(messages)

    def test_missing_destination_or_storage_failure_does_not_break_cleanup(self):
        online_shutdown.save_interrupted(make_game([0] * 10), None, self.fail)
        with tempfile.TemporaryDirectory() as directory:
            messages = []
            with patch('maxima_runner.learning_store.archive_game', side_effect=OSError('disk fixture')):
                online_shutdown.save_interrupted(make_game([0] * 10), Path(directory), messages.append)
            self.assertIn('could not save unfinished game', messages[0])

    def test_settings_are_validated_before_connecting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'config.json'
            for load in (fics.load_settings, lichess.load_settings):
                self.assertEqual(load(None)['shutdown_grace'], 15)
                for value in (-1, None, True, '15', float('nan'), float('inf')):
                    path.write_text(json.dumps({'shutdown_grace': value}))
                    with self.assertRaises(ValueError):
                        load(path)
                path.write_text(json.dumps({'shutdown_grace': 0.05}))
                self.assertEqual(load(path)['shutdown_grace'], 0.05)


engine_script = '''
import os,sys
from pathlib import Path
log = Path(os.environ['SHUTDOWN_FIXTURE_LOG'])
Path(os.environ['SHUTDOWN_FIXTURE_PID']).write_text(str(os.getpid()))
position = ''
for line in sys.stdin:
    command = line.strip()
    with log.open('a') as output:
        output.write(command + '\\n')
    if command == 'uci' and os.environ.get('SHUTDOWN_FIXTURE_STARTUP') != 'hang':
        print('id name shutdown fixture')
        print('uciok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command.startswith('position '):
        position = command
    elif command.startswith('go '):
        print('info depth 18 score cp 0', flush=True)
    elif command == 'stop':
        print('bestmove ' + ('e7e5' if 'moves e2e4' in position else 'e2e4'), flush=True)
    elif command == 'quit':
        break
'''


@unittest.skipUnless(os.name == 'posix', 'real Unix signal and executable fixtures')
class process_tests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.log, self.pid = self.root / 'engine.log', self.root / 'engine.pid'
        self.engine = self.root / 'engine'
        self.engine.write_text('#!' + sys.executable + '\n' + engine_script)
        self.engine.chmod(0o755)
        self.processes = []
        self.addAsyncCleanup(self.cleanup_processes)

    async def cleanup_processes(self):
        for process in self.processes:
            if process.returncode is None:
                process.kill()
            await process.wait()

    async def launch(self, service, *extra, grace='0.15', startup=''):
        environment = dict(os.environ, MAXIMA_DATA_DIR=str(self.root),
                           LICHESS_BOT_TOKEN='fixture-token', SHUTDOWN_FIXTURE_LOG=str(self.log),
                           SHUTDOWN_FIXTURE_PID=str(self.pid), SHUTDOWN_FIXTURE_STARTUP=startup)
        command = [sys.executable, '-m', 'maxima_runner', service, '--engine', str(self.engine),
                   '--no-learning', '--no-online-tablebase', '--shutdown-grace', grace]
        # Keep user auto-discovered configuration out of the fake-server fixtures.
        config = self.root / 'fixture-config.json'
        config.write_text('{}')
        command += ['--config', str(config)]
        command += list(extra)
        process = await asyncio.create_subprocess_exec(*command, env=environment, stdin=asyncio.subprocess.DEVNULL,
                                                       stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        self.processes.append(process)
        return process

    async def wait_for(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() >= deadline:
                self.fail('fixture did not reach expected state')
            await asyncio.sleep(0.01)

    def thinking(self):
        return self.log.exists() and 'go ' in self.log.read_text()

    async def stopped(self, process):
        output, _ = await asyncio.wait_for(process.communicate(), 5)
        text = output.decode()
        self.assertEqual(process.returncode, 0, text)
        self.assertNotIn('Traceback', text)
        self.assertIn('SIGTERM', text)
        if self.pid.exists():
            engine_pid = int(self.pid.read_text())
            with self.assertRaises(ProcessLookupError):
                os.kill(engine_pid, 0)
        return text

    async def fics_session(self, finish=False, second_signal=False):
        stopped_seeking = asyncio.Event()

        async def script(number, writer, read_line):
            await guest_login(writer, read_line)
            board = start.replace('GuestHJGC guestNinja 1', 'Opponent GuestTEST -1')
            reply = after_e4.replace('GuestHJGC guestNinja -1', 'Opponent GuestTEST 1')
            writer.write(('Creating: Opponent (1800) GuestTEST (++++) unrated blitz 3 0\n' + board + '\n').encode())
            # Consume login/seek/finger commands, then report a confirmed opponent move.
            while await read_line() != 'finger Opponent':
                pass
            writer.write(('Finger of Opponent:\n' + reply + '\n').encode())
            while True:
                command = await read_line()
                if command == 'unseek':
                    stopped_seeking.set()
                    if finish:
                        writer.write(b'{Game 17 (Opponent vs. GuestTEST) Opponent resigns} 0-1\n')
                if command == 'quit':
                    return

        async with fake_server(script, timeseal=False) as server:
            process = await self.launch('fics', '--host', '127.0.0.1', '--port', str(server.port), '--no-timeseal',
                                        grace='1' if finish or second_signal else '0.15')
            await self.wait_for(self.thinking)
            process.send_signal(signal.SIGTERM)
            await asyncio.wait_for(stopped_seeking.wait(), 2)
            if second_signal:
                process.send_signal(signal.SIGINT)
            text = await self.stopped(process)
            self.assertFalse(any('e7e5' == line for lines in server.connections for line in lines))
        return text

    async def test_fics_drain_timeout_saves_confirmed_moves_and_reaps_engine(self):
        text = await self.fics_session()
        self.assertIn('saved unfinished game', text)
        path = next((self.root / 'experience' / 'games' / 'interrupted').glob('*.pgn'))
        game = learning_store.read_games(path)[0]
        self.assertEqual([move.uci() for move in game.mainline_moves()], ['e2e4'])
        self.assertEqual(game.headers['Result'], '*')
        self.assertIsNone(learning.digest(game))

    async def test_fics_confirmed_end_during_grace_uses_normal_pgn_only(self):
        await self.fics_session(finish=True)
        paths = list((self.root / 'experience' / 'games').rglob('*.pgn'))
        self.assertEqual(len(paths), 1)
        self.assertEqual(learning_store.read_games(paths[0])[0].headers['Result'], '0-1')
        self.assertFalse(list((self.root / 'experience' / 'games' / 'interrupted').glob('*.pgn')))

    async def test_second_signal_ends_drain_without_duplicate_pgn(self):
        await self.fics_session(second_signal=True)
        self.assertEqual(len(list((self.root / 'experience' / 'games' / 'interrupted').glob('*.pgn'))), 1)

    async def test_signal_during_engine_startup_does_not_connect_and_reaps_child(self):
        process = await self.launch('fics', '--host', '127.0.0.1', '--port', '1', startup='hang', grace='5')
        await self.wait_for(self.pid.exists)
        started = time.monotonic()
        process.send_signal(signal.SIGTERM)
        await self.stopped(process)
        self.assertLess(time.monotonic() - started, 2)
        self.assertFalse(list((self.root / 'experience').rglob('*.pgn')))

    async def test_fics_signal_during_reconnect_exits_without_another_login(self):
        disconnected = asyncio.Event()

        async def script(number, writer, read_line):
            await guest_login(writer, read_line)
            await read_line()
            writer.close()
            disconnected.set()

        async with fake_server(script, timeseal=False) as server:
            process = await self.launch('fics', '--host', '127.0.0.1', '--port', str(server.port), '--no-timeseal', grace='5')
            await asyncio.wait_for(disconnected.wait(), 5)
            await asyncio.sleep(0.05)
            process.send_signal(signal.SIGTERM)
            await self.stopped(process)
            self.assertEqual(len(server.connections), 1)

    async def lichess_session(self, finish=False):
        server = fake_lichess()
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        self.addCleanup(server.close_streams)
        process = await self.launch('lichess', '--server', f'http://127.0.0.1:{server.server_port}', '--no-challenge',
                                    grace='1' if finish else '0.15')
        full = game_full('e2e4')
        full['white'], full['black'] = full['black'], full['white']  # Maxima is Black, engine thinks after e4.
        server.streams['/api/stream/event'].put({'type': 'gameStart', 'game': {'gameId': 'g1'}})
        server.streams['/api/bot/game/stream/g1'].put(full)
        await self.wait_for(self.thinking)
        process.send_signal(signal.SIGTERM)
        if finish:
            await asyncio.sleep(0.05)
            server.streams['/api/bot/game/stream/g1'].put(dict(full['state'], status='resign', winner='black'))
        text = await self.stopped(process)
        self.assertFalse(any('/move/' in path for path, _ in server.posts))
        self.assertFalse(any(path.endswith('/resign') for path, _ in server.posts))
        return text

    async def test_lichess_drain_timeout_saves_unknown_result_and_reaps_engine(self):
        text = await self.lichess_session()
        self.assertIn('saved unfinished game', text)
        path = next((self.root / 'experience' / 'games' / 'interrupted').glob('*.pgn'))
        game = learning_store.read_games(path)[0]
        self.assertEqual(game.headers['Result'], '*')
        self.assertEqual(game.headers['LearningIncomplete'], 'true')

    async def test_lichess_confirmed_finish_has_no_extra_interrupted_game(self):
        await self.lichess_session(finish=True)
        paths = list((self.root / 'experience' / 'games').rglob('*.pgn'))
        self.assertEqual(len(paths), 1)
        self.assertEqual(learning_store.read_games(paths[0])[0].headers['Result'], '0-1')
        self.assertFalse(list((self.root / 'experience' / 'games' / 'interrupted').glob('*.pgn')))


class pending_game_tests(unittest.IsolatedAsyncioTestCase):
    async def test_outgoing_challenge_completing_during_shutdown_is_cancelled(self):
        from unittest.mock import AsyncMock
        api = stub_api()
        started, release = asyncio.Event(), asyncio.Event()

        async def call(*args, **kwargs):
            started.set()
            await release.wait()
            return {'challenge': {'id': 'outgoing'}}

        api.call = AsyncMock(side_effect=call)
        bot = lichess.lichess_bot(api, None, lambda line: None)
        challenging = asyncio.create_task(bot.matchmaker.challenge(bot, 'Other', 'other', 'blitz', 180, 0))
        await started.wait()
        bot.soft_quit()
        self.assertFalse(bot.finished)
        release.set()
        await challenging
        self.assertFalse(bot.finished)
        await bot.cancel_challenge()
        self.assertTrue(bot.finished)
        self.assertEqual(api.posts, [('/api/challenge/outgoing/cancel', None)])
        await bot.matchmaker.challenge(bot, 'Other', 'other', 'blitz', 180, 0)
        self.assertEqual(api.call.await_count, 1)

    async def test_accepted_challenge_is_not_mistaken_for_idle_and_new_ones_are_declined(self):
        from test_lichess import challenge
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None)
        bot.accepted = ('accepted', time.monotonic())
        bot.soft_quit()
        self.assertFalse(bot.finished)
        await bot.on_challenge(challenge(), {'bot': True})
        self.assertTrue(api.posts[-1][0].endswith('/decline'))
        await bot.on_event({'type': 'challengeCanceled', 'challenge': {'id': 'accepted'}})
        self.assertTrue(bot.finished)
