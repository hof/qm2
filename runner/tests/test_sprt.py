"""Independent reference parity, pair integrity, and SPRT stopping behavior."""
from contextlib import asynccontextmanager, redirect_stdout
import io
import json
import math
import os
from pathlib import Path
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import chess

from maxima_runner.matches import main, match_summary, run_match, match_game_type
from maxima_runner.match_options import engine_profile
from maxima_runner.variants import royal_losers_board
from maxima_runner.sprt import pentanomial_counts, sprt_summary, validate_sprt
from maxima_runner.third_party.fastchess.llr import logistic_llr


def pair_records(scores):
    games = []
    for index, (white, black) in enumerate(scores):
        for color, score in [('white', white), ('black', black)]:
            result = ['0-1', '1/2-1/2', '1-0'][score if color == 'white' else 2 - score]
            games.append({'pair': index, 'a_color': color, 'result': result, 'error': None})
    return games


class sprt_tests(unittest.TestCase):
    def test_pair_categories_color_swap_incomplete_and_failures(self):
        games = pair_records([(0, 0), (0, 1), (0, 2), (1, 1), (1, 2), (2, 2)])
        self.assertEqual(pentanomial_counts(games), [1, 1, 2, 1, 1])
        self.assertEqual(pentanomial_counts(games[:-1]), [1, 1, 2, 1, 0])
        games[-1]['result'] = '*'
        self.assertEqual(pentanomial_counts(games), [1, 1, 2, 1, 0])
        games[-1]['result'] = '0-1'
        games[-1]['error'] = 'fixture failure'
        self.assertEqual(pentanomial_counts(games), [1, 1, 2, 1, 0])
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            pentanomial_counts(games + [games[0]])
        with self.assertRaises(ValueError):
            pentanomial_counts([{'pair': 0, 'a_color': 'green', 'result': '1-0'}])

    def test_parameters_and_thresholds(self):
        for parameters in [(5, 0, .05, .05), (0, math.inf, .05, .05),
                           (math.nan, 5, .05, .05), (-201, 5, .05, .05),
                           (0, 5, 0, .05), (0, 5, .6, .5)]:
            with self.subTest(parameters=parameters), self.assertRaises(ValueError):
                validate_sprt(*parameters)
        empty = sprt_summary([], 0, 5)
        self.assertEqual(empty['llr'], 0)
        self.assertEqual(empty['decision'], 'continue')
        self.assertAlmostEqual(empty['lower_bound'], -math.log(19))
        self.assertAlmostEqual(empty['upper_bound'], math.log(19))
        for scores, decision in [([(2, 2)] * 300, 'accept_h1'),
                                 ([(0, 0)] * 300, 'accept_h0')]:
            summary = sprt_summary(pair_records(scores), 0, 5)
            self.assertEqual(summary['decision'], decision)
            self.assertEqual(sprt_summary(pair_records(scores)[:-1], 0, 5)['decision'], 'continue')

    def test_pinned_fishtest_reference_vectors(self):
        path = Path(__file__).parent / 'data/sprt_reference.json'
        for case in json.loads(path.read_text())['cases']:
            with self.subTest(counts=case['counts'], bounds=case['bounds']):
                llr = logistic_llr(*case['bounds'], case['counts'])
                self.assertAlmostEqual(llr, case['llr'], delta=2e-7)
        # Mirrored observations and hypotheses reverse likelihood direction.
        counts = [3, 20, 50, 24, 6]
        self.assertAlmostEqual(logistic_llr(0, 5, counts),
                               -logistic_llr(-5, 0, list(reversed(counts))), delta=1e-9)

    def test_sprt_reports_no_fixed_sample_confidence_interval(self):
        args = SimpleNamespace(sprt=[0, 5], sprt_alpha=.05, sprt_beta=.05, pairs=300)
        summary = match_summary(pair_records([(2, 2)] * 300), args)
        self.assertIsNone(summary['score_interval'])
        self.assertIsNone(summary['confidence'])
        self.assertEqual(summary['sprt']['decision'], 'accept_h1')


class sprt_match_tests(unittest.IsolatedAsyncioTestCase):
    async def test_pair_boundary_decisions_and_inconclusive_cap(self):
        class session:
            identity = ['name fixture']
            protocol = SimpleNamespace(options={})
            async def configure(self, settings):
                return {}
        @asynccontextmanager
        async def open_fixture(*unused):
            yield session()
        with tempfile.TemporaryDirectory() as directory:
            for score, bounds, maximum, decision in [(2, [0, 50], 30, 'accept_h1'),
                                                    (0, [0, 50], 30, 'accept_h0'),
                                                    (1, [-5, 5], 10, None)]:
                args = SimpleNamespace(pairs=maximum, sprt=bounds, sprt_alpha=.05,
                    sprt_beta=.05, seed=1, timeout=1, hash_mb=1, variant='standard',
                    seconds=1, increment=0, max_plies=1,
                    json=Path(directory) / 'results.json')
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
                self.assertEqual(len(report['games']) % 2, 0)
                self.assertEqual(report.get('stop_reason'), decision)
                self.assertEqual(report.get('stopped_early', False), decision is not None)
                if decision:
                    prior = sprt_summary(report['games'][:-2], *bounds)
                    self.assertEqual(prior['decision'], 'continue')
                else:
                    self.assertEqual(len(report['games']), 2 * maximum)
                self.assertEqual(json.loads(args.json.read_text())['games'], report['games'])


    @unittest.skipUnless(os.environ.get('MAXIMA_ENGINE'), 'requires MAXIMA_ENGINE')
    async def test_real_sprt_profiles_and_terminals_both_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            for variant, board in [
                    ('standard', chess.Board('7k/5Q2/6K1/8/8/8/8/8 w - - 0 1')),
                    ('losers', royal_losers_board('7k/8/8/8/8/8/p7/R6K w - - 0 1'))]:
                args = SimpleNamespace(pairs=1, sprt=[0, 5], sprt_alpha=.05,
                    sprt_beta=.05, seed=1, timeout=3, hash_mb=1, variant=variant,
                    seconds=1, increment=.01, max_plies=8,
                    profiles=[engine_profile([('LMR', 'true')], []),
                              engine_profile([('LMR', 'false')], [])],
                    json=Path(directory) / (variant + '.json'))
                report = {'games': []}
                stream = io.StringIO()
                command = [os.environ['MAXIMA_ENGINE']]
                await run_match([command, command], [('fixture', board)], args, report, stream)
                self.assertEqual(len(report['games']), 2)
                self.assertEqual(report['summary']['sprt']['pentanomial'], [0, 0, 1, 0, 0])
                self.assertEqual(report['summary']['sprt']['decision'], 'continue')
                from maxima_runner.matches import read_match_pgn
                pgn = io.StringIO(stream.getvalue())
                for record in report['games']:
                    self.assertIsNone(record['error'])
                    self.assertEqual(record['termination'], 'checkmate' if variant == 'standard'
                                     else 'variant_win')
                    self.assertEqual([engine['options']['LMR'] for engine in record['engines']],
                                     ['true', 'false'])
                    self.assertEqual([engine['options']['Wild'] for engine in record['engines']],
                                     [variant, variant])
                    game = read_match_pgn(pgn)
                    self.assertFalse(game.errors)
                    position = game.board()
                    for move in game.mainline_moves():
                        self.assertTrue(position.is_legal(move))
                        position.push(move)
                    self.assertEqual(position.outcome(claim_draw=True).result(), record['result'])


class sprt_cli_tests(unittest.TestCase):
    def test_cli_validation_before_outputs_and_capped_inconclusive_result(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            opening = root / 'openings.json'
            opening.write_text(json.dumps({'variant': 'standard', 'openings': [{'id': 'fixture'}]}))
            report_path = root / 'report.json'
            pgn = root / 'games.pgn'
            arguments = ['match:run', '--engine-a', sys.executable, '--engine-b', sys.executable,
                         '--openings', str(opening), '--pairs', '1', '--pgn', str(pgn),
                         '--json', str(report_path)]
            for extra in [['--sprt', '5', '0'], ['--sprt', '0', 'nan'],
                          ['--sprt', '0', '5', '--sprt-alpha', '0'],
                          ['--sprt-beta', '.1']]:
                with patch.object(sys, 'argv', arguments + extra), \
                        patch('sys.stderr', io.StringIO()):
                    self.assertEqual(main(), 1)
                self.assertFalse(pgn.exists())
                self.assertFalse(report_path.exists())
            with patch.object(sys, 'argv', arguments + ['--sprt', '0', '5',
                                                       '--sequential-check-pairs', '1']), \
                    patch('sys.stderr', io.StringIO()):
                with self.assertRaises(SystemExit) as failure:
                    main()
                self.assertEqual(failure.exception.code, 2)
            async def cap_fixture(commands, openings, args, report, stream):
                report['games'] = pair_records([(1, 1)])
                report['summary'] = match_summary(report['games'], args)
            with patch.object(sys, 'argv', arguments + ['--sprt', '0', '5']), \
                    patch('maxima_runner.matches.run_match', cap_fixture), \
                    redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 0)
            report = json.loads(report_path.read_text())
            self.assertTrue(report['complete'])
            self.assertFalse(report['stopped_early'])
            self.assertEqual(report['stop_reason'], 'maximum_pairs')
            self.assertEqual(report['summary']['sprt']['decision'], 'inconclusive')
            self.assertEqual(report['sprt']['elo_model'], 'logistic')
            self.assertIsNone(report['summary']['score_interval'])
