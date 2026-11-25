"""Recoverable training cycles using privately configured, idempotent stage adapters."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import time

import chess.pgn

from . import matches, nnue_data, nnue_shards, sprt
from .learning_store import atomic_text, writer_lock


stages = ('generate', 'train', 'verify', 'test', 'promote', 'advance')


def artifact(path):
    path = Path(path).resolve()
    return dict(path=str(path), sha256=nnue_data.file_hash(path))


def checked(value):
    path = Path(value['path'])
    if not path.is_absolute() or nnue_data.file_hash(path) != value['sha256']:
        raise ValueError('cycle artifact is missing or changed')
    return path


def save(path, value):
    atomic_text(path, json.dumps(value, sort_keys=True) + '\n')


def validate_test(result, state, recipe):
    """Recompute whole-pair SPRT and legally replay PGNs before promotion."""
    report = json.loads(checked(result['report']).read_text())
    pgn = checked(result['pgn'])
    openings_path = checked(recipe['openings'])
    engine = checked(recipe['engine'])
    expected = {key: recipe[key] for key in ('elo0', 'elo1', 'alpha', 'beta')}
    for key in ('nodes', 'hash_mb', 'max_plies', 'seed', 'max_pairs'):
        if report.get(key) != recipe[key]:
            raise ValueError('test recipe changed: ' + key)
    if (report.get('openings_sha256') != recipe['openings']['sha256']
            or report.get('opening_sampling') != 'with_replacement' or report.get('pairs') is not None
            or report.get('stopping_mode') != 'sprt' or report.get('variant') != 'standard'
            or report.get('win_adjudication') is not None or report.get('syzygy_path') is not None
            or report.get('futility_alpha') is not None
            or any(report.get('sprt', {}).get(k) != v for k, v in expected.items())):
        raise ValueError('test provenance or stopping policy changed')
    models = (state['receipts']['train']['network'], state['champion'])
    identities = report.get('engines', [])
    if len(identities) != 2:
        raise ValueError('missing test engine identities')
    for identity, model in zip(identities, models):
        checked(model)
        if (identity.get('sha256') != recipe['engine']['sha256']
                or identity.get('models', {}).get('EvalFile') != model
                or identity.get('requested_options') != {'EvalFile': model['path']}):
            raise ValueError('test engine or model changed')
    games = report['games']
    if len(games) > 2 * recipe['max_pairs']:
        raise ValueError('test exceeded its pair budget')
    openings = matches.read_openings(openings_path, 'standard')
    rng = random.Random(recipe['seed'])
    selected = [rng.choice(openings) for _ in range((len(games) + 1) // 2)]
    if report.get('selected_openings')[:len(selected)] != [name for name, _ in selected]:
        raise ValueError('test opening selection changed')
    with pgn.open() as stream:
        for index, row in enumerate(games):
            game = chess.pgn.read_game(stream)
            name, position = selected[index // 2]
            if (game is None or game.errors or row.get('error') or row.get('pair') != index // 2
                    or row.get('a_color') != ('white' if index % 2 == 0 else 'black')
                    or row.get('opening') != name or game.headers.get('Result') != row.get('result')
                    or game.headers.get('NodeLimit') != str(recipe['nodes'])
                    or game.board().fen() != position.root().fen()):
                raise ValueError('invalid test game or PGN')
            board = game.board()
            moves = list(game.mainline_moves())
            if moves[:len(position.move_stack)] != position.move_stack:
                raise ValueError('opening history changed')
            for move in moves:
                if move not in board.legal_moves:
                    raise ValueError('illegal test move')
                board.push(move)
            outcome = board.outcome(claim_draw=True)
            if row['result'] == '*' or outcome is None or outcome.result() != row['result']:
                raise ValueError('nonterminal or failed test game')
            settings = []
            metadata = row.get('engines', [])
            if len(metadata) != 2:
                raise ValueError('missing applied test settings')
            for item, model in zip(metadata, models):
                options = dict(item['options'])
                if (item.get('models', {}).get('EvalFile') != model
                        or options.pop('EvalFile', None) != model['path']
                        or options.get('Threads') != 1 or options.get('Hash') != recipe['hash_mb']
                        or any(options.get(k, False) for k in ('OwnBook', 'Ponder', 'UseExperience', 'UCI_AnalyseMode'))
                        or options.get('SyzygyProbeLimit', 0) != 0):
                    raise ValueError('applied test settings changed')
                settings.append(options)
            if settings[0] != settings[1]:
                raise ValueError('asymmetric test settings')
        if chess.pgn.read_game(stream) is not None:
            raise ValueError('extra test PGNs')
    counts = [0] * 5
    scores = {'1-0': 2, '1/2-1/2': 1, '0-1': 0}
    lower = math.log(recipe['beta'] / (1 - recipe['alpha']))
    upper = math.log((1 - recipe['beta']) / recipe['alpha'])
    for offset in range(0, len(games) - 1, 2):
        counts[scores[games[offset]['result']] + 2 - scores[games[offset + 1]['result']]] += 1
        llr = sprt.logistic_llr(recipe['elo0'], recipe['elo1'], counts)
        if (llr <= lower or llr >= upper) and offset + 2 != len(games):
            raise ValueError('test continued beyond its first decision')
    summary = sprt.sprt_summary(games, **expected)
    if not report.get('complete'):
        return 'inconclusive'
    if summary['decision'] == 'continue':
        if len(games) != 2 * recipe['max_pairs']:
            raise ValueError('test ended without its declared boundary')
        return 'inconclusive'
    return summary['decision']


class training_cycle:
    def __init__(self, config_path):
        self.config_path = Path(config_path).resolve()
        self.config = json.loads(self.config_path.read_text())
        if self.config.get('version') != 1 or set(self.config['commands']) != set(stages) | {'notify'}:
            raise ValueError('invalid cycle configuration')
        self.root = Path(self.config['state_dir'])
        self.root.mkdir(parents=True, exist_ok=True)
        self.path = self.root / 'state.json'
        policy = self.config['policy']
        if (type(policy['batch_positions']) is not int or policy['batch_positions'] <= 0
                or policy['plateau_positions'] < policy['batch_positions']):
            raise ValueError('invalid training or plateau budget')
        test = self.config['test']
        sprt.validate_sprt(test['elo0'], test['elo1'], test['alpha'], test['beta'])
        if test['elo0'] < 0 or test['elo1'] <= 0 or min(test['nodes'], test['max_pairs']) <= 0:
            raise ValueError('invalid promotion test')
        self.digest = hashlib.sha256(self.config_path.read_bytes()).hexdigest()
        if self.path.exists():
            self.state = json.loads(self.path.read_text())
            if self.state['config_sha256'] != self.digest:
                raise ValueError('cycle configuration changed; migrate explicitly')
        else:
            initial = self.config['initial']
            for name in ('champion', 'checkpoint', 'parent_manifest'):
                checked(initial[name])
            self.state = dict(version=1, config_sha256=self.digest, iteration=1, stage='generate',
                              champion=initial['champion'], checkpoint=initial['checkpoint'],
                              parent_manifest=initial['parent_manifest'], receipts={},
                              without_promotion=0, hold=None, notice=None, completed=[])
            self.persist()

    def persist(self):
        save(self.path, self.state)

    def invoke(self, stage):
        command = self.config['commands'][stage]
        if not command or any(not isinstance(word, str) for word in command):
            raise ValueError('stage command must be an argument list')
        request = dict(config=str(self.config_path), state=self.state, root=str(self.root))
        run = subprocess.run(command, input=json.dumps(request), text=True, capture_output=True,
                             timeout=self.config.get('command_timeout', 60),
                             env=dict(os.environ, **self.config.get('environment', {})))
        if run.returncode:
            logs = self.root / 'adapter-logs'
            logs.mkdir(exist_ok=True)
            atomic_text(logs / f"{self.state['iteration']:06d}-{stage}.log", run.stdout + '\n' + run.stderr)
            # Child logs may contain credentials; do not include stdout/stderr in notifications.
            raise RuntimeError('cycle adapter failed: ' + stage + '; inspect private stage logs')
        result = json.loads(run.stdout)
        if result.get('status') not in ('complete', 'pending', 'blocked'):
            raise ValueError('invalid cycle adapter response')
        return result

    def notify(self, message, alert=False):
        self.state['notice'] = dict(message=message, alert=alert,
                                    id=f"cycle-{self.state['iteration']}-{self.state['stage']}-{hashlib.sha256(message.encode()).hexdigest()[:12]}")
        self.persist()

    def flush_notice(self):
        if self.state['notice']:
            try:
                result = self.invoke('notify')
                if result['status'] == 'complete':
                    self.state['notice'] = None
                    self.persist()
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
                pass  # Notice is durable; a delivery outage never loses the incident.

    def validate(self, stage, receipt):
        state = self.state
        if stage == 'generate':
            manifest_path = checked(receipt['manifest'])
            manifest = nnue_shards.read_manifest(manifest_path)
            if (manifest['positions'] != self.config['policy']['batch_positions'] or manifest.get('partial')
                    or receipt['champion_sha256'] != state['champion']['sha256']):
                raise ValueError('generation batch or champion mismatch')
        elif stage == 'train':
            for key in ('checkpoint', 'completion', 'manifest', 'network', 'export', 'cleanup'):
                checked(receipt[key])
            complete = json.loads(checked(receipt['completion']).read_text())
            export = json.loads(checked(receipt['export']).read_text())
            cleanup = json.loads(checked(receipt['cleanup']).read_text())
            if (complete.get('format') != 'maxima.nnue.stage.complete'
                    or complete['checkpoint_sha256'] != receipt['checkpoint']['sha256']
                    or complete['contract']['parent_sha256'] != state['checkpoint']['sha256']
                    or complete['contract']['manifest_sha256'] != state['receipts']['generate']['manifest']['sha256']
                    or receipt['manifest'] != state['receipts']['generate']['manifest']
                    or export.get('selection') != 'latest_completed'
                    or export.get('source_checkpoint_sha256') != receipt['checkpoint']['sha256']
                    or receipt['checkpoint']['sha256'] == state['checkpoint']['sha256']
                    or export['network_sha256'] != receipt['network']['sha256']
                    or not cleanup.get('compute_stopped') or not cleanup.get('backup_verified')):
                raise ValueError('training lineage, latest export, backup or GPU cleanup mismatch')
        elif stage == 'verify':
            proof = json.loads(checked(receipt['proof']).read_text())
            if (proof.get('network_sha256') != state['receipts']['train']['network']['sha256']
                    or proof.get('verified') is not True or proof.get('positions', 0) <= 0):
                raise ValueError('native verification required')
        elif stage == 'test':
            receipt['decision'] = validate_test(receipt, state, self.config['test'])
        elif stage == 'promote':
            candidate = state['receipts']['train']['network']
            if (state['receipts']['test']['decision'] != 'accept_h1'
                    or receipt.get('champion_sha256') != candidate['sha256']
                    or receipt.get('previous_sha256') != state['champion']['sha256']
                    or set(receipt.get('targets', {})) != set(self.config['promotion_targets'])
                    or any(value != candidate['sha256'] for value in receipt['targets'].values())):
                raise ValueError('incomplete champion deployment')
        elif stage == 'advance':
            if receipt.get('iteration') != state['iteration'] + 1:
                raise ValueError('next generation was not prepared')

    def tick(self):
        self.flush_notice()
        state = self.state
        if state['hold']:
            return False
        if self.config.get('stop_at') is not None and time.time() >= self.config['stop_at']:
            state['hold'] = 'lease_expired'
            self.notify('Self-training paused: its configured worker lease expired.', alert=True)
            return False
        stage = state['stage']
        try:
            result = self.invoke(stage)
            if result['status'] != 'complete':
                reason = result.get('reason') if result['status'] == 'blocked' else None
                if reason and state.get('blocked') != reason:
                    state['blocked'] = reason
                    self.notify(reason, alert=True)
                return False
            state.pop('blocked', None)
            receipt = result['receipt']
            self.validate(stage, receipt)
            state['receipts'][stage] = receipt
            save(self.root / f"iteration-{state['iteration']:06d}-{stage}.json", receipt)
            if stage == 'test' and receipt['decision'] != 'accept_h1':
                state['stage'] = 'advance'
            elif stage == 'promote':
                state['champion'] = state['receipts']['train']['network']
                state['without_promotion'] = 0
                state['stage'] = 'advance'
                self.notify('A new champion passed SPRT and is deployed on all runners.')
            elif stage == 'advance':
                trained = state['receipts']['train']
                state['checkpoint'], state['parent_manifest'] = trained['checkpoint'], trained['manifest']
                promoted = 'promote' in state['receipts']
                if not promoted:
                    state['without_promotion'] += self.config['policy']['batch_positions']
                state['completed'].append(dict(iteration=state['iteration'], promoted=promoted,
                                               decision=state['receipts']['test']['decision']))
                state['iteration'] += 1
                state['stage'], state['receipts'] = 'generate', {}
                if state['without_promotion'] >= self.config['policy']['plateau_positions']:
                    state['hold'] = 'plateau'
                    self.notify('Self-training paused for plateau review; online play continues.', alert=True)
                else:
                    self.notify('Training iteration finished; the next fresh position batch is starting.')
            else:
                state['stage'] = stages[stages.index(stage) + 1]
            self.persist()
            return True
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired):
            state['hold'] = 'fault'
            self.notify('Self-training paused after a cycle failure at ' + stage + '. Inspect the private stage logs; online runner safety remains active.', alert=True)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--once', action='store_true')
    args = parser.parse_args()
    root = Path(json.loads(args.config.read_text())['state_dir'])
    root.mkdir(parents=True, exist_ok=True)
    with writer_lock(root / '.cycle.lock'):
        cycle = training_cycle(args.config)
        while True:
            try:
                cycle.tick()
            except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired):
                if args.once:
                    raise
                print('Self-training is held; inspect the durable cycle state and private adapter logs.', file=sys.stderr)
            if args.once:
                return 0
            time.sleep(cycle.config.get('poll_seconds', 15))
