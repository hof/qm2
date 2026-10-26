"""Locked monthly PGN archives; individual game files are durable pending work."""
import argparse
from functools import lru_cache
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import chess.pgn

from .learning_store import atomic_text, merge_game, prepare_game, read_games, writer_lock


def month_path(game, directory):
    date = game.headers.get('UTCDate', game.headers.get('Date', ''))
    if not re.fullmatch(r'\d{4}\.\d{2}\.\d{2}', date):
        raise ValueError('monthly archive requires a known PGN date')
    from datetime import date as calendar_date
    calendar_date.fromisoformat(date.replace('.', '-'))
    return Path(directory) / game.headers['LearningSource'] / (date[:7].replace('.', '-') + '.pgn')


def index_path(path):
    return path.with_suffix('.index.json')


def publish_index(path, entries):
    stat = path.stat()
    atomic_text(index_path(path), json.dumps({'size': stat.st_size, 'mtime': stat.st_mtime_ns,
                                            'games': entries}, sort_keys=True) + '\n')


@lru_cache(maxsize=4)
def read_index(path, size, mtime):
    cached = json.loads(index_path(path).read_text(encoding='utf-8'))
    if (cached['size'], cached['mtime']) != (size, mtime):
        raise ValueError('stale monthly index')
    return cached['games']


def load_index(path):
    stat = path.stat()
    try:
        return read_index(path, stat.st_size, stat.st_mtime_ns)
    except (OSError, ValueError, KeyError, TypeError):
        pass
    # One scan for a legacy PGN, an externally changed file, or crash recovery.
    text = path.read_text(encoding='utf-8')
    if text.startswith('\ufeff'):
        raise ValueError('unsupported PGN byte order mark')
    stream = io.StringIO(text)
    entries, position, offset = {}, 0, 0
    while (game := chess.pgn.read_game(stream)) is not None:
        prepare_game(game)
        end = stream.tell()
        length = len(text[position:end].encode('utf-8'))
        identity = game.headers['LearningId']
        if identity in entries:
            raise ValueError(f'{path}: duplicate game ID in existing monthly archive')
        entries[identity] = [offset, length]
        position, offset = end, offset + length
    publish_index(path, entries)
    return entries


def recover_all(directory):
    """Recover interrupted appends before PGN-only learning reconstruction."""
    directory = Path(directory)
    with writer_lock(directory / '.pgn.lock'):
        for journal in directory.glob('*/*.pending.json'):
            recover(journal.with_name(journal.name[:-len('.pending.json')] + '.pgn'))


def recover(path):
    journal = path.with_suffix('.pending.json')
    if not journal.exists():
        return
    pending = json.loads(journal.read_text(encoding='utf-8'))
    offset, payload = pending['offset'], pending['text'].encode('utf-8')
    size = path.stat().st_size if path.exists() else 0
    if not offset <= size <= offset + len(payload):
        raise ValueError(f'{path}: archive size conflicts with recovery journal')
    with path.open('r+b' if path.exists() else 'w+b') as stream:
        stream.truncate(offset)
        stream.seek(offset)
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    index_path(path).unlink(missing_ok=True)
    load_index(path)
    journal.unlink()


def archived_game(game, directory):
    """Caller holds directory/.pgn.lock."""
    try:
        path = month_path(game, directory)
    except ValueError:
        return None  # Unknown dates may still be saved as pending individual files.
    recover(path)
    if not path.exists():
        return None
    entry = load_index(path).get(game.headers['LearningId'])
    if entry is None:
        return None
    with path.open('rb') as stream:
        stream.seek(entry[0])
        text = stream.read(entry[1]).decode('utf-8')
    return prepare_game(chess.pgn.read_game(io.StringIO(text)))


def write_month(game, directory, replace=False):
    """Caller holds directory/.pgn.lock; routine appends never parse old games."""
    path = month_path(game, directory)
    old = archived_game(game, directory)
    entries = load_index(path) if path.exists() else {}
    identity = game.headers['LearningId']
    if old is not None:
        previous = str(old)
        game = merge_game(old, game, replace)
        if str(game) == previous:
            return path
        # Corrections are exceptional: atomically replace just this game's record.
        offset, length = entries[identity]
        data = path.read_bytes()
        atomic_text(path, (data[:offset] + (str(game) + '\n\n').encode('utf-8')
                           + data[offset + length:]).decode('utf-8'))
        load_index(path)
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    offset = path.stat().st_size if path.exists() else 0
    separator = ''
    if offset:
        with path.open('rb') as stream:
            stream.seek(max(0, offset - 2))
            if stream.read() != b'\n\n':
                separator = '\n\n'
    text = separator + str(game) + '\n\n'
    journal = path.with_suffix('.pending.json')
    atomic_text(journal, json.dumps({'offset': offset, 'text': text}) + '\n')
    with path.open('ab') as stream:
        stream.write(text.encode('utf-8'))
        stream.flush()
        os.fsync(stream.fileno())
    entries[identity] = [offset, len(text.encode('utf-8'))]
    publish_index(path, entries)
    journal.unlink()
    return path


def archive_pending(source):
    source = Path(source)
    directory = source.parent
    with writer_lock(directory / '.pgn.lock'):
        if not source.exists():
            return None  # Another pod already consumed this pending file.
        games = read_games(source)
        if len(games) != 1:
            raise ValueError('archive input must contain exactly one game')
        game = prepare_game(games[0])
        if source.name != game.headers['LearningId'] + '.pgn':
            raise ValueError('archive input must be named <LearningId>.pgn')
        if game.headers['Result'] == '*' or game.headers.get('LearningIncomplete') == 'true':
            raise ValueError('unfinished game remains pending')
        destination = write_month(game, directory)
        # The monthly PGN and its index are durable before removing pending work.
        source.unlink()
        if os.name != 'nt':
            descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
        return destination


def archive_saved(paths, on_line):
    """Online clients invoke the same CLI command available for manual retries."""
    for path in dict.fromkeys(Path(path) for path in paths):
        if not re.fullmatch(r'[0-9a-f]{64}\.pgn', path.name):
            continue  # A replay may already have updated its monthly record.
        try:
            result = subprocess.run([sys.executable, '-m', 'maxima_runner', 'experience:archive', str(path)],
                                    capture_output=True, text=True)
        except OSError as error:
            on_line(f'-- PGN remains pending; could not start experience:archive: {error}')
            continue
        for line in (result.stdout + result.stderr).splitlines():
            on_line('-- ' + line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pgn', type=Path, nargs='+', help='completed <LearningId>.pgn files to archive')
    args = parser.parse_args()
    failed = False
    for source in args.pgn:
        try:
            destination = archive_pending(source)
            print(f'Archived {source} to {destination}' if destination else f'Already consumed {source}')
        except (OSError, ValueError) as error:
            print(f'Archive failed for {source}: {error}', file=sys.stderr)
            failed = True
    return int(failed)
