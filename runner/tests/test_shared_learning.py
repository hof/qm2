"""Shared PVC learning, concurrent publishers, imports and PGN-only reconstruction."""
import json
import multiprocessing
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

import chess
import chess.engine
import chess.pgn

from maxima_runner import learning, learning_store
from test_learning import make_game


def game_for(source='fics', group='bots', identity='one', result='1-0', annotated=True):
    game = make_game([0] * 10 if annotated else [], result=result)
    game.headers.update(GameId=identity, LearningSource=source, LearningGroup='engines' if group == 'bots' else group)
    if source == 'lichess':
        game.headers['Event'] = 'Lichess rated blitz game'
    if source == 'local':
        game.headers.update(Event='Maxima offline paired match', Site='offline',
                            White='A', Black='B', EngineA='name Maxima 2.0', EngineB='name Stockfish 18')
        game.headers.pop('WhiteEngine', None)
    return game


def concurrent_writer(root, index, ready, failures):
    try:
        ready.wait(10)
        store = learning_store.shared_store(root)
        game = game_for(source='fics' if index % 2 else 'lichess', identity=f'concurrent-{index}')
        store.add(game)
        store.add(game)  # a duplicate event from this producer
    except BaseException as error:
        failures.put(repr(error))
        raise


class shared_learning_tests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name) / 'experience'
        self.store = learning_store.shared_store(self.root)

    def upload(self, game, name='upload.pgn', **options):
        path = self.root.parent / name
        path.write_text(str(game) + '\n\n')
        return self.store.sync(path, **options)

    def test_next_game_sees_shared_groups_and_existing_game_keeps_its_view(self):
        fics = learning.learner(self.root, server='freechess.org')
        lichess = learning.learner(self.root, server='lichess.org')
        before = lichess.for_game('Beta', 'engines', 'blitz')
        self.assertFalse(before.layers[0])
        fics.add(game_for())
        after = lichess.for_game('Beta', 'engines', 'blitz')
        self.assertTrue(after.layers[0])
        self.assertTrue(after.layers[1])
        self.assertFalse(after.layers[2])  # the FICS handle is a different person
        self.assertFalse(before.layers[0])
        self.assertTrue(fics.for_game('Beta', 'engines', 'blitz').layers[2])
        self.assertEqual(learning.player_stats(lichess.records, 'lichess.org'), {})

    def test_all_shared_cohorts_and_personal_stats_are_namespaced(self):
        for group in ('humans', 'masters', 'bots'):
            self.store.add(game_for('fics', group, identity=group))
            self.store.add(game_for('lichess', group, identity=group))
        records = self.store.load()
        for group in ('humans', 'masters', 'engines'):
            opening = learning.opening_book(records, 'Beta', group, 'blitz', server='lichess.org')
            self.assertEqual(opening.layers[1][next(iter(opening.layers[1]))][1], 2)
            self.assertEqual(opening.layers[2][next(iter(opening.layers[2]))][1], 3)
        stats = learning.player_stats(records)
        self.assertEqual(stats['freechess.org/beta']['games'], 3)
        self.assertEqual(stats['lichess.org/beta']['games'], 3)
        self.assertTrue((self.root / 'players' / 'lichess.org' / 'beta.json').exists())

    def test_plain_local_match_upload_and_repeat_deduplication(self):
        game = game_for('local', identity='local-one', annotated=False)
        first = self.upload(game)
        self.assertEqual((first['added'], first['learned']), (1, 1))
        self.assertEqual(self.upload(game)['added'], 0)
        record = self.store.load()[0]
        self.assertEqual((record['group'], record['source'], record['server']), ('engines', 'local', 'local'))
        self.assertTrue(all(evaluation is None for _, _, evaluation, _ in record['values']))
        self.assertFalse(record['analyses'])
        # Equal moves in genuinely distinct new matches must still count twice.
        game.headers['GameId'] = 'local-two'
        self.assertEqual(self.upload(game)['added'], 1)

    def test_lichess_export_url_and_live_game_share_one_identity(self):
        live = game_for('lichess', identity='a1B2c3D4')
        self.store.add(live)
        exported = game_for('lichess', identity='a1B2c3D4')
        exported.headers.pop('GameId')
        exported.headers['Site'] = 'https://lichess.org/a1B2c3D4'
        self.assertEqual(self.upload(exported)['added'], 0)
        self.assertEqual(len(self.store.load()), 1)

    def test_ambiguous_player_needs_alias_and_explicit_group_is_retained(self):
        game = game_for('local', annotated=False)
        game.headers.update(White='MyLaptop', Black='Opponent')
        for name in ('EngineA', 'EngineB', 'WhiteType', 'BlackType'):
            game.headers.pop(name, None)
        with self.assertRaisesRegex(ValueError, 'identify our side'):
            self.upload(game)
        self.assertFalse(list(self.store.games.glob('*.pgn')))
        self.upload(game, players=['MyLaptop'], group='bots')
        record = self.store.load()[0]
        self.assertEqual(record['group'], 'engines')
        self.assertEqual(record['opponent'], 'Opponent')

    def test_rebuild_after_deleting_all_derived_learning_is_identical(self):
        self.store.add(game_for())
        self.store.add(game_for('lichess', 'masters', identity='two', result='0-1'))
        self.upload(game_for('local', identity='three', annotated=False))
        expected = self.store.snapshot.read_bytes()
        stats = {path.relative_to(self.root / 'players'): path.read_bytes()
                 for path in (self.root / 'players').rglob('*.json')}
        shutil.rmtree(self.root / 'index')
        shutil.rmtree(self.root / 'players')
        result = self.store.rebuild()
        self.assertEqual(result, {'games': 3, 'learned': 3})
        self.assertEqual(self.store.snapshot.read_bytes(), expected)
        self.assertEqual({path.relative_to(self.root / 'players'): path.read_bytes()
                          for path in (self.root / 'players').rglob('*.json')}, stats)

    def test_legacy_server_folders_migrate_once_and_rebuild(self):
        for folder, source in ((self.root / 'games' / 'fics', 'fics'),
                               (self.root / 'lichess' / 'games', 'lichess')):
            folder.mkdir(parents=True)
            game = game_for(source, identity=source)
            for tag in ('LearningSource', 'LearningGroup', 'GameId'):
                game.headers.pop(tag, None)
            (folder / '2026-10.pgn').write_text(str(game) + '\n\n')
        self.assertEqual(self.store.rebuild()['learned'], 2)
        self.assertEqual(self.store.rebuild()['learned'], 2)
        self.assertEqual(len(list(self.store.games.rglob('*.pgn'))), 2)
        self.assertEqual({record['source'] for record in self.store.load()}, {'fics', 'lichess'})

    def test_final_result_correction_is_explicit_and_survives_rebuild(self):
        game = game_for()
        self.upload(game)
        legacy = self.root / 'games' / 'fics'
        legacy.mkdir(parents=True)
        (legacy / 'old.pgn').write_text(str(game) + '\n\n')
        game.headers['Result'] = '0-1'
        with self.assertRaisesRegex(ValueError, 'conflicting final result'):
            self.upload(game)
        self.assertEqual(self.store.load()[0]['score'], 1)
        self.upload(game, replace=True)
        self.assertEqual(self.store.load()[0]['score'], 0)
        self.store.rebuild()
        self.assertEqual(self.store.load()[0]['score'], 0)

    def test_incomplete_replay_cannot_downgrade_result_or_analysis(self):
        game = game_for(result='*')
        self.upload(game)
        game.headers['Result'] = '1-0'
        node = game.next()
        node.set_eval(chess.engine.PovScore(chess.engine.Cp(80), chess.WHITE), 20)
        node.set_clock(123.5)
        self.upload(game)
        weaker = game_for(result='*', annotated=False)
        self.upload(weaker)
        archived = learning_store.read_games(next(self.store.games.glob('*.pgn')))[0]
        self.assertEqual(archived.headers['Result'], '1-0')
        self.assertEqual(archived.next().eval_depth(), 20)
        self.assertEqual(archived.next().clock(), 123.5)
        self.assertEqual(len(self.store.load()), 1)

    def test_invalid_input_is_rejected_before_archive_changes(self):
        path = self.root.parent / 'invalid.pgn'
        path.write_text(str(game_for()) + '\n\n[Event "bad"]\n[White "Maxima"]\n\n1. e9 *\n')
        with self.assertRaises(ValueError):
            self.store.sync(path)
        self.assertFalse(list(self.store.games.glob('*.pgn')))
        game = game_for()
        game.headers['LearningId'] = '../escape'
        with self.assertRaisesRegex(ValueError, 'LearningId'):
            self.upload(game)
        game.headers.pop('LearningId')
        game.headers['LearningServer'] = '..'
        with self.assertRaisesRegex(ValueError, 'server name'):
            self.upload(game)

    def test_identity_conflict_and_unsupported_variant_do_not_change_archive(self):
        record = self.store.add(game_for())
        expected = self.store.snapshot.read_bytes()
        changed = game_for()
        changed.headers['LearningId'] = record['id']
        changed.headers['White'] = 'DifferentPlayer'
        with self.assertRaisesRegex(ValueError, 'conflicting game content'):
            self.upload(changed)
        self.assertEqual(self.store.snapshot.read_bytes(), expected)
        variant = game_for()
        variant.headers['Variant'] = 'Chess960'
        with self.assertRaisesRegex(ValueError, 'unsupported board variant'):
            self.upload(variant)
        self.assertEqual(self.store.snapshot.read_bytes(), expected)

    def test_unfinished_unannotated_upload_is_archived_without_learning(self):
        summary = self.upload(game_for('local', annotated=False, result='*'))
        self.assertEqual((summary['games'], summary['learned']), (1, 0))
        self.assertEqual(self.store.load(), [])
        self.assertEqual(self.store.rebuild(), {'games': 1, 'learned': 0})

    def test_failed_snapshot_publication_leaves_rebuildable_pgn(self):
        from unittest.mock import patch
        self.store.rebuild()
        with patch.object(self.store, '_publish', side_effect=OSError('fixture disk error')):
            with self.assertRaises(OSError):
                self.store.add(game_for())
        self.assertEqual(len(list(self.store.games.glob('*.pgn'))), 1)
        self.assertEqual(self.store.rebuild()['learned'], 1)

    def test_concurrent_clients_never_lose_updates_or_expose_partial_snapshots(self):
        self.store.rebuild()
        context = multiprocessing.get_context('spawn')
        ready, failures = context.Event(), context.Queue()
        workers = [context.Process(target=concurrent_writer, args=(str(self.root), index, ready, failures))
                   for index in range(4)]
        for worker in workers:
            worker.start()
        try:
            ready.set()
            deadline = time.monotonic() + 20
            while any(worker.is_alive() for worker in workers) and time.monotonic() < deadline:
                snapshot = json.loads(self.store.snapshot.read_text())
                self.assertEqual(snapshot['version'], 1)
                self.assertLessEqual(len(snapshot['games']), 4)
                for identity in snapshot['games']:
                    self.assertEqual(len(learning_store.read_games(self.store.games / f'{identity}.pgn')), 1)
                time.sleep(0.005)
            for worker in workers:
                worker.join(1)
                self.assertEqual(worker.exitcode, 0)
            self.assertEqual(len(self.store.load()), 4)
            self.assertEqual(self.store.rebuild()['learned'], 4)
        finally:
            for worker in workers:
                if worker.is_alive():
                    worker.terminate()
                worker.join()
            failures.close()

    def test_sync_command_supports_file_and_folder_inputs(self):
        path = self.root.parent / 'incoming.pgn'
        path.write_text(str(game_for('local', annotated=False)) + '\n\n')
        for source in (path, path.parent):
            result = subprocess.run([sys.executable, '-m', 'maxima_runner', 'experience:sync', str(source), str(self.root)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.store.load()), 1)
