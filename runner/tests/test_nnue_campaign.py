"""Campaign resource admission must survive restart without relaxing limits."""
from pathlib import Path
import tempfile
import unittest
from argparse import Namespace

from maxima_runner import nnue_campaign as campaign


class campaign_tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.teacher = self.root / 'teacher'
        self.teacher.write_bytes(b'pinned teacher')
        self.now = 1_800_000_000
        self.store = self.root / 'store'
        self.initial = campaign.initialize(self.store, self.teacher, expiry=self.now + 10 * 86400,
                                           credit_evidence='fixture only; not real account verification', now=self.now)

    def ready(self, name):
        campaign.admit_block(self.store, name, self.now)
        campaign.transact(self.store, lambda state, stamp: state['blocks'][name].update(phase='ready'), self.now)

    def test_backlog_opens_as_soon_as_training_starts_without_waiting_for_test(self):
        self.ready('one')
        with self.assertRaisesRegex(ValueError, 'backlog'):
            campaign.admit_block(self.store, 'two', self.now)
        campaign.reserve(self.store, 'gpu-one', 'gpu', 'one', 200, 3600, self.now)
        campaign.start_training(self.store, 'one', 'gpu-one', now=self.now)
        campaign.admit_block(self.store, 'two', self.now)
        with self.assertRaisesRegex(ValueError, 'backlog'):
            campaign.admit_block(self.store, 'three', self.now)
        state = campaign.status(self.store, self.now)
        self.assertEqual(state['blocks']['one']['phase'], 'training')
        self.assertEqual(state['blocks']['two']['phase'], 'generating')
        self.assertEqual(state['champion'], self.initial['original_teacher'])

    def test_category_budget_and_existing_online_commitment(self):
        campaign.admit_block(self.store, 'one', self.now)
        campaign.reserve(self.store, 'cpu-one', 'cpu', 'one', 17000, 3600, self.now)
        before = (self.store / 'campaign.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'budget'):
            campaign.reserve(self.store, 'cpu-two', 'cpu', 'one', 1, 100, self.now)
        self.assertEqual(before, (self.store / 'campaign.json').read_bytes())
        state = campaign.status(self.store, self.now)
        self.assertEqual(state['remaining_cents']['online'], 0)
        self.assertEqual(state['remaining_cents']['cpu'], 0)
        campaign.settle(self.store, 'cpu-one', 1000, self.now + 30)
        self.assertEqual(campaign.status(self.store, self.now + 30)['remaining_cents']['cpu'], 16000)

    def test_expired_allocation_persists_stop_and_can_be_settled(self):
        campaign.admit_block(self.store, 'one', self.now)
        campaign.reserve(self.store, 'cpu', 'cpu', 'one', 100, 60, self.now)
        with self.assertRaisesRegex(ValueError, 'stopped'):
            campaign.allocation(self.store, 'cpu', 'cpu', 'one', self.now + 60)
        self.assertTrue((self.store / 'STOP').exists())
        self.assertIn('cpu', campaign.status(self.store, self.now + 60)['release_compute'])
        campaign.settle(self.store, 'cpu', 105, self.now + 61)
        self.assertEqual(campaign.status(self.store, self.now + 61)['spent_cents']['cpu'], 105)
        with self.assertRaisesRegex(ValueError, 'stopped'):
            campaign.admit_block(self.store, 'two', self.now + 61)

    def test_campaign_producer_cannot_start_without_an_allocation(self):
        from maxima_runner import nnue_produce
        campaign.admit_block(self.store, 'one', self.now)
        args = Namespace(store=self.store, block='one', target=100_000_000, lease=None)
        with self.assertRaisesRegex(ValueError, 'allocation'):
            nnue_produce.generation_gate(args)
        campaign.reserve(self.store, 'cpu', 'cpu', 'one', 100, 3600, self.now)
        args.lease = 'cpu'
        self.assertEqual(nnue_produce.generation_gate(args), self.now + 3600)
        (self.store / 'STOP').touch()
        with self.assertRaisesRegex(ValueError, 'stopped'):
            nnue_produce.generation_gate(args)

    def test_completion_receipt_preserves_latest_checkpoint_and_rejects_corruption(self):
        self.ready('one')
        campaign.reserve(self.store, 'gpu', 'gpu', 'one', 100, 3600, self.now)
        campaign.start_training(self.store, 'one', 'gpu', now=self.now)
        campaign.transact(self.store, lambda state, stamp: state['blocks']['one'].update(manifest_sha256='manifest'), self.now)
        stage = self.root / 'stage'
        stage.mkdir()
        (stage / 'training.pt').write_bytes(b'latest experimental checkpoint')
        (stage / 'candidate.pt').write_bytes(b'selected candidate')
        from maxima_runner.nnue_data import file_hash
        receipt = dict(format='maxima.nnue.stage.complete', version=1,
                       contract=dict(manifest_sha256='manifest'), checkpoint_sha256=file_hash(stage / 'training.pt'),
                       candidate_sha256=file_hash(stage / 'candidate.pt'))
        block = campaign.finish_training(self.store, 'one', stage, receipt, self.now)
        self.assertEqual(block['phase'], 'trained')
        self.assertEqual(campaign.status(self.store, self.now)['latest_checkpoint_sha256'], receipt['checkpoint_sha256'])
        (stage / 'candidate.pt').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            campaign.finish_training(self.store, 'one', stage, receipt, self.now)

    def test_audit_records_changes_but_read_only_status_does_not_grow_log(self):
        campaign.admit_block(self.store, 'one', self.now)
        campaign.reserve(self.store, 'cpu', 'cpu', 'one', 100, 3600, self.now)
        state = campaign.status(self.store, self.now)
        self.assertEqual([event['action'] for event in state['events']],
                         ['initialized', 'block_admitted', 'allocation_reserved'])
        before = (self.store / 'campaign.json').read_bytes()
        campaign.status(self.store, self.now)
        self.assertEqual(before, (self.store / 'campaign.json').read_bytes())
        self.assertIn('completed_playing_test', campaign.review(self.store, 'one')['missing_evidence'])

    def test_credit_expiry_and_unknown_eligibility_block_new_spend(self):
        store = self.root / 'unverified'
        campaign.initialize(store, self.teacher, now=self.now)
        campaign.admit_block(store, 'one', self.now)
        with self.assertRaisesRegex(ValueError, 'verified credit'):
            campaign.reserve(store, 'cpu', 'cpu', 'one', 100, 100, self.now)
        campaign.admit_block(self.store, 'one', self.now)
        with self.assertRaisesRegex(ValueError, '48-hour'):
            campaign.reserve(self.store, 'cpu', 'cpu', 'one', 100, 9 * 86400, self.now)
        state = campaign.status(self.store, self.now + 8 * 86400)
        self.assertEqual(state['stop'], 'credit_safety_deadline')
        self.assertTrue((self.store / 'STOP').exists())

    def test_model_snapshot_survives_original_file_change_and_gpu_idle_admission_denied(self):
        saved = self.store / 'models' / f"{self.initial['champion']}.nnue"
        self.teacher.write_bytes(b'changed original')
        self.assertEqual(saved.read_bytes(), b'pinned teacher')
        campaign.admit_block(self.store, 'one', self.now)
        with self.assertRaisesRegex(ValueError, 'ready'):
            campaign.reserve(self.store, 'idle', 'gpu', 'one', 100, 60, self.now)
        self.assertEqual(campaign.status(self.store, self.now)['allocations'], {})


if __name__ == '__main__':
    unittest.main()
