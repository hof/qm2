"""File-only experience, concurrent publishers, and legacy conversion."""
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import chess

from maxima_runner import cli, experience
from test_experience import pgn

try:
    import sqlite3
except ImportError:
    sqlite3 = None


class experience_store_tests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    @unittest.skipUnless(sqlite3, 'legacy conversion needs optional SQLite support')
    def test_legacy_conversion_preserves_counts_analysis_and_binary(self):
        from maxima_runner import experience_migration
        legacy_path = self.root / 'experience.sqlite'
        key = experience.key_of(chess.Board())
        with closing(sqlite3.connect(legacy_path)) as legacy:
            legacy.executescript('''
                PRAGMA user_version=1;
                CREATE TABLE games (id TEXT, headers TEXT);
                CREATE TABLE observations (game TEXT, key TEXT, move TEXT);
                CREATE TABLE moves (key TEXT, move TEXT, wins INT, draws INT, losses INT,
                    own_games INT, opponent_games INT, own_wins INT, own_draws INT, own_losses INT);
                CREATE TABLE analysis (key TEXT, move TEXT, engine TEXT, clock INT, depth INT,
                    score INT, score_type INT, nodes INT, think_ms INT, pv TEXT);
            ''')
            legacy.execute('INSERT INTO games VALUES (?, ?)', ('one', '{"White":"Maxima"}'))
            legacy.execute('INSERT INTO observations VALUES (?, ?, ?)', ('one', key, 'e2e4'))
            legacy.execute('INSERT INTO moves VALUES (?, ?, 1, 0, 0, 1, 0, 1, 0, 0)', (key, 'e2e4'))
            legacy.execute('INSERT INTO analysis VALUES (?, ?, ?, 0, 20, 40, 0, 1000, 60000, ?)',
                           (key, 'e2e4', 'fixture', '["e2e4"]'))
            legacy.commit()
        original = legacy_path.read_bytes()
        store_path = self.root / 'experience.json'
        self.assertTrue(experience_migration.migrate_legacy(legacy_path, store_path))
        self.assertFalse(experience_migration.migrate_legacy(legacy_path, store_path))
        self.assertEqual(legacy_path.read_bytes(), original)
        with closing(experience.open_store(store_path)) as store:
            self.assertEqual(store.data['observations'], {'one': [key + '/e2e4']})
            snapshot = self.root / 'experience.bin'
            experience.export_snapshot(store, snapshot)
        engine = int.from_bytes(hashlib.sha256(b'fixture').digest()[:8], 'little')
        expected = experience.header.pack(experience.magic, 1) + experience.record.pack(
            int(key, 16), 12 | (28 << 6), 1, 0, 0, 20, 40, 1000, 60000, 0, 0, engine, 1, 0, 0, 0)
        self.assertEqual(snapshot.read_bytes(), expected)

    @unittest.skipUnless(sqlite3, 'legacy conversion needs optional SQLite support')
    def test_failed_conversion_leaves_no_partial_store(self):
        from maxima_runner import experience_migration
        source = self.root / 'broken.sqlite'
        source.write_bytes(b'not a SQLite database')
        destination = self.root / 'experience.json'
        with self.assertRaisesRegex(ValueError, 'conversion failed'):
            experience_migration.migrate_legacy(source, destination)
        self.assertFalse(destination.exists())

    def test_normal_cli_runs_without_sqlite(self):
        games = self.root / 'games.pgn'
        games.write_text(pgn)
        store = self.root / 'experience.json'
        output = self.root / 'experience.bin'
        code = "import sys; sys.modules['sqlite3'] = None; from maxima_runner.cli import main; raise SystemExit(main(sys.argv[1:]))"
        for command in ('import', 'export', 'show'):
            arguments = ['--source', str(games if command == 'import' else store)]
            result = subprocess.run([sys.executable, '-c', code, 'experience:book:' + command,
                                     '--store', str(store), '--output', str(output), *arguments],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(output.exists())

    def test_failed_save_keeps_previous_file_and_rolls_back_memory(self):
        path = self.root / 'experience.json'
        with closing(experience.open_store(path)) as store:
            with store:
                store.data['games']['old'] = {}
            previous = path.read_bytes()
            with patch('maxima_runner.experience_store.atomic_text', side_effect=OSError('disk failure')):
                with self.assertRaises(OSError):
                    with store:
                        store.data['games']['new'] = {}
            self.assertEqual(store.data['games'], {'old': {}})
            self.assertEqual(path.read_bytes(), previous)

    def test_concurrent_cli_imports_and_replays_preserve_all_games(self):
        store, output = self.root / 'experience.json', self.root / 'experience.bin'
        commands = []
        for index in range(3):
            path = self.root / f'{index}.pgn'
            path.write_text(pgn.replace('fixture', f'fixture-{index}'))
            commands.append([sys.executable, '-m', 'maxima_runner', 'experience:book:import',
                             '--source', str(path), '--store', str(store), '--output', str(output)])
        for _ in range(2):
            processes = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                         for command in commands]
            try:
                for process in processes:
                    stdout, stderr = process.communicate(timeout=15)
                    self.assertEqual(process.returncode, 0, stdout + stderr)
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.kill()
                    process.wait()
        data = json.loads(store.read_text())
        self.assertEqual(len(data['games']), 9)
        self.assertEqual(sum(sum(row[2:5]) for row in data['moves'].values()), 18)

    def test_book_commands_default_to_archive_and_accept_explicit_sources(self):
        folder = self.root / 'experience' / 'games' / 'fics'
        folder.mkdir(parents=True)
        source = folder / '2026-10.pgn'
        source.write_text(pgn.replace('Maxima', 'GuestWhite').replace('[Round "1"]',
                          '[Round "1"]\n[LearningColor "white"]'))
        environment = dict(os.environ, MAXIMA_DATA_DIR=str(self.root))
        result = subprocess.run([sys.executable, '-m', 'maxima_runner', 'experience:book:import'],
                                env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        store = self.root / 'experience' / 'experience.json'
        data = json.loads(store.read_text())
        self.assertEqual(len(data['games']), 1)  # Tagged guest game; other games have no matching alias.
        key = experience.key_of(chess.Board())
        self.assertEqual(data['moves'][key + '/e2e4'][5], 1)
        for arguments in (
                ['experience:book:import', '--source', str(source)],
                ['experience:book:export', '--source', str(store)],
                ['experience:book:show', '--source', str(store)]):
            result = subprocess.run([sys.executable, '-m', 'maxima_runner', *arguments],
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(json.loads(store.read_text())['games']), 1)

    def test_analyze_source_defaults_and_search_budget_are_explicit(self):
        calls = []
        async def analyze(store, source, engine, max_ply, movetime):
            calls.append((source, max_ply, movetime))
            return []
        with patch.dict(os.environ, {'MAXIMA_DATA_DIR': str(self.root)}), \
                patch.object(experience, 'analyze_games', analyze):
            self.assertEqual(cli.main(['experience:book:analyze', '--movetime', '60000']), 0)
        self.assertEqual(calls, [(self.root / 'experience' / 'games', 32, 60000)])
