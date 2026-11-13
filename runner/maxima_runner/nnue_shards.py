"""Disk-indexed immutable NNUE blocks; no corpus-sized Python collections."""
import argparse
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import tempfile
import uuid

import chess

from . import nnue_binary, nnue_data
from .learning_store import writer_lock


def publish_json(path, value):
    nnue_data.publish_jsonl(path, value, [])


def validate_row(row, header):
    board = row['board']
    method = header.get('split_method', 'position')
    if method not in ('position', 'game'):
        raise ValueError('unsupported split method')
    split = nnue_data.game_split(row['source']) if method == 'game' else nnue_data.position_split(board)
    if (header.get('variant') != 'standard' or header.get('variant_state_size')
            or header.get('purpose') == 'heldout_evaluation' or board.chess960
            or not board.is_valid() or len(board.piece_map()) > 32
            or row['split'] != split or row['result_stm'] is None):
        raise ValueError('invalid standard training row/split/completed result')
    return nnue_data.position_key(board)


def connect(root):
    db = sqlite3.connect(root / 'index.sqlite3', timeout=60)
    db.execute('PRAGMA foreign_keys=ON')
    db.executescript('''
        CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS positions (key TEXT PRIMARY KEY) WITHOUT ROWID;
        CREATE TABLE IF NOT EXISTS blocks (name TEXT PRIMARY KEY, target INTEGER NOT NULL,
            positions INTEGER NOT NULL DEFAULT 0, sealed INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE IF NOT EXISTS inputs (digest TEXT PRIMARY KEY, block TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS cursors (digest TEXT PRIMARY KEY, next_record INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS shards (block TEXT NOT NULL, path TEXT NOT NULL UNIQUE,
            sha256 TEXT NOT NULL, positions INTEGER NOT NULL, train INTEGER NOT NULL,
            validation INTEGER NOT NULL);
    ''')
    db.execute('INSERT OR IGNORE INTO settings VALUES (?,?)', ('store_id', uuid.uuid4().hex))
    db.commit()
    return db


def ingest(root, block, inputs, *, target=100_000_000, shard_size=1_000_000, exclude=()):
    """One transaction per input; retry after a crash cannot count rows twice.

    Published content-addressed files can outlive rolled-back transactions. They
    are harmless and reused on retry. Only committed entries enter a manifest.
    All calls for a campaign use the same root, including future teacher stages.
    """
    root = Path(root)
    if target <= 0 or not 0 < shard_size <= 1_000_000 or not block or Path(block).name != block:
        raise ValueError('invalid block name, target or shard size')
    root.mkdir(parents=True, exist_ok=True)
    (root / 'shards').mkdir(exist_ok=True)
    with writer_lock(root / '.lock'), closing(connect(root)) as db:
        # Reserved sets must be installed before any training data is ingested.
        for path in exclude:
            digest = 'excluded:' + nnue_data.file_hash(path)
            if db.execute('SELECT 1 FROM inputs WHERE digest=?', (digest,)).fetchone():
                continue
            if db.execute('SELECT 1 FROM shards LIMIT 1').fetchone():
                raise ValueError('install all reserved exclusions before ingesting training rows')
            _, rows = nnue_data.read_dataset(path)
            db.executemany('INSERT OR IGNORE INTO positions VALUES (?)',
                           ((nnue_data.position_key(chess.Board(row['fen'])),) for row in rows))
            db.execute('INSERT INTO inputs VALUES (?, ?)', (digest, '__excluded__'))
        db.execute('INSERT OR IGNORE INTO blocks(name,target) VALUES (?,?)', (block, target))
        saved_target, count, sealed = db.execute(
            'SELECT target,positions,sealed FROM blocks WHERE name=?', (block,)).fetchone()
        if saved_target != target:
            raise ValueError('block target differs from saved target')
        db.commit()
        for path in inputs:
            digest = nnue_data.file_hash(path)
            if db.execute('SELECT 1 FROM inputs WHERE digest=?', (digest,)).fetchone():
                continue
            if sealed or (root / f'{block}.json').exists():
                raise ValueError('block is full or sealed; preserve remaining input for the next block')
            if count >= target:
                break
            with nnue_binary.binary_dataset(path) as data, db:
                pending = []

                def flush():
                    if not pending:
                        return
                    metadata = dict(data.header, input_sha256=digest)
                    # Keep each input's teacher/config metadata intact.
                    for key in ('sources', 'positions', 'store_promoted', 'record_size'):
                        metadata.pop(key, None)
                    with tempfile.TemporaryDirectory(dir=root) as directory:
                        temporary = Path(directory) / 'shard.mxdata'
                        nnue_binary.write_dataset(temporary, metadata, pending, store_promoted=True)
                        checksum = nnue_data.file_hash(temporary)
                        name = f'shards/{checksum}.mxdata'
                        destination = root / name
                        if destination.exists():
                            if nnue_data.file_hash(destination) != checksum:
                                raise ValueError('existing immutable shard checksum mismatch')
                        else:
                            temporary.rename(destination)
                    train = sum(row['split'] == 'train' for row in pending)
                    db.execute('INSERT INTO shards VALUES (?,?,?,?,?,?)',
                               (block, name, checksum, len(pending), train, len(pending) - train))
                    pending.clear()

                accepted = 0
                saved_cursor = db.execute('SELECT next_record FROM cursors WHERE digest=?', (digest,)).fetchone()
                cursor = saved_cursor[0] if saved_cursor else 0
                for index in range(cursor, len(data)):
                    if count + accepted >= target:
                        break
                    row = data[index]
                    key = validate_row(row, data.header)
                    inserted = db.execute('INSERT OR IGNORE INTO positions VALUES (?)', (key,)).rowcount
                    cursor = index + 1
                    if inserted:
                        pending.append(row)
                        accepted += 1
                        if len(pending) >= shard_size:
                            flush()
                flush()
                if nnue_data.file_hash(path) != digest:
                    raise ValueError('input changed during ingestion')
                if cursor == len(data):
                    db.execute('INSERT INTO inputs VALUES (?,?)', (digest, block))
                    db.execute('DELETE FROM cursors WHERE digest=?', (digest,))
                else:
                    db.execute('INSERT OR REPLACE INTO cursors VALUES (?,?)', (digest, cursor))
                db.execute('UPDATE blocks SET positions=positions+? WHERE name=?', (accepted, block))
            count += accepted
        return count


def seal(root, block, *, partial=False):
    root = Path(root)
    with writer_lock(root / '.lock'), closing(connect(root)) as db:
        entry = db.execute('SELECT target,positions FROM blocks WHERE name=?', (block,)).fetchone()
        if entry is None or not entry[1] or (entry[0] != entry[1] and not partial):
            raise ValueError('block is incomplete; --partial explicitly preserves a stopped block')
        shards = [dict(zip(('path', 'sha256', 'positions', 'train', 'validation'), row)) for row in
                  db.execute('SELECT path,sha256,positions,train,validation FROM shards WHERE block=? ORDER BY rowid',
                             (block,))]
        store_id = db.execute("SELECT value FROM settings WHERE key='store_id'").fetchone()[0]
        exclusions = [row[0][len('excluded:'):] for row in
                      db.execute("SELECT digest FROM inputs WHERE digest LIKE 'excluded:%' ORDER BY digest")]
        manifest = dict(format='maxima.nnue.shards', version=1, store_id=store_id,
                        excluded_dataset_sha256=exclusions, block=block, target=entry[0],
                        positions=entry[1], partial=entry[0] != entry[1], shards=shards)
        path = root / f'{block}.json'
        if path.exists():
            if json.loads(path.read_text()) != manifest:
                raise ValueError('existing manifest differs from committed block')
        else:
            publish_json(path, manifest)
        db.execute('UPDATE blocks SET sealed=1 WHERE name=?', (block,))
        db.commit()
        return path


def read_manifest(path):
    path = Path(path)
    value = json.loads(path.read_text())
    if value.get('format') != 'maxima.nnue.shards' or value.get('version') != 1 or not value.get('shards'):
        raise ValueError('unsupported or empty shard manifest')
    if not isinstance(value.get('store_id'), str) or len(value['store_id']) != 32:
        raise ValueError('missing global store identity')
    total = 0
    paths = set()
    for shard in value['shards']:
        file = path.parent / shard['path']
        if file.resolve().parent != (path.parent / 'shards').resolve() or file in paths:
            raise ValueError('invalid or repeated shard path')
        paths.add(file)
        if (not all(type(shard.get(key)) is int and shard[key] >= 0
                    for key in ('positions', 'train', 'validation'))
                or not 0 < shard['positions'] <= 1_000_000
                or shard['train'] + shard['validation'] != shard['positions']
                or nnue_data.file_hash(file) != shard['sha256']):
            raise ValueError('shard checksum/count mismatch')
        with nnue_binary.binary_dataset(file) as data:
            if len(data) != shard['positions']:
                raise ValueError('shard record count mismatch')
        total += shard['positions']
    if total != value.get('positions'):
        raise ValueError('manifest count mismatch')
    return value


def main():
    parser = argparse.ArgumentParser(prog='maxima nnue:shards', description=__doc__)
    parser.add_argument('--store', type=Path, required=True)
    parser.add_argument('--block', required=True)
    parser.add_argument('--target', type=int, default=100_000_000)
    parser.add_argument('--shard-size', type=int, default=1_000_000)
    parser.add_argument('--exclude', type=Path, action='append', default=[])
    parser.add_argument('--seal', action='store_true')
    parser.add_argument('--partial', action='store_true')
    parser.add_argument('inputs', type=Path, nargs='*')
    args = parser.parse_args()
    if args.partial and not args.seal:
        parser.error('--partial requires --seal')
    count = ingest(args.store, args.block, args.inputs, target=args.target,
                   shard_size=args.shard_size, exclude=args.exclude)
    print(f'Accepted block positions: {count}/{args.target}')
    if args.seal:
        print(f'Manifest: {seal(args.store, args.block, partial=args.partial)}')
    return 0
