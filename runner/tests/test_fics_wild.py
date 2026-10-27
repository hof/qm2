"""Orthodox wild learning shares exact positions and preserves PGN provenance."""
import io
from pathlib import Path
import random
import tempfile
import unittest

import chess
from maxima_runner import fics_game, learning, learning_store
from maxima_runner.variants import wild_boards, variant_game, read_pgn, board_variant, royal_losers_board
from test_fics_policy import player
from test_fics_losers import position


def training_game(kind='wild/2', source='fics'):
    board_type = royal_losers_board if kind == 'losers' else wild_boards.get(kind, chess.Board)
    board = board_type(chess.STARTING_FEN.replace('KQkq', '-'))
    for move in ['g1f3', 'g8f6', 'f3g1', 'f6g8'] * 5:
        board.push_uci(move)
    game = variant_game.from_board(board)
    game.headers.update(White='Alpha', Black='Beta', WhiteType='program',
                        LearningColor='white', LearningSource=source,
                        LearningServer='lichess.org' if source == 'lichess' else 'freechess.org',
                        GameId=kind, Result='1-0', LearningResults='true',
                        UTCDate='2026.10.26', UTCTime='12:00:00')
    return game


class wild_tests(unittest.TestCase):
    def test_observed_wild8_start_and_both_castling_wings(self):
        # FICS guest examinations, 2026-10-26: actual boards, not help-text assumptions.
        start = fics_game.style12.parse(
            '<12> rnbqkbnr -------- -------- pppppppp PPPPPPPP -------- -------- RNBQKBNR '
            'W -1 1 1 1 1 0 20 Alpha Beta -1 0 0 39 39 0 0 1 none (0:00.000) none')
        fixtures = [
            (('g1f3', 'g8f6', 'f1d3', 'f8d6', 'e1g1', 'e8g8'),
             'rnbq1rk1/8/3b1n2/pppppppp/PPPPPPPP/3B1N2/8/RNBQ1RK1 w - - 6 4'),
            (('b1c3', 'b8c6', 'c1e3', 'c8e6', 'd1d2', 'd8d7', 'e1c1', 'e8c8'),
             '2kr1bnr/3q4/2n1b3/pppppppp/PPPPPPPP/2N1B3/3Q4/2KR1BNR w - - 8 5'),
        ]
        for moves, expected in fixtures:
            with self.subTest(moves=moves):
                board = start.board(wild_boards['wild/8'])
                for move in moves:
                    board.push_uci(move)
                self.assertEqual(board.fen(), expected)
        unsupported = fics_game.style12.parse(
            '<12> rnbkqbnr -------- -------- PPPPPPPP pppppppp -------- -------- RNBKQBNR '
            'W -1 1 1 1 1 0 20 Alpha Beta -1 0 0 39 39 0 0 1 none (0:00.000) none')
        with self.assertRaisesRegex(ValueError, 'invalid style12 position'):
            unsupported.board(wild_boards['wild/8a'])

    def test_shared_positions_transfer_between_standard_and_wild_but_not_losers(self):
        record = learning.digest(training_game())
        start = training_game().board().fen()
        for kind in ('standard', *wild_boards):
            board_type = wild_boards.get(kind, chess.Board)
            choices = [learning.opening_book([record], variant=kind, random_source=random.Random(seed))
                       .choose(board_type(start)) for seed in range(20)]
            self.assertTrue(any(choice is not None for choice in choices), kind)
        # Castling rights distinguish positions, even when pieces occupy identical squares.
        self.assertIsNone(learning.opening_book([record]).choose(chess.Board()))
        losers = learning.opening_book([record], variant='losers')
        self.assertFalse(losers.layers[0])
        self.assertIsNone(losers.choose(royal_losers_board(start)))
        with self.assertRaises(ValueError):
            learning.opening_book([record]).choose(chess.Board(start, chess960=True))

    def test_rebuild_imports_legacy_wild_games_once_and_preserves_labels(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shared = learning_store.shared_store(root)
            shared.add(training_game('standard', 'lichess'))
            originals = []
            for kind in wild_boards:
                game = training_game(kind)
                parsed = read_pgn(io.StringIO(str(game)))
                self.assertEqual(board_variant(parsed.board()), kind)
                legacy = learning_store.shared_store(root / kind, kind)
                record = legacy.add(parsed)
                self.assertEqual(record['variant'], kind)
                originals.extend(legacy.games.rglob('*.pgn'))
            separate = learning_store.shared_store(root / 'losers', 'losers')
            separate.add(training_game('losers'))
            self.assertEqual(shared.rebuild()['games'], 7)
            first = shared.load()
            self.assertEqual({r['variant'] for r in first}, {'standard', *wild_boards})
            self.assertEqual(shared.rebuild()['games'], 7)
            self.assertEqual(shared.load(), first)
            self.assertTrue(all(path.exists() for path in originals))
            self.assertEqual(len(separate.load()), 1)
            with self.assertRaises(ValueError):
                shared.add(training_game('losers'))
            with self.assertRaises(ValueError):
                separate.add(training_game())

    def test_fics_uses_shared_learner_and_keeps_variant_pgn_path(self):
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
            self.assertIs(subject.game_learner, subject.learner)
            self.assertEqual(subject.game_pgn_dir, root / 'wild' / '2' / 'games')
