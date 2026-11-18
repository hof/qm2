"""Verified logical training progress and bounded physical-block generation."""
from contextlib import closing
import json
from pathlib import Path
import sqlite3

from . import nnue_campaign, nnue_data, nnue_shards
from .learning_store import writer_lock


def block_counts(root):
    root = Path(root)
    with closing(sqlite3.connect((root / 'index.sqlite3').resolve().as_uri() + '?mode=ro', uri=True)) as db:
        rows = [dict(zip(('name', 'target', 'positions', 'sealed'), row)) for row in
                db.execute('SELECT name,target,positions,sealed FROM blocks ORDER BY rowid')]
    for block in rows[:-1]:
        if block['positions'] != block['target'] or not block['sealed']:
            raise ValueError('physical blocks must form one contiguous sealed prefix')
    return rows


def learned_prefix(ranges, blocks):
    known = {block['name'] for block in blocks}
    if any(entry['block'] not in known for entry in ranges):
        raise ValueError('training history refers to an unknown physical block')
    total, ended = 0, False
    for block in blocks:
        selected = sorted((entry for entry in ranges if entry['block'] == block['name']),
                          key=lambda entry: entry['start'])
        if ended and selected:
            raise ValueError('training history has a gap in its global prefix')
        end = 0
        for entry in selected:
            if entry['start'] != end or entry['stop'] > block['positions']:
                raise ValueError('training history contains gaps, overlaps or uncommitted rows')
            end = entry['stop']
        total += end
        ended = end < block['target']
    if total <= 0:
        raise ValueError('missing completed training prefix')
    return total


def register_completed(root, stage, manifest, *, now=None):
    """Register a completed snapshot's actual checkpoint and contiguous lineage."""
    root, stage, manifest = Path(root), Path(stage).resolve(), Path(manifest).resolve()
    receipt = json.loads((stage / 'complete.json').read_text())
    if (receipt.get('format') != 'maxima.nnue.stage.complete' or receipt.get('version') != 1
            or nnue_data.file_hash(stage / 'training.pt') != receipt.get('checkpoint_sha256')
            or nnue_data.file_hash(stage / 'candidate.pt') != receipt.get('candidate_sha256')):
        raise ValueError('completed training receipt/checkpoint/candidate mismatch')
    contract = receipt['contract']
    ranges = nnue_shards.verified_parent_ranges(contract, receipt.get('seen_ranges'), manifest)
    through = learned_prefix(ranges, block_counts(root))

    def operation(state, stamp):
        nnue_campaign.require_running(state)
        if contract['store_id'] != state['store_id']:
            raise ValueError('completed training store identity mismatch')
        previous = state.get('generation_learning')
        digest = receipt['checkpoint_sha256']
        if previous and previous['checkpoint_sha256'] == digest:
            return previous
        if previous:
            if contract.get('parent_sha256') != previous['checkpoint_sha256'] or through <= previous['through']:
                raise ValueError('completed training must continue the latest registered checkpoint')
        elif (contract.get('parent_sha256') is not None
              or contract.get('warm_start_sha256') != state['original_teacher']):
            raise ValueError('register completed training from its pinned first-stage lineage')
        progress = dict(through=through, checkpoint_sha256=digest, stage=str(stage),
                        manifest=str(manifest), receipt_sha256=nnue_data.file_hash(stage / 'complete.json'),
                        registered_at=stamp)
        state.setdefault('generation_learning_history', []).append(progress)
        state['generation_learning'] = progress
        state['latest_checkpoint_sha256'] = digest
        schedule = state.get('generation_schedule')
        if schedule:
            while schedule['next_checkpoint'] <= through:
                schedule['next_checkpoint'] += schedule['step']
        return progress

    return nnue_campaign.transact(root, operation, now, event='completed_snapshot_registered')


def configure_schedule(root, next_checkpoint, step, *, now=None):
    nnue_campaign.positive_integer(next_checkpoint)
    nnue_campaign.positive_integer(step)
    blocks = block_counts(root)

    def operation(state, stamp):
        nnue_campaign.require_running(state)
        through = state.get('generation_learning', {}).get('through', 0)
        if (not through or not blocks or step > blocks[0]['target']
                or not through < next_checkpoint <= through + blocks[0]['target']):
            raise ValueError('next checkpoint must fit the one-block backlog after verified training')
        schedule = dict(next_checkpoint=next_checkpoint, step=step)
        state['generation_schedule'] = schedule
        return schedule

    return nnue_campaign.transact(root, operation, now, event='generation_schedule_configured')


def advance(root, lease_id, *, now=None):
    """Reuse one CPU allocation across sealed blocks without extending its scope in time/cost.

    The controller must drain the old producer invocation before a block handoff.
    Accepted backlog remains at most one physical block beyond completed training.
    """
    root = Path(root)
    blocks = block_counts(root)
    indexed = {block['name']: block for block in blocks}

    def operation(state, stamp):
        nnue_campaign.require_running(state)
        progress = state.get('generation_learning')
        if progress is None or not blocks:
            raise ValueError('verified training progress is required for rolling generation')
        lease = state['allocations'].get(lease_id)
        name = state.get('generation_block', lease.get('work') if lease else None)
        nnue_campaign.check_allocation(state, lease_id, 'cpu', name, stamp)
        block = state['blocks'][name]
        row = indexed.get(name)
        total = sum(entry['positions'] for entry in blocks)
        capacity = progress['through'] + blocks[0]['target']
        full = row and row['positions'] == row['target'] and row['sealed']
        if full:
            if block['phase'] != 'ready':
                raise ValueError('seal and mark the completed physical block ready before handoff')
            if total >= capacity:
                return dict(block=name, waiting=True, reason='backlog', accepted_global=total)
            next_name = f'block-{len(blocks) + 1:03d}'
            if next_name in state['blocks']:
                raise ValueError('next physical block is already registered inconsistently')
            block = dict(phase='generating', target=row['target'], teacher=state['champion'], admitted_at=stamp)
            state['blocks'][next_name] = block
            lease.setdefault('work_history', []).append(dict(work=name, ended_at=stamp))
            lease['work'] = name = next_name
            row = None
        if block['phase'] != 'generating':
            raise ValueError('current physical block is not generating')
        if row and row['target'] != block['target']:
            raise ValueError('physical block target changed')
        offset = total - (row['positions'] if row else 0)
        limit = min(block['target'], capacity - offset)
        count = row['positions'] if row else 0
        if limit <= 0 or count > limit:
            raise ValueError('accepted data exceeds the permitted generation backlog')
        block['generation_limit'] = limit
        state['generation_block'] = name
        return dict(block=name, target=block['target'], limit=limit, positions=count,
                    accepted_global=total, waiting=count >= limit)

    return nnue_campaign.transact(root, operation, now, event='generation_block_advanced')


def generation_state(root, *, state=None, now=None):
    state = nnue_campaign.status(root, now) if state is None else state
    blocks = block_counts(root)
    name = state.get('generation_block')
    if name is None:
        names = [name for name, block in state['blocks'].items() if block['phase'] == 'generating']
        name = names[0] if len(names) == 1 else None
    block = state['blocks'].get(name, {})
    row = next((entry for entry in blocks if entry['name'] == name), None)
    limit = block.get('generation_limit', block.get('target', 0))
    count = row['positions'] if row else 0
    active = (not state['stop'] and name is not None and block['phase'] == 'generating'
              and count < limit and block['teacher'] == state['champion'])
    return dict(block=name, target=block.get('target'), limit=limit, positions=count,
                accepted_global=sum(entry['positions'] for entry in blocks), active=active,
                teacher=state['champion'], stop=state['stop'])


def ingest_current(root, inputs, *, now=None):
    root = Path(root)

    def operation(state, stamp):
        current = generation_state(root, state=state)
        if current['active']:
            count = nnue_shards.ingest(root, current['block'], inputs, target=current['target'],
                                       accepted_limit=current['limit'])
            current['accepted_global'] += count - current['positions']
            current.update(positions=count, active=count < current['limit'])
        return current

    return nnue_campaign.transact(root, operation, now, event='current_generation_ingested')


def prepare_snapshot(root, directory, *, now=None):
    """Freeze the next logical batch without sealing the current physical block."""
    root, directory = Path(root), Path(directory)
    with writer_lock(root / '.generation-snapshot.lock'):
        state = nnue_campaign.status(root, now)
        nnue_campaign.require_running(state)
        schedule, progress = state['generation_schedule'], state['generation_learning']
        start, stop = progress['through'], schedule['next_checkpoint']
        blocks = block_counts(root)
        if sum(block['positions'] for block in blocks) < stop:
            return None
        old = state.get('generation_snapshot')
        if old and old['through'] == stop and old['parent_sha256'] == progress['checkpoint_sha256']:
            if nnue_data.file_hash(old['manifest']) != old['manifest_sha256']:
                raise ValueError('prepared snapshot manifest changed')
            return old
        offset, ranges = 0, []
        for block in blocks:
            left, right = max(0, start - offset), min(block['positions'], stop - offset)
            if right > left:
                ranges.append(dict(block=block['name'], start=left, stop=right))
            offset += block['target']
        output = directory / f'checkpoint-{stop:09d}'
        manifest = output / 'manifest.json'
        if output.exists():
            saved = nnue_shards.read_manifest(manifest)
            if saved['store_id'] != state['store_id'] or nnue_shards.source_ranges(saved) != ranges:
                raise ValueError('existing snapshot has different source ranges')
        else:
            manifest = nnue_shards.freeze_ranges(root, ranges, output)
        prepared = dict(start=start, through=stop, positions=stop - start, manifest=str(manifest.resolve()),
                        manifest_sha256=nnue_data.file_hash(manifest), parent_sha256=progress['checkpoint_sha256'])

        def operation(current, stamp):
            nnue_campaign.require_running(current)
            if (current['generation_learning']['checkpoint_sha256'] != progress['checkpoint_sha256']
                    or current['generation_schedule'] != schedule):
                raise ValueError('training progress or schedule changed during snapshot preparation')
            current['generation_snapshot'] = prepared
            return prepared

        return nnue_campaign.transact(root, operation, now, event='logical_snapshot_ready')
