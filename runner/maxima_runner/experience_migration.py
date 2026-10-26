"""One-time, read-only conversion of the retired SQLite experience format."""
import json
from contextlib import closing
from pathlib import Path
import sqlite3

from .experience_store import open_store


def migrate_legacy(source, destination):
    source, destination = Path(source), Path(destination)
    with closing(open_store(destination)) as store:
        if destination.exists():
            return False
        with store:
            try:
                with closing(sqlite3.connect(source.resolve().as_uri() + '?mode=ro', uri=True)) as legacy:
                    legacy.execute('BEGIN')
                    if legacy.execute('PRAGMA user_version').fetchone()[0] != 1:
                        raise ValueError('unsupported legacy experience database version')
                    store.data['games'] = {identity: json.loads(headers)
                                           for identity, headers in legacy.execute('SELECT * FROM games')}
                    for game, key, move in legacy.execute('SELECT * FROM observations'):
                        store.data['observations'].setdefault(game, []).append(key + '/' + move)
                    for row in legacy.execute('SELECT * FROM moves'):
                        store.data['moves'][row[0] + '/' + row[1]] = list(row)
                    for row in legacy.execute('SELECT * FROM analysis'):
                        row = list(row)
                        row[-1] = json.loads(row[-1])
                        store.data['analysis'][json.dumps(row[:4])] = row
            except sqlite3.Error as error:
                raise ValueError(f'legacy experience conversion failed: {error}') from error
        store.save(force=not destination.exists())
        return True
