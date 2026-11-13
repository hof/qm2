"""Global deduplication and transactional immutable block publication."""
import json
from contextlib import closing
from pathlib import Path
import random
import sqlite3
import tempfile
import unittest

import chess

from maxima_runner import nnue_data, nnue_shards


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
        source = self.dataset('source', self.rows)
        with self.assertRaisesRegex(ValueError, 'remainder'):
            nnue_shards.ingest(self.store, 'one', [source], target=8, shard_size=2)
        with closing(sqlite3.connect(self.store / 'index.sqlite3')) as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM positions').fetchone()[0], 0)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM shards').fetchone()[0], 0)
        smaller = self.dataset('smaller', self.rows[:8])
        self.assertEqual(nnue_shards.ingest(self.store, 'one', [smaller], target=8, shard_size=2), 8)
        self.assertEqual(nnue_shards.read_manifest(nnue_shards.seal(self.store, 'one'))['positions'], 8)

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

    def test_missing_completed_result_and_heldout_input_rejected(self):
        rows = [dict(self.rows[0], result_stm=None)]
        for name, values, header in [('unknown', rows, {}), ('heldout', self.rows, dict(purpose='heldout_evaluation'))]:
            source = self.dataset(name, values, **header)
            with self.assertRaisesRegex(ValueError, 'completed result'):
                nnue_shards.ingest(self.store, 'one', [source])


if __name__ == '__main__':
    unittest.main()
