"""Runner-owned CPU permission: live games, due deep analysis, then generation."""
import asyncio
import json
import os
from pathlib import Path
import secrets
import time

from .learning_store import atomic_text, writer_lock
from .project import env_value


def birth(pid):
    try:
        return Path('/proc', str(pid), 'stat').read_text().rsplit(')', 1)[1].split()[19]
    except (OSError, IndexError):
        return None


def alive(row):
    pid = row.get('pid')
    if type(pid) is not int or pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return row.get('birth') is None or row['birth'] == birth(pid)


class generation_permission:
    """A generation session acknowledges suspension outside search deadlines."""
    def __init__(self, path):
        self.path = Path(path)
        self.directory = self.path.with_name(self.path.name + '.workers')
        self.directory.mkdir(parents=True, exist_ok=True)
        self.ack = self.directory / (str(os.getpid()) + '-' + secrets.token_hex(8) + '.json')
        self.active = None
        self.mark(False)

    def mark(self, active):
        if self.active != active:
            atomic_text(self.ack, json.dumps(dict(pid=os.getpid(), birth=birth(os.getpid()), active=active)))
            self.active = active

    async def wait(self):
        while True:
            with writer_lock(self.path.with_name(self.path.name + '.lock')):
                allowed = False
                try:
                    value = json.loads(self.path.read_text())
                    age = time.time() - value.get('updated_at', 0)
                    allowed = value.get('allowed') is True and -2 <= age <= 30
                except (OSError, ValueError, TypeError):
                    pass
                self.mark(allowed)
            if allowed:
                return
            await asyncio.sleep(0.05)

    def close(self):
        try:
            self.ack.unlink()
        except FileNotFoundError:
            pass


class idle_scheduler:
    def __init__(self, path, state, deep=None, *, deep_minutes=10, on_line=print, on_failure=None, handoff_timeout=3, admission=None):
        self.path, self.state, self.deep = Path(path), state, deep
        self.deep_seconds = max(0, deep_minutes * 60)
        self.on_line, self.on_failure = on_line, on_failure
        self.admission = admission
        self.task = self.deep_task = None
        self.pending_until = 0
        self.idle_since = time.monotonic()
        self.handoff_timeout = handoff_timeout
        self.reason = None
        self.publish(False, 'starting')

    def publish(self, allowed, reason):
        with writer_lock(self.path.with_name(self.path.name + '.lock')):
            atomic_text(self.path, json.dumps(dict(allowed=allowed, reason=reason, updated_at=time.time())))
        if reason != self.reason:
            self.on_line('-- idle work: ' + reason)
            self.reason = reason

    def active_workers(self):
        directory = self.path.with_name(self.path.name + '.workers')
        active = []
        for path in directory.glob('*.json'):
            try:
                row = json.loads(path.read_text())
                if not alive(row):
                    path.unlink()
                elif row.get('active') is not False:
                    active.append(row['pid'])
            except FileNotFoundError:
                pass
        return active

    async def quiesce(self, reason):
        self.publish(False, reason)
        deadline = time.monotonic() + self.handoff_timeout
        while self.active_workers():
            if time.monotonic() >= deadline:
                raise RuntimeError('background generation did not acknowledge CPU handoff')
            await asyncio.sleep(0.02)

    async def cancel_deep(self):
        if self.deep_task is not None:
            task, self.deep_task = self.deep_task, None
            task.cancel()
            try:
                await asyncio.wait_for(asyncio.gather(task, return_exceptions=True), 4)
            except asyncio.TimeoutError as error:
                raise RuntimeError('deep analysis did not release the engine') from error

    async def before_game(self):
        self.pending_until = time.monotonic() + 20
        await self.quiesce('game')
        await self.cancel_deep()

    def listen(self, event):
        if event['type'] == 'game_end':
            self.pending_until = 0
            self.idle_since = time.monotonic()

    async def run(self):
        try:
            while True:
                state = self.state()
                now = time.monotonic()
                if state not in ('ready', 'offline') or now < self.pending_until:
                    self.idle_since = now
                    self.publish(False, state if now >= self.pending_until else 'game')
                    if state in ('fault', 'shutdown'):
                        await self.cancel_deep()
                elif self.deep_task is not None:
                    if self.deep_task.done():
                        task, self.deep_task = self.deep_task, None
                        learned = task.result()
                        self.on_line('-- idle deep analysis finished: ' + str(learned) + ' positions')
                        self.idle_since = now
                    else:
                        self.publish(False, 'deep')
                elif (state == 'ready' and self.deep is not None and self.deep_seconds
                      and now - self.idle_since >= self.deep_seconds):
                    await self.quiesce('deep')
                    if self.state() == 'ready' and time.monotonic() >= self.pending_until:
                        self.deep_task = asyncio.ensure_future(self.deep())
                else:
                    allowed = True if self.admission is None else self.admission()
                    self.publish(allowed, 'generation' if allowed else 'waiting for training controller')
                await asyncio.sleep(0.5)
        except asyncio.CancelledError:
            raise
        except Exception as error:
            try:
                self.publish(False, 'fault')
            except OSError:
                pass
            if self.on_failure is not None:
                self.on_failure(error)
            else:
                raise

    def start(self):
        self.task = asyncio.ensure_future(self.run())

    async def close(self):
        self.publish(False, 'shutdown')
        if self.task is not None:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
        await self.cancel_deep()


def configured_scheduler(platform, player, control, shutdown, reporter, deep=None, deep_minutes=10):
    path = env_value('MAXIMA_IDLE_CONTROL')
    if not path:
        return None
    if player is None:
        raise ValueError('idle scheduling requires a playing engine')
    def state():
        if player.safety.incident:
            return 'fault'
        if shutdown.requested or (control is not None and control.process_stop):
            return 'shutdown'
        if control is not None and control.offline:
            return 'offline'
        if player.game is not None or (platform == 'fics' and player.created is not None):
            return 'game'
        if player.stopping or player.session is None or player.name is None:
            return 'starting'
        return 'ready'
    value = env_value('MAXIMA_IDLE_DEEP_MINUTES')
    minutes = float(value) if value else deep_minutes
    if not 0 <= minutes <= 1440:
        raise ValueError('idle deep interval must be between 0 and 1440 minutes')
    admission_path = env_value('MAXIMA_IDLE_ADMISSION')
    def admitted():
        try:
            value = json.loads(Path(admission_path).read_text())
            return value.get('allowed') is True and time.time() < value.get('expires_at', 0)
        except (OSError, ValueError, TypeError):
            return False
    return idle_scheduler(path, state, deep, deep_minutes=minutes, on_line=reporter.on_line,
                          on_failure=reporter.on_failure, admission=admitted if admission_path else None)
