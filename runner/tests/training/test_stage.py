"""Bounded stage loading, interruption, parent Adam continuity and frozen export."""
import contextlib
import io
import json
from pathlib import Path
import random
import tempfile
import unittest
from unittest.mock import patch

import chess
import torch

from maxima_runner import nnue_data, nnue_shards, nnue_stage


class stage_tests(unittest.TestCase):
    def setUp(self):
        torch.set_num_threads(1)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        board, rng, rows = chess.Board(), random.Random(17), []
        for i in range(90):
            rows.append(dict(fen=board.fen(), score_cp=i * 11 - 300, result_stm=[0, 0.5, 1][i % 3],
                             split=nnue_data.position_split(board),
                             source=dict(sha256='a' * 64, game=1, ply=board.ply())))
            board.push(rng.choice(list(board.legal_moves)))
        self.manifests = []
        self.expected = []
        for i, part in enumerate((rows[:45], rows[45:])):
            dataset = self.root / f'data-{i}'
            nnue_data.save_dataset(dataset, dict(variant='standard', score_pov='side_to_move'), part)
            store = self.root / 'store'
            nnue_shards.ingest(store, f'block-{i}', [dataset], target=len(part), shard_size=8)
            self.manifests.append(nnue_shards.seal(store, f'block-{i}'))
            self.expected.append(part)

    def train(self, name, manifest=0, *extra):
        argv = ['nnue:stage', str(self.manifests[manifest]), '--output', str(self.root / name),
                '--width', '2', '--hidden', '2', '--batch-size', '5', '--shuffle-size', '13',
                '--metrics-positions', '8', '--epochs', '2', *extra]
        with patch('sys.argv', argv), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(nnue_stage.main(), 0)
        return torch.load(self.root / name / 'training.pt', weights_only=True)

    def assert_equal(self, left, right):
        if isinstance(left, torch.Tensor):
            self.assertTrue(torch.equal(left, right))
        elif isinstance(left, dict):
            self.assertEqual(left.keys(), right.keys())
            for key in left:
                self.assert_equal(left[key], right[key])
        elif isinstance(left, (tuple, list)):
            self.assertEqual(len(left), len(right))
            for a, b in zip(left, right):
                self.assert_equal(a, b)
        else:
            self.assertEqual(left, right)

    def test_streamed_index_matches_source_with_bounded_cache(self):
        manifest = nnue_shards.read_manifest(self.manifests[0])
        for split in ('train', 'validation'):
            rows = nnue_stage.shard_rows(self.manifests[0], manifest, split)
            try:
                expected = [r for r in self.expected[0] if r['split'] == split]
                self.assertEqual(len(rows), len(expected))
                for i in reversed(range(len(rows))):
                    self.assertEqual(rows[i]['fen'], expected[i]['fen'])
                    self.assertLessEqual(len(rows.cache), 16)
            finally:
                rows.close()

    def test_shuffle_reuses_shards_instead_of_rebuilding_split_indices(self):
        manifest = nnue_shards.read_manifest(self.manifests[0])
        rows = nnue_stage.shard_rows(self.manifests[0], manifest, 'train')
        indices = [sum(s['train'] for s in manifest['shards'][:i])
                   for i, s in enumerate(manifest['shards']) if s['train']]
        original = nnue_stage.nnue_binary.binary_dataset
        try:
            with patch.object(nnue_stage.nnue_binary, 'binary_dataset', wraps=original) as opened:
                for _ in range(3):
                    for index in indices:
                        rows[index]
                self.assertEqual(opened.call_count, len(indices))
        finally:
            rows.close()

    def test_binary_board_batch_matches_fen_batch_exactly(self):
        from maxima_runner import nnue_training
        manifest = nnue_shards.read_manifest(self.manifests[0])
        rows = nnue_stage.shard_rows(self.manifests[0], manifest, 'train')
        try:
            fast = [rows.board_row(i) for i in range(len(rows))]
            ordinary = [rows[i] for i in range(len(rows))]
            for architecture in ('halfkp', 'halfkav2_hm'):
                self.assert_equal(nnue_training.batch(torch, fast, architecture),
                                  nnue_training.batch(torch, ordinary, architecture))
            self.assertEqual([r['score_cp'] for r in fast], [r['score_cp'] for r in ordinary])
            self.assertEqual([r['result_stm'] for r in fast], [r['result_stm'] for r in ordinary])
        finally:
            rows.close()

    def test_mid_chunk_and_epoch_boundary_resume_matches_uninterrupted(self):
        full = self.train('full')
        paused = self.train('paused', 0, '--max-batches', '1')
        self.assertEqual(paused['phase'], 'training')
        self.assertFalse((self.root / 'paused/candidate.pt').exists())
        for _ in range(30):
            paused = self.train('paused', 0, '--resume', '--max-batches', '2')
            if paused['phase'] == 'complete':
                break
        self.assert_equal(full, paused)
        metrics = [json.loads(path.read_text()) for path in (self.root / 'paused/invocations').glob('*.json')]
        self.assertEqual(sum(item['presentations'] for item in metrics), paused['presentations'])
        self.assertTrue(all(item['elapsed_seconds'] > 0 for item in metrics))
        self.assertTrue((self.root / 'paused/complete.json').is_file())
        self.assert_equal(torch.load(self.root / 'full/candidate.pt', weights_only=True),
                          torch.load(self.root / 'paused/candidate.pt', weights_only=True))

    def test_parent_preserves_latest_weights_adam_and_counters(self):
        parent = self.train('parent')
        seen = []
        parameters = []
        original = torch.optim.Adam.step

        def inspect(optimizer, *args, **kwargs):
            if not seen:
                seen.append(nnue_stage.cpu_copy(torch, optimizer.state_dict()))
                parameters.extend(p.detach().cpu().clone() for group in optimizer.param_groups for p in group['params'])
            return original(optimizer, *args, **kwargs)

        with patch.object(torch.optim.Adam, 'step', inspect):
            child = self.train('child', 1, '--parent', str(self.root / 'parent/training.pt'), '--max-batches', '1')
        self.assert_equal(seen[0], parent['optimizer'])
        for actual, expected in zip(parameters, parent['model'].values()):
            self.assertTrue(torch.equal(actual, expected))
        self.assertEqual(child['global_step'], parent['global_step'] + 1)
        self.assertEqual(child['presentations'], parent['presentations'] + 5)
        with self.assertRaisesRegex(ValueError, 'overlaps'):
            self.train('replay', 0, '--parent', str(self.root / 'parent/training.pt'))
        with self.assertRaisesRegex(ValueError, 'recipe mismatch'):
            self.train('bad-recipe', 1, '--parent', str(self.root / 'parent/training.pt'), '--learning-rate', '.01')

    def test_resume_rejects_changed_recipe_and_export_does_not_modify_training(self):
        self.train('run', 0, '--max-batches', '1')
        checkpoint = self.root / 'run/training.pt'
        before = checkpoint.read_bytes()
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            self.train('run', 0, '--resume', '--shuffle-size', '14')
        self.assertEqual(before, checkpoint.read_bytes())
        self.train('run', 0, '--resume')
        before = checkpoint.read_bytes()
        argv = ['nnue:export', str(self.root / 'run/candidate.pt'), '--manifest', str(self.manifests[0]),
                '--output', str(self.root / 'export'), '--positions', '12']
        with patch('sys.argv', argv), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(nnue_stage.export_main(), 0)
        self.assertTrue((self.root / 'export/network.nnue').is_file())
        self.assertEqual(before, checkpoint.read_bytes())
        probe = Path(__file__).resolve().parents[3] / 'build/cpp26/halfka/tests/nnue_probe'
        if probe.is_file():
            from maxima_runner import nnue_training
            argv = ['nnue:verify', str(self.root / 'export/network.nnue'), '--probe', str(probe),
                    '--dataset', str(self.root / 'data-0'), '--positions', '16']
            with patch('sys.argv', argv), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(nnue_training.verify_main(), 0)

    def test_parent_rejects_another_global_dedup_store(self):
        self.train('parent')
        manifest = json.loads(self.manifests[1].read_text())
        manifest['store_id'] = 'b' * 32
        other = self.manifests[1].with_name('other-store.json')
        other.write_text(json.dumps(manifest))
        self.manifests[1] = other
        with self.assertRaisesRegex(ValueError, 'store_id'):
            self.train('other', 1, '--parent', str(self.root / 'parent/training.pt'))

    def test_parent_restores_ft_bound_and_continues_latest_instead_of_selected(self):
        with patch.object(nnue_stage, 'evaluate', side_effect=[dict(loss=0.0), dict(loss=1.0), dict(loss=2.0)]):
            parent = self.train('wide')
        self.assertEqual(parent['best_epoch'], 0)
        self.assertTrue(any(not torch.equal(parent['best_model'][key], value)
                            for key, value in parent['model'].items()))
        parent['ft_bound'] = 4
        parent['model']['ft_bias'].fill_(3)
        torch.save(parent, self.root / 'wide/training.pt')
        child = self.train('wide-child', 1, '--parent', str(self.root / 'wide/training.pt'), '--max-batches', '1')
        self.assertEqual(child['ft_bound'], 4)
        self.assertGreater(child['model']['ft_bias'].min().item(), 1)

    def test_rewritten_boundaries_cannot_hide_overlapping_training_rows(self):
        store = self.root / 'store'
        first = nnue_shards.freeze_range(store, 'block-0', self.root / 'first', positions=20)
        overlap = nnue_shards.freeze_range(store, 'block-0', self.root / 'overlap', start=19, positions=20)
        following = nnue_shards.freeze_range(store, 'block-0', self.root / 'following', start=20, positions=20)
        self.manifests = [first, overlap, following]
        parent = self.train('parent')
        old_hashes = set(parent['seen_shards'])
        self.assertFalse(old_hashes & {s['sha256'] for s in nnue_shards.read_manifest(overlap)['shards']})
        with self.assertRaisesRegex(ValueError, 'overlaps previously trained source rows'):
            self.train('bad-child', 1, '--parent', str(self.root / 'parent/training.pt'))
        child = self.train('good-child', 2, '--parent', str(self.root / 'parent/training.pt'))
        self.assertEqual(child['seen_ranges'], [dict(block='block-0', start=0, stop=20),
                                               dict(block='block-0', start=20, stop=40)])

    def test_legacy_parent_requires_manifest_and_explicit_safe_reader_transition(self):
        parent = self.train('parent')
        parent.pop('seen_ranges')
        parent['contract'].pop('source_range')
        parent['contract']['implementation']['nnue_stage.py'] = 'prior-reader'
        path = self.root / 'parent/training.pt'
        torch.save(parent, path)
        with self.assertRaisesRegex(ValueError, 'implementation'):
            self.train('unapproved', 1, '--parent', str(path))
        options = ['--parent', str(path), '--allow-parent-reader-update', '--parent-manifest', str(self.manifests[0])]
        child = self.train('verified', 1, *options)
        self.assertEqual(len(child['seen_ranges']), 2)
        with self.assertRaisesRegex(ValueError, 'manifest checksum'):
            self.train('wrong-manifest', 1, '--parent', str(path), '--allow-parent-reader-update',
                       '--parent-manifest', str(self.manifests[1]))
        parent['contract']['implementation']['nnue_model.py'] = 'different-neural-model'
        torch.save(parent, path)
        with self.assertRaisesRegex(ValueError, 'implementation'):
            self.train('unsafe', 1, *options)

    @unittest.skipUnless(torch.cuda.is_available(), 'CUDA hardware unavailable')
    def test_cuda_forward_matches_integer_export_and_resume(self):
        self.train('gpu', 0, '--device', 'cuda', '--max-batches', '1')
        self.train('gpu', 0, '--device', 'cuda', '--resume')
        candidate = torch.load(self.root / 'gpu/candidate.pt', weights_only=True)
        from maxima_runner.nnue_model import halfkp_model
        from maxima_runner import nnue_format
        model = halfkp_model(2, 2, 'halfkav2_hm').double().cuda()
        model.load_state_dict(candidate['model'])
        integer = nnue_format.integer_network(nnue_format.serialize(model.integer_weights(), 'halfkav2_hm'))
        with torch.no_grad():
            actual = model(*nnue_stage.inputs(torch, model, self.expected[0])).tolist()
        self.assertEqual(actual, [integer.evaluate(chess.Board(r['fen']))[1] for r in self.expected[0]])


if __name__ == '__main__':
    unittest.main()
