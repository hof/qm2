"""Engine fault latching and clock-bounded recovery shared by online clients."""
import asyncio
import json
import time
import uuid
from pathlib import Path

from .learning_store import atomic_text


def remaining_clock(clock, received_at=None):
    return max(0.0, clock - (max(0.0, time.monotonic() - received_at) if received_at is not None else 0.0))


def search_timeout(clock, received_at=None):
    remaining = remaining_clock(clock, received_at)
    reserve = min(remaining / 2, max(.25, min(5.0, remaining * .1)))
    return max(.001, remaining - reserve)


class engine_safety:
    def __init__(self, platform, root=None, report=None):
        self.platform = platform
        self.path = Path(root) / 'engine_fault.json' if root is not None else None
        self.report = report or (lambda *args, **kwargs: None)
        self.incident = None
        self.recovery_used = False
        if self.path is not None and self.path.exists():
            self.incident = json.loads(self.path.read_text())

    def check_start(self):
        if self.incident is not None:
            raise RuntimeError('engine fault hold is active; review and explicitly clear the saved incident before restarting')

    def fail(self, error, game=None, fen=None):
        first = self.incident is None
        if first:
            self.incident = dict(id=uuid.uuid4().hex, platform=self.platform, time=time.time(),
                                 error=type(error).__name__, reason=str(error), game=game, fen=fen)
            if self.path is not None:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                atomic_text(self.path, json.dumps(self.incident) + '\n')
            self.report('engine_incident', **self.incident)
        return first

    async def recover(self, error, stop, start, clock):
        if self.recovery_used or isinstance(error, (AssertionError, ValueError)) or clock <= .5:
            return False
        self.recovery_used = True
        deadline = time.monotonic() + min(3.0, clock / 2)
        try:
            await asyncio.wait_for(stop(), min(.5, clock / 4))
            await asyncio.wait_for(start(), max(.001, deadline - time.monotonic()))
        except (asyncio.TimeoutError, OSError, RuntimeError, AssertionError):
            self.report('recovery_failed', incident=self.incident['id'])
            return False
        self.report('engine_recovered', incident=self.incident['id'], message='Current game only; new games remain blocked')
        return True
