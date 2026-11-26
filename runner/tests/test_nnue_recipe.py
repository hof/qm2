"""Recipe quotas, recovery and actual worker-to-UCI-to-canonical-store integration."""
import base64
from contextlib import closing
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import chess

from maxima_runner import nnue_data, nnue_recipe, nnue_selfplay, nnue_shards
from test_nnue_produce import seeded_engine


class recipe_tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.engine=self.root/'engine';self.engine.write_text('#!'+sys.executable+'\n'+seeded_engine);self.engine.chmod(0o700)
        self.network=self.root/'network';self.network.write_bytes(b'fixture network, unused by protocol fixture')
        self.config=dict(version=1,output='run',store='store',block='one',campaign='fixture',
                         engine='engine',network='network',nodes=10000,workers=2,require_coordinator=False,job_positions=2,job_games=1,job_seconds=10,
                         sources=[dict(id='first',positions=1),dict(id='second',positions=1)])
        self.path=self.root/'recipe.json';self.write_config()

    def write_config(self):
        self.path.write_text(json.dumps(self.config))

    def chunk(self, controller, job, rows):
        path=self.root/(job['id']+'.mxdata')
        header=dict(variant='standard',score_pov='side_to_move',split_method='position',nodes=10000,
                    generation_recipe=controller.identity,generation_source=job['source'],
                    engine_sha256=controller.state['engine'],network_sha256=controller.state['network'],
                    generator_sha256=controller.state['generator_sha256'],label_source='completed_exact_iteration')
        nnue_data.save_dataset(path,header,rows)
        raw=path.read_bytes()
        return dict(op='submit',worker=job['worker'],job=job['id'],sha256=hashlib.sha256(raw).hexdigest(),data=base64.b64encode(raw).decode())

    def rows(self, count=4):
        board=chess.Board();rng=random.Random(17);rows=[]
        for index in range(count):
            rows.append(dict(fen=board.fen(en_passant='fen'),score_cp=17,depth=3,nodes=8,result_stm=.5,
                             split=nnue_data.position_split(board),source=dict(sha256='a'*64,game=0,ply=index)))
            board.push(rng.choice(list(board.legal_moves)))
        return rows

    def test_concurrent_submissions_obey_source_quotas_deduplicate_and_resume(self):
        controller=nnue_recipe.recipe(self.path)
        jobs=[controller.handle(dict(op='claim',worker=name))['job'] for name in ('a','b')]
        requests=[self.chunk(controller,j,self.rows()) for j in jobs]
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(2) as pool:results=list(pool.map(controller.handle,requests))
        status=nnue_recipe.recipe(self.path).handle(dict(op='status'))['status']
        self.assertEqual(status['counts'],dict(first=1,second=0))
        self.assertFalse(status['complete'])
        controller.handle(requests[0])
        job=controller.handle(dict(op='claim',worker='a'))['job']
        controller.handle(self.chunk(controller,job,self.rows()[1:]))
        status=nnue_recipe.recipe(self.path).handle(dict(op='status'))['status']
        self.assertTrue(status['complete'])
        self.assertEqual(status['counts'],dict(first=1,second=1))
        self.assertEqual(nnue_shards.read_manifest(status['manifest']['path'])['positions'],2)

    def test_crash_after_ingestion_recovers_counts_from_committed_shards(self):
        controller=nnue_recipe.recipe(self.path);job=controller.handle(dict(op='claim',worker='a'))['job']
        request=self.chunk(controller,job,self.rows())
        original=nnue_recipe.save
        def fail_state(path,value):
            if path==controller.state_path:raise OSError('simulated interrupted state publication')
            return original(path,value)
        with patch.object(nnue_recipe,'save',side_effect=fail_state):
            with self.assertRaises(OSError):controller.handle(request)
        recovered=nnue_recipe.recipe(self.path)
        status=recovered.handle(dict(op='status'))['status']
        self.assertEqual(status['counts'],dict(first=1,second=0))
        recovered.handle(request)
        self.assertEqual(recovered.handle(dict(op='status'))['status']['positions'],1)

    def test_unexpected_writer_or_changed_recipe_cannot_silently_reassign_data(self):
        controller=nnue_recipe.recipe(self.path);controller.handle(dict(op='status'))
        path=self.root/'external.mxdata';nnue_data.save_dataset(path,dict(variant='standard',score_pov='side_to_move'),self.rows()[:1])
        nnue_shards.ingest(controller.store,'one',[path],target=2)
        with self.assertRaises(ValueError):controller.handle(dict(op='status'))
        self.config['sources'][0]['positions']=2;self.write_config()
        with self.assertRaises(ValueError):nnue_recipe.recipe(self.path).handle(dict(op='status'))

    def test_failed_admission_blocks_generation_without_changing_the_batch(self):
        self.config['admission_command']=[sys.executable,'-c','raise SystemExit(1)'];self.write_config()
        controller=nnue_recipe.recipe(self.path)
        result=controller.handle(dict(op='claim',worker='a'))
        self.assertIsNone(result['job']);self.assertFalse(result['status']['allowed'])
        self.assertEqual(result['status']['positions'],0)
        self.assertFalse((controller.store/'one.json').exists())

    def test_missing_or_stale_coordinator_cannot_admit_worker_jobs(self):
        self.config['require_coordinator']=True;self.write_config()
        controller=nnue_recipe.recipe(self.path)
        self.assertIsNone(controller.handle(dict(op='claim',worker='a'))['job'])
        controller.handle(dict(op='tick'))
        self.assertIsNotNone(controller.handle(dict(op='claim',worker='a'))['job'])
        controller.state['owner']['at']=0
        nnue_recipe.save(controller.state_path,controller.state)
        self.assertFalse(nnue_recipe.recipe(self.path).handle(dict(op='status'))['status']['allowed'])

    def test_worker_binary_must_be_approved_and_completed_data_cannot_be_replaced(self):
        controller=nnue_recipe.recipe(self.path)
        with self.assertRaises(ValueError):
            controller.handle(dict(op='claim',worker='other',engine_sha256='0'*64))
        job=controller.handle(dict(op='claim',worker='one'))['job']
        request=self.chunk(controller,job,self.rows())
        controller.handle(request)
        with self.assertRaises(ValueError):
            controller.handle(dict(request,sha256='0'*64))

    def test_existing_data_requires_explicit_adoption_and_remains_unchanged(self):
        data=self.root/'existing.mxdata'
        nnue_data.save_dataset(data,dict(variant='standard',score_pov='side_to_move'),self.rows()[:1])
        store=self.root/'store'
        nnue_shards.ingest(store,'one',[data],target=2)
        with self.assertRaises(ValueError):nnue_recipe.recipe(self.path).handle(dict(op='status'))
        self.config['adopt_existing']='first';self.write_config()
        status=nnue_recipe.recipe(self.path).handle(dict(op='status'))['status']
        self.assertEqual(status['counts'],dict(first=1,second=0))
        self.assertEqual(nnue_shards.ingest(store,'one',[data],target=2),1)

    def test_check_does_not_create_state_or_start_an_engine(self):
        result=subprocess.run([sys.executable,'-m','maxima_runner','nnue:generate','--config',str(self.path),'--check'],
                              capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse((self.root/'run').exists())
        self.assertFalse((self.root/'store').exists())

    def test_public_local_workflow_runs_real_worker_processes_and_seals_both_sources(self):
        self.run_public_workflow()

    def test_external_worker_cli_uses_transport_and_seals_the_same_canonical_store(self):
        self.run_public_workflow(external=True)

    def run_public_workflow(self, external=False):
        self.config['require_coordinator']=True
        for number,source in enumerate(self.config['sources']):
            initial=chess.Board()
            if number:
                initial.remove_piece_at(chess.B1);initial.set_piece_at(chess.A3,chess.Piece(chess.KNIGHT,chess.WHITE))
            initial.fullmove_number=9;board=initial.copy()
            for move in ('f2f3','e7e5'):board.push_uci(move)
            path=self.root/(source['id']+'.json')
            path.write_text(json.dumps(dict(format='maxima.nnue.starts',version=1,starts=[dict(
                initial_fen=initial.fen(),moves=['f2f3','e7e5'],fen=board.fen(en_passant='fen'),
                source_game=('a' if not number else 'b')*64)])))
            source['input']=path.name
        self.write_config()
        args=[sys.executable,'-m','maxima_runner','nnue:generate','--config',str(self.path)]
        if external:
            coordinator=subprocess.Popen(args+['--coordinator'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            self.addCleanup(lambda:coordinator.kill() if coordinator.poll() is None else None)
            worker=self.root/'worker.json'
            worker.write_text(json.dumps(dict(version=1,id='separate',output=str(self.root/'worker'),
                engine=str(self.engine),network=str(self.network),slots=2,coordinator=args+['--rpc'])))
            run=subprocess.run([sys.executable,'-m','maxima_runner','nnue:generate','--worker-config',str(worker)],
                               capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stderr)
            stdout,stderr=coordinator.communicate(timeout=15)
            result=subprocess.CompletedProcess(args,coordinator.returncode,stdout,stderr)
        else:
            result=subprocess.run(args,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        status=json.loads(result.stdout)
        self.assertTrue(status['complete'])
        self.assertEqual(status['counts'],dict(first=1,second=1))
        manifest=nnue_shards.read_manifest(status['manifest']['path'])
        self.assertEqual(manifest['positions'],2)
        from maxima_runner.nnue_binary import binary_dataset
        for shard in manifest['shards']:
            with binary_dataset(self.root/'store'/shard['path']) as data:
                self.assertEqual(data[0]['score_cp'],17)
                self.assertEqual(data[0]['result_stm'],0)
                archive=data.header['generation_archive']
                saved=self.root/'run/games'/(archive+'.tar.gz')
                self.assertEqual(nnue_data.file_hash(saved),archive)
