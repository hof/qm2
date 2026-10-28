"""Asynchronous UCI sessions using python-chess, with bounded process cleanup."""
import asyncio
from contextlib import asynccontextmanager
import time

import chess.engine

from .variants import royal_losers_board, orthodox_wild_board


class online_protocol(chess.engine.UciProtocol):
    """Keep complete evaluation reports alongside python-chess's managed pondering."""

    def __init__(self):
        super().__init__()
        self.evidence = None
        self.collecting = False
        self.ponder_info = {}
        self.ponder_evidence = {}
        self.hit_info = {}
        self.ponder_hit = False
        self.ponder_started = None
        self.ponder_seconds = None
        self.on_search_started = None
        self.search_started = None
        self.search_seconds = None

    def send_line(self, line):
        if line.startswith("go ponder"):
            self.ponder_started = time.monotonic()
            self.collecting = False
            self.ponder_info = {}
            self.ponder_evidence = {}
        elif line.startswith("go ") or line == "ponderhit":
            self.collecting = True
            self.search_started = time.monotonic()
            self.ponder_seconds = (self.search_started - self.ponder_started
                                   if line == "ponderhit" and self.ponder_started is not None else None)
            self.hit_info = dict(self.ponder_info) if line == "ponderhit" else {}
            if self.evidence is not None:
                self.evidence.clear()
                if line == "ponderhit":
                    self.evidence.update(self.ponder_evidence)
        if line == "ponderhit":
            self.ponder_hit = True
        super().send_line(line)
        timed_search = line.startswith("go ") and not line.startswith("go ponder")
        if (line == "ponderhit" or timed_search) and self.on_search_started is not None:
            self.on_search_started(line == "ponderhit")

    def line_received(self, line):
        if line.startswith("bestmove ") and self.collecting and self.search_started is not None:
            self.search_seconds = time.monotonic() - self.search_started
        if line.startswith("info ") and (not self.collecting or self.evidence is not None):
            # Keep ponder reports: a solved search can return bestmove immediately
            # on ponderhit without repeating its evaluation. Fresh searches discard
            # this buffer, so a missed prediction cannot inherit another position.
            report = chess.engine._parse_uci_info(line[5:], self.board, chess.engine.INFO_ALL)
            if report.get("multipv", 1) == 1:
                if not self.collecting:
                    self.ponder_info.update(report)
                evidence = self.evidence if self.collecting else self.ponder_evidence
                if evidence is not None and ("depth" in report or "score" in report):
                    evidence.clear()
                    evidence.update(report)
        super().line_received(line)


class engine_session:
    def __init__(self, protocol, timeout):
        self.protocol = protocol
        self.timeout = timeout
        self.pondering = False

    @property
    def identity(self):
        return [f"{name} {value}" for name, value in self.protocol.id.items()]

    async def configure(self, settings):
        # python-chess compares saved target values with parsed values for ponder reuse.
        applied = {name: self.protocol.options[name].parse(value) for name, value in settings.items()
                   if name in self.protocol.options}
        # python-chess manages these when starting analysis.
        configurable = {name: value for name, value in applied.items()
                        if not self.protocol.options[name].is_managed()}
        await asyncio.wait_for(self.protocol.configure(configurable), self.timeout)
        return {name: str(value).lower() if isinstance(value, bool) else str(value)
                for name, value in applied.items()}

    def variant_settings(self, board):
        if not board.is_valid():
            raise ValueError("invalid chess position")
        if board.chess960:
            raise ValueError("unsupported board variant")
        settings = {}
        if type(board) is royal_losers_board:
            wild = self.protocol.options.get("Wild")
            if wild is None or wild.type != "combo" or "losers" not in wild.var:
                raise ValueError("engine does not support royal losers through Wild=losers")
            settings["Wild"] = "losers"
        elif type(board) is chess.Board or isinstance(board, orthodox_wild_board):
            wild = self.protocol.options.get("Wild")
            if wild is not None:
                if wild.type != "combo" or "standard" not in wild.var:
                    raise ValueError("engine does not support Wild=standard")
                settings["Wild"] = "standard"
        else:
            raise ValueError("unsupported board variant")
        return settings

    async def play(self, board, limit, *, game=None, opponent=None, timeout=None, options=None, ceiling=None,
                   on_search_started=None):
        """Choose a move under game clocks; a new game token makes python-chess send ucinewgame.

        options are per-search UCI settings, applied only when the engine has them; None restores the
        option's default, since python-chess keeps the last value sent. An optional ceiling
        sends stop while retaining the ordinary clock allocation and collects bestmove."""
        self.protocol.ponder_hit = False
        settings = {name: self.protocol.options[name].default if value is None else value
                    for name, value in (options or {}).items() if name in self.protocol.options}
        settings.update(self.variant_settings(board))
        # Keep game evaluation behavior while python-chess manages pondering.
        if "UCI_AnalyseMode" in self.protocol.options:
            settings["UCI_AnalyseMode"] = False

        async def play_search():
            self.protocol.on_search_started = on_search_started
            self.protocol.ponder_seconds = None
            self.protocol.search_seconds = None
            self.pondering = False
            evidence = {}
            self.protocol.evidence = evidence
            # play(ponder=True) starts go ponder after bestmove, and reuses the
            # search via ponderhit only for the same game, board and options.
            timer = None
            stopped = False

            def stop_once():
                nonlocal stopped
                if not stopped:
                    stopped = True
                    self.protocol.send_line("stop")

            if ceiling is not None:
                timer = asyncio.get_running_loop().call_later(ceiling, stop_once)
            try:
                result = await self.protocol.play(board, limit, game=game, info=chess.engine.INFO_ALL,
                                                  ponder=True, options=settings, opponent=opponent)
                self.pondering = result.ponder is not None
                result.info = dict(self.protocol.hit_info, **result.info)
                result.info["_ponder_hit"] = self.protocol.ponder_hit
                result.info["_ponder_seconds"] = self.protocol.ponder_seconds
                result.info["_resignation"] = dict(evidence)
                result.info["_search_seconds"] = self.protocol.search_seconds
                return result
            finally:
                self.protocol.on_search_started = None
                self.protocol.evidence = None
                if timer is not None:
                    timer.cancel()

        deadline = self.timeout if timeout is None else timeout
        if ceiling is not None:
            deadline = min(deadline, ceiling + min(self.timeout, 2))
        return await asyncio.wait_for(play_search(), deadline)

    async def stop_pondering(self):
        """Stop background work without changing the game or clearing its hash."""
        if self.pondering:
            self.pondering = False
            await asyncio.wait_for(self.protocol.ping(), self.timeout)

    async def search(self, board, limit, *, game=None, on_info=None):
        settings = self.variant_settings(board)
        started = time.monotonic()
        analysis = None

        async def consume():
            nonlocal analysis
            analysis = await self.protocol.analysis(board, limit, game=game, options=settings)
            async for info in analysis:
                if info.get("multipv", 1) == 1 and on_info is not None:
                    on_info(info, (time.monotonic() - started) * 1000)
            return await analysis.wait()

        try:
            best = await asyncio.wait_for(consume(), self.timeout)
            return best, (time.monotonic() - started) * 1000
        finally:
            if analysis is not None:
                analysis.stop()
                # Reap analysis without cancelling its protocol-owned future.
                completion = asyncio.ensure_future(analysis.wait())
                completion.add_done_callback(
                    lambda task: task.exception() if not task.cancelled() else None)
                try:
                    await asyncio.wait_for(asyncio.shield(completion), min(self.timeout, 2))
                except (asyncio.TimeoutError, chess.engine.EngineError):
                    pass


@asynccontextmanager
async def open_engine(command, directory, timeout=30):
    transport = protocol = None
    try:
        # Keep the transport before the handshake so every failure can reap it.
        transport, protocol = await online_protocol.popen(command, cwd=directory)
        await asyncio.wait_for(protocol.initialize(), timeout)
        yield engine_session(protocol, timeout)
    except asyncio.TimeoutError as error:
        if protocol is not None and protocol.returncode.done():
            raise RuntimeError(f"engine exited with status {protocol.returncode.result()}") from error
        raise RuntimeError("engine response timed out") from error
    except chess.engine.EngineTerminatedError as error:
        raise RuntimeError(f"engine exited: {error}") from error
    except chess.engine.EngineError as error:
        raise RuntimeError(f"UCI protocol error: {error}") from error
    finally:
        if transport is not None:
            try:
                if not protocol.returncode.done():
                    await asyncio.wait_for(protocol.quit(), min(timeout, 2))
            except (asyncio.TimeoutError, chess.engine.EngineError, BrokenPipeError):
                pass
            finally:
                if not protocol.returncode.done():
                    try:
                        transport.kill()
                    except ProcessLookupError:
                        pass
                await asyncio.shield(protocol.returncode)
                transport.close()
