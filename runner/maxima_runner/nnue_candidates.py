"""Frozen candidates and paired SPRT decisions pinned to a campaign champion."""
import argparse
import json
import hashlib
from pathlib import Path
import random
import subprocess
import sys

import chess.pgn

from . import matches, nnue_campaign as campaign, nnue_data, sprt


def configure(root, engine, openings, pairs, seconds, increment, elo0, elo1, alpha, beta, seed=2026, futility_alpha=0.01):
    campaign.positive_integer(pairs)
    from .strength import validate_min_elo
    if elo0 < 0 or elo1 <= 0:
        raise ValueError('promotion SPRT must test a positive gain against a nonnegative null')
    if futility_alpha:
        validate_min_elo(0, pairs, futility_alpha)
    if not 0 <= futility_alpha < beta:
        raise ValueError('futility error allocation must be nonnegative and below total rejection beta')
    sprt.validate_sprt(elo0, elo1, alpha, beta)
    matches.positive_float(str(seconds))
    matches.nonnegative_float(str(increment))
    if len(matches.read_openings(openings, 'standard')) < pairs:
        raise ValueError('insufficient distinct paired openings')
    recipe = dict(engine=str(engine.resolve()), engine_sha256=nnue_data.file_hash(engine),
                  openings=str(openings.resolve()), openings_sha256=nnue_data.file_hash(openings),
                  pairs=pairs, seconds=seconds, increment=increment, seed=seed,
                  clock_timing='uci_go_to_bestmove_monotonic',
                  elo0=elo0, elo1=elo1, alpha=alpha, beta=beta,
                  futility_alpha=futility_alpha or None, sprt_beta=beta - futility_alpha)

    def operation(state, stamp):
        campaign.require_running(state)
        if 'test_recipe' in state and state['test_recipe'] != recipe:
            raise ValueError('test recipe is immutable; do not change hypotheses after seeing outcomes')
        state['test_recipe'] = recipe
        return recipe
    return campaign.transact(root, operation, event='test_recipe_frozen')


def register(root, block_name, export, probe):
    root, export = Path(root), Path(export)
    state = campaign.status(root)
    block = state['blocks'][block_name]
    if block['phase'] != 'trained':
        raise ValueError('candidate requires a completed campaign stage')
    report = json.loads((export / 'report.json').read_text())
    digest = nnue_data.file_hash(export / 'network.nnue')
    if (report.get('format') != 'maxima.nnue.stage.export' or report.get('version') != 1
            or report['network_sha256'] != digest
            or report['candidate_sha256'] != block['stage']['candidate_sha256']
            or report.get('export_checked_positions', 0) <= 0):
        raise ValueError('export provenance does not match the frozen stage candidate')
    frozen = campaign.snapshot(root, export / 'network.nnue')
    network = root / 'models' / f'{frozen}.nnue'
    probe_digest = nnue_data.file_hash(probe)
    command = [sys.executable, '-m', 'maxima_runner', 'nnue:verify', str(network.resolve()),
               '--probe', str(probe.resolve())]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise ValueError(f'native candidate verification failed: {result.stderr[-1000:]}')
    if nnue_data.file_hash(network) != digest or nnue_data.file_hash(probe) != probe_digest:
        raise ValueError('candidate changed during native verification')

    def operation(state, stamp):
        campaign.require_running(state)
        if digest in state['candidates']:
            return state['candidates'][digest]
        entry = dict(block=block_name, network_sha256=digest, phase='verified', registered_at=stamp,
                     native_probe_sha256=probe_digest, native_output=result.stdout.strip(),
                     export_report_sha256=nnue_data.file_hash(export / 'report.json'),
                     export_checked_positions=report['export_checked_positions'],
                     validation_loss=report.get('validation_loss'), selected_epoch=report.get('selected_epoch'))
        state['candidates'][digest] = entry
        return entry
    return campaign.transact(root, operation, event='candidate_native_verified')


def prepare(root, candidate, test_id, lease):
    root = Path(root).resolve()
    campaign.identifier(test_id)

    def operation(state, stamp):
        campaign.check_allocation(state, lease, 'tests', candidate, stamp)
        if state['active_test'] is not None or test_id in state['tests']:
            raise ValueError('another test is active or test identity already exists')
        entry = state['candidates'][candidate]
        if entry['phase'] != 'verified':
            raise ValueError('candidate is not awaiting a test')
        recipe = state['test_recipe']
        for key in ('engine', 'openings'):
            if nnue_data.file_hash(recipe[key]) != recipe[key + '_sha256']:
                raise ValueError('pinned test input changed')
        opponent = state['champion']
        for digest in (candidate, opponent):
            if nnue_data.file_hash(root / 'models' / f'{digest}.nnue') != digest:
                raise ValueError('pinned test model changed')
        directory = root / 'tests'
        directory.mkdir(exist_ok=True)
        prefix = directory / test_id
        command = [sys.executable, '-m', 'maxima_runner', 'match:run',
                   '--engine-a', recipe['engine'], '--engine-b', recipe['engine'],
                   '--model-a', f'EvalFile={root / "models" / (candidate + ".nnue")}',
                   '--model-b', f'EvalFile={root / "models" / (opponent + ".nnue")}',
                   '--openings', recipe['openings'], '--pairs', str(recipe['pairs']),
                   '--seconds', str(recipe['seconds']), '--increment', str(recipe['increment']),
                   '--seed', str(recipe['seed']), '--sprt', str(recipe['elo0']), str(recipe['elo1']),
                   '--sprt-alpha', str(recipe['alpha']), '--sprt-beta', str(recipe['sprt_beta']),
                   '--pgn', str(prefix.with_suffix('.pgn')), '--json', str(prefix.with_suffix('.json')),
                   '--stop-file', str(root / 'STOP'), '--stop-at', str(state['allocations'][lease]['deadline'])]
        if recipe['futility_alpha'] is not None:
            command.extend(['--futility-alpha', str(recipe['futility_alpha'])])
        record = dict(candidate=candidate, champion=opponent, recipe=recipe, command=command,
                      lease=lease, phase='prepared', report=str(prefix.with_suffix('.json')),
                      pgn=str(prefix.with_suffix('.pgn')))
        state['tests'][test_id] = record
        state['active_test'] = test_id
        entry['phase'] = 'testing'
        return record
    return campaign.transact(root, operation, event='test_prepared')


def validate_report(record, report):
    recipe = record['recipe']
    for key in ('pairs', 'seconds', 'increment', 'seed', 'openings_sha256', 'clock_timing'):
        if report.get(key) != recipe[key]:
            raise ValueError(f'test recipe mismatch: {key}')
    expected_sprt = {key: recipe[key] for key in ('elo0', 'elo1', 'alpha')}
    expected_sprt['beta'] = recipe['sprt_beta']
    if report.get('futility_alpha') != recipe['futility_alpha']:
        raise ValueError('early rejection policy changed')
    if report.get('max_plies') != 600 or report.get('timeout') != 30:
        raise ValueError('test search limits changed')
    if (report.get('variant') != 'standard' or report.get('stopping_mode') != 'sprt'
            or any(report.get('sprt', {}).get(key) != value for key, value in expected_sprt.items())
            or report.get('win_adjudication') is not None or report.get('syzygy_path') is not None
            or report.get('sequential_check_pairs') is not None or report.get('minimum_gain') is not None):
        raise ValueError('test stopping rules or adjudication changed')
    engines = report.get('engines', [])
    if len(engines) != 2:
        raise ValueError('missing pinned engines')
    for engine, model in zip(engines, (record['candidate'], record['champion'])):
        if (engine.get('sha256') != recipe['engine_sha256'] or set(engine.get('requested_options', {})) != {'EvalFile'}
                or set(engine.get('models', {})) != {'EvalFile'}
                or engine['models']['EvalFile']['sha256'] != model
                or engine['requested_options']['EvalFile'] != engine['models']['EvalFile']['path']):
            raise ValueError('engine/model/options mismatch')
    games = report.get('games', [])
    if len(games) > recipe['pairs'] * 2:
        raise ValueError('too many games')
    selected = random.Random(recipe['seed']).sample(matches.read_openings(Path(recipe['openings']), 'standard'), recipe['pairs'])
    if report.get('selected_openings') != [name for name, board in selected]:
        raise ValueError('paired opening selection changed')
    # Replay PGNs independently; a report flag alone cannot authorize promotion.
    with Path(record['pgn']).open() as stream:
        for index, game_record in enumerate(games):
            game = chess.pgn.read_game(stream)
            pair, color = index // 2, 'white' if index % 2 == 0 else 'black'
            if (game is None or game.errors or game_record.get('pair') != pair
                    or game_record.get('a_color') != color or game_record.get('error')
                    or game_record.get('opening') != selected[pair][0]
                    or game.headers.get('Result') != game_record.get('result')):
                raise ValueError('invalid paired game/PGN record')
            metadata = game_record.get('engines', [])
            if len(metadata) != 2:
                raise ValueError('missing applied engine settings')
            applied = []
            for engine, model in zip(metadata, (record['candidate'], record['champion'])):
                options = dict(engine.get('options', {}))
                if (engine.get('models', {}).get('EvalFile', {}).get('sha256') != model
                        or options.get('Hash') != 64 or options.get('Threads') != 1
                        or any(options.get(key, False) for key in ('OwnBook', 'Ponder', 'UseExperience', 'UCI_AnalyseMode'))
                        or options.get('SyzygyProbeLimit', 0) != 0
                        or options.get('Wild', 'standard') != 'standard'):
                    raise ValueError('applied match settings differ from campaign policy')
                if options.pop('EvalFile', None) != str(Path(record['report']).parent.parent / 'models' / f'{model}.nnue'):
                    raise ValueError('applied model path changed')
                applied.append(options)
            if applied[0] != applied[1]:
                raise ValueError('engine settings differ beyond model')
            board = game.board()
            if board.fen() != selected[pair][1].fen():
                raise ValueError('game started from a different opening')
            board = selected[pair][1].copy()
            for move in game.mainline_moves():
                if move not in board.legal_moves:
                    raise ValueError('illegal PGN move')
                board.push(move)
            outcome = board.outcome(claim_draw=True)
            if report.get('complete') and (outcome is None or outcome.result() != game_record['result']):
                raise ValueError('unfinished, flagged or nonterminal game cannot authorize promotion')
        if chess.pgn.read_game(stream) is not None:
            raise ValueError('extra PGN games')
    computed = sprt.sprt_summary(games, **expected_sprt)
    if not report.get('complete'):
        return 'inconclusive'
    counts = [0] * 5
    futile = False
    scores = {'1-0': 2, '1/2-1/2': 1, '0-1': 0}
    for offset in range(0, len(games) - 1, 2):
        counts[scores[games[offset]['result']] + 2 - scores[games[offset + 1]['result']]] += 1
        llr = sprt.logistic_llr(recipe['elo0'], recipe['elo1'], counts)
        if recipe['futility_alpha'] is not None:
            from .strength import futility_summary
            futile = futility_summary(games[:offset + 2], recipe['pairs'], recipe['futility_alpha'])['decision'] == 'futility_rejection'
        if ((llr >= computed['upper_bound'] or llr <= computed['lower_bound'] or futile)
                and offset + 2 != len(games)):
            raise ValueError('match continued after its first SPRT decision')
    if futile:
        return 'futility_rejection'
    if computed['decision'] == 'continue':
        if len(games) != recipe['pairs'] * 2:
            raise ValueError('test ended without a statistical or pair-budget stopping boundary')
        return 'inconclusive'
    return computed['decision']


def finish(root, test_id):
    root = Path(root)
    record = campaign.status(root)['tests'][test_id]
    if record['phase'] == 'complete':
        return record
    report_path = Path(record['report'])
    raw = report_path.read_bytes()
    raw_digest = hashlib.sha256(raw).hexdigest()
    report = json.loads(raw)
    for model in (record['candidate'], record['champion']):
        if nnue_data.file_hash(root / 'models' / f'{model}.nnue') != model:
            raise ValueError('registered model changed before decision')
    decision = validate_report(record, report)

    def operation(state, stamp):
        if state['active_test'] != test_id or record['champion'] != state['champion']:
            raise ValueError('test has no active decision or used a stale champion')
        entry = state['tests'][test_id]
        entry.update(phase='complete', decision=decision, report_sha256=raw_digest,
                     pgn_sha256=nnue_data.file_hash(record['pgn']))
        state['active_test'] = None
        state['candidates'][record['candidate']]['phase'] = decision
        if decision == 'accept_h1' and state['stop'] is None:
            state['champion'] = record['candidate']
            if record['champion'] == state['original_teacher']:
                campaign.stop(root, state, 'original_teacher_strength_target_reached')
        return entry
    return campaign.transact(root, operation, event='test_decided')



def run_test(root, test_id):
    root = Path(root)
    campaign.identifier(test_id)
    with campaign.writer_lock(root / 'tests' / f'{test_id}.run.lock'):
        return execute_test(root, test_id)


def execute_test(root, test_id):
    state = campaign.status(root)
    record = state['tests'][test_id]
    campaign.allocation(root, record['lease'], 'tests', record['candidate'])
    if state['active_test'] != test_id or record['phase'] != 'prepared':
        raise ValueError('test is not prepared for execution')
    for key in ('engine', 'openings'):
        if nnue_data.file_hash(record['recipe'][key]) != record['recipe'][key + '_sha256']:
            raise ValueError('pinned test input changed')
    with (root / 'tests' / f'{test_id}.log').open('ab') as log:
        subprocess.run(record['command'], stdout=log, stderr=subprocess.STDOUT)
    return finish(root, test_id)


def abandon(root, test_id):
    def operation(state, stamp):
        if state['active_test'] != test_id:
            raise ValueError('test is not active')
        entry = state['tests'][test_id]
        entry.update(phase='complete', decision='abandoned')
        state['candidates'][entry['candidate']]['phase'] = 'abandoned'
        state['active_test'] = None
        return entry
    return campaign.transact(root, operation, event='test_abandoned')


def main():
    parser = argparse.ArgumentParser(prog='maxima nnue:candidates', description=__doc__)
    parser.add_argument('--store', type=Path, required=True)
    commands = parser.add_subparsers(dest='action', required=True)
    recipe = commands.add_parser('recipe')
    recipe.add_argument('--engine', type=Path, required=True)
    recipe.add_argument('--openings', type=Path, required=True)
    recipe.add_argument('--pairs', type=int, required=True)
    recipe.add_argument('--seconds', type=float, required=True)
    recipe.add_argument('--increment', type=float, required=True)
    recipe.add_argument('--sprt', type=float, nargs=2, required=True)
    recipe.add_argument('--alpha', type=float, default=0.05)
    recipe.add_argument('--beta', type=float, default=0.05, help='total rejection allowance, shared by SPRT and futility')
    recipe.add_argument('--futility-alpha', type=float, default=0.01)
    add = commands.add_parser('register')
    add.add_argument('--block', required=True)
    add.add_argument('--export', type=Path, required=True)
    add.add_argument('--probe', type=Path, required=True)
    test = commands.add_parser('prepare')
    test.add_argument('--candidate', required=True)
    test.add_argument('--test', required=True)
    test.add_argument('--lease', required=True)
    execute = commands.add_parser('run')
    execute.add_argument('--test', required=True)
    abort = commands.add_parser('abandon')
    abort.add_argument('--test', required=True)
    done = commands.add_parser('finish')
    done.add_argument('--test', required=True)
    args = parser.parse_args()
    if args.action == 'recipe':
        result = configure(args.store, args.engine, args.openings, args.pairs, args.seconds, args.increment,
                           *args.sprt, args.alpha, args.beta, futility_alpha=args.futility_alpha)
    elif args.action == 'register':
        result = register(args.store, args.block, args.export, args.probe)
    elif args.action == 'prepare':
        result = prepare(args.store, args.candidate, args.test, args.lease)
    elif args.action == 'run':
        result = run_test(args.store, args.test)
    elif args.action == 'finish':
        result = finish(args.store, args.test)
    else:
        result = abandon(args.store, args.test)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0
