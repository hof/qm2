"""Persistent NNUE campaign admission, allocation ledger and graceful stop policy."""
import argparse
import copy
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import tempfile
import time

from . import nnue_data, nnue_shards
from .learning_store import writer_lock


budget_cents = dict(online=2000, cpu=17000, gpu=2500, storage=1800, tests=1000, margin=700)


def atomic_json(path, value):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile('w', dir=path.parent, prefix=f'.{path.name}.', delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, sort_keys=True, indent=2, allow_nan=False)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def identifier(value):
    if not isinstance(value, str) or not value or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in value):
        raise ValueError('identity must contain only letters, digits, underscore or hyphen')
    return value


def positive_integer(value):
    if type(value) is not int or value <= 0:
        raise ValueError('expected a positive integer')
    return value


def snapshot(root, source):
    """Retain immutable model bytes independently of mutable run paths."""
    source = Path(source)
    digest = nnue_data.file_hash(source)
    directory = root / 'models'
    directory.mkdir(exist_ok=True)
    target = directory / f'{digest}.nnue'
    if target.exists():
        if nnue_data.file_hash(target) != digest:
            raise ValueError('registered model bytes changed')
        return digest
    with tempfile.NamedTemporaryFile(dir=directory, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with source.open('rb') as original:
                for block in iter(lambda: original.read(1024 * 1024), b''):
                    stream.write(block)
            stream.flush()
            os.fsync(stream.fileno())
            if nnue_data.file_hash(temporary) != digest:
                raise ValueError('source model changed during registration')
            os.link(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
    return digest


def initialize(root, teacher, *, expiry=None, credit_evidence=None, now=None):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    now = time.time() if now is None else now
    if expiry is not None and (not math.isfinite(expiry) or expiry <= now + 48 * 3600 or not credit_evidence):
        raise ValueError('verified eligible credit expiry must leave more than 48 hours of safety')
    if expiry is None and credit_evidence:
        raise ValueError('credit evidence requires an exact expiry')
    with writer_lock(root / '.campaign.lock'):
        if (root / 'campaign.json').exists() or (root / 'STOP').exists():
            raise ValueError('campaign already exists or store has a stop signal')
        from contextlib import closing
        with closing(nnue_shards.connect(root)) as db:
            store_id = db.execute("SELECT value FROM settings WHERE key='store_id'").fetchone()[0]
        champion = snapshot(root, teacher)
        state = dict(format='maxima.nnue.campaign', version=1, store_id=store_id,
                     original_teacher=champion, champion=champion, created_at=now,
                     credit_expiry=expiry, credit_evidence=credit_evidence,
                     stop=None, blocks={}, allocations={}, candidates={}, tests={}, active_test=None,
                     budget_cents=budget_cents, committed_cents=dict(online=2000),
                     spent_cents={key: 0 for key in budget_cents})
        atomic_json(root / 'campaign.json', state)
        return state


def load(root):
    state = json.loads((root / 'campaign.json').read_text())
    if state.get('format') != 'maxima.nnue.campaign' or state.get('version') != 1:
        raise ValueError('unsupported campaign state')
    return state


def stop(root, state, reason):
    if state['stop'] is None:
        state['stop'] = reason
    # Signal producers before publishing the decision. A crash errs toward stopping.
    (root / 'STOP').touch(exist_ok=True)


def refresh(root, state, now):
    if (root / 'STOP').exists() and state['stop'] is None:
        stop(root, state, 'external_stop')
    if state['credit_expiry'] is not None and now >= state['credit_expiry'] - 48 * 3600:
        stop(root, state, 'credit_safety_deadline')
    if any(lease['status'] == 'reserved' and now >= lease['deadline'] for lease in state['allocations'].values()):
        stop(root, state, 'allocation_deadline: settle costs and release compute')


def transact(root, operation, now=None):
    root = Path(root)
    now = time.time() if now is None else now
    with writer_lock(root / '.campaign.lock'):
        state = load(root)
        original = copy.deepcopy(state)
        refresh(root, state, now)
        if state != original:
            atomic_json(root / 'campaign.json', state)
        result = operation(state, now)
        atomic_json(root / 'campaign.json', state)
        return result


def require_running(state):
    if state['stop']:
        raise ValueError(f"campaign stopped: {state['stop']}")


def available(state, category):
    reserved = sum(item['maximum_cents'] for item in state['allocations'].values()
                   if item['category'] == category and item['status'] == 'reserved')
    return (state['budget_cents'][category] - state['spent_cents'][category]
            - state['committed_cents'].get(category, 0) - reserved)


def admit_block(root, name, now=None):
    identifier(name)

    def operation(state, stamp):
        require_running(state)
        if name in state['blocks']:
            return state['blocks'][name]
        # Exactly one generating/ready block may wait behind active training.
        if any(block['phase'] in ('generating', 'ready') for block in state['blocks'].values()):
            raise ValueError('one-block backlog is occupied; start training the ready block first')
        block = dict(phase='generating', target=100_000_000, teacher=state['champion'], admitted_at=stamp)
        state['blocks'][name] = block
        return block

    return transact(root, operation, now)


def mark_ready(root, name, now=None):
    root = Path(root)
    manifest_path = root / f'{identifier(name)}.json'
    manifest = nnue_shards.read_manifest(manifest_path)
    digest = nnue_data.file_hash(manifest_path)

    def operation(state, stamp):
        require_running(state)
        block = state['blocks'][name]
        if (manifest['store_id'] != state['store_id'] or manifest['positions'] != block['target']
                or manifest.get('partial') or manifest['block'] != name):
            raise ValueError('ready block must be a complete 100M manifest from this store')
        if block['phase'] == 'ready' and block['manifest_sha256'] == digest:
            return block
        if block['phase'] != 'generating':
            raise ValueError('block is not generating')
        block.update(phase='ready', manifest_sha256=digest, ready_at=stamp)
        return block

    return transact(root, operation, now)


def reserve(root, lease_id, category, work, maximum_cents, seconds, now=None):
    identifier(lease_id)
    positive_integer(maximum_cents)
    positive_integer(seconds)
    if category not in ('cpu', 'gpu', 'tests', 'storage', 'margin'):
        raise ValueError('invalid allocation category; existing online allocation is already committed')

    def operation(state, stamp):
        require_running(state)
        if state['credit_expiry'] is None:
            raise ValueError('record verified credit eligibility and exact expiry before allocation')
        deadline = stamp + seconds
        if deadline > state['credit_expiry'] - 48 * 3600:
            raise ValueError('allocation crosses the 48-hour credit safety deadline')
        if category in ('cpu', 'gpu'):
            expected = 'generating' if category == 'cpu' else 'ready'
            if work not in state['blocks'] or state['blocks'][work]['phase'] != expected:
                raise ValueError(f'allocation needs a {expected} block')
        if lease_id in state['allocations']:
            raise ValueError('allocation ID already exists')
        if maximum_cents > available(state, category):
            raise ValueError('allocation exceeds remaining category budget')
        if category == 'gpu' and any(item['category'] == 'gpu' and item['status'] == 'reserved'
                                     for item in state['allocations'].values()):
            raise ValueError('a GPU allocation is already active; settle and release it first')
        lease = dict(category=category, work=work, maximum_cents=maximum_cents,
                     started_at=stamp, deadline=deadline, status='reserved')
        state['allocations'][lease_id] = lease
        return lease

    return transact(root, operation, now)


def check_allocation(state, lease_id, category, work, stamp):
    require_running(state)
    lease = state['allocations'].get(lease_id)
    if (lease is None or lease['status'] != 'reserved' or lease['category'] != category
            or lease['work'] != work or stamp >= lease['deadline']):
        raise ValueError('missing, mismatched or expired allocation')
    return lease


def allocation(root, lease_id, category, work, now=None):
    return transact(root, lambda state, stamp: check_allocation(state, lease_id, category, work, stamp), now)


def start_training(root, name, lease_id, category='gpu', now=None):
    def operation(state, stamp):
        check_allocation(state, lease_id, category, name, stamp)
        block = state['blocks'][name]
        if block['phase'] not in ('ready', 'training'):
            raise ValueError('block is not ready for training')
        if any(key != name and other['phase'] == 'training' for key, other in state['blocks'].items()):
            raise ValueError('another experimental training stage is active')
        block.update(phase='training', training_allocation=lease_id)
        return block

    return transact(root, operation, now)


def settle(root, lease_id, actual_cents, now=None):
    if type(actual_cents) is not int or actual_cents < 0:
        raise ValueError('actual cost must be nonnegative integer cents')

    def operation(state, stamp):
        lease = state['allocations'][lease_id]
        if lease['status'] != 'reserved':
            raise ValueError('allocation is already settled')
        lease.update(status='settled', actual_cents=actual_cents, settled_at=stamp)
        state['spent_cents'][lease['category']] += actual_cents
        # Record reality even if it exceeded the reserved ceiling; never hide overspend.
        if actual_cents > lease['maximum_cents'] or available(state, lease['category']) < 0:
            stop(Path(root), state, 'allocation_overrun')
        return lease

    return transact(root, operation, now)


def status(root, now=None):
    def operation(state, stamp):
        result = copy.deepcopy(state)
        result['remaining_cents'] = {category: available(state, category) for category in budget_cents}
        result['release_compute'] = [key for key, item in state['allocations'].items()
                                     if item['status'] == 'reserved' and
                                     (state['stop'] or stamp >= item['deadline'])]
        return result
    return transact(root, operation, now)


def expiry_timestamp(text):
    try:
        value = datetime.fromisoformat(text.replace('Z', '+00:00'))
        if value.tzinfo is None:
            raise ValueError('expiry must include a timezone')
        return value.astimezone(timezone.utc).timestamp()
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def main():
    parser = argparse.ArgumentParser(prog='maxima nnue:campaign', description=__doc__)
    parser.add_argument('--store', type=Path, required=True)
    commands = parser.add_subparsers(dest='action', required=True)
    init = commands.add_parser('init')
    init.add_argument('--teacher', type=Path, required=True)
    init.add_argument('--credit-expiry', type=expiry_timestamp)
    init.add_argument('--credit-evidence', help='record of verified promotional eligibility and exact expiry')
    commands.add_parser('status')
    block = commands.add_parser('block')
    block.add_argument('name')
    ready = commands.add_parser('ready')
    ready.add_argument('name')
    hold = commands.add_parser('reserve')
    hold.add_argument('lease')
    hold.add_argument('--category', choices=('cpu', 'gpu', 'tests', 'storage', 'margin'), required=True)
    hold.add_argument('--work', required=True)
    hold.add_argument('--maximum-cents', type=int, required=True)
    hold.add_argument('--seconds', type=int, required=True)
    finish = commands.add_parser('settle')
    finish.add_argument('lease')
    finish.add_argument('--actual-cents', type=int, required=True)
    halt = commands.add_parser('stop')
    halt.add_argument('--reason', required=True)
    args = parser.parse_args()
    if args.action == 'init':
        result = initialize(args.store, args.teacher, expiry=args.credit_expiry, credit_evidence=args.credit_evidence)
    elif args.action == 'status':
        result = status(args.store)
    elif args.action == 'block':
        result = admit_block(args.store, args.name)
    elif args.action == 'ready':
        result = mark_ready(args.store, args.name)
    elif args.action == 'reserve':
        result = reserve(args.store, args.lease, args.category, args.work, args.maximum_cents, args.seconds)
    elif args.action == 'settle':
        result = settle(args.store, args.lease, args.actual_cents)
    else:
        result = transact(args.store, lambda state, stamp: stop(args.store, state, args.reason))
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0
