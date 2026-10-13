"""Repeated-look minimum-gain coverage, claim semantics and match stopping."""
from contextlib import asynccontextmanager, redirect_stdout
import io
import json
import math
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import chess

from maxima_runner.matches import main, match_game_type, match_summary, run_match
from maxima_runner.strength import min_elo_summary, validate_min_elo
from test_sprt import pair_records


class strength_tests(unittest.TestCase):
    def test_reference_bound_and_repeated_look_budget(self):
        # Independently computed: 50 pairs, mean .95, two-sided alpha .05,
        # union bound over all 200 planned looks gives lower .6502134623.
        games = pair_records([(2, 2)] * 45 + [(1, 1)] * 5)
        summary = min_elo_summary(games, 10, 200)
        self.assertAlmostEqual(summary['score_interval'][0], .6502134622658653)
        self.assertAlmostEqual(summary['target_score'], .5143871841659987)
        self.assertEqual(summary['decision'], 'minimum_gain_established')
        for n in (1, 10, 50, 200):
            bound = min_elo_summary(pair_records([(2, 2)] * n), 10, 200)
            radius = 1 - bound['score_interval'][0]
            if radius < 1:
                self.assertAlmostEqual(2 * math.exp(-2 * n * radius ** 2), .05 / 200)

    def test_sprt_acceptance_is_not_a_minimum_gain_claim(self):
        # A 75% score can favor +10 against 0 while a small sample cannot
        # establish +10. A two-million-pair cap makes the bound conservative.
        args = SimpleNamespace(sprt=[0, 10], sprt_alpha=.05, sprt_beta=.05,
                               sprt_min_elo_stop=True, pairs=2_000_000)
        summary = match_summary(pair_records([(2, 1)] * 150), args)
        self.assertEqual(summary['sprt']['decision'], 'accept_h1')
        self.assertEqual(summary['minimum_gain']['decision'], 'continue')
        self.assertEqual(summary['sprt']['alpha'], .025)
        self.assertEqual(summary['sprt']['beta'], .025)
        self.assertEqual(summary['minimum_gain']['alpha'], .025)

    def test_pair_integrity_and_parameters(self):
        games = pair_records([(2, 2)] * 100)
        for records in (games[:-1], games + [{**games[0], 'pair': 100}]):
            summary = min_elo_summary(records, 10, 200)
            self.assertFalse(summary['decision_eligible'])
            self.assertIsNone(summary['score_interval'])
        games[-1]['error'] = 'failed game'
        self.assertEqual(min_elo_summary(games, 10, 200)['decision'], 'continue')
        for parameters in [(math.nan, 200, .05), (201, 200, .05),
                           (-1, 200, .05), (10, 0, .05), (10, 200, 0)]:
            with self.subTest(parameters=parameters), self.assertRaises(ValueError):
                validate_min_elo(*parameters)
        with self.assertRaises(ValueError):
            min_elo_summary(pair_records([(2, 2)] * 2), 10, 1)


class strength_match_tests(unittest.IsolatedAsyncioTestCase):
    async def test_both_rules_first_crossing_and_negative_result(self):
        class session:
            identity = ['name fixture']
            protocol = SimpleNamespace(options={})
            async def configure(self, settings):
                return {}
        @asynccontextmanager
        async def open_fixture(*unused):
            yield session()
        cases = [(2, [0, 10], None, True, 200, 'minimum_gain_established'),
                 (2, [0, 200], None, True, 200, 'accept_h1'),
                 (0, [0, 10], None, True, 200, 'below_minimum_gain'),
                 (2, None, 10, False, 200, 'minimum_gain_established'),
                 (1, None, 10, False, 1, None)]
        with tempfile.TemporaryDirectory() as directory:
            for score, sprt, minimum, hybrid, maximum, decision in cases:
                args = SimpleNamespace(pairs=maximum, sprt=sprt, min_elo=minimum,
                    sprt_min_elo_stop=hybrid, sprt_alpha=.05, sprt_beta=.05,
                    seed=1, timeout=1, hash_mb=1, variant='standard', seconds=1,
                    increment=0, max_plies=1, json=Path(directory) / 'result.json')
                async def play_fixture(*unused, **settings):
                    color = 'white' if settings['a_white'] else 'black'
                    record = pair_records([(score, score)])[0 if color == 'white' else 1]
                    record.update(pair=settings['pair'], termination='fixture')
                    game = match_game_type()()
                    game.headers['Result'] = record['result']
                    return game, record
                report = {'games': []}
                with patch('maxima_runner.uci.open_engine', open_fixture), \
                        patch('maxima_runner.matches.play_game', play_fixture), \
                        redirect_stdout(io.StringIO()):
                    await run_match([['a'], ['b']], [(str(i), chess.Board()) for i in
                                    range(maximum)], args, report, io.StringIO())
                self.assertEqual(report.get('stop_reason'), decision)
                self.assertEqual(len(report['games']) % 2, 0)
                if decision:
                    prior = match_summary(report['games'][:-2], args)
                    self.assertEqual(prior['minimum_gain']['decision'], 'continue')
                    if sprt:
                        self.assertEqual(prior['sprt']['decision'], 'continue')
                else:
                    self.assertEqual(len(report['games']), 2 * maximum)
                self.assertEqual(json.loads(args.json.read_text())['games'], report['games'])


class strength_cli_tests(unittest.TestCase):
    def test_validation_budget_identity_and_inconclusive_cap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            opening = root / 'openings.json'
            opening.write_text(json.dumps({'variant': 'standard', 'openings': [{'id': 'fixture'}]}))
            output = root / 'report.json'
            pgn = root / 'games.pgn'
            arguments = ['match:run', '--engine-a', sys.executable, '--engine-b', sys.executable,
                         '--openings', str(opening), '--pairs', '1', '--pgn', str(pgn),
                         '--json', str(output)]
            for extra in [['--min-elo', 'nan'], ['--min-elo', '-1'], ['--min-elo', '201'],
                          ['--sprt-min-elo-stop'], ['--sprt', '-10', '-5', '--sprt-min-elo-stop']]:
                with patch.object(sys, 'argv', arguments + extra), patch('sys.stderr', io.StringIO()):
                    self.assertEqual(main(), 1)
                self.assertFalse(pgn.exists())
                self.assertFalse(output.exists())
            for extra in [['--min-elo', '10', '--sprt', '0', '10'],
                          ['--min-elo', '10', '--sequential-check-pairs', '1']]:
                with patch.object(sys, 'argv', arguments + extra), patch('sys.stderr', io.StringIO()):
                    with self.assertRaises(SystemExit):
                        main()
            async def cap_fixture(commands, openings, args, report, stream):
                report['games'] = pair_records([(1, 1)])
                report['summary'] = match_summary(report['games'], args)
            for extra in [['--min-elo', '10'], ['--sprt', '0', '10', '--sprt-min-elo-stop',
                                               '--sprt-alpha', '.1', '--sprt-beta', '.04']]:
                with patch.object(sys, 'argv', arguments + extra), \
                        patch('maxima_runner.matches.run_match', cap_fixture), redirect_stdout(io.StringIO()):
                    self.assertEqual(main(), 0)
                report = json.loads(output.read_text())
                self.assertEqual(report['stop_reason'], 'maximum_pairs')
                self.assertEqual(report['summary']['minimum_gain']['decision'], 'inconclusive')
                if report['sprt']:
                    self.assertEqual(report['sprt']['alpha'], .05)
                    self.assertEqual(report['sprt']['beta'], .02)
                    self.assertEqual(report['minimum_gain']['alpha'], .02)
                    self.assertEqual(report['summary']['sprt']['decision'], 'inconclusive')
                output.unlink()
                pgn.unlink()
