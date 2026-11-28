"""Cycle recovery, training lineage, plateau policy and independently replayed SPRT."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import chess
import chess.pgn

from maxima_runner import nnue_cycle as cycle, sprt


class cycle_tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        def file(name, value):
            path = self.root / name
            path.write_text(json.dumps(value))
            return cycle.artifact(path)
        self.file = file
        initial = {key: file(key, {'initial': key}) for key in ('champion', 'checkpoint', 'parent_manifest')}
        self.config = dict(version=1, state_dir=str(self.root/'state'),
                           policy=dict(batch_positions=50, plateau_positions=100, progress_positions=10),
                           initial=initial, promotion_targets=['first', 'second'],
                           commands={key: ['unused'] for key in (*cycle.stages, 'notify')},
                           test=dict(elo0=0, elo1=10, alpha=.05, beta=.05, nodes=5000, hash_mb=16,
                                     max_plies=600, seed=41, max_pairs=1000,
                                     engine=file('engine', {}), openings=file('openings', {})))
        self.path = self.root/'config.json'
        self.path.write_text(json.dumps(self.config))
        self.worker = cycle.training_cycle(self.path)

    def test_restart_recovers_stage_and_rejects_configuration_drift(self):
        self.worker.state['stage'] = 'train'
        self.worker.state['receipts']['generate'] = {'durable': True}
        self.worker.persist()
        recovered = cycle.training_cycle(self.path)
        self.assertEqual(recovered.state, self.worker.state)
        self.config['policy']['batch_positions'] = 51
        self.path.write_text(json.dumps(self.config))
        with self.assertRaisesRegex(ValueError, 'configuration changed'):
            cycle.training_cycle(self.path)

    def test_fault_latches_and_notification_survives_delivery_outage(self):
        with patch.object(self.worker, 'invoke', side_effect=RuntimeError('private child failure')):
            with self.assertRaises(RuntimeError):
                self.worker.tick()
            self.assertEqual(self.worker.state['hold'], 'fault')
            self.assertTrue(self.worker.state['notice']['alert'])
            recovered = cycle.training_cycle(self.path)
            with patch.object(recovered, 'invoke', side_effect=RuntimeError('delivery unavailable')):
                self.assertFalse(recovered.tick())
                self.assertIsNotNone(recovered.state['notice'])

    def test_notification_queue_survives_outage_and_deduplicates_after_restart(self):
        self.worker.notify('first event', key='first')
        self.worker.notify('incident', alert=True, key='second')
        recovered = cycle.training_cycle(self.path)
        with patch.object(recovered, 'invoke', side_effect=RuntimeError('delivery unavailable')):
            recovered.flush_notice()
        recovered = cycle.training_cycle(self.path)
        recovered.notify('first event', key='first')
        received = []
        def deliver(stage):
            self.assertEqual(stage, 'notify')
            received.append(recovered.state['notice'].copy())
            return {'status': 'complete'}
        with patch.object(recovered, 'invoke', side_effect=deliver):
            recovered.flush_notice()
            recovered.flush_notice()
        recovered.notify('incident', alert=True, key='second')
        self.assertIsNone(recovered.state['notice'])
        self.assertEqual(len({notice['id'] for notice in received}), 2)
        self.assertTrue(received[1]['alert'])

    def test_progress_skips_old_milestones_and_survives_restart(self):
        self.worker.report_progress('generate', {'progress': {'positions': 35, 'target': 50}})
        recovered = cycle.training_cycle(self.path)
        recovered.report_progress('generate', {'progress': {'positions': 36, 'target': 50}})
        self.assertEqual(len(recovered.state['notice_ids']), 1)
        recovered.report_progress('generate', {'progress': {'positions': 41, 'target': 50}})
        self.assertEqual(len(recovered.state['notice_ids']), 2)
        self.assertEqual(recovered.state['stage'], 'generate')
        with self.assertRaises(ValueError):
            recovered.report_progress('generate', {'progress': {'positions': 51, 'target': 50}})

    def test_stage_start_is_announced_once_without_completing_work(self):
        for stage in ('train', 'test'):
            self.worker.state['stage'] = stage
            for _ in range(2):
                self.worker.report_progress(stage, {'started': True})
        recovered = cycle.training_cycle(self.path)
        recovered.report_progress('test', {'started': True})
        self.assertEqual(len(recovered.state['notice_ids']), 2)
        self.assertEqual(recovered.state['receipts'], {})
        self.assertEqual(recovered.state['stage'], 'test')

    def test_rejection_keeps_champion_and_continues_latest_optimizer_then_plateau(self):
        state = self.worker.state
        champion = copy.deepcopy(state['champion'])
        checkpoint = self.file('new-checkpoint', {})
        manifest = self.file('new-manifest', {})
        for iteration in (1, 2):
            state.update(stage='test', receipts={'train': dict(checkpoint=checkpoint, manifest=manifest)})
            with patch.object(self.worker, 'validate', side_effect=lambda stage, receipt: receipt.update(decision='accept_h0') if stage == 'test' else None), patch.object(self.worker, 'invoke', return_value={'status':'complete','receipt':{'iteration':iteration+1}}):
                self.worker.tick()
                self.assertEqual(state['stage'], 'advance')
                self.worker.tick()
            self.assertEqual(state['champion'], champion)
            self.assertEqual(state['checkpoint'], checkpoint)
        self.assertEqual(state['hold'], 'plateau')
        self.assertEqual(state['without_promotion'], 100)

    def test_partial_target_deployment_cannot_change_champion(self):
        state = self.worker.state
        state['receipts'] = {'train': {'network': self.file('network', {})}, 'test': {'decision':'accept_h1'}}
        result = dict(champion_sha256=state['receipts']['train']['network']['sha256'],
                      previous_sha256=state['champion']['sha256'], targets={'first':'wrong'})
        with self.assertRaisesRegex(ValueError, 'deployment'):
            self.worker.validate('promote', result)

    def test_training_requires_new_parent_final_export_backup_and_stopped_gpu(self):
        state = self.worker.state
        checkpoint = self.file('trained', {})
        manifest = self.file('manifest', {})
        network = self.file('network', {})
        state['receipts']['generate'] = {'manifest':manifest}
        completion = dict(format='maxima.nnue.stage.complete', checkpoint_sha256=checkpoint['sha256'],
                          contract=dict(parent_sha256=state['checkpoint']['sha256'], manifest_sha256=manifest['sha256']))
        export = dict(selection='latest_completed', source_checkpoint_sha256=checkpoint['sha256'], network_sha256=network['sha256'])
        receipt = dict(checkpoint=checkpoint, manifest=manifest, network=network,
                       completion=self.file('complete', completion), export=self.file('export', export),
                       cleanup=self.file('cleanup',dict(compute_stopped=True,backup_verified=True)))
        self.worker.validate('train', receipt)
        for key, change in (('cleanup',dict(compute_stopped=False,backup_verified=True)),
                            ('export',dict(export,selection='validation_best')),
                            ('completion',dict(completion,contract=dict(completion['contract'],parent_sha256='wrong')))):
            broken = dict(receipt, **{key:self.file('broken-'+key,change)})
            with self.assertRaisesRegex(ValueError, 'training lineage'):
                self.worker.validate('train', broken)

    def match(self):
        state, recipe = self.worker.state, self.config['test']
        recipe['openings'] = self.file('opening-list', {'variant':'standard','positions':[{'id':'start','fen':chess.STARTING_FEN}]})
        # Actual read_openings schema uses an array named openings.
        Path(recipe['openings']['path']).write_text(json.dumps({'variant':'standard','openings':[{'id':'start','fen':chess.STARTING_FEN}]}))
        recipe['openings'] = cycle.artifact(recipe['openings']['path'])
        network = self.file('trained-network', {})
        state['receipts']['train'] = {'network':network}
        models = (network, state['champion'])
        metadata = [dict(models={'EvalFile':model}, options=dict(EvalFile=model['path'], Threads=1,Hash=16,OwnBook=False,Ponder=False,UseExperience=False,UCI_AnalyseMode=False,SyzygyProbeLimit=0)) for model in models]
        identities = [dict(sha256=recipe['engine']['sha256'],models={'EvalFile':model},requested_options={'EvalFile':model['path']}) for model in models]
        games, pgns = [], []
        for pair in range(1000):
            for color, moves, result in [('white','e2e4 e7e5 d1h5 b8c6 f1c4 g8f6 h5f7','1-0'),('black','f2f3 e7e5 g2g4 d8h4','0-1')]:
                board = chess.Board();game = chess.pgn.Game();node = game
                for move in moves.split():
                    played=chess.Move.from_uci(move);self.assertIn(played,board.legal_moves);board.push(played);node=node.add_variation(played)
                game.headers.update(Result=result,NodeLimit='5000')
                pgns.append(str(game)+'\n\n')
                games.append(dict(pair=pair,a_color=color,opening='start',result=result,error=None,engines=metadata))
            if sprt.sprt_summary(games,0,10)['decision'] != 'continue':
                break
        report = dict(nodes=5000,hash_mb=16,max_plies=600,seed=41,max_pairs=1000,
                      openings_sha256=recipe['openings']['sha256'],opening_sampling='with_replacement',pairs=None,
                      stopping_mode='sprt',variant='standard',win_adjudication=None,syzygy_path=None,futility_alpha=None,
                      sprt=dict(elo0=0,elo1=10,alpha=.05,beta=.05),engines=identities,games=games,
                      selected_openings=['start']*(len(games)//2),complete=True)
        pgn=self.root/'games.pgn';pgn.write_text(''.join(pgns))
        return {'report':self.file('report',report),'pgn':cycle.artifact(pgn)}, report, state, recipe

    def test_promotion_uses_replayed_games_not_report_claim(self):
        result, report, state, recipe = self.match()
        self.assertEqual(cycle.validate_test(result,state,recipe),'accept_h1')
        report['games'][0]['result']='0-1'
        result['report']=self.file('forged-report',report)
        with self.assertRaisesRegex(ValueError,'PGN'):
            cycle.validate_test(result,state,recipe)

    def test_changed_pgn_or_extra_games_cannot_promote(self):
        result, report, state, recipe = self.match()
        path=Path(result['pgn']['path']);path.write_text(path.read_text()+'*\n\n')
        with self.assertRaisesRegex(ValueError,'artifact'):
            cycle.validate_test(result,state,recipe)

class latest_export_tests(unittest.TestCase):
    def test_completed_checkpoint_exports_final_weights_and_keeps_recovery_file(self):
        try:
            import torch
        except ImportError:
            self.skipTest('training dependencies are optional')
        import random
        import sys
        from maxima_runner import nnue_data, nnue_shards, nnue_stage
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);board=chess.Board();rng=random.Random(19);rows=[]
            for i in range(64):
                rows.append(dict(fen=board.fen(),score_cp=i,result_stm=.5,split='validation' if i%3==0 else 'train',source=dict(sha256='a'*64,game=1,ply=i)))
                rows[-1]['split']=nnue_data.position_split(board)
                board.push(rng.choice(list(board.legal_moves)))
            data=root/'data';nnue_data.save_dataset(data,dict(variant='standard',score_pov='side_to_move'),rows)
            nnue_shards.ingest(root/'store','block',[data],target=64)
            manifest=nnue_shards.seal(root/'store','block')
            arguments=['stage',str(manifest),'--output',str(root/'training'),'--width','1','--hidden','1','--batch-size','2','--shuffle-size','4','--metrics-positions','8']
            with patch.object(sys,'argv',arguments+['--max-batches','1']):self.assertEqual(nnue_stage.main(),0)
            with patch.object(sys,'argv',arguments+['--resume']):self.assertEqual(nnue_stage.main(),0)
            checkpoint=root/'training/training.pt';before=cycle.artifact(checkpoint)
            with patch.object(sys,'argv',['export',str(checkpoint),'--latest-checkpoint','--manifest',str(manifest),'--output',str(root/'export'),'--positions','4']):
                self.assertEqual(nnue_stage.export_main(),0)
            report=json.loads((root/'export/report.json').read_text())
            self.assertEqual(report['source_checkpoint_sha256'],before['sha256'])
            self.assertEqual(report['selection'],'latest_completed')
            self.assertEqual(cycle.artifact(checkpoint),before)
            state=torch.load(checkpoint,weights_only=True)
            self.assertTrue(state['optimizer']);self.assertIn('rng',state)
