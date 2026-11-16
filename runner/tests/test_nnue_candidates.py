"""A report cannot promote changed models, stale baselines or unverified games."""
import copy
import json
from pathlib import Path
import tempfile
import time
import unittest

import chess
import chess.pgn

from maxima_runner import nnue_campaign as campaign, nnue_candidates as candidates, nnue_data


class candidate_tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = self.root / 'store'
        self.engine = self.root / 'engine'
        self.engine.write_bytes(b'fixture engine')
        self.teacher = self.root / 'teacher'
        self.teacher.write_bytes(b'fixture original teacher')
        campaign.initialize(self.store, self.teacher, expiry=time.time() + 10 * 86400,
                            credit_evidence='fixture, not real credit verification')
        model = self.root / 'candidate'
        model.write_bytes(b'fixture candidate')
        self.digest = campaign.snapshot(self.store, model)
        self.openings = self.root / 'openings.json'
        self.openings.write_text(json.dumps(dict(variant='standard', openings=[dict(id='fixture')])) )
        # Deliberately permissive thresholds exercise controller transitions only.
        candidates.configure(self.store, self.engine, self.openings, 1, 10, 0.1, 0, 100, .49, .49)
        campaign.transact(self.store, lambda state, stamp: state['candidates'].update(
            {self.digest: dict(phase='verified', network_sha256=self.digest)}))
        campaign.reserve(self.store, 'test-lease', 'tests', self.digest, 100, 3600)
        self.record = candidates.prepare(self.store, self.digest, 'comparison', 'test-lease')
        recipe = self.record['recipe']
        self.report = {key: recipe[key] for key in ('pairs', 'seconds', 'increment', 'seed', 'openings_sha256', 'clock_timing')}
        self.report.update(max_plies=600, timeout=30, variant='standard', stopping_mode='sprt', complete=True,
                           sprt=dict(elo0=recipe['elo0'], elo1=recipe['elo1'], alpha=recipe['alpha'], beta=recipe['sprt_beta']),
                           futility_alpha=recipe['futility_alpha'],
                           selected_openings=['fixture'], games=[], engines=[])
        for digest in (self.digest, self.record['champion']):
            path = str((self.store / 'models' / f'{digest}.nnue').resolve())
            self.report['engines'].append(dict(sha256=recipe['engine_sha256'], requested_options=dict(EvalFile=path),
                                              models=dict(EvalFile=dict(path=path, sha256=digest))))
        games = []
        for color, moves in [('white', 'e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7'),
                             ('black', 'f2f3 e7e5 g2g4 d8h4')]:
            board = chess.Board()
            for text in moves.split():
                board.push_uci(text)
            game = chess.pgn.Game.from_board(board)
            games.append(str(game))
            self.report['games'].append(dict(pair=0, opening='fixture', a_color=color,
                                             result=game.headers['Result'], error=None, engines=[
                dict(models=dict(EvalFile=engine['models']['EvalFile']), options=dict(
                    EvalFile=engine['models']['EvalFile']['path'], Hash=64, Threads=1))
                for engine in self.report['engines']]))
        Path(self.record['pgn']).write_text('\n\n'.join(games) + '\n\n')

    def write(self):
        Path(self.record['report']).write_text(json.dumps(self.report))

    def test_recomputed_pass_promotes_and_stops_without_forcing_next_block(self):
        self.report['summary'] = dict(sprt=dict(decision='accept_h0'))  # ignored
        self.write()
        result = candidates.finish(self.store, 'comparison')
        self.assertEqual(result['decision'], 'accept_h1')
        state = campaign.status(self.store)
        self.assertEqual(state['champion'], self.digest)
        self.assertEqual(state['stop'], 'original_teacher_strength_target_reached')
        self.assertTrue((self.store / 'STOP').exists())
        self.assertEqual(candidates.finish(self.store, 'comparison'), result)

    def test_incomplete_and_inconclusive_never_replace_champion(self):
        self.report['complete'] = False
        self.report['summary'] = dict(sprt=dict(decision='accept_h1'))
        self.write()
        self.assertEqual(candidates.finish(self.store, 'comparison')['decision'], 'inconclusive')
        state = campaign.status(self.store)
        self.assertEqual(state['champion'], state['original_teacher'])
        self.assertIsNone(state['stop'])
        self.assertIsNone(state['active_test'])

    def test_phase_review_hold_blocks_promotion_even_after_statistical_pass(self):
        campaign.transact(self.store, lambda state, stamp: state.update(promotion_hold='endgame review pending'))
        self.write()
        result = candidates.finish(self.store, 'comparison')
        self.assertEqual(result['decision'], 'accept_h1')
        self.assertEqual(result['promotion_held'], 'endgame review pending')
        state = campaign.status(self.store)
        self.assertEqual(state['champion'], state['original_teacher'])
        self.assertIsNone(state['stop'])
        self.assertEqual(state['candidates'][self.digest]['phase'], 'awaiting_review')

    def test_rejection_retains_champion(self):
        with Path(self.record['pgn']).open() as stream:
            first, second = chess.pgn.read_game(stream), chess.pgn.read_game(stream)
        Path(self.record['pgn']).write_text(str(second) + '\n\n' + str(first) + '\n\n')
        self.report['games'][0]['result'] = second.headers['Result']
        self.report['games'][1]['result'] = first.headers['Result']
        self.write()
        self.assertEqual(candidates.finish(self.store, 'comparison')['decision'], 'accept_h0')
        state = campaign.status(self.store)
        self.assertEqual(state['champion'], state['original_teacher'])
        self.assertIsNone(state['stop'])

    def test_changed_provenance_or_pair_records_rejected_without_state_change(self):
        for change in ('engine', 'model', 'options', 'opening', 'color', 'clock', 'clock_timing'):
            report = copy.deepcopy(self.report)
            if change == 'engine':
                report['engines'][0]['sha256'] = 'wrong'
            elif change == 'model':
                report['engines'][0]['models']['EvalFile']['sha256'] = 'wrong'
            elif change == 'options':
                report['engines'][0]['requested_options']['Extra'] = 'wrong'
            elif change == 'opening':
                report['games'][0]['opening'] = 'wrong'
            elif change == 'color':
                report['games'][1]['a_color'] = 'white'
            elif change == 'clock_timing':
                report['clock_timing'] = 'request_wall_time'
            else:
                report['seconds'] = 11
            before = (self.store / 'campaign.json').read_bytes()
            with self.assertRaises(ValueError, msg=change):
                candidates.validate_report(self.record, report)
            self.assertEqual(before, (self.store / 'campaign.json').read_bytes())

    def test_no_parallel_decisions_and_stale_baseline_cannot_promote(self):
        with self.assertRaisesRegex(ValueError, 'active'):
            candidates.prepare(self.store, self.digest, 'second', 'test-lease')
        self.write()
        campaign.transact(self.store, lambda state, stamp: state.update(champion='changed'))
        with self.assertRaisesRegex(ValueError, 'stale'):
            candidates.finish(self.store, 'comparison')
        candidates.abandon(self.store, 'comparison')
        self.assertIsNone(campaign.status(self.store)['active_test'])

    def test_stopped_campaign_can_record_result_without_promoting(self):
        self.write()
        (self.store / 'STOP').touch()
        candidates.finish(self.store, 'comparison')
        state = campaign.status(self.store)
        self.assertEqual(state['champion'], state['original_teacher'])
        self.assertEqual(state['stop'], 'external_stop')


if __name__ == '__main__':
    unittest.main()
