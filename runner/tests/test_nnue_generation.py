"""Physical handoff preserves global dedup, training lineage and resource limits."""
from contextlib import closing
import json
from pathlib import Path
import random
import sqlite3
import subprocess
import sys
import tempfile
import unittest

import chess

from maxima_runner import nnue_campaign, nnue_data, nnue_generation, nnue_shards


class block_counts_tests(unittest.TestCase):
    def test_status_recovers_an_interrupted_transaction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            database = root / 'index.sqlite3'
            with sqlite3.connect(database) as db:
                db.executescript('CREATE TABLE blocks (name TEXT, target INTEGER, positions INTEGER, sealed INTEGER);'
                                 "INSERT INTO blocks VALUES ('block-001', 100, 12, 0);"
                                 'CREATE TABLE padding (data BLOB);')
                db.executemany('INSERT INTO padding VALUES (?)', [(b'x' * 1024,)] * 1024)
            subprocess.run([sys.executable, '-c', '''
import os,sqlite3,sys
db=sqlite3.connect(sys.argv[1])
db.execute('PRAGMA cache_size=10')
db.execute('BEGIN IMMEDIATE')
db.execute('UPDATE blocks SET positions=99')
db.execute('UPDATE padding SET data=zeroblob(1024)')
os._exit(0)
''', str(database)], check=True)
            self.assertTrue(database.with_name('index.sqlite3-journal').exists())
            self.assertEqual(nnue_generation.block_counts(root),
                             [dict(name='block-001', target=100, positions=12, sealed=0)])


class generation_tests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.store, self.teacher = self.root / 'store', self.root / 'teacher'
        self.teacher.write_bytes(b'pinned teacher')
        self.now = 1800000000
        self.initial = nnue_campaign.initialize(self.store, self.teacher,
            expiry=self.now + 10 * 86400, credit_evidence='fixture', now=self.now)
        nnue_campaign.admit_block(self.store, 'block-001', self.now)
        nnue_campaign.transact(self.store, lambda state, stamp:
            state['blocks']['block-001'].update(target=6), self.now)
        nnue_campaign.reserve(self.store, 'cpu', 'cpu', 'block-001', 100, 3600, self.now)
        board, rng, rows = chess.Board(), random.Random(29), []
        for index in range(18):
            rows.append(dict(fen=board.fen(), score_cp=index, result_stm=0.5,
                split=nnue_data.position_split(board), source=dict(sha256='a' * 64, game=1, ply=index)))
            board.push(rng.choice(list(board.legal_moves)))
        self.source = self.root / 'source.mxdata'
        nnue_data.save_dataset(self.source, dict(variant='standard', score_pov='side_to_move'), rows)
        nnue_shards.ingest(self.store, 'block-001', [self.source], target=6, shard_size=2)
        nnue_shards.seal(self.store, 'block-001')
        nnue_campaign.mark_ready(self.store, 'block-001', self.now)
        self.first_range = dict(block='block-001', start=0, stop=1)
        self.second_range = dict(block='block-001', start=1, stop=3)
        self.first = self.stage('first', [self.first_range], None, None)
        self.second = self.stage('second', [self.second_range], self.first[2],
                                 [self.first_range, self.second_range])

    def stage(self, name, ranges, parent, history):
        manifest = nnue_shards.freeze_ranges(self.store, ranges, self.root / (name + '-data'))
        stage = self.root / name
        stage.mkdir()
        (stage / 'training.pt').write_bytes(('latest ' + name).encode())
        (stage / 'candidate.pt').write_bytes(('selected ' + name).encode())
        contract = dict(store_id=self.initial['store_id'], manifest_sha256=nnue_data.file_hash(manifest),
                        parent_sha256=parent, warm_start_sha256=nnue_data.file_hash(self.teacher))
        receipt = dict(format='maxima.nnue.stage.complete', version=1, contract=contract,
                       checkpoint_sha256=nnue_data.file_hash(stage / 'training.pt'),
                       candidate_sha256=nnue_data.file_hash(stage / 'candidate.pt'))
        if history is not None:
            receipt['seen_ranges'] = history
        (stage / 'complete.json').write_text(json.dumps(receipt))
        return stage, manifest, receipt['checkpoint_sha256']

    def register(self):
        for stage, manifest, _ in (self.first, self.second):
            nnue_generation.register_completed(self.store, stage, manifest, now=self.now)
        nnue_generation.configure_schedule(self.store, 8, 2, now=self.now)

    def test_handoff_reuses_cpu_lease_and_ingest_cap_preserves_tail(self):
        self.register()
        before = nnue_campaign.load(self.store)['allocations']['cpu'].copy()
        current = nnue_generation.advance(self.store, 'cpu', now=self.now)
        self.assertEqual((current['block'], current['limit']), ('block-002', 3))
        after = nnue_campaign.load(self.store)
        lease = after['allocations']['cpu']
        for key in ('deadline', 'maximum_cents', 'started_at', 'category', 'status'):
            self.assertEqual(lease[key], before[key])
        self.assertEqual(after['champion'], self.initial['champion'])
        self.assertEqual(lease['work'], 'block-002')
        saved = (self.store / 'campaign.json').read_bytes()
        self.assertEqual(nnue_generation.advance(self.store, 'cpu', now=self.now), current)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), saved)
        current = nnue_generation.ingest_current(self.store, [self.source], now=self.now)
        self.assertEqual((current['accepted_global'], current['positions'], current['active']), (9, 3, False))
        self.assertEqual(nnue_generation.ingest_current(self.store, [self.source], now=self.now), current)
        with closing(nnue_shards.connect(self.store)) as db:
            self.assertEqual(db.execute('SELECT next_record FROM cursors').fetchone(), (9,))
            self.assertEqual(db.execute("SELECT target,positions,sealed FROM blocks WHERE name='block-002'").fetchone(),
                             (6, 3, 0))
        self.assertFalse((self.store / 'STOP').exists())
        self.assertFalse((self.store / 'block-002.json').exists())

    def test_cross_block_snapshot_then_completed_training_opens_backlog(self):
        self.register()
        nnue_generation.advance(self.store, 'cpu', now=self.now)
        nnue_generation.ingest_current(self.store, [self.source], now=self.now)
        snapshot = nnue_generation.prepare_snapshot(self.store, self.root / 'snapshots', now=self.now)
        ranges = [dict(block='block-001', start=3, stop=6), dict(block='block-002', start=0, stop=2)]
        self.assertEqual((snapshot['start'], snapshot['through'], snapshot['positions']), (3, 8, 5))
        self.assertEqual(nnue_shards.source_ranges(json.loads(Path(snapshot['manifest']).read_text())), ranges)
        self.assertEqual(nnue_generation.prepare_snapshot(self.store, self.root / 'snapshots', now=self.now), snapshot)
        third = self.stage('third', ranges, self.second[2], [self.first_range, self.second_range] + ranges)
        nnue_generation.register_completed(self.store, third[0], third[1], now=self.now)
        self.assertEqual(nnue_campaign.load(self.store)['generation_schedule']['next_checkpoint'], 10)
        self.assertEqual(nnue_generation.advance(self.store, 'cpu', now=self.now)['limit'], 6)
        self.assertEqual(nnue_generation.ingest_current(self.store, [self.source], now=self.now)['accepted_global'], 12)
        nnue_shards.seal(self.store, 'block-002')
        nnue_campaign.mark_ready(self.store, 'block-002', self.now)
        current = nnue_generation.advance(self.store, 'cpu', now=self.now)
        self.assertEqual((current['block'], current['limit']), ('block-003', 2))
        self.assertEqual(nnue_generation.ingest_current(self.store, [self.source], now=self.now)['accepted_global'], 14)

    def test_explicit_capacity_resumes_generation_without_claiming_training(self):
        self.register()
        nnue_generation.advance(self.store, 'cpu', now=self.now)
        nnue_generation.ingest_current(self.store, [self.source], now=self.now)
        before = nnue_campaign.load(self.store)
        nnue_generation.configure_capacity_floor(self.store, 12, now=self.now)
        self.assertEqual(nnue_generation.advance(self.store, 'cpu', now=self.now)['limit'], 6)
        self.assertEqual(nnue_generation.ingest_current(self.store, [self.source], now=self.now)['accepted_global'], 12)
        after = nnue_campaign.load(self.store)
        for key in ('generation_learning', 'allocations', 'budget_cents', 'champion', 'original_teacher'):
            self.assertEqual(after[key], before[key])

    def test_capacity_configuration_rejects_invalid_values_and_stop(self):
        self.register()
        before = (self.store / 'campaign.json').read_bytes()
        for positions in (0, -1, True, 5, 9.5):
            with self.assertRaises(ValueError):
                nnue_generation.configure_capacity_floor(self.store, positions, now=self.now)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), before)
        (self.store / 'STOP').touch()
        with self.assertRaisesRegex(ValueError, 'stopped'):
            nnue_generation.configure_capacity_floor(self.store, 12, now=self.now)

    def test_ceiling_caps_live_ingest_before_next_advance_and_preserves_tail(self):
        self.register()
        nnue_generation.configure_capacity_floor(self.store, 18, now=self.now)
        nnue_generation.advance(self.store, 'cpu', now=self.now)
        before = nnue_campaign.load(self.store)
        nnue_generation.configure_capacity_ceiling(self.store, 8, now=self.now)
        current = nnue_generation.ingest_current(self.store, [self.source], now=self.now)
        self.assertEqual((current['accepted_global'], current['limit'], current['active']), (8, 2, False))
        self.assertTrue(nnue_generation.advance(self.store, 'cpu', now=self.now)['waiting'])
        self.assertEqual(nnue_generation.ingest_current(self.store, [self.source], now=self.now), current)
        with closing(nnue_shards.connect(self.store)) as db:
            self.assertEqual(db.execute('SELECT next_record FROM cursors').fetchone(), (8,))
        after = nnue_campaign.load(self.store)
        for key in ('generation_learning', 'allocations', 'champion', 'budget_cents'):
            self.assertEqual(after[key], before[key])
        self.assertFalse((self.store / 'STOP').exists())

    def test_final_boundary_blocks_rollover_even_after_completed_training(self):
        self.register()
        nnue_generation.configure_capacity_ceiling(self.store, 12, now=self.now)
        nnue_generation.configure_capacity_floor(self.store, 18, now=self.now)
        nnue_generation.advance(self.store, 'cpu', now=self.now)
        nnue_generation.ingest_current(self.store, [self.source], now=self.now)
        ranges = [dict(block='block-001', start=3, stop=6), dict(block='block-002', start=0, stop=6)]
        third = self.stage('third', ranges, self.second[2], [self.first_range, self.second_range] + ranges)
        nnue_generation.register_completed(self.store, third[0], third[1], now=self.now)
        nnue_shards.seal(self.store, 'block-002')
        nnue_campaign.mark_ready(self.store, 'block-002', self.now)
        current = nnue_generation.advance(self.store, 'cpu', now=self.now)
        self.assertEqual((current['block'], current['waiting'], current['accepted_global']), ('block-002', True, 12))
        self.assertNotIn('block-003', nnue_campaign.load(self.store)['blocks'])
        self.assertIsNone(nnue_generation.prepare_snapshot(self.store, self.root / 'snapshots', now=self.now))
        self.assertFalse(nnue_generation.generation_state(self.store, now=self.now)['active'])

    def test_ceiling_rejects_invalid_values_without_mutation(self):
        self.register()
        before = (self.store / 'campaign.json').read_bytes()
        for positions in (0, -1, True, 5, 9.5):
            with self.assertRaises(ValueError):
                nnue_generation.configure_capacity_ceiling(self.store, positions, now=self.now)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), before)
        (self.store / 'STOP').touch()
        with self.assertRaisesRegex(ValueError, 'stopped'):
            nnue_generation.configure_capacity_ceiling(self.store, 12, now=self.now)

    def test_no_snapshot_before_checkpoint_and_status_does_not_rewrite_state(self):
        self.register()
        before = (self.store / 'campaign.json').read_bytes()
        self.assertIsNone(nnue_generation.prepare_snapshot(self.store, self.root / 'snapshots', now=self.now))
        nnue_generation.generation_state(self.store, now=self.now)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), before)
        self.assertFalse((self.root / 'snapshots').exists())

    def test_rejects_missing_first_stage_and_changed_completed_checkpoint(self):
        before = (self.store / 'campaign.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'first-stage'):
            nnue_generation.register_completed(self.store, self.second[0], self.second[1], now=self.now)
        (self.first[0] / 'training.pt').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            nnue_generation.register_completed(self.store, self.first[0], self.first[1], now=self.now)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), before)

    def test_rejects_overlapping_training_history_before_changing_allocation(self):
        nnue_generation.register_completed(self.store, self.first[0], self.first[1], now=self.now)
        overlapping = self.stage('overlap', [dict(block='block-001', start=0, stop=3)], self.first[2],
                                  [self.first_range, dict(block='block-001', start=0, stop=3)])
        before = (self.store / 'campaign.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'gaps, overlaps'):
            nnue_generation.register_completed(self.store, overlapping[0], overlapping[1], now=self.now)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), before)

    def test_idempotent_registration_rejects_stale_parent_and_wrong_store(self):
        self.register()
        before = (self.store / 'campaign.json').read_bytes()
        nnue_generation.register_completed(self.store, self.second[0], self.second[1], now=self.now)
        self.assertEqual((self.store / 'campaign.json').read_bytes(), before)
        with self.assertRaisesRegex(ValueError, 'latest registered'):
            nnue_generation.register_completed(self.store, self.first[0], self.first[1], now=self.now)
        path = self.second[0] / 'complete.json'
        receipt = json.loads(path.read_text())
        receipt['contract']['store_id'] = '0' * 32
        path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(ValueError, 'store mismatch'):
            nnue_generation.register_completed(self.store, self.second[0], self.second[1], now=self.now)

    def test_stop_and_allocation_deadline_prevent_handoff(self):
        self.register()
        nnue_generation.configure_capacity_floor(self.store, 12, now=self.now)
        before = nnue_campaign.load(self.store)['allocations']['cpu'].copy()
        with self.assertRaisesRegex(ValueError, 'stopped'):
            nnue_generation.advance(self.store, 'cpu', now=self.now + 3600)
        self.assertEqual(nnue_campaign.load(self.store)['allocations']['cpu'], before)
        self.assertNotIn('block-002', nnue_campaign.load(self.store)['blocks'])
        self.assertFalse(nnue_generation.ingest_current(self.store, [self.source], now=self.now + 3600)['active'])

    def test_schedule_cannot_exceed_one_block_backlog(self):
        self.register()
        for checkpoint, step in ((12, 2), (3, 2), (8, 7), (8, 0)):
            with self.assertRaises(ValueError):
                nnue_generation.configure_schedule(self.store, checkpoint, step, now=self.now)

    def test_unsealed_or_unready_block_cannot_be_handed_off(self):
        self.register()
        nnue_campaign.transact(self.store, lambda state, stamp:
            state['blocks']['block-001'].update(phase='generating'), self.now)
        with self.assertRaisesRegex(ValueError, 'mark.*ready'):
            nnue_generation.advance(self.store, 'cpu', now=self.now)


if __name__ == '__main__':
    unittest.main()
