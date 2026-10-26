"""Mixed FICS seeks, compulsory captures, and isolated royal-losers learning."""
from datetime import datetime, timezone
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

import chess
import chess.engine

from maxima_runner import fics, fics_game, learning, learning_store, monthly_pgn, online_tablebase
from maxima_runner.variants import royal_losers_board, variant_game, read_pgn
from test_fics_policy import player


def position(board, relation=-1, number=17):
    rows = [''.join(board.piece_at(chess.square(file, rank)).symbol()
                    if board.piece_at(chess.square(file, rank)) else '-' for file in range(8))
            for rank in reversed(range(8))]
    return fics_game.style12.parse(
        f"<12> {' '.join(rows)} {'W' if board.turn else 'B'} -1 0 0 0 0 {board.halfmove_clock} {number} "
        f"Alpha Beta {relation} 3 0 39 39 180000 180000 {board.fullmove_number} none (0:00.000) none")


def losers_game():
    board = royal_losers_board()
    for move in ['g1f3', 'g8f6', 'f3g1', 'f6g8'] * 5:
        board.push_uci(move)
    game = variant_game.from_board(board)
    game.headers.update(White='Alpha', Black='Beta', WhiteEngine='Maxima', WhiteType='program',
                        LearningColor='white', LearningSource='fics', Event='FICS unrated losers game',
                        UTCDate='2026.10.26', UTCTime='12:00:00', Result='1-0', LearningResults='true')
    return game


class losers_policy_tests(unittest.IsolatedAsyncioTestCase):
    def test_example_configuration_and_invalid_pool_settings(self):
        example = Path(__file__).resolve().parents[2] / 'docs' / 'fics-mixed.json'
        settings = fics.load_settings(example)
        self.assertEqual((settings['seeks_at_once'], settings['losers_seeks_at_once']), (2, 1))
        for key, value in (('losers_seeks_at_once', -1), ('losers', None), ('losers_seeks', [17])):
            import json
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'invalid.json'
                path.write_text(json.dumps({key: value}))
                with self.assertRaises(ValueError):
                    fics.load_settings(path)

    async def test_two_standard_and_one_losers_seek_and_challenge(self):
        subject = player(seeks=['3 0', '3 1', '5 0', '5 1'], seeks_at_once=2,
                         losers_seeks=['3 0 u', '3 1 u', '5 0 u', '5 1 u'], guest=False)
        await subject.on_login('Alpha')
        self.assertEqual(subject.connection.sent[0], 'set f3 losers')
        seeks = [line for line in subject.connection.sent if line.startswith('seek ')]
        self.assertEqual(len(seeks), 3)
        self.assertEqual(sum('losers' in line for line in seeks), 1)
        await subject.handle('Challenge: Beta (1500) Alpha (1500) unrated losers 3 0.')
        self.assertEqual(subject.connection.sent[-1], 'accept Beta')
        standard = player()
        await standard.handle('Challenge: Beta (1500) Alpha (1500) unrated losers 3 0.')
        self.assertEqual(standard.connection.sent[-1], 'decline Beta')

    async def test_losers_only_pool_reseeks(self):
        subject = player(losers_seeks=['5 1'])
        await subject.seek()
        self.assertEqual(subject.connection.sent, ['unseek', 'seek 5 1 losers u'])

    async def test_selects_rules_from_metadata_and_preserves_resync_type(self):
        subject = player(losers=True)
        await subject.handle('Creating: Alpha (1500) Beta (1500) unrated losers 3 0')
        board = royal_losers_board('4k3/7p/8/8/3p4/4P3/8/4K3 w - - 0 1')
        subject.on_board(position(board))
        self.assertIs(type(subject.history.board), royal_losers_board)
        self.assertEqual([move.uci() for move in subject.history.board.legal_moves], ['e3d4'])
        changed = royal_losers_board('4k3/7p/8/8/3P4/8/8/4K3 b - - 0 1')
        self.assertEqual(subject.history.update(position(changed)), 'reset')
        self.assertIs(type(subject.history.board), royal_losers_board)
        self.assertFalse(online_tablebase.covers(subject.history.board))
        await subject.handle('Creating: Alpha (1500) Beta (1500) unrated blitz 3 0')
        subject.on_board(position(chess.Board(), number=18))
        self.assertIs(type(subject.history.board), chess.Board)

    async def test_missing_variant_metadata_is_not_searched(self):
        subject = player(losers=True)
        with self.assertRaisesRegex(ValueError, 'metadata'):
            subject.on_board(position(chess.Board()))
        self.assertIsNone(subject.history)

    async def test_losers_uses_own_learner_and_disables_native_book_and_resignation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'experience'
            standard = learning.learner(root)
            losers = learning.learner(root / 'losers', variant='losers', mood='off')
            subject = player(losers=True)
            subject.learner, subject.losers_learner = standard, losers
            subject.pgn_dir = root / 'games'
            await subject.handle('Creating: Alpha (1500) Beta (1500) unrated losers 3 0')
            subject.on_board(position(royal_losers_board('4k3/7p/8/8/3p4/4P3/8/4K3 w - - 0 1')))
            self.assertIs(subject.game_learner, losers)
            self.assertEqual(subject.game_pgn_dir, root / 'losers' / 'games')
            subject.opponent_ready.set()
            subject.loss.decision = unittest.mock.Mock(side_effect=AssertionError('standard resignation'))
            subject.loss.ceiling = unittest.mock.Mock(side_effect=AssertionError('standard loss ceiling'))
            info = {'depth': 20, 'score': chess.engine.PovScore(chess.engine.Cp(-1000), chess.WHITE)}
            with patch.object(fics.move_choice, 'choose_move', AsyncMock(return_value=(
                    chess.Move.from_uci('e3d4'), info, 'engine'))) as choose:
                await subject.play(subject.latest_position, subject.history.board.copy())
            self.assertFalse(choose.call_args.kwargs['options']['OwnBook'])
            self.assertIsNone(choose.call_args.kwargs['ceiling'])
            self.assertIsNone(subject.opening.book_path)
            self.assertFalse(subject.opening.mood)
            self.assertEqual(subject.connection.sent[-1], 'e3d4')


class losers_storage_tests(unittest.TestCase):
    def test_pgn_roundtrip_monthly_archive_rebuild_and_cross_variant_rejection(self):
        game = losers_game()
        parsed = read_pgn(io.StringIO(str(game)))
        self.assertFalse(parsed.errors)
        self.assertIs(type(parsed.board()), royal_losers_board)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'experience'
            standard = learning_store.shared_store(root)
            losers = learning_store.shared_store(root / 'losers', 'losers')
            with self.assertRaisesRegex(ValueError, 'cannot enter standard'):
                standard.add(game)
            record = losers.add(game)
            self.assertEqual(record['variant'], 'losers')
            path = losers.games / (game.headers['LearningId'] + '.pgn')
            monthly = monthly_pgn.archive_pending(path)
            self.assertEqual(monthly, losers.games / 'fics' / '2026-10.pgn')
            self.assertFalse(path.exists())
            self.assertEqual(losers.rebuild(), {'games': 1, 'learned': 1})
            self.assertEqual(losers.load(), [record])
            self.assertEqual(standard.rebuild(), {'games': 0, 'learned': 0})
            # Even an accidentally mixed list cannot influence the other variant's opening.
            self.assertFalse(learning.opening_book([record]).layers[0])
            self.assertTrue(learning.opening_book([record], variant='losers').layers[0])
            orthodox = variant_game.from_board(chess.Board())
            orthodox.add_variation(chess.Move.from_uci('e2e4'))
            with self.assertRaisesRegex(ValueError, 'cannot enter losers'):
                losers.add(orthodox)

    def test_record_and_interrupted_games_keep_royal_variant(self):
        board = royal_losers_board()
        board.push_uci('e2e4')
        record = fics_game.game_record({'White': 'Alpha', 'Black': 'Beta', 'LearningColor': 'white'},
                                       datetime(2026, 10, 26, tzinfo=timezone.utc))
        game = record.game(board, '*', 'shutdown')
        self.assertIs(type(game.board()), royal_losers_board)
        with tempfile.TemporaryDirectory() as directory:
            from maxima_runner import online_shutdown
            root = Path(directory)
            online_shutdown.save_interrupted(game, root, lambda text: None)
            saved = list((root / 'interrupted').glob('*.pgn'))
            self.assertEqual(len(saved), 1)
            self.assertIs(type(learning_store.read_games(saved[0])[0].board()), royal_losers_board)


if __name__ == '__main__':
    unittest.main()
