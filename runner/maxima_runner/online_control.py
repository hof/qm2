"""Optional Telegram command role and authenticated runner control endpoints."""
import asyncio
from datetime import datetime
import hmac
import html
import json
from pathlib import Path
import re
import shutil
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request

from .learning_store import atomic_text
from .project import env_value


read_commands = ('status', 'today', 'history', 'eval', 'book', 'stats', 'name')
play_commands = ('pause', 'resume', 'softquit', 'softrestart', 'resign', 'abort', 'draw',
                 'challenge', 'nochallenge', 'reseek', 'seek-pool')
help_text = ('Maxima commands:\n/maxima status\n/maxima today\n/maxima history\n'
             '/maxima <runner|all> eval|book|stats|name\n'
             '/maxima <runner|all> status|today|history|pause|resume\n'
             '/maxima <runner|all> softquit|softrestart|resign|abort|draw\n'
             '/maxima lichess challenge|nochallenge\n'
             '/maxima fics reseek\n/maxima fics seek-pool <pool>')


def history_text(games, zone):
    if not games:
        return 'No recorded games yet.'
    lines = ['Date/time    Runner     Opponent              R  Clock']
    for row in sorted(games, key=lambda row: row['at'], reverse=True)[:10]:
        when = datetime.fromtimestamp(row['at'], zone).strftime('%d/%m %H:%M')
        opponent = ' '.join((row.get('title', '') + ' ' + row['opponent']).split())[:21]
        clock = str(row['clock'])
        seconds, separator, increment = clock.partition('+')
        if separator and seconds.isdigit():
            clock = f'{int(seconds) / 60:g}+{increment}'
        clock = clock[:8]
        lines.append(f"{when:12} {row['runner'][:10]:10} {opponent:21} {row['result']:1}  {clock}")
    return '\n'.join(lines)


def configured_control(platform, player, reporter):
    if player is not None and (env_value('MAXIMA_CONTROL_BIND') or env_value('MAXIMA_TELEGRAM_LISTENER') == '1'):
        return runner_control(platform, player, reporter)
    return None


def read_json(path, fallback):
    return json.loads(path.read_text()) if path.exists() else fallback


class no_redirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        return None


def post_json(url, payload, token=None, ca_file=None, timeout=6):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    request = urllib.request.Request(url, json.dumps(payload).encode(), headers, method='POST')
    context = ssl.create_default_context(cafile=ca_file)
    opener = urllib.request.build_opener(no_redirect(), urllib.request.HTTPSHandler(context=context))
    try:
        with opener.open(request, timeout=timeout) as response:
            return json.loads(response.read(65536))
    except urllib.error.HTTPError as error:
        error.close()
        raise


def validate_target(target):
    url = urllib.parse.urlsplit(target['url'])
    if (url.username or url.password or url.query or url.fragment or not url.hostname
            or url.path != '/command' or (url.scheme != 'https' and not (
                url.scheme == 'http' and url.hostname in ('127.0.0.1', '::1', 'localhost')))):
        raise ValueError('control targets require HTTPS (HTTP is limited to loopback) and /command')
    if len(target.get('token', '')) < 32:
        raise ValueError('control tokens must contain at least 32 characters')


class runner_control:
    def __init__(self, platform, player, reporter):
        self.platform, self.player, self.reporter = platform, player, reporter
        self.runner_id = env_value('MAXIMA_RUNNER_ID') or platform
        if not re.fullmatch(r'[a-z][a-z0-9_-]{0,31}', self.runner_id) or self.runner_id in ('all', 'help'):
            raise ValueError('invalid runner ID')
        self.root = reporter.root / 'control'
        self.root.mkdir(exist_ok=True)
        self.state_path = self.root / 'state.json'
        self.state = read_json(self.state_path, {'paused': False, 'command_id': -1})
        self.local_runner = player is not None
        if player is not None:
            self.player.paused = bool(self.state['paused'])
        self.lock = asyncio.Lock()
        self.server = None
        self.task = None
        self.listener = None
        self.connections = set()
        self.shutdown_requested = None
        self.shutdown_ack = asyncio.Event()

    def status(self):
        player = self.player
        if player is None:
            return self.runner_id + ': Telegram monitor online'
        safety = player.safety
        mode = ('fault hold' if safety.incident else 'stopping' if player.stopping
                else 'paused' if player.paused else 'playing' if player.game is not None
                else 'ready' if player.session is not None else 'starting')
        text = self.runner_id + ': ' + mode
        if player.game is not None:
            game = getattr(player.game, 'id', player.game)
            text += ' · current game ' + str(game)
        if getattr(player, 'name', None):
            text += ' · ' + player.name
        return text

    async def command(self, command, identity, arguments=''):
        if not self.local_runner:
            raise ValueError('the monitor has no local chess client')
        if command not in read_commands + play_commands:
            raise ValueError('unsupported command')
        if not isinstance(identity, int) or isinstance(identity, bool) or identity < 0:
            raise ValueError('invalid command ID')
        if not isinstance(arguments, str) or len(arguments) > 512 or '\n' in arguments or '\r' in arguments:
            raise ValueError('invalid command arguments')
        if arguments and command != 'seek-pool':
            raise ValueError('unexpected command arguments')
        if command == 'status':
            return self.status()
        if command == 'today':
            day = datetime.now(self.reporter.zone).date().isoformat()
            return self.runner_id + ': ' + self.reporter.summary(day)
        if command == 'history':
            rows = await asyncio.get_running_loop().run_in_executor(None, self.reporter.recent_games)
            return {'games': [dict(row, runner=self.runner_id) for row in rows]}
        if command in ('eval', 'book', 'stats', 'name'):
            return self.runner_id + ': ' + self.game_reply(command)
        if (command in ('challenge', 'nochallenge') and self.platform != 'lichess'
                or command in ('reseek', 'seek-pool') and self.platform != 'fics'):
            return self.runner_id + ': command not available on this server.'
        if command == 'seek-pool':
            from .fics import parse_seek_pool
            try:
                parse_seek_pool(arguments)
            except ValueError as error:
                return self.runner_id + ': ' + str(error)
        async with self.lock:
            if identity <= self.state['command_id']:
                return self.status() + ' · command already processed'
            player = self.player
            if command in ('resume', 'softrestart') and (player.stopping or player.safety.incident
                    or (player.safety.path is not None and player.safety.path.exists())
                    or self.reporter.stop_requested or self.reporter.resource_reported
                    or (self.reporter.stop_at and time.time() >= self.reporter.stop_at)
                    or (self.reporter.minimum_free and shutil.disk_usage(self.root).free < self.reporter.minimum_free)):
                return self.status() + ' · ' + command + ' refused; local review required'
            # Commit desired admission state before any network await. Repeated
            # deliveries cannot undo a newer command, including across restarts.
            state = dict(self.state, command_id=identity)
            if command in ('pause', 'resume'):
                state['paused'] = command == 'pause'
            atomic_text(self.state_path, json.dumps(state) + '\n')
            self.state = state
            player.paused = state['paused']
            self.reporter.on_line('-- operator command: ' + self.runner_id + ' ' + command)
            if command not in ('pause', 'resume'):
                return self.runner_id + ': ' + await self.native_command(command, arguments, identity)
            if self.platform == 'lichess':
                if player.paused:
                    await player.cancel_challenge()
                    if player.in_tournament:
                        await player.leave_tournament()
            elif player.paused:
                await player.send('unseek')
            else:
                await player.seek()
            return self.status() + (' · finishing current game' if player.paused and player.game is not None else '')

    async def native_command(self, command, arguments, identity):
        player = self.player
        if command in ('softquit', 'softrestart'):
            self.shutdown_requested = identity
            restart = command == 'softrestart'
            if self.platform == 'lichess':
                player.soft_quit(restart=restart)
                await player.cancel_challenge()
            else:
                player.restart = restart
                await player.stop()
            return ('Restarting' if restart else 'Stopping') + (' after this game.' if player.game is not None else '.')
        if command in ('challenge', 'nochallenge'):
            return player.set_challenging(command == 'challenge') + '.'
        if command == 'reseek':
            if player.game is not None:
                return 'Game in progress; seeks refresh after this game.'
            await player.seek()
            return 'Seeks refreshed.'
        if command == 'seek-pool':
            pool = await player.set_seek_pool(arguments)
            return 'Seek pool set: ' + ', '.join(pool)
        if player.game is None:
            return 'No game in progress.'
        if self.platform == 'lichess':
            path = 'draw/yes' if command == 'draw' else command
            if not await player.api.post(player.game.path(path)):
                return 'Could not ' + command + '.'
        else:
            await player.connection.send(command)
        return command.capitalize() + ' sent.'

    def game_reply(self, command):
        player = self.player
        if command == 'name':
            protocol = getattr(player.session, 'protocol', None)
            return (getattr(protocol, 'id', None) or {}).get('name', 'Maxima')
        if player.game is None:
            return 'No game in progress.'
        game = player.game if self.platform == 'lichess' else player
        if command == 'eval':
            return game.last_eval or 'No search yet.'
        if command == 'book':
            if game.left_book:
                return 'Out of book since ' + game.left_book + '.'
            return game.last_book or 'No book move yet.'
        if self.platform == 'lichess':
            return game.stats_line()
        learner = player.game_learner
        if learner is None:
            return 'No stats: learning is off.'
        from . import learning
        name = player.opponent.name if player.opponent else '?'
        stats = learning.player_stats(learner.records, learner.server).get(name.lower(), {})
        return (f"Vs {name}: {stats.get('games', 0)} games, W/D/L "
                f"{stats.get('wins', 0)}/{stats.get('draws', 0)}/{stats.get('losses', 0)}.")

    async def serve(self, reader, writer):
        task = asyncio.current_task()
        self.connections.add(task)
        status, result = '400 Bad Request', {'error': 'invalid request'}
        try:
            headers, body = await asyncio.wait_for(self.read_request(reader), 3)
            supplied = headers.get('authorization', '')
            if not hmac.compare_digest(supplied, 'Bearer ' + self.token):
                status, result = '403 Forbidden', {'error': 'forbidden'}
            else:
                payload = json.loads(body)
                text = await asyncio.wait_for(self.command(payload['command'], payload['id'], payload.get('arguments', '')), 8)
                status, result = '200 OK', text if isinstance(text, dict) else {'text': text}
        except (ValueError, KeyError, TypeError, UnicodeError, asyncio.TimeoutError,
                asyncio.IncompleteReadError, asyncio.LimitOverrunError):
            pass
        except asyncio.CancelledError:
            writer.close()
            self.connections.discard(task)
            raise
        except Exception:
            status, result = '503 Service Unavailable', {'error': 'command failed; inspect runner logs'}
            self.reporter.on_line('-- control command failed; admission state preserved')
        try:
            body = json.dumps(result).encode()
            writer.write(('HTTP/1.1 ' + status + '\r\nContent-Type: application/json\r\n'
                          'Content-Length: ' + str(len(body)) + '\r\nConnection: close\r\n\r\n').encode() + body)
            await asyncio.wait_for(writer.drain(), 2)
            if status == '200 OK' and self.shutdown_requested == payload.get('id'):
                self.shutdown_ack.set()
        except (OSError, asyncio.TimeoutError):
            pass
        finally:
            writer.close()
            self.connections.discard(task)

    async def read_request(self, reader):
        header = await reader.readuntil(b'\r\n\r\n')
        lines = header.decode('ascii').split('\r\n')
        if len(header) > 4096 or lines[0] != 'POST /command HTTP/1.1':
            raise ValueError('invalid request')
        headers = {}
        for line in lines[1:-2]:
            key, value = line.split(':', 1)
            key = key.strip().lower()
            if key in headers:
                raise ValueError('duplicate header')
            headers[key] = value.strip()
        length = int(headers['content-length'])
        if not 0 < length <= 4096 or 'transfer-encoding' in headers:
            raise ValueError('invalid request body')
        return headers, await reader.readexactly(length)

    async def start(self):
        bind = env_value('MAXIMA_CONTROL_BIND')
        enabled = env_value('MAXIMA_TELEGRAM_LISTENER') == '1'
        if bind:
            host, port = bind.rsplit(':', 1)
            self.token = env_value('MAXIMA_CONTROL_TOKEN') or ''
            if len(self.token) < 32:
                raise ValueError('a private control token of at least 32 characters is required')
            context = None
            cert, key = env_value('MAXIMA_CONTROL_CERT'), env_value('MAXIMA_CONTROL_KEY')
            if bool(cert) != bool(key):
                raise ValueError('control TLS requires both certificate and private key')
            if cert and key:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(cert, key)
            elif host not in ('127.0.0.1', '::1', 'localhost'):
                raise ValueError('non-loopback control endpoints require TLS')
            self.server = await asyncio.start_server(self.serve, host, int(port), ssl=context, limit=8192)
        if enabled:
            self.listener = telegram_listener(self)
            self.task = asyncio.ensure_future(self.listener.run())
        self.reporter.on_line('-- runner control: ' + self.runner_id + ('; Telegram listener enabled' if enabled else '; Telegram listener disabled'))

    async def close(self):
        if self.shutdown_requested is not None and not self.shutdown_ack.is_set():
            try:
                await asyncio.wait_for(self.shutdown_ack.wait(), 30)
            except asyncio.TimeoutError:
                pass
        if self.task is not None:
            self.listener.stopping = True
            try:
                await asyncio.wait_for(asyncio.shield(self.task), 27)
            except asyncio.TimeoutError:
                self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
        connections = list(self.connections)
        for task in connections:
            task.cancel()
        await asyncio.gather(*connections, return_exceptions=True)


class telegram_listener:
    def __init__(self, control):
        self.control = control
        self.token = env_value('TELEGRAM_BOT_TOKEN')
        self.chat = env_value('TELEGRAM_CHAT_ID')
        self.operators = {int(value.strip()) for value in (env_value('MAXIMA_TELEGRAM_OPERATORS') or '').split(',') if value.strip()}
        if not self.token or not self.chat or not self.operators:
            raise ValueError('Telegram listener requires bot, chat and numeric operator IDs')
        path = env_value('MAXIMA_CONTROL_TARGETS')
        self.targets = json.loads(Path(path).read_text()) if path else {}
        for name, target in self.targets.items():
            if not re.fullmatch(r'[a-z][a-z0-9_-]{0,31}', name) or name in ('all', 'help', control.runner_id):
                raise ValueError('invalid or duplicate target ID')
            validate_target(target)
        if not control.local_runner and not self.targets:
            raise ValueError('the Telegram monitor requires configured runner targets')
        self.cursor = control.root / 'telegram_offset'
        self.offset = int(self.cursor.read_text()) if self.cursor.exists() else 0
        self.username = None
        self.monospace = False
        self.stopping = False

    async def api(self, method, payload):
        return await asyncio.get_running_loop().run_in_executor(
            None, lambda: post_json('https://api.telegram.org/bot' + self.token + '/' + method, payload, timeout=25))

    async def route(self, target, command, identity, arguments=''):
        try:
            if self.control.local_runner and target == self.control.runner_id:
                return await asyncio.wait_for(self.control.command(command, identity, arguments), 8)
            settings = self.targets[target]
            answer = await asyncio.get_running_loop().run_in_executor(None, lambda: post_json(
                settings['url'], {'command': command, 'id': identity, 'arguments': arguments}, settings['token'], settings.get('ca_file')))
            return answer if command == 'history' else str(answer['text'])
        except Exception:
            # Do not echo request URLs or headers: they contain private configuration.
            return target + (': offline or unavailable.' if command in read_commands else ': command unconfirmed; check status.')

    async def handle(self, update):
        self.monospace = False
        message = update.get('message') or {}
        sender = message.get('from') or {}
        if (str((message.get('chat') or {}).get('id')) != self.chat
                or sender.get('id') not in self.operators or sender.get('is_bot')
                or message.get('sender_chat') or message.get('forward_origin') or message.get('forward_from')
                or message.get('forward_sender_name') or message.get('via_bot')):
            return None
        words = message.get('text', '').strip().split()
        if not words:
            return None
        command, _, username = words.pop(0).partition('@')
        if command != '/maxima' or (username and username.lower() != (self.username or '').lower()):
            return None
        names = ([self.control.runner_id] if self.control.local_runner else []) + list(self.targets)
        if not words or words == ['help']:
            return help_text
        if len(words) == 1 and words[0] in read_commands:
            target, action = 'all', words[0]
            arguments = ''
        elif len(words) >= 2:
            target, action, *extra = words
            arguments = ' '.join(extra)
        else:
            return help_text
        if target not in names + ['all'] or action not in read_commands + play_commands:
            return help_text
        if arguments and action != 'seek-pool':
            return help_text
        if action in play_commands and abs(time.time() - message.get('date', 0)) > 120:
            return 'Command expired; send a fresh /maxima command.'
        chosen = names if target == 'all' else [target]
        # Telegram can randomize update IDs after a week without updates. Date
        # keeps desired-state commands ordered across that reset as well.
        identity = int(message.get('date', 0)) * (1 << 32) + update['update_id']
        answers = await asyncio.gather(*(self.route(name, action, identity, arguments) for name in chosen))
        if action == 'history':
            self.monospace = True
            rows = [row for answer in answers if isinstance(answer, dict) for row in answer['games']]
            errors = [answer for answer in answers if isinstance(answer, str)]
            return history_text(rows, self.control.reporter.zone) + ('\n' + '\n'.join(errors) if errors else '')
        return 'Listener: ' + self.control.runner_id + '\n' + '\n'.join(answers)

    async def run(self):
        while self.username is None:
            try:
                identity = await self.api('getMe', {})
                if not identity.get('ok'):
                    raise ValueError('Telegram bot identity unavailable')
                self.username = identity['result']['username']
            except Exception:
                self.control.reporter.on_line('-- Telegram listener could not start; retrying; inspect private configuration')
                await asyncio.sleep(10)
        self.control.reporter.event('operator_notice', message='Telegram commands enabled. Listener: ' + self.control.runner_id)
        while not self.stopping:
            try:
                answer = await self.api('getUpdates', {'offset': self.offset, 'timeout': 15, 'allowed_updates': ['message']})
                if not answer.get('ok'):
                    raise ValueError('Telegram polling rejected')
                if self.stopping:
                    return
                for update in answer['result']:
                    if update['update_id'] < self.offset:
                        continue
                    text = await self.handle(update)
                    if text:
                        formatted = html.escape(text[:3500])
                        if self.monospace:
                            formatted = '<pre>' + formatted + '</pre>'
                        result = await self.api('sendMessage', {'chat_id': self.chat, 'text': formatted, 'parse_mode': 'HTML'})
                        if not result.get('ok'):
                            raise ValueError('Telegram reply rejected')
                    self.offset = update['update_id'] + 1
                    atomic_text(self.cursor, str(self.offset) + '\n')
                    if self.control.shutdown_requested is not None:
                        self.control.shutdown_ack.set()
                        return
            except urllib.error.HTTPError as error:
                if error.code == 409:
                    self.control.reporter.event('operator_notice', message='Telegram listener conflict; commands disabled here. Configure exactly one listener.')
                    return
                self.control.reporter.on_line('-- Telegram listener unavailable; retrying')
                await asyncio.sleep(10)
            except Exception:
                self.control.reporter.on_line('-- Telegram command delivery pending; retrying')
                await asyncio.sleep(10)


def main():
    import argparse
    import signal
    from .online_reporting import online_reporter
    from .online_safety import engine_safety
    from .project import data_root
    parser = argparse.ArgumentParser(description='Run the Telegram listener independently of chess clients.')
    parser.parse_args()
    if env_value('MAXIMA_TELEGRAM_LISTENER') != '1':
        raise ValueError('enable MAXIMA_TELEGRAM_LISTENER=1 for the Telegram monitor role')

    async def run():
        reporter = online_reporter('telegram', data_root()/'runtime/telegram', daily=False)
        safety = engine_safety('telegram', reporter.root, reporter.event)
        safety.check_start()
        control = runner_control('telegram', None, reporter)
        stop = asyncio.Event()
        reporter.on_stop = stop.set
        def failed(error):
            safety.fail(error)
            stop.set()
        reporter.on_failure = failed
        loop = asyncio.get_running_loop()
        for number in (signal.SIGTERM, signal.SIGINT):
            try:
                loop.add_signal_handler(number, stop.set)
            except NotImplementedError:
                pass
        try:
            reporter.start()
            await control.start()
            # A listener error must not leave an apparently online but idle monitor.
            waiting = asyncio.ensure_future(stop.wait())
            try:
                done, _ = await asyncio.wait({waiting, control.task}, return_when=asyncio.FIRST_COMPLETED)
                if control.task in done:
                    control.task.result()
                    raise RuntimeError('Telegram listener stopped unexpectedly')
            finally:
                waiting.cancel()
                await asyncio.gather(waiting, return_exceptions=True)
        except Exception as error:
            safety.fail(error)
            raise
        finally:
            await control.close()
            await reporter.close()
    asyncio.run(run())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
