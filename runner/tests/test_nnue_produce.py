"""Producer concurrency, campaign stop, pinned identities and block tail recovery."""
import argparse
import json
from pathlib import Path
import random
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import chess

from maxima_runner import nnue_data, nnue_produce, nnue_selfplay, nnue_shards


class produce_tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.engine, self.network = self.root / 'engine', self.root / 'network'
        self.engine.write_bytes(b'engine')
        self.network.write_bytes(b'network')
        self.args = argparse.Namespace(output=self.root / 'pool', store=self.root / 'store',
            block='one', campaign='test', engine=self.engine, network=self.network, book=None, exclude=[],
            workers=2, jobs=4, positions_per_job=5, games_per_job=3, nodes=100, target=20,
            max_seconds=10, start_job=0, stop_file=self.root / 'STOP')

    def test_concurrency_is_bounded_and_stop_drains_without_new_jobs(self):
        active, maximum, started = 0, 0, []
        lock = threading.Lock()

        def worker(args, index, deadline, block_stop):
            nonlocal active, maximum
            with lock:
                active += 1
                maximum = max(maximum, active)
                started.append(index)
                if len(started) == 2:
                    args.stop_file.touch()
            time.sleep(0.02)
            with lock:
                active -= 1

        with patch.object(nnue_produce, 'run_job', worker):
            self.assertEqual(nnue_produce.produce(self.args), 0)
        self.assertEqual(maximum, 2)
        self.assertEqual(len(started), 2)
        self.assertEqual(active, 0)
        with patch.object(nnue_produce, 'run_job') as mocked:
            nnue_produce.produce(self.args)
            mocked.assert_not_called()

    def test_job_rejects_teacher_change_and_passes_stop_bounds(self):
        self.args.output.mkdir()
        self.args.diversity = True
        commands = []

        def process(command, **kwargs):
            commands.append(command)
            directory = self.args.output / 'job-00000000'
            (directory / 'settings.json').write_text('{}')
            return argparse.Namespace(returncode=0)

        with patch.object(nnue_produce.subprocess, 'run', process), patch.object(nnue_selfplay, 'load_run', return_value=([], set(), [])):
            nnue_produce.run_job(self.args, 0, time.time() + 10, self.root / 'block.stop')
        self.assertIn('--stop-file', commands[0])
        self.assertIn('--stop-at', commands[0])
        self.assertIn('--no-book', commands[0])
        self.assertIn('--diversity', commands[0])
        self.network.write_bytes(b'changed teacher')
        with self.assertRaisesRegex(ValueError, 'pinned inputs'):
            nnue_produce.run_job(self.args, 0, time.time() + 10, self.root / 'block.stop')

    def test_producer_chunk_tail_survives_next_block_and_skips_completed_hashes(self):
        chunks = self.args.output / 'chunks'
        chunks.mkdir(parents=True)
        board, rng, rows = chess.Board(), random.Random(17), []
        for i in range(12):
            rows.append(dict(fen=board.fen(), score_cp=i, result_stm=0.5,
                             split=nnue_data.position_split(board),
                             source=dict(sha256='a' * 64, game=1, ply=i)))
            board.push(rng.choice(list(board.legal_moves)))
        source = chunks / 'pending'
        nnue_data.save_dataset(source, dict(variant='standard', score_pov='side_to_move'), rows)
        source.rename(chunks / f'{nnue_data.file_hash(source)}.mxdata')
        self.args.target = 5
        self.assertEqual(nnue_produce.ingest_jobs(self.args), 5)
        nnue_shards.seal(self.args.store, self.args.block)
        self.args.block, self.args.target = 'two', 7
        self.assertEqual(nnue_produce.ingest_jobs(self.args), 7)
        self.args.block, self.args.target = 'three', 7
        with patch.object(nnue_data, 'file_hash', side_effect=AssertionError('completed input rehashed')):
            self.assertEqual(nnue_produce.ingest_jobs(self.args), 0)

    def test_selfplay_stops_before_scheduling_next_game(self):
        options = argparse.Namespace(stop_file=[self.args.stop_file], stop_at=time.time() + 10)
        self.assertFalse(nnue_selfplay.generation_stopped(options))
        self.args.stop_file.touch()
        self.assertTrue(nnue_selfplay.generation_stopped(options))
        options.stop_file, options.stop_at = [], time.time() - 1
        self.assertTrue(nnue_selfplay.generation_stopped(options))


if __name__ == '__main__':
    unittest.main()
