"""Engine fault latching and clock-bounded recovery shared by online clients."""
import asyncio
from collections.abc import Mapping
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
        self.details = {}
        if self.path is not None and self.path.exists():
            self.incident = json.loads(self.path.read_text())

    def describe(self, session):
        protocol = getattr(session, 'protocol', None)
        identity = getattr(protocol, 'id', None)
        options = getattr(protocol, 'config', None)
        if isinstance(identity, Mapping):
            self.details['engine'] = dict(identity)
        if isinstance(options, Mapping):
            self.details['options'] = {key: options[key] for key in ('Threads','Hash','EvalFile','Wild') if key in options}

    def check_start(self):
        if self.incident is not None:
            raise RuntimeError('engine fault hold is active; review and explicitly clear the saved incident before restarting')

    def fail(self, error, game=None, fen=None):
        first = self.incident is None
        if first:
            self.incident = dict(id=uuid.uuid4().hex, platform=self.platform, time=time.time(),
                                 error=type(error).__name__, reason=str(error), game=game, fen=fen, engine_details=self.details)
            if self.path is not None:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                atomic_text(self.path, json.dumps(self.incident) + '\n')
            self.report('engine_incident', **self.incident)
        return first

    async def recover(self, error, stop, start, clock):
        if self.recovery_used:
            return False
        self.recovery_used = True
        if isinstance(error, (AssertionError, ValueError)) or clock <= .5:
            return False
        deadline = time.monotonic() + min(3.0, clock / 2)
        try:
            await asyncio.wait_for(stop(), min(.5, clock / 4))
            await asyncio.wait_for(start(), max(.001, deadline - time.monotonic()))
        except Exception:
            self.report('recovery_failed', incident=self.incident['id'])
            return False
        self.report('engine_recovered', incident=self.incident['id'], message='Current game only; new games remain blocked')
        return True


def main():
    import argparse
    from .project import data_root
    parser=argparse.ArgumentParser(description='Inspect an online engine fault or explicitly clear a reviewed incident.')
    parser.add_argument('--platform',choices=('lichess','fics','telegram'),required=True)
    parser.add_argument('--clear-fault',metavar='INCIDENT_ID')
    args=parser.parse_args()
    root=data_root()/'runtime'/args.platform
    safety=engine_safety(args.platform,root)
    if args.clear_fault:
        if safety.incident is None or safety.incident['id'] != args.clear_fault:
            parser.error('incident ID does not match the current fault')
        archive=root/'reviewed';archive.mkdir(exist_ok=True)
        safety.path.replace(archive/(safety.incident['id']+'.json'))
        print('Reviewed fault cleared; the bot remains offline until explicitly started.')
    else:
        print(json.dumps(safety.incident,indent=2))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
