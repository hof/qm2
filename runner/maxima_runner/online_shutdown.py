"""Signal-driven draining for online clients, independent of server transport."""
import asyncio
import hashlib
import math
import signal


def grace_seconds(value):
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValueError('shutdown_grace must be finite, nonnegative seconds')
    return value


class shutdown_controller:
    def __init__(self, on_line, grace=15):
        self.on_line, self.grace = on_line, grace_seconds(grace)
        self.requested = False
        self.request = asyncio.Event()
        self.force = asyncio.Event()

    def notify(self, name, stop):
        if self.requested:
            self.on_line(f'-- {name}: ending shutdown drain now')
            self.force.set()
            return
        self.requested = True
        draining = stop()  # Stop accepting/seeking before another event can start work.
        if draining is False:
            self.force.set()
        self.on_line(f'-- {name}: draining current game for up to {self.grace:g}s')
        self.request.set()

    async def run(self, session, stop):
        loop = asyncio.get_running_loop()
        previous = {}
        for number in (signal.SIGTERM, signal.SIGINT):
            previous[number] = signal.getsignal(number)
            try:
                loop.add_signal_handler(number, self.notify, signal.Signals(number).name, stop)
            except NotImplementedError:
                signal.signal(number, lambda number, frame: loop.call_soon_threadsafe(
                    self.notify, signal.Signals(number).name, stop))
        working = asyncio.ensure_future(session)
        requested = asyncio.ensure_future(self.request.wait())
        force = asyncio.ensure_future(self.force.wait())
        try:
            done, _ = await asyncio.wait({working, requested}, return_when=asyncio.FIRST_COMPLETED)
            if working not in done:
                done, _ = await asyncio.wait({working, force}, timeout=self.grace,
                                             return_when=asyncio.FIRST_COMPLETED)
                if working not in done:
                    self.on_line('-- shutdown drain ended; preserving unfinished game and closing')
                    working.cancel()
                    try:
                        await asyncio.wait_for(asyncio.shield(working), 5)
                    except asyncio.CancelledError:
                        if not working.cancelled():
                            raise
                    except asyncio.TimeoutError:
                        self.on_line('-- shutdown cleanup deadline reached')
                        working.cancel()
            try:
                return await working
            except asyncio.CancelledError:
                if not self.requested:
                    raise
                return 0
        finally:
            requested.cancel()
            force.cancel()
            if not working.done():
                working.cancel()
            await asyncio.gather(requested, force, working, return_exceptions=True)
            for number, handler in previous.items():
                try:
                    loop.remove_signal_handler(number)
                except NotImplementedError:
                    pass
                signal.signal(number, handler)


def save_interrupted(game, directory, on_line):
    """Keep confirmed moves with an unknown result; never masquerade as a finished game."""
    if game is None or directory is None or game.next() is None:
        return
    from .learning_store import archive_game, prepare_game
    try:
        game.headers.update(Result='*', Termination='unterminated', LearningIncomplete='true')
        prepare_game(game)
        # A resumed game may later finish with more moves. Keep this diagnostic
        # snapshot separate from the eventual complete game's identity.
        game.headers.setdefault('LearningParentId', game.headers['LearningId'])
        identity = game.headers['LearningParentId'] + game.board().fen() + ' '.join(
            move.uci() for move in game.mainline_moves())
        game.headers['LearningId'] = hashlib.sha256(('interrupted:' + identity).encode()).hexdigest()
        path, _ = archive_game(game, directory / 'interrupted')
        on_line(f'-- saved unfinished game to {path} (result unknown)')
    except (OSError, ValueError) as error:
        on_line(f'-- could not save unfinished game: {error}')
