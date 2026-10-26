"""Shared online experience: durable PGNs, serialized writers, atomic derived snapshots."""
from contextlib import contextmanager
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tempfile
from urllib.parse import urlparse

import chess
import chess.pgn

from .variants import board_variant, read_pgn, learning_variant, wild_boards


@contextmanager
def writer_lock(path):
    """An advisory process lock; the PVC must support filesystem locks and atomic rename."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            stream.seek(0)
            if not stream.read(1):
                stream.write(b'\0')
                stream.flush()
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def atomic_text(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.' + path.name, suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        if os.name != 'nt':
            directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def server_name(value):
    name = (urlparse(value if '://' in value else '//' + value).netloc or value).lower().rstrip('/')
    if name in ('', '.', '..') or any(character.isspace() or character in '/\\' for character in name):
        raise ValueError('invalid learning server name')
    return name


def prepare_game(game, source=None, server=None, players=(), results_only=False, group=None, variant=None):
    """Validate and attach provenance, own colour and a reproducible identity to a PGN."""
    actual_variant = board_variant(game.board())
    if game.errors or not game.board().is_valid():
        raise ValueError('shared learning accepts valid supported-variant PGNs only')
    if variant is not None and learning_variant(actual_variant) != learning_variant(variant):
        raise ValueError(f'{actual_variant} PGN cannot enter {variant} experience')
    if game.next() is None:
        raise ValueError('PGN has no moves')
    replay = game.board()
    for move in game.mainline_moves():
        if not replay.is_legal(move):
            raise ValueError('illegal PGN move')
        replay.push(move)
    headers = game.headers
    if headers.get('Result', '*') not in ('*', '1-0', '0-1', '1/2-1/2'):
        raise ValueError('invalid PGN result')
    source = headers.get('LearningSource') or source
    if source is None:
        event, site = headers.get('Event', '').lower(), headers.get('Site', '').lower()
        if 'lichess' in event or 'lichess.org' in site:
            source = 'lichess'
        elif 'fics' in event or 'freechess.org' in site:
            source = 'fics'
        else:
            source = 'local'
    if source not in ('fics', 'lichess', 'local'):
        raise ValueError(f'unknown learning source: {source}')
    server = server_name(headers.get('LearningServer') or server or
                         ('freechess.org' if source == 'fics' else 'lichess.org' if source == 'lichess' else 'local'))
    headers.update(LearningSource=source, LearningServer=server)
    if source == 'lichess' and 'GameId' not in headers:
        path = urlparse(headers.get('Site', '')).path.strip('/').split('/')[0]
        if re.fullmatch(r'[A-Za-z0-9]{8}(?:[A-Za-z0-9]{4})?', path):
            headers['GameId'] = path[:8]
    aliases = {name.casefold() for name in players}
    candidates = []
    for color in ('White', 'Black'):
        name = headers.get(color, '')
        engine = headers.get(f'{color}Engine', '')
        if not engine and name in ('A', 'B'):
            engine = headers.get(f'Engine{name}', '')
        if engine:
            headers[f'{color}Engine'] = engine
            headers[f'{color}Type'] = 'program'
        if name.casefold() in aliases or (not aliases and (re.search(r'\bmaxima2?\b', engine, re.I)
                                                         or name.casefold() in ('maxima', 'maxima2', 'maxdrone', 'maximaengine'))):
            candidates.append(color.lower())
    own_color = headers.get('LearningColor')
    if own_color is None:
        if len(candidates) == 1:
            own_color = candidates[0]
        elif not aliases:
            programs = [color.lower() for color in ('White', 'Black')
                        if headers.get(f'{color}Type') == 'program' and headers.get(f'{color}Engine')]
            if len(programs) == 1:
                own_color = programs[0]
    if own_color not in ('white', 'black'):
        raise ValueError('cannot identify our side; supply --player with our PGN player name')
    if aliases and headers.get(own_color.title(), '').casefold() not in aliases:
        raise ValueError('PGN LearningColor conflicts with --player')
    headers['LearningColor'] = own_color
    prefix = own_color.title()
    headers[f'{prefix}Type'] = 'program'
    headers.setdefault(f'{prefix}Engine', 'Maxima')
    if group is not None:
        headers['LearningGroup'] = 'engines' if group == 'bots' else group
    if headers.get('LearningGroup') not in (None, 'humans', 'masters', 'engines'):
        raise ValueError('invalid LearningGroup')
    if results_only:
        headers['LearningResults'] = 'true'
    identity = headers.get('LearningId')
    if identity is not None and not re.fullmatch(r'[0-9a-f]{64}', identity):
        raise ValueError('invalid LearningId (expected a SHA-256 identity)')
    if identity is None:
        native = headers.get('GameId')
        if native:
            parts = [source, server, native]
            if source == 'fics':
                parts += [headers.get('UTCDate', headers.get('Date')), headers.get('UTCTime'),
                          headers.get('White'), headers.get('Black')]
        else:
            parts = [source, server, headers.get('UTCDate', headers.get('Date')), headers.get('UTCTime'),
                     headers.get('Round'), headers.get('White', '').casefold(), headers.get('Black', '').casefold(),
                     game.board().fen(), [move.uci() for move in game.mainline_moves()]]
        identity = hashlib.sha256(json.dumps(parts, sort_keys=True).encode()).hexdigest()
        headers['LearningId'] = identity
    return game


def read_games(path):
    """Materialize input before publishing anything; reject malformed games instead of partial ingestion."""
    path = Path(path)
    before = path.stat()
    text = path.read_text(encoding='utf-8-sig')
    after = path.stat()
    if (before.st_mtime_ns, before.st_size, before.st_ino) != (after.st_mtime_ns, after.st_size, after.st_ino):
        raise ValueError(f'{path}: PGN changed during ingestion; retry the command')
    stream = io.StringIO(text)
    games = []
    while True:
        game = read_pgn(stream)
        if game is None:
            return games
        if game.errors:
            raise ValueError(f'{path}: malformed PGN')
        games.append(game)


def merge_game(old, new, replace=False):
    """Keep richer annotations on replay; require explicit correction of a final result."""
    if (board_variant(old.board()) != board_variant(new.board())
            or old.board().fen() != new.board().fen() or list(old.mainline_moves()) != list(new.mainline_moves())
            or any(old.headers.get(key) != new.headers.get(key)
                   for key in ('LearningSource', 'LearningServer', 'LearningColor', 'White', 'Black'))):
        raise ValueError('conflicting game content for one LearningId')
    previous, result = old.headers.get('Result', '*'), new.headers.get('Result', '*')
    if previous != '*' and result != '*' and previous != result and not replace:
        raise ValueError('conflicting final result; use --replace to correct the archived game')
    if result != '*' and (previous == '*' or replace):
        old.headers['Result'] = result
        if 'Termination' in new.headers:
            old.headers['Termination'] = new.headers['Termination']
    for key, value in new.headers.items():
        old.headers.setdefault(key, value)
    from .search_effort import effort, analysis_rank
    for before, after in zip(old.mainline(), new.mainline()):
        clocks = {name: getattr(before, name)() for name in ("clock", "emt")}
        # An older export must not erase our stronger analysis or clock annotations.
        def rank(node):
            nodes, _, seconds = effort(node)
            score = node.eval()
            cp = score.pov(node.parent.board().turn).score(mate_score=2000) if score else 0
            return analysis_rank(node.eval_depth(), cp, score.is_mate() if score else False, seconds, nodes)
        if rank(after) > rank(before):
            before.comment = after.comment
        elif after.comment and not before.comment:
            before.comment = after.comment
        for get, put in (('clock', 'set_clock'), ('emt', 'set_emt')):
            if getattr(before, get)() is None and getattr(after, get)() is not None:
                getattr(before, put)(getattr(after, get)())
            if getattr(before, get)() is None and clocks[get] is not None:
                getattr(before, put)(clocks[get])
    return old


def archive_game(game, directory, replace=False):
    """Save pending work, or update an already compacted monthly record."""
    from .monthly_pgn import archived_game, write_month
    directory = Path(directory)
    path = directory / (game.headers['LearningId'] + '.pgn')
    with writer_lock(directory / '.pgn.lock'):
        monthly = archived_game(game, directory)
        if monthly is not None:
            game = merge_game(monthly, game, replace)
            return write_month(game, directory, replace), game
        if path.exists():
            with path.open(encoding='utf-8') as stream:
                game = merge_game(read_pgn(stream), game, replace)
        text = str(game) + '\n\n'
        if not path.exists() or path.read_text(encoding='utf-8') != text:
            atomic_text(path, text)
    return path, game


def find_archived(game, directory):
    from .monthly_pgn import archived_game
    with writer_lock(Path(directory) / '.pgn.lock'):
        return archived_game(game, directory)


class shared_store:
    def __init__(self, root, variant='standard'):
        self.variant = learning_variant(variant)
        self.root = Path(root)
        self.snapshot = self.root / 'index' / 'shared.json'
        self.lock = self.root / '.learning.lock'
        self.games = self.root / 'games'

    def _read(self):
        try:
            value = json.loads(self.snapshot.read_text(encoding='utf-8'))
            if (value.get('version') == 2 and value.get('variant', 'standard') == self.variant
                    and isinstance(value.get('games'), dict)):
                return value['games']
        except (OSError, ValueError, AttributeError):
            pass
        return None

    def load(self):
        cached = self._read()
        if cached is None:
            self.rebuild()
            cached = self._read()
        return self.records(cached)

    @staticmethod
    def records(cached):
        return sorted((record for record in cached.values() if record is not None),
                      key=lambda r: (r['date'], r['time'], r['id']))

    def _publish(self, cached):
        from .learning import player_stats, write_player_stats
        write_player_stats(player_stats(self.records(cached)), self.root / 'players')
        atomic_text(self.snapshot, json.dumps({'version': 2, 'variant': self.variant, 'games': cached}, sort_keys=True) + '\n')

    def _ingest(self, games, cached, replace=False):
        from .learning import digest
        learned = []
        # Preflight every conflict before any archived game is replaced.
        prepared = {}
        for game in games:
            identity = game.headers['LearningId']
            path = self.games / (identity + '.pgn')
            old = prepared.get(identity)
            if old is None and path.exists():
                old = read_games(path)[0]
            if old is None:
                old = find_archived(game, self.games)
            prepared[identity] = merge_game(old, game, replace) if old is not None else game
        for identity, game in prepared.items():
            path, game = archive_game(game, self.games, replace)
            record = digest(game)
            cached[identity] = record
            learned.append(record)
        self._publish(cached)
        return learned

    def add(self, game):
        prepare_game(game, variant=self.variant)
        with writer_lock(self.lock):
            cached = self._read()
            if cached is None:
                cached = self._rebuild()
            return self._ingest([game], cached)[0]

    def _rebuild(self):
        from .monthly_pgn import recover_all
        recover_all(self.games)
        # Include both old server layouts on first use; never delete the original PGNs.
        paths = set(self.games.rglob('*.pgn')) if self.games.exists() else set()
        legacy_roots = [self.root / 'lichess' / 'games', self.root / 'fics' / 'games']
        if self.variant == 'standard':
            legacy_roots.extend(self.root / kind / 'games' for kind in wild_boards)
        for legacy in legacy_roots:
            if legacy.exists():
                recover_all(legacy)
                paths.update(legacy.rglob('*.pgn'))
        games = []
        for path in sorted(paths):
            for game in read_games(path):
                prepare_game(game, variant=self.variant)
                canonical = self.games / (game.headers['LearningId'] + '.pgn')
                if canonical.exists() and canonical != path:
                    current = read_games(canonical)[0]
                    # An explicitly corrected archived result wins over old exports.
                    game.headers['Result'] = current.headers.get('Result', '*')
                elif canonical != path:
                    current = find_archived(game, self.games)
                    if current is not None:
                        game.headers['Result'] = current.headers.get('Result', '*')
                games.append(game)
        cached = {}
        self._ingest(games, cached)
        return cached

    def rebuild(self):
        with writer_lock(self.lock):
            cached = self._rebuild()
            return {'games': len(cached), 'learned': sum(record is not None for record in cached.values())}

    def sync(self, source, players=(), source_name=None, replace=False, group=None):
        source = Path(source)
        if not source.exists():
            raise ValueError(f'PGN source does not exist: {source}')
        paths = [source] if source.is_file() else sorted(source.rglob('*.pgn'))
        games = [prepare_game(game, source_name, players=players, results_only=True, group=group, variant=self.variant)
                 for path in paths for game in read_games(path)]
        with writer_lock(self.lock):
            cached = self._read()
            if cached is None:
                cached = self._rebuild()
            before = len(cached)
            records = self._ingest(games, cached, replace)
            return {'read': len(games), 'added': len(cached) - before, 'games': len(cached),
                    'learned': sum(record is not None for record in records)}
