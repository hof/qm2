import asyncio
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch
from types import SimpleNamespace
import chess.engine
from maxima_runner.online_safety import engine_safety, remaining_clock, search_timeout


class safety_tests(unittest.IsolatedAsyncioTestCase):
    def test_captures_settings_from_the_actual_uci_option_map(self):
        safety = engine_safety('fixture')
        safety.describe(SimpleNamespace(protocol=SimpleNamespace(id={'name': 'Maxima'},
            config=chess.engine.UciOptionMap({'Threads': 2, 'Hash': 128}))))
        self.assertEqual(safety.details['options'], {'Threads': 2, 'Hash': 128})

    async def test_fault_survives_restart_and_cannot_recover_twice(self):
        with tempfile.TemporaryDirectory() as directory:
            events=[]
            safety=engine_safety('fixture',directory,lambda *a,**k:events.append((a,k)))
            safety.fail(RuntimeError('engine exited'), 'game', 'fen')
            safety.fail(RuntimeError('again'))
            with self.assertRaisesRegex(RuntimeError,'fault hold'):
                engine_safety('fixture',directory).check_start()
            stop,start=AsyncMock(),AsyncMock()
            self.assertTrue(await safety.recover(RuntimeError(),stop,start,10))
            self.assertFalse(await safety.recover(RuntimeError(),stop,start,10))
            self.assertEqual(start.await_count,1)
            self.assertEqual(sum(e[0][0]=='engine_incident' for e in events),1)

    async def test_correctness_failure_and_low_clock_never_restart(self):
        for error,clock in [(AssertionError(),10),(ValueError(),10),(RuntimeError(),.2)]:
            safety=engine_safety('fixture');safety.fail(error)
            start=AsyncMock()
            self.assertFalse(await safety.recover(error,AsyncMock(),start,clock))
            start.assert_not_awaited()

    def test_elapsed_time_is_not_replenished_on_retry(self):
        with patch('maxima_runner.online_safety.time.monotonic',return_value=105):
            self.assertEqual(remaining_clock(10,100),5)
            self.assertLess(search_timeout(10,100),5)
            self.assertGreater(search_timeout(.1,105),0)
            self.assertLess(search_timeout(.1,105),.1)
