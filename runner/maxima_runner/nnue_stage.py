"""Bounded sharded training stages with explicit optimizer continuation and CUDA."""
import argparse
from array import array
from bisect import bisect_right
from collections import OrderedDict
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import random
import time

import chess

from . import nnue_binary, nnue_data, nnue_format, nnue_shards, nnue_training
from .learning_store import writer_lock


class shard_rows:
    """Random access with at most sixteen mapped shards and compact split indices."""
    def __init__(self, path, manifest, split):
        self.root, self.shards, self.split = path.parent, manifest['shards'], split
        self.ends, total = [], 0
        for shard in self.shards:
            total += shard[split]
            self.ends.append(total)
        self.cache = OrderedDict()

    def __len__(self):
        return self.ends[-1]

    def __getitem__(self, index):
        row = self.board_row(index)
        board = row.pop('board')
        return dict(row, fen=board.fen(en_passant='fen', promoted=True))

    def board_row(self, index):
        if not 0 <= index < len(self):
            raise IndexError(index)
        number = bisect_right(self.ends, index)
        if number not in self.cache:
            if len(self.cache) == 16:
                _, (old, _) = self.cache.popitem(last=False)
                old.close()
            data = nnue_binary.binary_dataset(self.root / self.shards[number]['path'])
            try:
                # The split flag is in a fixed byte, so indexing does not decode boards.
                flag_offset = 32 + 8 + 2 + 2 + 2 + 1
                indices = array('I', (i for i in range(len(data)) if
                    bool(data.mapping[data.offset + i * data.record_size + flag_offset] & 2)
                    == (self.split == 'validation')))
                if len(indices) != self.shards[number][self.split]:
                    raise ValueError('shard split counts differ from manifest')
            except BaseException:
                data.close()
                raise
            self.cache[number] = data, indices
        self.cache.move_to_end(number)
        data, indices = self.cache[number]
        row = data[indices[index - (self.ends[number - 1] if number else 0)]]
        row.pop('variant_state')
        return row

    def close(self):
        for data, _ in self.cache.values():
            data.close()
        self.cache.clear()


def sample_rows(rows, limit, seed):
    rng = random.Random(seed)
    return [rows[i] for i in sorted(rng.sample(range(len(rows)), min(limit, len(rows))))]


def order_chunks(count, size, seed, epoch):
    chunks = list(range((count + size - 1) // size))
    random.Random(f'{seed}:{epoch}:chunks').shuffle(chunks)
    return chunks


def chunk_order(count, size, seed, epoch, chunk):
    indices = list(range(chunk * size, min((chunk + 1) * size, count)))
    random.Random(f'{seed}:{epoch}:{chunk}').shuffle(indices)
    return indices


def inputs(torch, model, rows):
    device = next(model.parameters()).device
    return tuple(value.to(device) for value in nnue_training.batch(torch, rows, model.architecture))


def labels(torch, model, rows):
    options = dict(dtype=torch.float64, device=next(model.parameters()).device)
    return (torch.tensor([row['score_cp'] for row in rows], **options),
            torch.tensor([row['result_stm'] for row in rows], **options))


def evaluate(torch, model, rows, batch_size, search_weight):
    total = 0.0
    with torch.no_grad():
        for start in range(0, len(rows), batch_size):
            selected = rows[start:start + batch_size]
            cp, results = labels(torch, model, selected)
            total += nnue_training.loss(torch, model(*inputs(torch, model, selected)), cp,
                                        results, search_weight).item() * len(selected)
    return dict(positions=len(rows), loss=total / len(rows))


def cpu_copy(torch, value):
    if isinstance(value, torch.Tensor):
        return value.detach().cpu().clone()
    if isinstance(value, dict):
        return {key: cpu_copy(torch, item) for key, item in value.items()}
    if isinstance(value, list):
        return [cpu_copy(torch, item) for item in value]
    if isinstance(value, tuple):
        return tuple(cpu_copy(torch, item) for item in value)
    return copy.deepcopy(value)


def implementation():
    names = ('nnue_stage.py', 'nnue_shards.py', 'nnue_model.py', 'nnue_format.py',
             'nnue_training.py', 'nnue_binary.py', 'nnue_data.py', 'nnue_warm_start.py')
    return {name: nnue_data.file_hash(Path(__file__).with_name(name)) for name in names}


def stage_contract(args, torch, manifest):
    result = {name: getattr(args, name) for name in
              ('epochs', 'batch_size', 'shuffle_size', 'metrics_positions', 'seed', 'learning_rate',
               'search_weight', 'width', 'hidden', 'device')}
    result.update(format='maxima.nnue.stage.contract', version=1, architecture='halfkav2_hm',
                  dtype='float64', torch=str(torch.__version__), implementation=implementation(),
                  store_id=manifest['store_id'],
                  manifest_sha256=nnue_data.file_hash(args.manifest),
                  shard_sha256=[item['sha256'] for item in manifest['shards']],
                  parent_sha256=nnue_data.file_hash(args.parent) if args.parent else None,
                  warm_start_sha256=nnue_data.file_hash(args.warm_start) if args.warm_start else None)
    return result



def publish_completion(args, state):
    from . import nnue_campaign
    receipt = dict(format='maxima.nnue.stage.complete', version=1, contract=state['contract'],
                   presentations=state['presentations'], global_step=state['global_step'],
                   unique_positions=state['unique_positions'], selected_epoch=state['best_epoch'],
                   validation_loss=state['best_loss'], history=state['history'],
                   checkpoint_sha256=nnue_data.file_hash(args.output / 'training.pt'),
                   candidate_sha256=nnue_data.file_hash(args.output / 'candidate.pt'))
    nnue_campaign.atomic_json(args.output / 'complete.json', receipt)
    if (args.manifest.parent / 'campaign.json').exists():
        manifest = json.loads(args.manifest.read_text())
        nnue_campaign.finish_training(args.manifest.parent, manifest['block'], args.output, receipt)


def run(args):
    invocation_started = time.monotonic()
    invocation_id = str(time.time_ns())
    if args.parent and args.warm_start:
        raise ValueError('parent continuation and warm start are mutually exclusive')
    if (min(args.epochs, args.batch_size, args.shuffle_size, args.metrics_positions,
            args.checkpoint_every, args.width, args.hidden) <= 0
            or max(args.shuffle_size, args.batch_size, args.metrics_positions) > 1_000_000
            or args.width > 256 or args.hidden > 32
            or not math.isfinite(args.learning_rate) or args.learning_rate <= 0
            or not math.isfinite(args.search_weight) or not 0 <= args.search_weight <= 1
            or (args.max_batches is not None and args.max_batches <= 0)):
        raise ValueError('invalid stage settings')
    if args.warm_start and (args.width != 256 or args.hidden != 32):
        raise ValueError('warm start requires full 256/32 geometry')
    manifest = nnue_shards.read_manifest(args.manifest)
    if args.output.exists() and not args.resume:
        raise ValueError('output already exists')
    campaign_root = args.manifest.parent
    campaign_deadline = None
    if (campaign_root / 'campaign.json').exists():
        from . import nnue_campaign
        campaign_state = nnue_campaign.status(campaign_root)
        tracked = campaign_state['blocks'][manifest['block']]
        completed_here = (tracked['phase'] == 'trained' and args.resume
                          and tracked['stage']['path'] == str(args.output.resolve()))
        if nnue_data.file_hash(args.manifest) != tracked['manifest_sha256']:
            raise ValueError('campaign manifest changed after readiness validation')
        if not completed_here:
            expected_parent = campaign_state.get('latest_checkpoint_sha256')
            if expected_parent:
                if not args.parent or nnue_data.file_hash(args.parent) != expected_parent:
                    raise ValueError('campaign must continue its latest experimental checkpoint')
            elif not args.warm_start or nnue_data.file_hash(args.warm_start) != campaign_state['original_teacher']:
                raise ValueError('first campaign stage requires the pinned original teacher warm start')
            category = 'gpu' if args.device == 'cuda' else 'cpu'
            campaign_deadline = nnue_campaign.allocation(campaign_root, args.lease, category, manifest['block'])['deadline']
    if args.device == 'cuda':
        os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG', ':4096:8')
    torch, model_type = nnue_training.training_libraries()
    torch.set_num_threads(1)
    if args.device == 'cuda' and not torch.cuda.is_available():
        raise ValueError('CUDA requested but unavailable')
    # Float64 retains the integer-domain forward path; no AMP/TF32 approximation.
    torch.manual_seed(args.seed)
    torch.use_deterministic_algorithms(True)
    model = model_type(args.width, args.hidden, 'halfkav2_hm').double()
    stage_warm_start = None
    if args.warm_start and not args.resume:
        from .nnue_warm_start import initialize_from_halfkp
        stage_warm_start = initialize_from_halfkp(torch, model, args.warm_start.read_bytes())
    model.to(args.device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.learning_rate)
    contract = stage_contract(args, torch, manifest)
    checkpoint = args.output / 'training.pt'
    if args.resume:
        state = torch.load(checkpoint, map_location='cpu', weights_only=True)
        if state.get('format') != 'maxima.nnue.stage' or state.get('version') != 1 or state['contract'] != contract:
            raise ValueError('stage input/config/implementation mismatch')
        if state['phase'] == 'complete':
            publish_completion(args, state)
            print(f'Stage already complete: {args.output}')
            return 0
    else:
        parent = None
        if args.parent:
            parent = torch.load(args.parent, map_location='cpu', weights_only=True)
            if (parent.get('format') != 'maxima.nnue.stage' or parent.get('version') != 1
                    or parent['phase'] != 'complete'):
                raise ValueError('parent must be a completed stage recovery checkpoint')
            for key in ('architecture', 'dtype', 'torch', 'implementation', 'store_id', 'width', 'hidden',
                        'batch_size', 'shuffle_size', 'learning_rate', 'search_weight', 'device'):
                if parent['contract'][key] != contract[key]:
                    raise ValueError(f'parent training recipe mismatch: {key}')
            if set(contract['shard_sha256']) & set(parent['seen_shards']):
                raise ValueError('new stage repeats previously trained shards; replay must be explicit')
            model.ft_bound = parent['ft_bound']
            model.load_state_dict(parent['model'])
            optimizer.load_state_dict(parent['optimizer'])
            torch.set_rng_state(parent['rng'])
            if args.device == 'cuda':
                torch.cuda.set_rng_state_all(parent['cuda_rng'])
        state = dict(format='maxima.nnue.stage', version=1, contract=contract, phase='training',
                     epoch=0, chunk=0, cursor=0, history=[], best_loss=math.inf, best_model=None,
                     best_epoch=None, ft_bound=model.ft_bound, warm_start=stage_warm_start,
                     global_step=parent['global_step'] if parent else 0,
                     presentations=parent['presentations'] if parent else 0,
                     unique_positions=(parent['unique_positions'] if parent else 0) + sum(s['train'] for s in manifest['shards']),
                     seen_shards=(parent['seen_shards'] if parent else []) + contract['shard_sha256'])
    if args.resume:
        model.ft_bound = state['ft_bound']
        model.load_state_dict(state['model'])
        optimizer.load_state_dict(state['optimizer'])
        torch.set_rng_state(state['rng'])
        if args.device == 'cuda':
            torch.cuda.set_rng_state_all(state['cuda_rng'])
    if campaign_deadline is not None:
        nnue_campaign.start_training(campaign_root, manifest['block'], args.lease, category)
    train = shard_rows(args.manifest, manifest, 'train')
    validation = shard_rows(args.manifest, manifest, 'validation')
    try:
        if not len(train) or not len(validation):
            raise ValueError('stage needs both train and validation positions')
        diagnostics = sample_rows(validation, args.metrics_positions, args.seed)
        if not args.resume:
            initial = evaluate(torch, model, diagnostics, args.batch_size, args.search_weight)
            state.update(best_loss=initial['loss'], best_model=cpu_copy(torch, model.state_dict()), best_epoch=0)
            state['initial'] = initial
            args.output.mkdir(parents=True, exist_ok=False)

        initial_presentations = state['presentations']

        def save():
            state.update(model=cpu_copy(torch, model.state_dict()),
                         optimizer=cpu_copy(torch, optimizer.state_dict()), rng=torch.get_rng_state(),
                         cuda_rng=torch.cuda.get_rng_state_all() if args.device == 'cuda' else [])
            nnue_training.atomic_write(checkpoint, lambda stream: torch.save(state, stream))
            from . import nnue_campaign
            directory = args.output / 'invocations'
            directory.mkdir(exist_ok=True)
            elapsed = time.monotonic() - invocation_started
            presented = state['presentations'] - initial_presentations
            nnue_campaign.atomic_json(directory / f'{invocation_id}.json', dict(
                format='maxima.nnue.stage.invocation', version=1, elapsed_seconds=elapsed,
                presentations=presented, presentations_per_second=presented / elapsed,
                global_step=state['global_step'], epoch=state['epoch'], phase=state['phase'], device=args.device))

        save()
        batches = 0
        while state['epoch'] < args.epochs:
            chunks = order_chunks(len(train), args.shuffle_size, args.seed, state['epoch'])
            while state['chunk'] < len(chunks):
                order = chunk_order(len(train), args.shuffle_size, args.seed, state['epoch'], chunks[state['chunk']])
                for start in range(state['cursor'], len(order), args.batch_size):
                    if campaign_deadline is not None and ((campaign_root / 'STOP').exists() or time.time() >= campaign_deadline):
                        save()
                        nnue_campaign.status(campaign_root)
                        print('Campaign scheduling stopped; training checkpoint saved')
                        return 0
                    selected = [train.board_row(i) for i in order[start:start + args.batch_size]]
                    cp, results = labels(torch, model, selected)
                    optimizer.zero_grad(set_to_none=True)
                    objective = nnue_training.loss(torch, model(*inputs(torch, model, selected)), cp,
                                                   results, args.search_weight)
                    if not torch.isfinite(objective):
                        raise ValueError('nonfinite training loss')
                    objective.backward()
                    optimizer.step()
                    model.constrain()
                    state.update(cursor=start + len(selected), global_step=state['global_step'] + 1,
                                 presentations=state['presentations'] + len(selected))
                    batches += 1
                    paused = args.max_batches is not None and batches >= args.max_batches
                    if paused or batches % args.checkpoint_every == 0:
                        save()
                    if paused:
                        print(f"Paused stage at global step {state['global_step']}; resume with --resume")
                        return 0
                state.update(chunk=state['chunk'] + 1, cursor=0)
            scores = evaluate(torch, model, diagnostics, args.batch_size, args.search_weight)
            if not math.isfinite(scores['loss']):
                raise ValueError('nonfinite validation loss')
            state['history'].append(scores)
            if scores['loss'] < state['best_loss']:
                state.update(best_loss=scores['loss'], best_model=cpu_copy(torch, model.state_dict()),
                             best_epoch=state['epoch'] + 1)
            state.update(epoch=state['epoch'] + 1, chunk=0, cursor=0)
            print(f"Epoch {state['epoch']}: validation {scores['loss']:.6f}", flush=True)
            save()
        # The experimental model/optimizer remain paired at the final step.
        candidate = dict(format='maxima.nnue.candidate', version=1, contract=contract,
                         model=state['best_model'], ft_bound=state['ft_bound'],
                         selected_epoch=state['best_epoch'], validation_loss=state['best_loss'],
                         global_step=state['global_step'], presentations=state['presentations'])
        nnue_training.atomic_write(args.output / 'candidate.pt', lambda stream: torch.save(candidate, stream))
        state['phase'] = 'complete'
        save()
        publish_completion(args, state)
        print(f'Stage complete; frozen candidate: {args.output / "candidate.pt"}')
        return 0
    finally:
        train.close()
        validation.close()


def main():
    parser = argparse.ArgumentParser(prog='maxima nnue:stage', description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--lease', help='required allocation ID for campaign stores')
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--parent', type=Path, help='completed prior stage training.pt; retains Adam and latest student')
    parser.add_argument('--warm-start', type=Path)
    parser.add_argument('--device', choices=('cpu', 'cuda'), default='cpu')
    parser.add_argument('--epochs', type=int, default=1, help='local passes over this block, not global corpus epochs')
    parser.add_argument('--batch-size', type=int, default=1024)
    parser.add_argument('--shuffle-size', type=int, default=65536)
    parser.add_argument('--metrics-positions', type=int, default=8192)
    parser.add_argument('--checkpoint-every', type=int, default=100)
    parser.add_argument('--max-batches', type=int)
    parser.add_argument('--width', type=int, default=256)
    parser.add_argument('--hidden', type=int, default=32)
    parser.add_argument('--learning-rate', type=float, default=0.0001)
    parser.add_argument('--search-weight', type=float, default=0.7)
    parser.add_argument('--seed', type=int, default=2026)
    args = parser.parse_args()
    # The per-run writer lock lives outside output so validation failures create no run.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with writer_lock(args.output.parent / f'.{args.output.name}.lock'):
        return run(args)


def export_main():
    parser = argparse.ArgumentParser(prog='maxima nnue:export', description='Export a frozen sharded-stage candidate.')
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new directory for network and verification report')
    parser.add_argument('--positions', type=int, default=4096, help='bounded deterministic export comparisons')
    args = parser.parse_args()
    if args.positions <= 0 or args.positions > 1_000_000 or args.output.exists():
        raise ValueError('invalid sample size or output already exists')
    manifest = nnue_shards.read_manifest(args.manifest)
    torch, model_type = nnue_training.training_libraries()
    torch.set_num_threads(1)
    candidate = torch.load(args.candidate, map_location='cpu', weights_only=True)
    if (candidate.get('format') != 'maxima.nnue.candidate' or candidate.get('version') != 1
            or candidate['contract']['manifest_sha256'] != nnue_data.file_hash(args.manifest)):
        raise ValueError('candidate/manifest mismatch')
    config = candidate['contract']
    model = model_type(config['width'], config['hidden'], 'halfkav2_hm').double()
    model.ft_bound = candidate['ft_bound']
    model.load_state_dict(candidate['model'])
    serialized = nnue_format.serialize(model.integer_weights(), model.architecture)
    selected = []
    for split in ('train', 'validation'):
        rows = shard_rows(args.manifest, manifest, split)
        try:
            selected.extend(sample_rows(rows, (args.positions + (split == 'train')) // 2, config['seed']))
        finally:
            rows.close()
    selected.extend(dict(fen=fen) for fen in nnue_training.verification_fens)
    nnue_training.verify_export(torch, model, selected, serialized)
    report = dict(format='maxima.nnue.stage.export', version=1,
                  candidate_sha256=nnue_data.file_hash(args.candidate),
                  network_sha256=hashlib.sha256(serialized).hexdigest(),
                  export_checked_positions=len(selected), verification='sampled_scalar_and_edge_fixtures',
                  native_verification_required=True, selected_epoch=candidate['selected_epoch'],
                  validation_loss=candidate['validation_loss'])
    args.output.mkdir(parents=True, exist_ok=False)
    nnue_training.atomic_write(args.output / 'network.nnue', lambda stream: stream.write(serialized))
    nnue_shards.publish_json(args.output / 'report.json', report)
    print(f'Export saved: {args.output}; run nnue:verify before playing tests')
    return 0
