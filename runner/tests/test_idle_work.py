"""Real generation-search acknowledgements and preemptible idle scheduling."""
import asyncio
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import chess
import chess.engine

from maxima_runner.idle_work import idle_scheduler, generation_permission
from maxima_runner.uci import open_engine
from test_uci import fake_engine


class idle_work_tests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'idle.json'
        self.state = 'ready'
        self.scheduler = idle_scheduler(self.path, lambda: self.state, on_line=lambda line: None)
        self.fixture = self.root / 'engine.py'
        self.fixture.write_text(fake_engine)

    async def test_play_handoff_waits_for_inflight_search_then_resumes_without_search_timeout(self):
        with patch.dict(os.environ, MAXIMA_GENERATION_IDLE_CONTROL=str(self.path)):
            async with open_engine([sys.executable, str(self.fixture)], self.root, timeout=0.3,
                                   root_pv_only=True) as engine:
                self.scheduler.publish(True, 'generation')
                search = asyncio.create_task(engine.search(chess.Board(), chess.engine.Limit(nodes=8)))
                while engine.generation_permission is None:
                    await asyncio.sleep(0.005)
                self.state = 'game'
                handoff = asyncio.create_task(self.scheduler.before_game())
                # Permission remains active until this bounded search finishes.
                self.assertFalse(handoff.done())
                await search
                blocked = asyncio.create_task(engine.search(chess.Board(), chess.engine.Limit(nodes=8)))
                await asyncio.wait_for(handoff, 1)
                await asyncio.sleep(0.4)
                self.assertFalse(blocked.done())
                self.state = 'ready'
                self.scheduler.listen({'type': 'game_end'})
                self.scheduler.publish(True, 'generation')
                result, _ = await asyncio.wait_for(blocked, 1)
                self.assertEqual(result.move.uci(), 'e2e4')
        self.assertFalse(list(self.root.glob('idle.json.workers/*.json')))

    async def test_missing_or_stale_owner_never_grants_generation(self):
        permission = generation_permission(self.path)
        try:
            self.path.write_text(json.dumps({'allowed': True, 'updated_at': time.time() - 31}))
            task = asyncio.create_task(permission.wait())
            await asyncio.sleep(0.12)
            self.assertFalse(task.done())
            self.assertFalse(json.loads(permission.ack.read_text())['active'])
            self.scheduler.publish(True, 'generation')
            await asyncio.wait_for(task, 1)
        finally:
            permission.close()

    async def test_deep_job_takes_idle_cpu_and_is_cancelled_before_live_play(self):
        started, cancelled = asyncio.Event(), asyncio.Event()
        async def deep():
            started.set()
            try:
                await asyncio.sleep(60)
            finally:
                cancelled.set()
        self.scheduler.deep = deep
        self.scheduler.deep_seconds = 0.01
        self.scheduler.idle_since -= 1
        self.scheduler.start()
        try:
            await asyncio.wait_for(started.wait(), 1)
            self.assertFalse(json.loads(self.path.read_text())['allowed'])
            self.state = 'game'
            await self.scheduler.before_game()
            self.assertTrue(cancelled.is_set())
            self.assertIsNone(self.scheduler.deep_task)
            self.assertFalse(json.loads(self.path.read_text())['allowed'])
        finally:
            await self.scheduler.close()

    async def test_unacknowledged_live_worker_blocks_game_handoff(self):
        permission = generation_permission(self.path)
        permission.mark(True)
        self.scheduler.handoff_timeout = 0.05
        try:
            with self.assertRaisesRegex(RuntimeError, 'handoff'):
                await self.scheduler.before_game()
            self.assertFalse(json.loads(self.path.read_text())['allowed'])
        finally:
            permission.close()

    async def test_clients_pause_generation_before_accepting_a_challenge(self):
        from types import SimpleNamespace
        from unittest.mock import AsyncMock
        from maxima_runner import lichess, fics
        calls = []
        async def pause():
            calls.append('pause')
        async def post(path, fields=None):
            calls.append(path)
            return True
        bot = lichess.lichess_bot(SimpleNamespace(post=post), None, lambda line: None)
        bot.id = 'ours'
        bot.decline_reason = lambda challenge, compat: None
        bot.idle_work = SimpleNamespace(before_game=pause)
        await bot.on_challenge({'id': 'game', 'speed': 'blitz', 'challenger': {'id': 'other'}}, {})
        self.assertEqual(calls[:2], ['pause', '/api/challenge/game/accept'])
        calls.clear()
        player = fics.engine_player(None, lambda line: None)
        player.idle_work = SimpleNamespace(before_game=pause)
        async def send(line):
            calls.append(line)
        player.send = send
        await player.on_challenge({'name': 'Opponent', 'type': 'blitz', 'time': '3', 'increment': '0'})
        self.assertEqual(calls, ['pause', 'accept Opponent'])

    async def test_real_deep_process_is_reaped_before_cpu_handoff(self):
        from unittest.mock import patch
        from maxima_runner import deep_learning
        script = self.root / 'deep_engine'
        pid_path = self.root / 'deep.pid'
        script.write_text('#!' + sys.executable + '\nimport os\n'
                          + 'open(' + repr(str(pid_path)) + ', "w").write(str(os.getpid()))\n' + fake_engine)
        script.chmod(0o700)
        started = asyncio.Event()
        async def analyse(*args, **kwargs):
            started.set()
            await asyncio.sleep(60)
        self.scheduler.deep = lambda: deep_learning.learn(script, self.root/'book.bin', [], positions=1)
        self.scheduler.deep_seconds = 0.01
        self.scheduler.idle_since -= 1
        with patch.object(deep_learning, 'queue', return_value=[chess.Board()]), patch.object(deep_learning, 'good_moves', side_effect=analyse):
            self.scheduler.start()
            try:
                await asyncio.wait_for(started.wait(), 2)
                pid = int(pid_path.read_text())
                self.state = 'game'
                await self.scheduler.before_game()
                with self.assertRaises(ProcessLookupError):
                    os.kill(pid, 0)
            finally:
                await self.scheduler.close()

    async def test_admission_hold_pauses_generation_but_keeps_deep_analysis_available(self):
        permitted = False
        started = asyncio.Event()
        async def deep():
            started.set()
            await asyncio.sleep(60)
        self.scheduler.admission = lambda: permitted
        self.scheduler.start()
        try:
            await asyncio.sleep(0.05)
            self.assertFalse(json.loads(self.path.read_text())['allowed'])
            permitted = True
            await asyncio.sleep(0.55)
            self.assertTrue(json.loads(self.path.read_text())['allowed'])
            permitted = False
            self.scheduler.deep = deep
            self.scheduler.deep_seconds = 0.01
            self.scheduler.idle_since -= 1
            await asyncio.wait_for(started.wait(), 1)
            self.assertFalse(json.loads(self.path.read_text())['allowed'])
        finally:
            await self.scheduler.close()

    async def test_offline_match_session_acknowledges_pause_without_root_pv_mode(self):
        with patch.dict(os.environ, MAXIMA_BACKGROUND_IDLE_CONTROL=str(self.path)):
            async with open_engine([sys.executable, str(self.fixture)], self.root, timeout=.3,
                                   root_pv_only=False) as engine:
                self.scheduler.publish(True, 'background test')
                await engine.search(chess.Board(), chess.engine.Limit(nodes=8))
                self.state = 'game'
                handoff = asyncio.create_task(self.scheduler.before_game())
                blocked = asyncio.create_task(engine.search(chess.Board(), chess.engine.Limit(nodes=8)))
                await asyncio.wait_for(handoff, 1)
                await asyncio.sleep(.35)
                self.assertFalse(blocked.done())
                self.scheduler.publish(True, 'background test')
                await asyncio.wait_for(blocked, 1)
