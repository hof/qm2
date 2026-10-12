"""Predeclared checkpoint bounds and whole-pair early stopping."""
from contextlib import asynccontextmanager
import io
import json
import math
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import chess

from maxima_runner.matches import match_summary, match_game_type, run_match


def records(pairs, result='win'):
    return [{'pair': pair, 'a_color': color,
             'result': ('1/2-1/2' if result == 'draw' else
                        '1-0' if color == 'white' else '0-1')}
            for pair in range(pairs) for color in ('white', 'black')]


class sequential_tests(unittest.TestCase):
    def test_adjusted_bound_and_only_declared_whole_pair_checks(self):
        args = SimpleNamespace(pairs=50, sequential_check_pairs=10)
        summary = match_summary(records(20), args)
        self.assertEqual(summary['planned_checks'], 5)
        self.assertEqual(summary['confidence'], 0.95)
        self.assertAlmostEqual(summary['checkpoint_confidence'], 0.99)
        self.assertAlmostEqual(summary['score_interval'][0], 1 - math.sqrt(math.log(200) / 40))
        self.assertGreater(summary['score_interval'][0], 0.5)
        self.assertEqual(match_summary(records(20, 'draw'), args)['a_score'], 0.5)
        self.assertIsNone(match_summary(records(19), args)['score_interval'])
        self.assertIsNone(match_summary(records(20) + records(21)[-2:-1], args)['score_interval'])
        self.assertIsNone(match_summary(records(20) + [{'pair': 20, 'a_color': 'white',
                                                     'result': '*'}], args)['score_interval'])
        args.pairs = 25
        self.assertEqual(match_summary(records(25), args)['planned_checks'], 3)
        self.assertIsNotNone(match_summary(records(25), args)['score_interval'])


class sequential_match_tests(unittest.IsolatedAsyncioTestCase):
    async def test_stop_after_complete_pair_or_finish_inconclusive(self):
        class session:
            identity = ['name fixture']
            protocol = SimpleNamespace(options={})
            async def configure(self, settings):
                return {}
        @asynccontextmanager
        async def open_fixture(*unused):
            yield session()
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(pairs=50, sequential_check_pairs=10, seed=1,
                timeout=1, hash_mb=1, variant='standard', seconds=1, increment=0,
                max_plies=1, json=Path(directory) / 'results.json')
            openings = [(str(i), chess.Board()) for i in range(50)]
            for result in ('win', 'loss', 'draw'):
                async def play_fixture(*unused, **settings):
                    a_white = settings['a_white']
                    record = records(1, 'draw' if result == 'draw' else 'win')[0 if a_white else 1]
                    if result == 'loss':
                        record['result'] = '0-1' if a_white else '1-0'
                    record.update(pair=settings['pair'], error=None, termination='fixture')
                    game = match_game_type()()
                    game.headers['Result'] = record['result']
                    return game, record
                report = {'games': []}
                with patch('maxima_runner.uci.open_engine', open_fixture), \
                        patch('maxima_runner.matches.play_game', play_fixture):
                    await run_match([['a'], ['b']], openings, args, report, io.StringIO())
                self.assertEqual(len(report['games']), 100 if result == 'draw' else 40)
                self.assertEqual(report.get('stopped_early', False), result != 'draw')
                self.assertEqual(report.get('stop_reason'), {'win': 'a_stronger',
                                 'loss': 'b_stronger', 'draw': None}[result])
                self.assertEqual(json.loads(args.json.read_text())['games'], report['games'])
