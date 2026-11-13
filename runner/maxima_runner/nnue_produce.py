"""Bounded independent self-play producers feeding an immutable training block."""
import argparse
from contextlib import closing
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from . import nnue_data, nnue_selfplay, nnue_shards
from .learning_store import writer_lock


def run_job(args, index, deadline, block_stop):
    if args.stop_file.exists() or block_stop.exists() or time.time() >= deadline:
        return None
    directory = args.output / f'job-{index:08d}'
    directory.mkdir(parents=True, exist_ok=True)
    # Job IDs do not repeat seeds across producer pools in the same campaign.
    identity = f'{args.campaign}:{index}'
    seed = int.from_bytes(hashlib.sha256(identity.encode()).digest()[:8], 'big')
    job = dict(identity=identity, seed=seed, engine_sha256=nnue_data.file_hash(args.engine),
               network_sha256=nnue_data.file_hash(args.network),
               book_sha256=nnue_data.file_hash(args.book) if args.book else None)
    path = directory / 'job.json'
    if path.exists():
        if json.loads(path.read_text()) != job:
            raise ValueError('job teacher/engine/book changed; resume with pinned inputs or assign new job IDs')
    else:
        nnue_shards.publish_json(path, job)
    command = [sys.executable, '-m', 'maxima_runner', 'nnue:selfplay', '--output', str(directory),
               '--engine', str(args.engine.resolve()), '--network', str(args.network.resolve()),
               '--positions', str(args.positions_per_job), '--games-per-run', str(args.games_per_job),
               '--nodes', str(args.nodes), '--seed', str(seed), '--stop-at', str(deadline),
               '--stop-file', str(args.stop_file.resolve()), '--stop-file', str(block_stop.resolve())]
    # Explicitly suppress the ordinary CLI's automatic book lookup unless chosen.
    command.extend(['--book', str(args.book.resolve())] if args.book else ['--book-plies', '0'])
    if args.book is None:
        # Selfplay's optional default book is resolved in its own process.
        command.extend(['--no-book'])
    with (directory / 'producer.log').open('ab') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise ValueError(f'producer job {index} failed ({result.returncode}); see {directory / "producer.log"}')
    config = json.loads((directory / 'settings.json').read_text())
    rows, _, _ = nnue_selfplay.load_run(directory, config)
    if rows:
        chunks = args.output / 'chunks'
        chunks.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=chunks) as temporary:
            source = Path(temporary) / 'chunk.mxdata'
            nnue_data.save_dataset(source, dict(config, variant='standard', score_pov='side_to_move',
                                                split_method='game', label_source='completed_exact_iteration'), rows)
            digest = nnue_data.file_hash(source)
            destination = chunks / f'{digest}.mxdata'
            if not destination.exists():
                os.link(source, destination)
    return directory


def ingest_jobs(args):
    count = nnue_shards.ingest(args.store, args.block, [], target=args.target, exclude=args.exclude)
    if count >= args.target:
        return count
    # Chunk names are immutable content hashes. Completed inputs are skipped by
    # indexed lookup without rehashing/reloading previously accepted corpora.
    with closing(nnue_shards.connect(args.store)) as db:
        for path in sorted((args.output / 'chunks').glob('*.mxdata')):
            if db.execute('SELECT 1 FROM inputs WHERE digest=?', (path.stem,)).fetchone():
                continue
            if nnue_data.file_hash(path) != path.stem:
                raise ValueError('producer chunk checksum mismatch')
            count = nnue_shards.ingest(args.store, args.block, [path], target=args.target)
            if count >= args.target:
                return count
    return count


def produce(args):
    if (not args.campaign or min(args.workers, args.jobs, args.positions_per_job,
                                args.games_per_job, args.nodes, args.target) <= 0
            or args.start_job < 0 or args.positions_per_job > 1_000_000
            or args.jobs * args.positions_per_job > args.target
            or not 0 < args.max_seconds <= 7 * 86400):
        raise ValueError('invalid producer bounds; each invocation may schedule at most one block')
    args.output.mkdir(parents=True, exist_ok=True)
    args.store.mkdir(parents=True, exist_ok=True)
    recipe = dict(format='maxima.nnue.producers', version=1, campaign=args.campaign,
                  positions_per_job=args.positions_per_job, games_per_job=args.games_per_job, nodes=args.nodes,
                  generator_sha256=nnue_data.file_hash(nnue_selfplay.__file__))
    with writer_lock(args.output / '.pool.lock'):
        path = args.output / 'pool.json'
        if path.exists():
            if json.loads(path.read_text()) != recipe:
                raise ValueError('producer pool recipe mismatch')
        else:
            nnue_shards.publish_json(path, recipe)
        block_stop = args.output / f'{args.block}.stop'
        count = ingest_jobs(args)
        if count >= args.target:
            nnue_shards.seal(args.store, args.block)
            print(f'Block ready: {count}/{args.target}')
            return 0
        if (args.store / f'{args.block}.json').exists():
            raise ValueError('block was sealed partial; choose a new block for further generation')
        block_stop.unlink(missing_ok=True)
        deadline = time.time() + args.max_seconds
        indices = iter(range(args.start_job, args.start_job + args.jobs))
        futures = set()
        error = None
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            def schedule():
                if args.stop_file.exists() or block_stop.exists() or time.time() >= deadline:
                    return
                index = next(indices, None)
                if index is not None:
                    futures.add(pool.submit(run_job, args, index, deadline, block_stop))

            for _ in range(min(args.workers, args.jobs)):
                schedule()
            while futures:
                done, _ = wait(futures, return_when=FIRST_COMPLETED)
                for future in done:
                    futures.remove(future)
                    try:
                        future.result()
                        count = ingest_jobs(args)
                        if count >= args.target:
                            block_stop.touch()
                    except Exception as failure:
                        error = failure
                        block_stop.touch()
                    if error is None:
                        schedule()
        # All running games have drained before publication or failure return.
        count = ingest_jobs(args)
        if error is not None:
            raise error
        if count >= args.target:
            nnue_shards.seal(args.store, args.block)
        print(f'Accepted: {count}/{args.target}; complete game chunks retained in {args.output}')
        return 0


def main():
    parser = argparse.ArgumentParser(prog='maxima nnue:produce', description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='resumable producer pool directory')
    parser.add_argument('--store', type=Path, required=True)
    parser.add_argument('--block', required=True)
    parser.add_argument('--campaign', required=True, help='globally unique seed namespace')
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--network', type=Path, required=True)
    parser.add_argument('--book', type=Path)
    parser.add_argument('--exclude', type=Path, action='append', default=[])
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--start-job', type=int, default=0)
    parser.add_argument('--jobs', type=int, required=True, help='bounded job range; reuse unfinished IDs to resume')
    parser.add_argument('--positions-per-job', type=int, default=100000)
    parser.add_argument('--games-per-job', type=int, default=1000, help='per invocation, per job')
    parser.add_argument('--nodes', type=int, default=10000)
    parser.add_argument('--target', type=int, default=100_000_000)
    parser.add_argument('--max-seconds', type=float, default=3600, help='stop scheduling; current games finish')
    parser.add_argument('--stop-file', type=Path, help='campaign stop signal, checked between games')
    args = parser.parse_args()
    if args.stop_file is None:
        args.stop_file = args.store / 'STOP'
    return produce(args)
