"""Producer concurrency, campaign stop, pinned identities and block tail recovery."""
import argparse
import json
from pathlib import Path
import random
import tempfile
import threading
import time
import sys
import unittest
from unittest.mock import patch

import chess

from maxima_runner import nnue_data, nnue_produce, nnue_selfplay, nnue_shards


class produce_tests(unittest.TestCase):
    def test_seeded_producer_publishes_fresh_completed_labels_and_seals_a_block(self):
        script = '''
import sys
moves = []
for line in sys.stdin:
    if line.strip() == 'uci':
        print('id name seeded fixture')
        print('uciok', flush=True)
    elif line.strip() == 'isready':
        print('readyok', flush=True)
    elif line.startswith('position '):
        moves = line.split(' moves ', 1)[1].split() if ' moves ' in line else []
    elif line.startswith('go '):
        move = 'g2g4' if len(moves) == 2 else 'd8h4'
        score = 'cp 17' if len(moves) == 2 else 'mate 1'
        print('info depth 3 nodes 8 score ' + score + ' pv ' + move)
        print('bestmove ' + move, flush=True)
    elif line.strip() == 'quit':
        break
'''
        self.engine.write_text('#!' + sys.executable + '\n' + script)
        self.engine.chmod(0o700)
        initial = chess.Board()
        initial.fullmove_number = 9
        board = initial.copy()
        for move in ('f2f3', 'e7e5'):
            board.push_uci(move)
        starts = self.root / 'starts.json'
        starts.write_text(json.dumps(dict(format='maxima.nnue.starts', version=1, starts=[dict(
            initial_fen=initial.fen(), moves=['f2f3', 'e7e5'], fen=board.fen(en_passant='fen'),
            source_game='a' * 64)])))
        self.args.start_positions = starts
        self.args.jobs = self.args.workers = self.args.positions_per_job = self.args.target = 1
        self.args.games_per_job = 1
        self.args.nodes = 10000
        self.assertEqual(nnue_produce.produce(self.args), 0)
        manifest = nnue_shards.read_manifest(self.args.store / 'one.json')
        self.assertEqual(manifest['positions'], 1)
        from maxima_runner.nnue_binary import binary_dataset
        with binary_dataset(self.args.store / manifest['shards'][0]['path']) as data:
            self.assertEqual(data[0]['source']['seed_game'], 'a' * 64)
            self.assertEqual(data[0]['score_cp'], 17)
            self.assertEqual(data[0]['result_stm'], 0)

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

    def test_backlog_limit_drains_without_sealing_or_starting_jobs(self):
        nnue_shards.ingest(self.args.store, self.args.block, [], target=self.args.target)
        self.args.accepted_limit = 3
        from contextlib import closing
        with closing(nnue_shards.connect(self.args.store)) as db, db:
            db.execute('UPDATE blocks SET positions=3')
        with patch.object(nnue_produce, 'run_job') as worker:
            self.assertEqual(nnue_produce.produce(self.args), 0)
            worker.assert_not_called()
        self.assertFalse((self.args.store / 'one.json').exists())
        self.assertFalse((self.args.store / 'STOP').exists())

    def test_campaign_limit_cannot_be_bypassed_by_cli_limit(self):
        from maxima_runner import nnue_campaign
        nnue_campaign.initialize(self.args.store, self.network)
        nnue_campaign.admit_block(self.args.store, self.args.block)
        nnue_campaign.transact(self.args.store, lambda state, stamp:
            state['blocks'][self.args.block].update(generation_limit=4))
        self.args.accepted_limit = self.args.target
        self.assertEqual(nnue_produce.accepted_limit(self.args), 4)
        self.args.accepted_limit = 2
        self.assertEqual(nnue_produce.accepted_limit(self.args), 2)

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
