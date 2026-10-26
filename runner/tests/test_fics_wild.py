"""Wild provenance, scoped learning, and book choices across PGN round trips."""
import io
from pathlib import Path
import random
import tempfile
import unittest

import chess
from maxima_runner import learning, learning_store
from maxima_runner.variants import wild_boards, variant_game, read_pgn, board_variant, royal_losers_board
from test_fics_policy import player
from test_fics_losers import position


class wild_tests(unittest.TestCase):
    def test_each_variant_round_trips_and_has_its_own_learned_book(self):
        with tempfile.TemporaryDirectory() as directory:
            for kind, board_type in dict(wild_boards, losers=royal_losers_board).items():
                board = board_type()
                for move in ['g1f3', 'g8f6', 'f3g1', 'f6g8'] * 5:
                    board.push_uci(move)
                game = variant_game.from_board(board)
                game.headers.update(White='Alpha', Black='Beta', WhiteType='program',
                                    LearningColor='white', LearningSource='fics', LearningServer='freechess.org',
                                    Result='1-0', LearningResults='true', UTCDate='2026.10.26', UTCTime='12:00:00')
                parsed = read_pgn(io.StringIO(str(game)))
                self.assertEqual(board_variant(parsed.board()), kind)
                store = learning_store.shared_store(Path(directory) / kind, kind)
                record = store.add(parsed)
                self.assertEqual(record['variant'], kind)
                self.assertEqual(store.rebuild()['games'], 1)
                with self.assertRaises(ValueError):
                    learning_store.shared_store(Path(directory) / 'standard').add(parsed)
                with self.assertRaises(ValueError):
                    other = 'wild/3' if kind == 'wild/2' else 'wild/2'
                    learning_store.shared_store(Path(directory) / 'other', other).add(parsed)
                choices = [learning.opening_book([record], variant=kind, random_source=random.Random(seed))
                           .choose(board_type(), 5000) for seed in range(20)]
                self.assertTrue(any(choice is not None for choice in choices), kind)
                wrong = learning.opening_book([record], variant=kind)
                self.assertIsNone(wrong.choose(chess.Board(), 5000))

    def test_fics_uses_variant_learner_and_separate_pgn_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subject = player()
            subject.learner = learning.learner(root / 'experience', mood='off')
            subject.pgn_dir = root / 'games'
            subject.created = {'type': 'wild/2', 'white': 'Alpha', 'black': 'Beta', 'time': '3', 'increment': '0'}
            board = wild_boards['wild/2']()
            for move in ('g1f3', 'g8f6', 'f3g1', 'f6g8'):
                board.push_uci(move)
            subject.on_board(position(board))
            learner = subject.game_learner
            self.assertEqual(learner.variant, 'wild/2')
            self.assertEqual(learner.experience_dir, root / 'experience' / 'wild' / '2')
            self.assertEqual(learner.book_path, learner.experience_dir / 'book.bin')
            self.assertEqual(subject.game_pgn_dir, root / 'wild' / '2' / 'games')
            self.assertIs(subject.game_learner, learner)
