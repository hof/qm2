"""Offline provenance and label checks for the frozen contemporary corpus."""
import hashlib
import contextlib
import io
import json
from pathlib import Path
import unittest
import tempfile
from unittest.mock import patch

import chess
import chess.pgn
from maxima_runner import run_suite


class modern_suite_tests(unittest.TestCase):
    def test_changed_engine_rejects_benchmark_report(self):
        async def changing_search(command, position, *args):
            Path(command[0]).write_bytes(b'changed binary')
            return {'solved': False, 'bestmove': position['expected_moves'][0],
                    'elapsed_ms': 1, 'tbhits': 0}, {'id': ['name fixture'], 'options': {}}

        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine'
            engine.write_bytes(b'original binary')
            report = Path(directory) / 'report.json'
            with patch('sys.argv', ['suite', '--engine', str(engine), '--nodes', '40', '--json', str(report)]), \
                    patch.object(run_suite, 'run_position', changing_search), \
                    contextlib.redirect_stdout(io.StringIO()), \
                    contextlib.redirect_stderr(io.StringIO()) as errors:
                self.assertEqual(run_suite.main(), 1)
            self.assertIn('changed during the run', errors.getvalue())
            self.assertFalse(report.exists())

    def test_default_suite_and_report_identity(self):
        async def fake_search(command, position, *args):
            return {'solved': True, 'bestmove': position['expected_moves'][0],
                    'elapsed_ms': 1, 'tbhits': 0}, {'id': ['name fixture'], 'options': {}}

        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine'
            engine.write_bytes(b'fixture binary')
            report = Path(directory) / 'report.json'
            with patch('sys.argv', ['suite', '--engine', str(engine), '--nodes', '40', '--json', str(report)]), \
                    patch.object(run_suite, 'run_position', fake_search), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run_suite.main(), 0)
            data = json.loads(report.read_text())
            suite = Path(run_suite.__file__).parent / 'data/suites/modern_2026.epd'
            self.assertEqual(Path(data['suite']), suite.resolve())
            self.assertEqual(data['total'], 22)
            self.assertEqual(data['suite_sha256'], hashlib.sha256(suite.read_bytes()).hexdigest())
            self.assertEqual(data['engine_sha256'], hashlib.sha256(engine.read_bytes()).hexdigest())

    def test_frozen_provenance_and_labels(self):
        directory = Path(run_suite.__file__).parent / 'data/suites'
        manifest = json.loads((directory / 'modern_2026.json').read_text())
        positions = run_suite.read_suite(directory / 'modern_2026.epd')
        self.assertEqual(len(positions), 22)
        self.assertEqual(len(manifest['positions']), len(positions))
        self.assertEqual(len({row['fen'] for row in positions}), 22)
        self.assertEqual(manifest['selection']['verify_nodes'], [2_000_000, 8_000_000])
        self.assertEqual(manifest['reference']['identity'][0], 'name Stockfish 19')
        self.assertEqual(len(manifest['reference']['binary_sha256']), 64)
        self.assertTrue(manifest['reference']['cold_hash_each_pass'])
        self.assertEqual(manifest['reference']['options']['Threads'], '1')
        games = {}
        for source in manifest['sources']:
            path = directory / 'sources' / source['file']
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), source['sha256'])
            with path.open() as stream:
                while True:
                    game = chess.pgn.read_game(stream)
                    if game is None:
                        break
                    self.assertFalse(game.errors)
                    self.assertTrue(game.headers['Date'].startswith('2026.'))
                    games[game.headers['GameURL']] = (source['file'], game)
        self.assertEqual(len(games), 21)
        counts = {}
        for position, row in zip(positions, manifest['positions']):
            with self.subTest(position=row['id']):
                source, game = games[row['game_url']]
                self.assertEqual(row['source'], source)
                board = game.board()
                for ply, move in enumerate(game.mainline_moves(), 1):
                    self.assertIn(move, board.legal_moves)
                    board.push(move)
                    if ply == row['ply']:
                        break
                self.assertEqual(board.fen(), chess.Board(position['fen']).fen())
                self.assertEqual(board.fen(), chess.Board(row['fen']).fen())
                self.assertFalse(board.is_game_over(claim_draw=True))
                self.assertGreaterEqual(board.legal_moves.count(), 8)
                self.assertEqual(position['id'], row['id'])
                self.assertEqual(position['expected_moves'], [row['expected_move']])
                self.assertEqual(position['expected_san'], [row['expected_san']])
                self.assertEqual([stage['nodes_budget'] for stage in row['verification']],
                                 manifest['selection']['verify_nodes'])
                self.assertGreaterEqual(row['verification'][-1]['completed_depth'], 18)
                for stage in row['verification']:
                    top, alternative = stage['top']
                    self.assertEqual(top['move'], row['expected_move'])
                    self.assertNotEqual(top['move'], alternative['move'])
                    self.assertEqual(stage['gap_cp'], top['cp'] - alternative['cp'])
                    self.assertGreaterEqual(stage['gap_cp'], 50)
                    self.assertLessEqual(abs(top['cp']), 1000)
                    for line in stage['top']:
                        replay = chess.Board(position['fen'])
                        self.assertEqual(line['move'], line['pv'][0])
                        for name in line['pv']:
                            move = chess.Move.from_uci(name)
                            self.assertIn(move, replay.legal_moves)
                            replay.push(move)
                counts[row['game_url']] = counts.get(row['game_url'], 0) + 1
        self.assertLessEqual(max(counts.values()), 2)
        self.assertEqual(len({row['id'] for row in manifest['positions']}), 22)


if __name__ == '__main__':
    unittest.main()
