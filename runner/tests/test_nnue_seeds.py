"""Logged-score selection and fresh searched labels from replayable starting histories."""
import asyncio
import io
import json
from pathlib import Path
import random
import tempfile
import unittest

import chess
import chess.engine
import chess.pgn

from maxima_runner import nnue_data, nnue_seeds, nnue_selfplay, nnue_shards
from test_nnue_selfplay import fools_mate_engine


class seed_tests(unittest.TestCase):
    def game(self, color=chess.WHITE, rise=False):
        game=chess.pgn.Game();game.headers['LearningColor']='white' if color else 'black'
        board=game.board();node=game;rng=random.Random(9)
        before=16 if color else 17
        for ply in range(24):
            move=rng.choice(list(board.legal_moves));node=node.add_variation(move)
            if ply in (before-2,before,before+2):
                score=0 if ply<=before else (200 if rise else -200)
                node.set_eval(chess.engine.PovScore(chess.engine.Cp(score),chess.WHITE),30)
            board.push(move)
        return game,before


    def test_annotations_do_not_change_the_source_game_family(self):
        game,_=self.game()
        first=nnue_seeds.select(game,min_ply=0)[0]['source_game']
        game.comment='Additional analysis of the same played game'
        second=nnue_seeds.select(game,min_ply=0)[0]['source_game']
        self.assertEqual(first,second)


    def test_scan_deduplicates_copied_games_and_load_rejects_fen_mismatch(self):
        game,_=self.game()
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'games.pgn';path.write_text(str(game)+'\n\n'+str(game)+'\n\n')
            report=nnue_seeds.scan([path],min_ply=0)
            self.assertTrue(report['starts'])
            keys=[nnue_data.position_key(chess.Board(seed['fen'])) for seed in report['starts']]
            self.assertEqual(len(keys),len(set(keys)))
            output=Path(directory)/'starts.json';output.write_text(json.dumps(report))
            self.assertEqual(len(nnue_seeds.load(output)),len(report['starts']))
            report['starts'][0]['fen']=chess.STARTING_FEN;output.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError,'history'):
                nnue_seeds.load(output)

    def test_seeded_continuations_use_new_scores_and_keep_source_game_split(self):
        board=chess.Board();history=['f2f3','e7e5']
        for move in history:board.push_uci(move)
        start={'initial_fen':chess.STARTING_FEN,'moves':history,'fen':board.fen(en_passant='fen'),
               'source_game':'a'*64,'selection':{'after_cp':-999}}
        config={'seed':23,'nodes':10000,'random_plies':0,'book_plies':0,'min_ply':0,
                'max_plies':4,'score_limit':None,'network_sha256':'b'*64}
        rows=[]
        for index in (1,2):
            text,produced,_,_=asyncio.run(nnue_selfplay.play_game(fools_mate_engine(10),config,index,set(),starts=[start]))
            self.assertTrue(produced)
            self.assertEqual({row['score_cp'] for row in produced},{10})
            self.assertEqual({row['source']['seed_game'] for row in produced},{'a'*64})
            game=chess.pgn.read_game(io.StringIO(text))
            self.assertEqual(game.headers['SeedPly'],'2')
            self.assertEqual(list(game.mainline_moves())[:2],[chess.Move.from_uci(move) for move in history])
            rows.extend(produced)
        self.assertEqual(len({row['split'] for row in rows}),1)
        self.assertNotEqual(rows[0]['source']['sha256'],rows[-1]['source']['sha256'])

    def test_seeded_dataset_round_trip_resume_and_shard_ingestion(self):
        import hashlib
        board=chess.Board();history=['f2f3','e7e5']
        for move in history:board.push_uci(move)
        start={'initial_fen':chess.STARTING_FEN,'moves':history,'fen':board.fen(en_passant='fen'),'source_game':'a'*64}
        config={'seed':23,'nodes':10000,'random_plies':0,'book_plies':0,'min_ply':0,'max_plies':4,
                'score_limit':None,'network_sha256':'b'*64,'start_positions_sha256':'c'*64}
        text,rows,filters,plies=asyncio.run(nnue_selfplay.play_game(fools_mate_engine(),config,1,set(),starts=[start]))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);game=root/'game-000001';game.mkdir()
            (game/'game.pgn').write_text(text)
            header=dict(config,variant='standard',score_pov='side_to_move',split_method='game',label_source='completed_exact_iteration')
            dataset=game/'positions.mxdata';nnue_data.save_dataset(dataset,header,rows)
            report={'game':1,'config':config,'positions':len(rows),'plies':plies,'filters':filters,
                    'pgn_sha256':hashlib.sha256(text.encode()).hexdigest(),'data_sha256':nnue_data.file_hash(dataset)}
            (game/'report.json').write_text(json.dumps(report))
            saved,_,games=nnue_selfplay.load_run(root,config)
            self.assertEqual(len(saved),len(rows));self.assertEqual(len(games),1)
            self.assertEqual(saved[0]['source']['seed_game'],'a'*64)
            store=root/'store';self.assertEqual(nnue_shards.ingest(store,'seeds',[dataset],target=len(rows)),len(rows))
            self.assertEqual(nnue_shards.read_manifest(nnue_shards.seal(store,'seeds'))['positions'],len(rows))

    def test_existing_game_splits_remain_unchanged(self):
        import hashlib
        source={'sha256':'c'*64,'game':17}
        digest=hashlib.sha256((source['sha256']+':17').encode()).digest()
        expected='validation' if int.from_bytes(digest[:8],'big')%10==0 else 'train'
        self.assertEqual(nnue_data.game_split(source),expected)
        with self.assertRaises(ValueError):nnue_shards.source_split(dict(source,seed_game='../bad'))
