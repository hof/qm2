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
                                   read_match_pgn, run_match, main, win_adjudicator)
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


class scored_engine(scripted_engine):
    def __init__(self, moves, scores):
        super().__init__(moves)
        self.scores = iter(scores)

    async def search(self, board, limit, **kwargs):
        score = next(self.scores)
        if kwargs.get('on_info') is not None and score is not None:
            kwargs['on_info']({'score': chess.engine.PovScore(chess.engine.Cp(score), board.turn)}, 0)
        return await super().search(board, limit, **kwargs)


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

    async def test_win_adjudication_requires_both_engines_and_records_evidence(self):
        for white_wins in (True, False):
            sign = 1 if white_wins else -1
            white = scored_engine(['e2e4', 'g1f3', 'f1c4'], [1200 * sign] * 3)
            black = scored_engine(['e7e5', 'b8c6', 'g8f6'], [-1100 * sign] * 3)
            game, record = await self.play([black, white], a_white=False, max_plies=10,
                adjudicate_win_cp=1000, adjudicate_min_move=1)
            self.assertEqual(record['result'], '1-0' if white_wins else '0-1')
            self.assertEqual(record['termination'], 'win adjudication')
            self.assertIsNone(record['error'])
            self.assertEqual(record['plies'], 6)
            evidence = record['adjudication']['evidence']
            self.assertEqual([entry['engine_color'] for entry in evidence], ['white', 'black'] * 3)
            self.assertTrue(all(entry['score_white_cp'] * sign >= 1000 for entry in evidence))
            replay = read_match_pgn(io.StringIO(str(game)))
            self.assertFalse(replay.errors)
            self.assertEqual(replay.headers['Termination'], 'win adjudication')
            self.assertFalse(replay.end().board().is_game_over(claim_draw=True))
            self.assertIn('Win adjudication', replay.end().comment)

    def test_win_adjudication_resets_on_missing_bounds_disagreement_and_small_scores(self):
        def info(cp, **extra):
            return {'score': chess.engine.PovScore(chess.engine.Cp(cp), chess.WHITE), **extra}

        for interrupted in ({}, info(1200, lowerbound=True), info(1200, upperbound=True),
                            info(-1200), info(999)):
            judge = win_adjudicator(1000, 4, 20)
            for ply in range(3):
                self.assertIsNone(judge.observe(info(1200), ply % 2 == 0, 20, ply))
            self.assertIsNone(judge.observe(interrupted, False, 20, 3))
            for ply in range(3):
                self.assertIsNone(judge.observe(info(1200), ply % 2 == 0, 21, ply + 4))
            self.assertIsNotNone(judge.observe(info(1200), False, 21, 7))
        judge = win_adjudicator(1000, 2, 20)
        self.assertIsNone(judge.observe(info(1200), True, 19, 1))
        self.assertIsNone(judge.observe(info(1200), False, 19, 2))
        self.assertIsNone(judge.observe(info(1200), True, 20, 3))
        self.assertIsNotNone(judge.observe(info(1200), False, 20, 4))
        mate = {'score': chess.engine.PovScore(chess.engine.Mate(-3), chess.BLACK)}
        judge = win_adjudicator(1000, 2, 1)
        self.assertIsNone(judge.observe(mate, True, 1, 1))
        self.assertEqual(judge.observe(mate, False, 1, 2)['winner'], 'white')

    async def test_win_adjudication_preserves_draws_failures_and_missing_scores(self):
        board = chess.Board()
        for move in ['g1f3', 'g8f6', 'f3g1', 'f6g8', 'g1f3']:
            board.push_uci(move)
        white = scored_engine(['f3g1'], [1200])
        black = scored_engine(['g8f6'], [-1200])
        _, record = await self.play([white, black], board, adjudicate_win_cp=1000,
            adjudicate_win_plies=2, adjudicate_min_move=1)
        self.assertEqual(record['result'], '1/2-1/2')
        self.assertEqual(record['termination'], 'threefold_repetition')
        self.assertIsNone(record['adjudication'])
        for black in (scored_engine(['e7e5'], [None]), scored_engine(['e7e6'], [1200])):
            _, record = await self.play([scored_engine(['e2e4'], [1200]), black],
                max_plies=2, adjudicate_win_cp=1000, adjudicate_win_plies=2, adjudicate_min_move=1)
            self.assertEqual(record['result'], '*')
        _, record = await self.play([scored_engine(['e2e4'], [1200]),
                                     scored_engine(['e7e4'], [-1200])],
            adjudicate_win_cp=1000, adjudicate_win_plies=2, adjudicate_min_move=1)
        self.assertEqual(record['result'], '*')
        self.assertEqual(record['termination'], 'engine failure')
        self.assertIsNotNone(record['error'])
        board = chess.Board()
        board.push_uci('f2f3')
        board.push_uci('e7e5')
        _, record = await self.play([scored_engine(['g2g4'], [1200]),
                                     scored_engine(['d8h4'], [-1200])], board,
            adjudicate_win_cp=1000, adjudicate_win_plies=2, adjudicate_min_move=1)
        self.assertEqual(record['result'], '0-1')
        self.assertEqual(record['termination'], 'checkmate')

        class crashing_engine:
            async def search(self, *args, **kwargs):
                raise chess.engine.EngineTerminatedError('fixture crash')

        _, record = await self.play([scored_engine(['e2e4'], [1200]), crashing_engine()],
            adjudicate_win_cp=1000, adjudicate_win_plies=2, adjudicate_min_move=1)
        self.assertEqual(record['result'], '*')
        self.assertEqual(record['termination'], 'engine failure')
        self.assertIn('fixture crash', record['error'])

    def test_invalid_adjudication_configuration(self):
        for settings in ((0, 6, 20), (1000, 1, 20), (1000, 6, 0)):
            with self.assertRaises(ValueError):
                win_adjudicator(*settings)

    async def test_win_adjudication_through_uci_stream(self):
        from maxima_runner.uci import open_engine
        script = Path(self.directory.name) / 'scored_uci.py'
        script.write_text('''import sys
moves = 'e2e4 e7e5 g1f3 b8c6 f1c4 g8f6'.split()
ply = 0
for line in sys.stdin:
    if line.strip() == 'uci':
        print('id name scored fixture\\nuciok', flush=True)
    elif line.strip() == 'isready':
        print('readyok', flush=True)
    elif line.startswith('position '):
        ply = len(line.split(' moves ', 1)[1].split()) if ' moves ' in line else 0
    elif line.startswith('go '):
        score = 1200 if ply % 2 == 0 else -1200
        print(f'info depth 3 multipv 1 score cp {score} pv {moves[ply]}')
        print(f'info depth 3 multipv 2 score cp {-score}')
        print('info nodes 100 time 1')
        print(f'bestmove {moves[ply]}', flush=True)
    elif line.strip() == 'quit':
        break
''')
        command = [sys.executable, str(script)]
        async with open_engine(command, self.directory.name, 2) as a:
            async with open_engine(command, self.directory.name, 2) as b:
                _, record = await self.play([a, b], max_plies=10, adjudicate_win_cp=1000,
                                             adjudicate_min_move=1)
        self.assertEqual(record['result'], '1-0')
        self.assertEqual(record['termination'], 'win adjudication')
        self.assertEqual(record['plies'], 6)
        self.assertTrue(all(entry['score_white_cp'] == 1200
                            for entry in record['adjudication']['evidence']))

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
