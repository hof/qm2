"""Pair sampling, clocks, terminal rules, and independent PGN replay."""
import asyncio
from contextlib import asynccontextmanager
import io
import contextlib
import sys
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import chess
import chess.engine

from maxima_runner.matches import (read_openings, paired_summary, play_game,
                                   read_match_pgn, run_match, main)
from maxima_runner.variants import royal_losers_board


class scripted_engine:
    def __init__(self, moves, delay=0):
        self.moves = iter(moves)
        self.delay = delay
        self.limits = []

    async def search(self, board, limit, **kwargs):
        self.limits.append(limit)
        await asyncio.sleep(self.delay)
        return chess.engine.BestMove(chess.Move.from_uci(next(self.moves)), None), 0


class match_tests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'openings.json'

    def openings(self, variant, entries):
        self.path.write_text(json.dumps({'variant': variant, 'openings': entries}))
        return read_openings(self.path, variant)

    async def play(self, engines, board=None, **kwargs):
        settings = dict(a_white=True, seconds=1, increment=0.1, max_plies=4,
                        pair=0, opening='fixture', variant='standard')
        settings.update(kwargs)
        return await play_game(engines, board if board is not None else chess.Board(), **settings)

    def test_opening_validation_and_history(self):
        board = self.openings('standard', [{'id': 'line', 'moves': ['e2e4', 'e7e5']}])[0][1]
        self.assertEqual(len(board.move_stack), 2)
        with self.assertRaisesRegex(ValueError, 'selected variant'):
            read_openings(self.path, 'losers')
        for entries in ([{'id': 'x', 'moves': ['e2e5']}],
                        [{'id': 'x'}, {'id': 'x'}],
                        [{'id': 'x', 'fen': '8/8/8/8/8/8/8/K6k w - - 0 1',
                          'moves': ['a1a2']}], [{'id': 'x', 'moves': 'e2e4'}]):
            with self.assertRaises(ValueError):
                self.openings('standard', entries)

    def test_losers_compulsory_capture_opening(self):
        with self.assertRaises(ValueError):
            self.openings('losers', [{'id': 'x', 'fen': '7k/8/8/8/8/8/p7/R6K w - - 0 1',
                                     'moves': ['a1b1']}])

    async def test_color_swap_clocks_increment_and_ply_cap(self):
        a = scripted_engine(['f7f6', 'g7g5'])
        b = scripted_engine(['e2e4', 'd1h5'])
        # Use a legal mate in two, assigning A black.
        board = chess.Board()
        board.push_uci('e2e4')
        board.push_uci('f7f6')
        board.push_uci('d2d4')
        a = scripted_engine(['g7g5'])
        b = scripted_engine(['d1h5'])
        game, record = await self.play([a, b], board, a_white=False, max_plies=2)
        self.assertEqual(record['result'], '1-0')
        self.assertEqual(record['a_color'], 'black')
        self.assertEqual(game.headers['White'], 'B')
        self.assertEqual(len(a.limits), 1)
        self.assertEqual(a.limits[0].black_clock, 1)
        self.assertEqual(a.limits[0].white_inc, 0.1)
        self.assertGreater(record['clocks']['white'], 1)
        replay = read_match_pgn(io.StringIO(str(game)))
        self.assertFalse(replay.errors)
        self.assertEqual(replay.end().board().fen(), board.fen())
        self.assertIn('[%clk', str(game))

    async def test_ply_cap_is_unfinished(self):
        _, record = await self.play([scripted_engine(['e2e4']), scripted_engine([])], max_plies=1)
        self.assertEqual(record['result'], '*')

    async def test_clock_forfeit_rejects_late_move(self):
        game, record = await self.play([scripted_engine(['e2e4'], 0.02), scripted_engine([])],
                                       seconds=0.001)
        self.assertEqual(record['result'], '0-1')
        self.assertEqual(record['termination'], 'time forfeit')
        self.assertEqual(len(list(game.mainline_moves())), 0)

    async def test_illegal_move_is_not_pushed(self):
        board = chess.Board()
        game, record = await self.play([scripted_engine(['e2e5']), scripted_engine([])], board)
        self.assertEqual(record['result'], '*')
        self.assertIn('no legal move', record['error'])
        self.assertEqual(game.headers['Termination'], 'engine failure')
        self.assertEqual(board.fen(), chess.STARTING_FEN)

    async def test_losers_terminal_and_pgn_variant(self):
        board = royal_losers_board('7k/8/8/8/8/8/p7/7K w - - 0 1')
        game, record = await self.play([], board, variant='losers')
        self.assertEqual(record['result'], '1-0')
        replay = read_match_pgn(io.StringIO(str(game)))
        self.assertFalse(replay.errors)
        self.assertIs(type(replay.board()), royal_losers_board)
        self.assertEqual(replay.board().outcome().result(), '1-0')

    async def test_losers_capture_pgn_replays_without_registry_change(self):
        board = royal_losers_board('7k/8/8/8/8/8/p7/R6K w - - 0 1')
        game, _ = await self.play([scripted_engine(['a1a2']), scripted_engine([])], board,
                                  variant='losers', max_plies=1)
        replay = read_match_pgn(io.StringIO(str(game)))
        self.assertFalse(replay.errors)
        self.assertIs(type(replay.end().board()), royal_losers_board)
        self.assertEqual(replay.end().board().fen(), board.fen())
        self.assertNotIn('Royal losers', chess.Board.aliases)

    def test_paired_confidence_ignores_incomplete_pairs(self):
        games = [{'pair': 0, 'a_color': 'white', 'result': '1-0'},
                 {'pair': 0, 'a_color': 'black', 'result': '0-1'},
                 {'pair': 1, 'a_color': 'white', 'result': '1/2-1/2'},
                 {'pair': 1, 'a_color': 'black', 'result': '*'}]
        summary = paired_summary(games)
        self.assertEqual(summary['completed_pairs'], 1)
        self.assertEqual(summary['a_score'], 1)
        self.assertEqual(summary['wins'], 2)
        self.assertEqual(summary['unfinished'], 1)
        self.assertIsNone(summary['score_interval'])
        self.assertIsNone(paired_summary([])['score_interval'])
        balanced = [{'pair': pair, 'a_color': color, 'result': '1/2-1/2'}
                    for pair in range(100) for color in ('white', 'black')]
        low, high = paired_summary(balanced)['score_interval']
        self.assertLess(low, 0.5)
        self.assertGreater(high, 0.5)
        self.assertLess(high - low, 0.28)

    def test_cli_preserves_existing_outputs_and_rejects_invalid_inputs(self):
        self.openings('standard', [{'id': 'x'}])
        pgn = Path(self.directory.name) / 'games.pgn'
        report = Path(self.directory.name) / 'games.json'
        arguments = ['match:run', '--engine-a', sys.executable, '--engine-b', sys.executable,
                     '--openings', str(self.path), '--pgn', str(pgn), '--json', str(report),
                     '--pairs', '1']
        pgn.write_text('preserve me')
        with patch.object(sys, 'argv', arguments), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(), 1)
        self.assertEqual(pgn.read_text(), 'preserve me')
        self.assertFalse(report.exists())
        pgn.unlink()
        self.path.write_text('{"variant": "losers", "openings": []}')
        with patch.object(sys, 'argv', arguments), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(main(), 1)
        self.assertFalse(pgn.exists())
        self.assertFalse(report.exists())

    async def test_selection_is_repeatable_and_records_survive_engine_failure(self):
        openings = self.openings('standard', [
            {'id': str(i), 'fen': '7k/8/8/8/8/8/8/K7 w - - 0 1'} for i in range(5)])
        args = SimpleNamespace(seed=17, pairs=3, seconds=1, increment=0,
            max_plies=2, variant='standard', timeout=1, hash_mb=1,
            json=Path(self.directory.name) / 'report.json')

        class fake_session(scripted_engine):
            identity = ['name fixture']
            protocol = SimpleNamespace(options={})
            async def configure(self, settings):
                return {}

        @asynccontextmanager
        async def fake_open(*args):
            yield fake_session(['e2e5'])

        reports = []
        with patch('maxima_runner.uci.open_engine', fake_open):
            for _ in range(2):
                report = {'games': []}
                await run_match([['a'], ['b']], openings, args, report, io.StringIO())
                reports.append(report)
            self.assertEqual(reports[0], reports[1])
            self.assertEqual(len(set(reports[0]['selected_openings'])), 3)
            self.assertEqual(reports[0]['summary']['completed_pairs'], 3)
            args.pairs = 1
            stream = io.StringIO()
            report = {'games': []}
            with self.assertRaisesRegex(RuntimeError, 'no legal move'):
                await run_match([['a'], ['b']], [('bad', chess.Board())], args, report, stream)
            self.assertEqual(report['games'][0]['result'], '*')
            self.assertEqual(json.loads(args.json.read_text())['games'], report['games'])
            self.assertEqual(read_match_pgn(io.StringIO(stream.getvalue())).headers['Result'], '*')

    async def test_repetition_history_claim_precedes_search(self):
        board = chess.Board()
        for move in ['g1f3', 'g8f6', 'f3g1', 'f6g8'] * 2:
            board.push_uci(move)
        game, record = await self.play([], board)
        self.assertEqual(record['result'], '1/2-1/2')
        replay = read_match_pgn(io.StringIO(str(game)))
        self.assertTrue(replay.end().board().can_claim_threefold_repetition())

    @unittest.skipUnless(os.environ.get('MAXIMA_ENGINE'), 'requires MAXIMA_ENGINE')
    async def test_real_engine_paired_games_both_variants(self):
        for variant in ('standard', 'losers'):
            openings = self.openings(variant, [{'id': 'king-ending',
                'fen': '7k/8/8/8/8/8/P7/K7 w - - 0 1'}])
            args = SimpleNamespace(seed=17, pairs=1, seconds=0.1, increment=0,
                max_plies=100, variant=variant, timeout=3, hash_mb=1,
                json=Path(self.directory.name) / (variant + '.json'))
            report = {'games': []}
            stream = io.StringIO()
            command = [os.environ['MAXIMA_ENGINE']]
            await run_match([command, command], openings, args, report, stream)
            self.assertEqual(report['selected_openings'], ['king-ending'])
            self.assertEqual([game['a_color'] for game in report['games']], ['white', 'black'])
            self.assertEqual(len(report['games']), 2)
            self.assertTrue(all(game['result'] != '*' for game in report['games']))
            pgn = io.StringIO(stream.getvalue())
            for _ in range(2):
                game = read_match_pgn(pgn)
                self.assertFalse(game.errors)
                for node in game.mainline():
                    self.assertTrue(node.parent.board().is_legal(node.move))
