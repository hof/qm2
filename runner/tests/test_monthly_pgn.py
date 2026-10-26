"""Monthly PGN durability, deduplication, learning replay and concurrent CLI writers."""
import multiprocessing
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import chess
import chess.engine

from maxima_runner import learning_store, monthly_pgn
from test_shared_learning import game_for


def archive_worker(directory, identity):
    game = learning_store.prepare_game(game_for(identity=str(identity)))
    store = learning_store.shared_store(Path(directory).parent)
    store.add(game)
    path = Path(directory) / (game.headers['LearningId'] + '.pgn')
    monthly_pgn.archive_pending(path)
    store.add(game)  # Duplicate events after compaction must not recreate pending files.


class monthly_pgn_tests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.games = self.root / 'games'

    def pending(self, identity='one', source='fics'):
        game = learning_store.prepare_game(game_for(source=source, identity=identity))
        path, _ = learning_store.archive_game(game, self.games)
        return game, path

    def test_cli_moves_and_retry_does_not_duplicate(self):
        game, source = self.pending()
        for _ in range(2):
            result = subprocess.run([sys.executable, '-m', 'maxima_runner', 'experience:archive', str(source)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        monthly = self.games / 'fics' / '2026-10.pgn'
        self.assertFalse(source.exists())
        self.assertEqual([str(g) for g in learning_store.read_games(monthly)], [str(game)])
        # A duplicate event saves directly to its existing monthly record.
        self.assertEqual(learning_store.archive_game(game, self.games)[0], monthly)
        self.assertFalse(source.exists())

    def test_appending_uses_index_without_parsing_previous_games(self):
        _, first = self.pending()
        destination = monthly_pgn.archive_pending(first)
        _, second = self.pending('two')
        original = monthly_pgn.chess.pgn.read_game
        calls = []
        def observe(stream):
            calls.append(1)
            return original(stream)
        with patch.object(monthly_pgn.chess.pgn, 'read_game', side_effect=observe):
            monthly_pgn.archive_pending(second)
        self.assertEqual(len(calls), 2)  # Input game and EOF; no archive parsing.
        self.assertEqual(len(learning_store.read_games(destination)), 2)

    def test_failed_index_publication_and_partial_append_recover(self):
        _, first = self.pending()
        destination = monthly_pgn.archive_pending(first)
        before = destination.read_bytes()
        _, source = self.pending('two')
        with patch.object(monthly_pgn, 'publish_index', side_effect=OSError('disk failure')):
            with self.assertRaises(OSError):
                monthly_pgn.archive_pending(source)
        self.assertTrue(source.exists())
        # Simulate a short write after the durable journal was published.
        with destination.open('r+b') as stream:
            stream.truncate(len(before) + 13)
        monthly_pgn.archive_pending(source)
        self.assertFalse(source.exists())
        self.assertEqual(len(learning_store.read_games(destination)), 2)
        self.assertTrue(destination.read_bytes().startswith(before))
        self.assertFalse(destination.with_suffix('.pending.json').exists())

    def test_legacy_month_is_indexed_and_duplicate_keeps_richer_analysis(self):
        game, source = self.pending()
        destination = monthly_pgn.month_path(game, self.games)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(str(game) + '\n\n', encoding='utf-8')
        game.next().set_eval(chess.engine.PovScore(chess.engine.Cp(80), chess.WHITE), 25)
        source.write_text(str(game) + '\n\n', encoding='utf-8')
        monthly_pgn.archive_pending(source)
        games = learning_store.read_games(destination)
        self.assertEqual(len(games), 1)
        self.assertEqual(games[0].next().eval_depth(), 25)

    def test_learning_rebuild_and_explicit_correction_after_compaction(self):
        store = learning_store.shared_store(self.root)
        game = game_for()
        store.add(game)
        source = self.games / (game.headers['LearningId'] + '.pgn')
        destination = monthly_pgn.archive_pending(source)
        expected = store.snapshot.read_bytes()
        store.rebuild()
        self.assertEqual(store.snapshot.read_bytes(), expected)
        game.headers['Result'] = '0-1'
        upload = self.root / 'upload.pgn'
        upload.write_text(str(game), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'conflicting final result'):
            store.sync(upload)
        store.sync(upload, replace=True)
        self.assertEqual(learning_store.read_games(destination)[0].headers['Result'], '0-1')
        self.assertEqual(store.rebuild()['games'], 1)
        self.assertEqual(store.load()[0]['score'], 0)
        self.assertFalse(source.exists())

    def test_rebuild_recovers_an_interrupted_monthly_append(self):
        store = learning_store.shared_store(self.root)
        game = game_for()
        store.add(game)
        source = self.games / (game.headers['LearningId'] + '.pgn')
        with patch.object(monthly_pgn, 'publish_index', side_effect=OSError('disk failure')):
            with self.assertRaises(OSError):
                monthly_pgn.archive_pending(source)
        destination = monthly_pgn.month_path(game, self.games)
        with destination.open('r+b') as stream:
            stream.truncate(12)
        self.assertEqual(store.rebuild()['games'], 1)
        monthly_pgn.archive_pending(source)
        self.assertFalse(source.exists())
        self.assertEqual(len(learning_store.read_games(destination)), 1)

    def test_rejected_input_is_never_deleted(self):
        game, source = self.pending()
        game.headers['Result'] = '*'
        source.write_text(str(game), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'unfinished'):
            monthly_pgn.archive_pending(source)
        self.assertTrue(source.exists())
        self.assertFalse((self.games / 'fics').exists())

    def test_multiple_processes_share_one_month_without_duplicates(self):
        context = multiprocessing.get_context('spawn')
        workers = [context.Process(target=archive_worker, args=(self.games, index)) for index in range(6)]
        for worker in workers:
            worker.start()
        try:
            for worker in workers:
                worker.join(15)
                self.assertEqual(worker.exitcode, 0)
            games = learning_store.read_games(self.games / 'fics' / '2026-10.pgn')
            self.assertEqual(len(games), 6)
            self.assertEqual(len({game.headers['LearningId'] for game in games}), 6)
            self.assertFalse(list(self.games.glob('*.pgn')))
            self.assertEqual(learning_store.shared_store(self.root).rebuild()['games'], 6)
        finally:
            for worker in workers:
                if worker.is_alive():
                    worker.terminate()
                worker.join()
