"""Global deduplication and transactional immutable block publication."""
import json
from contextlib import closing
from pathlib import Path
import random
import sqlite3
import tempfile
import unittest

import chess

from maxima_runner import nnue_binary, nnue_campaign, nnue_data, nnue_shards


class shard_tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store = self.root / 'store'
        board, rng, self.rows = chess.Board(), random.Random(19), []
        for i in range(12):
            self.rows.append(dict(fen=board.fen(), score_cp=32767 - i, result_stm=0.5,
                                  split=nnue_data.position_split(board),
                                  source=dict(sha256='a' * 64, game=1, ply=board.ply())))
            board.push(rng.choice(list(board.legal_moves)))

    def dataset(self, name, rows, **header):
        path = self.root / name
        nnue_data.save_dataset(path, dict(variant='standard', score_pov='side_to_move', **header), rows)
        return path

    def test_global_deduplication_retry_and_immutable_manifest(self):
        first = self.dataset('first', self.rows[:6])
        second = self.dataset('second', self.rows[3:])
        self.assertEqual(nnue_shards.ingest(self.store, 'one', [first], target=6, shard_size=2), 6)
        self.assertEqual(nnue_shards.ingest(self.store, 'one', [first], target=6), 6)
        manifest = nnue_shards.seal(self.store, 'one')
        value = nnue_shards.read_manifest(manifest)
        self.assertEqual(value['positions'], 6)
        self.assertEqual(len(value['shards']), 3)
        self.assertEqual(nnue_shards.seal(self.store, 'one'), manifest)
        self.assertEqual(nnue_shards.ingest(self.store, 'two', [second], target=6), 6)
        with self.assertRaisesRegex(ValueError, 'full or sealed'):
            nnue_shards.ingest(self.store, 'one', [self.dataset('third', self.rows[1:4])], target=6)
        shard = self.store / value['shards'][0]['path']
        shard.write_bytes(shard.read_bytes() + b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            nnue_shards.read_manifest(manifest)

    def test_failed_input_rolls_back_index_and_can_retry(self):
        source = self.dataset('source', [*self.rows[:7], dict(self.rows[7], result_stm=None)])
        with self.assertRaisesRegex(ValueError, 'completed result'):
            nnue_shards.ingest(self.store, 'one', [source], target=8, shard_size=2)
        with closing(sqlite3.connect(self.store / 'index.sqlite3')) as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM positions').fetchone()[0], 0)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM shards').fetchone()[0], 0)
        smaller = self.dataset('smaller', self.rows[:8])
        self.assertEqual(nnue_shards.ingest(self.store, 'one', [smaller], target=8, shard_size=2), 8)
        self.assertEqual(nnue_shards.read_manifest(nnue_shards.seal(self.store, 'one'))['positions'], 8)

    def test_block_boundary_preserves_input_tail_for_next_block(self):
        source = self.dataset('source', self.rows)
        self.assertEqual(nnue_shards.ingest(self.store, 'one', [source], target=5), 5)
        nnue_shards.seal(self.store, 'one')
        self.assertEqual(nnue_shards.ingest(self.store, 'two', [source], target=7), 7)
        nnue_shards.seal(self.store, 'two')
        self.assertEqual(nnue_shards.ingest(self.store, 'three', [source], target=7), 0)

    def test_reserved_positions_and_explicit_partial_stop(self):
        excluded = self.dataset('reserved', self.rows[:3], purpose='heldout_evaluation')
        source = self.dataset('source', self.rows)
        self.assertEqual(nnue_shards.ingest(self.store, 'one', [source], exclude=[excluded]), 9)
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            nnue_shards.seal(self.store, 'one')
        manifest = nnue_shards.seal(self.store, 'one', partial=True)
        self.assertTrue(json.loads(manifest.read_text())['partial'])
        later = self.dataset('later', self.rows[4:6], purpose='heldout_evaluation')
        with self.assertRaisesRegex(ValueError, 'before ingesting'):
            nnue_shards.ingest(self.store, 'two', [], exclude=[later])

    def test_snapshots_partition_rows_without_sealing_live_block(self):
        source = self.dataset('source', self.rows[:8])
        nnue_shards.ingest(self.store, 'one', [source], target=12, shard_size=3)
        first = nnue_shards.freeze_range(self.store, 'one', self.root / 'first', positions=5)
        tail = self.dataset('tail', self.rows[8:])
        nnue_shards.ingest(self.store, 'one', [tail], target=12, shard_size=3)
        second = nnue_shards.freeze_range(self.store, 'one', self.root / 'second', start=5, positions=7)
        recovered = []
        for path in (first, second):
            manifest = nnue_shards.read_manifest(path)
            for shard in manifest['shards']:
                with nnue_binary.binary_dataset(path.parent / shard['path']) as data:
                    recovered.extend(data[i]['score_cp'] for i in range(len(data)))
        self.assertEqual(recovered, [row['score_cp'] for row in self.rows])
        with closing(sqlite3.connect(self.store / 'index.sqlite3')) as db:
            self.assertEqual(db.execute('SELECT positions,sealed FROM blocks').fetchone(), (12, 0))
        original = self.store / nnue_shards.read_manifest(first)['shards'][0]['path']
        linked = first.parent / nnue_shards.read_manifest(first)['shards'][0]['path']
        self.assertEqual(original.stat().st_ino, linked.stat().st_ino)

    def test_snapshot_failures_leave_generation_and_output_intact(self):
        source = self.dataset('source', self.rows)
        nnue_shards.ingest(self.store, 'one', [source], target=12, shard_size=3)
        destination = self.root / 'snapshot'
        with self.assertRaisesRegex(ValueError, 'exceeds committed'):
            nnue_shards.freeze_range(self.store, 'one', destination, positions=13)
        self.assertFalse(destination.exists())
        nnue_shards.freeze_range(self.store, 'one', destination, positions=4)
        with self.assertRaisesRegex(ValueError, 'already exists'):
            nnue_shards.freeze_range(self.store, 'one', destination, positions=5)
        self.assertEqual(nnue_shards.read_manifest(destination / 'manifest.json')['positions'], 4)

    def test_snapshot_spans_sealed_and_generating_blocks_without_losing_rows(self):
        source = self.dataset('source', self.rows)
        nnue_shards.ingest(self.store, 'one', [source], target=6, shard_size=3)
        nnue_shards.seal(self.store, 'one')
        nnue_shards.ingest(self.store, 'two', [source], target=8, shard_size=3)
        ranges = [dict(block='one', start=4, stop=6), dict(block='two', start=0, stop=5)]
        path = nnue_shards.freeze_ranges(self.store, ranges, self.root / 'combined')
        manifest = nnue_shards.read_manifest(path)
        self.assertEqual(nnue_shards.source_ranges(manifest), ranges)
        recovered = []
        for shard in manifest['shards']:
            with nnue_binary.binary_dataset(path.parent / shard['path']) as data:
                recovered.extend(data[i]['score_cp'] for i in range(len(data)))
        self.assertEqual(recovered, [row['score_cp'] for row in self.rows[4:11]])
        with closing(sqlite3.connect(self.store / 'index.sqlite3')) as db:
            self.assertEqual(db.execute('SELECT name,positions,sealed FROM blocks ORDER BY name').fetchall(),
                             [('one', 6, 1), ('two', 6, 0)])
        teacher = self.root / 'teacher.nnue'
        teacher.write_bytes(b'teacher')
        state = nnue_campaign.initialize_training_snapshot(path.parent, teacher, maximum_cents=100,
            deadline=1800000060, funding_evidence='test', now=1800000000)
        self.assertEqual(state['blocks']['combined']['source_ranges'], ranges)

    def test_multiblock_snapshot_rejects_overlap_and_incomplete_ranges_atomically(self):
        source = self.dataset('source', self.rows)
        nnue_shards.ingest(self.store, 'one', [source], target=6)
        for ranges in ([dict(block='one', start=0, stop=4), dict(block='one', start=3, stop=6)],
                       [dict(block='one', start=4, stop=6), dict(block='missing', start=0, stop=3)]):
            output = self.root / 'rejected'
            with self.assertRaises(ValueError):
                nnue_shards.freeze_ranges(self.store, ranges, output)
            self.assertFalse(output.exists())

    def test_prepaid_snapshot_trial_limits_do_not_stop_source_generation(self):
        source = self.dataset('source', self.rows)
        nnue_shards.ingest(self.store, 'one', [source], target=12, shard_size=3)
        manifest = nnue_shards.freeze_range(self.store, 'one', self.root / 'trial', positions=5)
        teacher = self.root / 'teacher.nnue'
        teacher.write_bytes(b'pinned test teacher')
        now = 1800000000
        nnue_campaign.initialize_training_snapshot(manifest.parent, teacher, maximum_cents=300,
                                                  deadline=now + 3600, funding_evidence='test prepaid receipt', now=now)
        state = nnue_campaign.status(manifest.parent, now)
        self.assertEqual(state['store_id'], nnue_shards.read_manifest(manifest)['store_id'])
        self.assertEqual(state['budget_cents']['gpu'], 300)
        self.assertEqual(state['committed_cents'], {})
        for category, cents, seconds in [('gpu', 301, 60), ('gpu', 300, 3601), ('cpu', 1, 60)]:
            with self.assertRaises(ValueError):
                nnue_campaign.reserve(manifest.parent, 'bad', category, 'trial', cents, seconds, now)
        nnue_campaign.reserve(manifest.parent, 'gpu', 'gpu', 'trial', 300, 3600, now)
        nnue_campaign.start_training(manifest.parent, 'trial', 'gpu', now=now)
        with self.assertRaisesRegex(ValueError, 'stopped'):
            nnue_campaign.allocation(manifest.parent, 'gpu', 'gpu', 'trial', now + 3600)
        self.assertTrue((manifest.parent / 'STOP').exists())
        self.assertFalse((self.store / 'STOP').exists())
        self.assertFalse((self.store / 'campaign.json').exists())

    def test_missing_completed_result_and_heldout_input_rejected(self):
        rows = [dict(self.rows[0], result_stm=None)]
        for name, values, header in [('unknown', rows, {}), ('heldout', self.rows, dict(purpose='heldout_evaluation'))]:
            source = self.dataset(name, values, **header)
            with self.assertRaisesRegex(ValueError, 'completed result'):
                nnue_shards.ingest(self.store, 'one', [source])

    def test_prepaid_continuation_requires_matching_parent_receipt(self):
        source = self.dataset('source', self.rows)
        nnue_shards.ingest(self.store, 'one', [source], target=12, shard_size=3)
        old_manifest = nnue_shards.freeze_range(self.store, 'one', self.root / 'old', positions=5)
        manifest = nnue_shards.freeze_range(self.store, 'one', self.root / 'trial', start=5, positions=5)
        teacher, parent, receipt = (self.root / name for name in ('teacher', 'parent', 'complete.json'))
        teacher.write_bytes(b'teacher'); parent.write_bytes(b'checkpoint fixture')
        metadata = dict(format='maxima.nnue.stage.complete', version=1,
                        checkpoint_sha256=nnue_data.file_hash(parent),
                        contract=dict(store_id='wrong', manifest_sha256=nnue_data.file_hash(old_manifest), parent_sha256=None))
        receipt.write_text(json.dumps(metadata))
        options = dict(maximum_cents=300, deadline=1800003600, funding_evidence='test',
                       parent=parent, parent_receipt=receipt, parent_manifest=old_manifest, now=1800000000)
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            nnue_campaign.initialize_training_snapshot(manifest.parent, teacher, **options)
        self.assertFalse((manifest.parent / 'campaign.json').exists())
        metadata['contract']['store_id'] = nnue_shards.read_manifest(manifest)['store_id']
        receipt.write_text(json.dumps(metadata))
        overlapping = nnue_shards.freeze_range(self.store, 'one', self.root / 'overlap', start=4, positions=5)
        with self.assertRaisesRegex(ValueError, 'overlaps'):
            nnue_campaign.initialize_training_snapshot(overlapping.parent, teacher, **options)
        self.assertFalse((overlapping.parent / 'campaign.json').exists())
        state = nnue_campaign.initialize_training_snapshot(manifest.parent, teacher, **options)
        self.assertEqual(state['latest_checkpoint_sha256'], nnue_data.file_hash(parent))


if __name__ == '__main__':
    unittest.main()
