"""Atomic, locked file storage for native engine experience; no SQL at runtime."""
from copy import deepcopy
import json
from pathlib import Path

from .learning_store import atomic_text, writer_lock


class experience_store:
    def __init__(self, path):
        self.path = Path(path)
        self.lock = writer_lock(self.path.with_suffix(self.path.suffix + '.lock'))
        self.lock.__enter__()
        self.closed = False
        try:
            if self.path.exists():
                self.data = json.loads(self.path.read_text(encoding='utf-8'))
                if (not isinstance(self.data, dict) or self.data.get('version') != 1 or any(not isinstance(self.data.get(key), dict)
                        for key in ('games', 'observations', 'moves', 'analysis'))):
                    raise ValueError('unsupported engine experience store')
            else:
                self.data = {'version': 1, 'games': {}, 'observations': {}, 'moves': {}, 'analysis': {}}
            self.saved = deepcopy(self.data)
        except BaseException:
            self.lock.__exit__(None, None, None)
            raise

    def save(self, force=False):
        if force or self.data != self.saved:
            atomic_text(self.path, json.dumps(self.data) + '\n')
            self.saved = deepcopy(self.data)

    def __enter__(self):
        self.before = deepcopy(self.data)
        return self

    def __exit__(self, kind, error, traceback):
        if kind is not None:
            self.data = self.before
        else:
            try:
                self.save()
            except BaseException:
                self.data = self.before
                raise
        del self.before

    def close(self):
        if not self.closed:
            try:
                self.save()
            finally:
                self.closed = True
                self.lock.__exit__(None, None, None)


def open_store(path):
    return experience_store(path)
