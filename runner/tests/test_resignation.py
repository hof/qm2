"""Shared loss evidence and real UCI stop/clock contracts."""
import asyncio
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

import chess
import chess.engine

from maxima_runner import fics, lichess, resignation
from maxima_runner.uci import open_engine
from test_lichess import game_full, stub_api
from test_fics_game import start
from test_fics_policy import fake_connection


def search_info(board, move, reply, score=-900, **extras):
    return dict(score=chess.engine.PovScore(chess.engine.Cp(score), board.turn), depth=18,
                pv=[move, reply], **extras)


def losing_policy(scores=(-800, -850, -900, -950)):
    policy = resignation.loss_policy()
    board = chess.Board()
    board.fullmove_number = 21
    for index, score in enumerate(scores):
        move = next(iter(board.legal_moves))
        after = board.copy()
        after.push(move)
        reply = next(iter(after.legal_moves))
        info = search_info(board, move, reply, score)
        policy.on_search(board, move, info, 'engine')
        if index < len(scores) - 1:
            board.push(move)
            policy.on_reply(board, reply, 3)
            board.push(reply)
    return policy, board, info


def decision(policy, board, info, **changes):
    args = dict(our_rating=2000, their_rating=1800, bot=None, speed='blitz', ours=100,
                theirs=100, increment=2, info=info, board=board)
    args.update(changes)
    return policy.decision(**args)


class evidence_tests(unittest.TestCase):
    def test_move_boundary_and_disabled_policy(self):
        policy, board, info = losing_policy()
        policy.scores[-1] = (20, -950)
        self.assertIsNone(decision(policy, board, info))
        policy, board, info = losing_policy()
        policy.settings['enabled'] = False
        self.assertIsNone(decision(policy, board, info))
        self.assertEqual(policy.ceiling(False), 30)

    def test_loss_and_clock_audit(self):
        policy, board, info = losing_policy()
        result = decision(policy, board, info)
        self.assertEqual(result['required_reserve'], 50)
        self.assertEqual(result['prediction_matches'], [True] * 3)
        policy.sent = True
        self.assertIsNone(decision(policy, board, info))

    def test_improvement_flat_scores_shallow_bounds_and_book(self):
        for scores in ((-950, -900, -850, -800), (-900,) * 4, (-800, -1000, -900, -1100)):
            policy, board, info = losing_policy(scores)
            self.assertIsNone(decision(policy, board, info))
        for extras, kind in (({'depth': 15}, 'engine'), ({'lowerbound': True}, 'engine'),
                             ({'upperbound': True}, 'engine'), ({}, 'book'), ({'source': 'book'}, 'engine')):
            policy, board, info = losing_policy()
            move, reply = info['pv']
            changed = dict(info, **extras)
            # A distinct completed own turn must break evidence.
            board.fullmove_number += 1
            policy.on_search(board, move, changed, kind)
            self.assertEqual(len(policy.scores), 0)

    def test_reply_predictions_and_unavailable_times(self):
        for seconds in (3, None, -1):
            policy, board, info = losing_policy()
            policy.on_reply(board, next(iter(board.legal_moves)), seconds)
            self.assertIsNone(decision(policy, board, info))
        policy, board, info = losing_policy()
        policy.pending = None
        policy.on_reply(board, next(iter(board.legal_moves)), 3)
        self.assertIsNone(decision(policy, board, info))

    def test_ratings_and_confirmed_bot_clock_exception(self):
        policy, board, info = losing_policy()
        self.assertIsNone(decision(policy, board, info, their_rating=1799))
        self.assertIsNotNone(decision(policy, board, info, their_rating=None))
        self.assertIsNone(decision(policy, board, info, our_rating=None))
        self.assertIsNone(decision(policy, board, info, ours=180, theirs=70))
        self.assertIsNotNone(decision(policy, board, info, ours=180, theirs=70, bot=True, their_rating=1200))

    def test_bullet_flag_chances_increment_changes_missing_clocks(self):
        policy, board, info = losing_policy()
        self.assertIsNone(decision(policy, board, info, speed='bullet', ours=100, theirs=30))
        self.assertIsNone(decision(policy, board, info, increment=0, theirs=80))
        self.assertIsNotNone(decision(policy, board, info, increment=3, theirs=40, ours=30))
        for key in ('ours', 'theirs', 'increment'):
            self.assertIsNone(decision(policy, board, info, **{key: None}))
        self.assertIsNone(decision(policy, board, info, speed='unknown', theirs=70))

    def test_tablebase_safety_and_resets(self):
        policy, board, info = losing_policy()
        for category in ('draw', 'win', 'cursed-win', 'blessed-loss'):
            self.assertIsNone(decision(policy, board, dict(info, tablebase={'category': category})))
        endgame = chess.Board('8/8/8/8/8/4k3/6r1/4K3 w - - 0 24')
        draw = dict(info, tbhits=1, score=chess.engine.PovScore(chess.engine.Cp(-10), chess.WHITE))
        self.assertIsNone(decision(policy, endgame, draw))
        policy.reset()
        self.assertIsNone(decision(policy, board, info))
        self.assertFalse(policy.sent)
        self.assertIsNone(policy.pending)

    def test_duplicate_search_and_explicit_casual_cap(self):
        policy, board, info = losing_policy()
        original = list(policy.scores)
        policy.on_search(board, info['pv'][0], info, 'engine')
        self.assertEqual(list(policy.scores), original)
        self.assertEqual(policy.ceiling(False), 30)
        self.assertIsNone(policy.ceiling(True))
        self.assertIsNone(policy.ceiling(None))
        policy.reset()
        self.assertIsNone(policy.ceiling(False))

    def test_repeated_search_updates_score_without_adding_confirmation(self):
        policy, board, info = losing_policy()
        updated = dict(info, score=chess.engine.PovScore(chess.engine.Cp(0), board.turn))
        self.assertFalse(policy.on_search(board, info['pv'][0], updated, 'engine'))
        self.assertEqual(len(policy.scores), 4)
        self.assertEqual(policy.scores[-1][1], 0)
        self.assertIsNone(decision(policy, board, updated))

    def test_settings_both_servers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'settings.json'
            for load in (fics.load_settings, lichess.load_settings):
                for bad in (None, [], {'enabled': 1}, {'min_depth': 0}, {'loss_cp': 800},
                            {'confirmations': True}, {'rating_margin': -1}, {'unrated_ceiling': float('nan')},
                            {'unrated_ceiling': 0}, {'unknown': 1}):
                    path.write_text(json.dumps({'resignation': bad}))
                    with self.assertRaises(ValueError):
                        load(path)
                path.write_text(json.dumps({'resignation': {'enabled': False, 'unrated_ceiling': 10}}))
                self.assertEqual(load(path)['resignation']['unrated_ceiling'], 10)


clock_engine = '''
import sys
for line in sys.stdin:
    command = line.strip()
    with open(sys.argv[1], 'a') as log:
        log.write(command + '\\n')
    if command == 'uci':
        print('uciok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command.startswith('go '):
        print('info depth 18 score cp -900 pv e2e4 e7e5', flush=True)
        if sys.argv[2] == 'quick':
            print('bestmove e2e4', flush=True)
    elif command == 'stop':
        print('bestmove e2e4 ponder e7e5', flush=True)
    elif command == 'quit':
        break
'''


class ceiling_tests(unittest.IsolatedAsyncioTestCase):
    async def test_donated_clock_stops_collects_bestmove_and_keeps_allocation(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
            script.write_text(clock_engine)
            async with open_engine([sys.executable, str(script), str(log), 'slow'], directory, 2) as session:
                started = time.monotonic()
                result = await session.play(chess.Board(), chess.engine.Limit(white_clock=100000, black_clock=100000),
                                            ceiling=0.05)
                self.assertLess(time.monotonic() - started, 1)
                self.assertEqual(result.move.uci(), 'e2e4')
                self.assertEqual(result.info['depth'], 18)
            commands = log.read_text().splitlines()
            self.assertIn('stop', commands)
            go = next(line for line in commands if line.startswith('go '))
            self.assertIn('wtime 100000000', go)
            self.assertNotIn('movetime', go)

    async def test_engine_ignoring_planned_stop_has_a_bounded_deadline(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
            script.write_text(clock_engine.replace("print('bestmove e2e4 ponder e7e5', flush=True)", 'pass'))
            async with open_engine([sys.executable, str(script), str(log), 'slow'], directory, 0.1) as session:
                started = time.monotonic()
                with self.assertRaises(asyncio.TimeoutError):
                    await session.play(chess.Board(), chess.engine.Limit(white_clock=100000, black_clock=100000),
                                       ceiling=0.03, timeout=100000)
                self.assertLess(time.monotonic() - started, 0.7)

    async def test_normal_allocation_can_finish_below_ceiling_and_rated_has_no_stop(self):
        for ceiling in (0.5, None):
            with tempfile.TemporaryDirectory() as directory:
                script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
                script.write_text(clock_engine)
                async with open_engine([sys.executable, str(script), str(log), 'quick'], directory, 2) as session:
                    result = await session.play(chess.Board(), chess.engine.Limit(white_clock=100000, black_clock=100000),
                                                ceiling=ceiling)
                    self.assertEqual(result.move.uci(), 'e2e4')
                self.assertNotIn('stop', log.read_text().splitlines())


class client_tests(unittest.IsolatedAsyncioTestCase):
    def lichess_game(self, rated=False):
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None, online_tablebase=False)
        bot.id = 'maximaengine'
        game = lichess.lichess_game(bot, 'g1')
        full = game_full()
        full['rated'] = rated
        game.setup(full)
        policy, board, info = losing_policy()
        game.board = board
        game.loss = policy
        game.loss.last_search = None
        game.state = dict(full['state'])
        bot.game = game
        return game, board, info

    async def test_lichess_one_request_and_server_confirmed_finish(self):
        for rated in (True, False):
            game, board, info = self.lichess_game(rated)
            with patch('maxima_runner.move_choice.choose_move', return_value=(info['pv'][0], info, 'engine')) as choose:
                await game.play(board.copy(), game.state)
                await game.play(board.copy(), game.state)
                self.assertEqual(choose.call_args.kwargs['ceiling'], None if rated else 30)
            self.assertFalse(game.over)
            self.assertEqual([path for path, _ in game.bot.api.posts if path.endswith('/resign')],
                             ['/api/bot/game/g1/resign'])
            await game.finish('resign', 'black')
            await game.finish('resign', 'black')
            self.assertTrue(game.over)
            self.assertEqual(game.bot.played, 1)

    async def test_lichess_termination_or_reset_during_search(self):
        for ending in ('finish', 'reset'):
            game, board, info = self.lichess_game()

            async def choose(*args, **kwargs):
                if ending == 'finish':
                    await game.finish('resign', 'black')
                else:
                    game.generation = object()
                    game.loss.reset()
                return info['pv'][0], info, 'engine'

            with patch('maxima_runner.move_choice.choose_move', side_effect=choose):
                await game.play(board.copy(), game.state)
            self.assertFalse(any('/resign' in path or '/move/' in path for path, _ in game.bot.api.posts))

    async def test_lichess_takeback_duplicate_and_increment_timing(self):
        game, board, info = self.lichess_game()
        game.initial = chess.Board()
        game.board = chess.Board()
        game.color = chess.BLACK  # e4 is an opponent reply
        game.searched = 1
        game.state = dict(game.state, moves='', wtime=180000)
        game.reply_clock = (180000, 2000)
        state = dict(game.state, moves='e2e4', wtime=179000, status='started')
        await game.on_state(state)
        self.assertEqual(list(game.loss.reply_times), [3, 3, 3])  # 1 s + 2 s increment
        matches = list(game.loss.matches)
        await game.on_state(state)
        self.assertEqual(list(game.loss.matches), matches)
        state.update(moves='', winc=3000)
        await game.on_state(state)
        self.assertFalse(game.loss.scores)
        self.assertFalse(game.loss.reply_times)
        game.searched = 1
        state.update(moves='e2e4', wtime=178000, winc=4000)
        await game.on_state(state)
        self.assertFalse(game.loss.reply_times)  # changed increment is not a measured move time

    async def test_fics_resignation_rated_cap_and_stale_position(self):
        from types import SimpleNamespace
        from maxima_runner import fics_game
        for rated in ('rated', 'unrated', None):
            subject = fics.engine_player(None, lambda line: None, online_tablebase=False)
            subject.connection = fake_connection()
            policy, board, info = losing_policy()
            subject.loss = policy
            subject.loss.last_search = None
            subject.game, subject.token = 7, object()
            subject.our_color = board.turn
            subject.history = SimpleNamespace(board=board)
            subject.opponent = chess.engine.Opponent('Someone', None, 1800, False)
            subject.created = {'rated': rated, 'type': 'blitz', 'white_rating': '2000', 'black_rating': '1800'} if rated else None
            subject.record = fics_game.game_record({})
            position = SimpleNamespace(white_ms=180000, black_ms=180000, increment_seconds=2)
            subject.latest_position = position
            with patch('maxima_runner.move_choice.choose_move', return_value=(info['pv'][0], info, 'engine')) as choose:
                await subject.play(position, board.copy())
                await subject.play(position, board.copy())
                self.assertEqual(choose.call_args.kwargs['ceiling'], 30 if rated == 'unrated' else None)
            self.assertEqual(subject.connection.sent, ['resign'] if rated else ['h7f8', 'h7f8'])
            self.assertEqual(subject.game, 7)  # server game-end still owns PGN and lifecycle

    async def test_fics_takeback_and_new_game_clear_evidence(self):
        from maxima_runner import fics_game
        subject = fics.engine_player(None, lambda line: None)
        subject.connection = fake_connection()
        # Opponent's turn prevents search tasks in this fixture.
        position = fics_game.style12.parse(start.replace('guestNinja 1', 'guestNinja -1'))
        subject.loss, _, _ = losing_policy()
        subject.on_board(position)
        self.assertFalse(subject.loss.scores)
        subject.cancel()
        subject.loss, _, _ = losing_policy()
        # A resync from a different board invalidates every old evaluation.
        subject.history.board.push_uci('e2e4')
        subject.on_board(position)
        self.assertFalse(subject.loss.scores)
        subject.cancel()


class report_tests(unittest.IsolatedAsyncioTestCase):
    async def test_incomplete_final_report_cannot_reuse_a_score(self):
        with tempfile.TemporaryDirectory() as directory:
            script, log = Path(directory) / 'engine.py', Path(directory) / 'commands'
            script.write_text(clock_engine.replace("print('bestmove e2e4', flush=True)",
                                                  "print('info depth 19 pv e2e4', flush=True)\n            print('bestmove e2e4', flush=True)"))
            async with open_engine([sys.executable, str(script), str(log), 'quick'], directory, 2) as session:
                result = await session.play(chess.Board(), chess.engine.Limit(white_clock=1000, black_clock=1000))
            self.assertIn('score', result.info)  # python-chess aggregated an older score
            self.assertNotIn('score', result.info['_resignation'])
            policy, board, info = losing_policy()
            board.fullmove_number += 1
            policy.on_search(board, info['pv'][0], result.info, 'engine')
            self.assertFalse(policy.scores)


class reset_tests(unittest.IsolatedAsyncioTestCase):
    async def test_lichess_reconnect_clears_evidence_and_pending_request(self):
        from unittest.mock import AsyncMock
        api = stub_api()
        bot = lichess.lichess_bot(api, None, lambda line: None)
        game = lichess.lichess_game(bot, 'g1')
        game.loss, _, _ = losing_policy()
        game.loss.sent = True
        game.searched = 6
        game.reply_clock = (180000, 2000)
        attempts = 0

        async def stream(path):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise OSError('fixture disconnect')
            self.assertFalse(game.loss.scores)
            self.assertFalse(game.loss.sent)
            self.assertIsNone(game.searched)
            self.assertIsNone(game.reply_clock)
            game.over = True
            yield {'type': 'fixture'}

        api.stream = stream
        with patch('maxima_runner.lichess.asyncio.sleep', new_callable=AsyncMock):
            await game.follow()
        self.assertEqual(attempts, 2)

    async def test_fics_login_resets_evidence(self):
        subject = fics.engine_player(None, lambda line: None)
        subject.connection = fake_connection()
        subject.loss, _, _ = losing_policy()
        subject.loss.sent = True
        await subject.on_login('GuestTEST')
        self.assertFalse(subject.loss.scores)
        self.assertFalse(subject.loss.sent)

    async def test_lichess_clock_donation_invalidates_reply_timing(self):
        game = lichess.lichess_game(lichess.lichess_bot(stub_api(), None, lambda line: None), 'g1')
        game.bot.id = 'maximaengine'
        full = game_full()
        game.setup(full)
        game.color = chess.BLACK
        game.searched = 0
        game.state = dict(full['state'])
        game.reply_clock = (180000, 2000)
        state = dict(full['state'], wtime=190000)
        await game.on_state(state)
        self.assertIsNone(game.reply_clock)
        game.searched = 1
        await game.on_state(dict(state, moves='e2e4', wtime=189000))
        self.assertFalse(game.loss.reply_times)
