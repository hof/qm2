import asyncio
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import AsyncMock, patch

from maxima_runner import fics, lichess
from maxima_runner.online_control import runner_control, telegram_listener, validate_target
from maxima_runner.online_reporting import online_reporter
from test_lichess import challenge, stub_api


class control_tests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.settings = {}
        self.patch = patch('maxima_runner.online_control.env_value', side_effect=self.settings.get)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        with patch('maxima_runner.online_reporting.env_value', return_value=None):
            self.reporter = online_reporter('lichess', self.directory.name, lambda text: None)
        self.bot = lichess.lichess_bot(stub_api(), None, lambda text: None)
        self.control = runner_control('lichess', self.bot, self.reporter)

    def listener(self):
        self.settings.update(TELEGRAM_BOT_TOKEN='fixture', TELEGRAM_CHAT_ID='-123', MAXIMA_TELEGRAM_OPERATORS='11,22')
        return telegram_listener(self.control)

    def update(self, text, identity=1, **fields):
        message = {'chat': {'id': -123}, 'from': {'id': 11}, 'text': text, 'date': int(time.time())}
        message.update(fields)
        return {'update_id': identity, 'message': message}

    async def test_pause_preserves_current_game_and_blocks_incoming_outgoing_challenges(self):
        self.bot.game = object()
        await self.control.command('pause', 1)
        self.assertIsNotNone(self.bot.game)
        self.assertFalse(self.bot.stopping)
        self.bot.game = None
        self.assertEqual(self.bot.decline_reason(challenge(), {}), 'later')
        self.bot.api.call = AsyncMock()
        await self.bot.matchmaker.challenge(self.bot, 'Other', 'other', 'blitz', 180, 2, True)
        self.bot.api.call.assert_not_awaited()

    async def test_pause_survives_restart_and_late_game_start_is_aborted(self):
        await self.control.command('pause', 4)
        replacement = lichess.lichess_bot(stub_api(), None, lambda text: None)
        runner_control('lichess', replacement, self.reporter)
        self.assertTrue(replacement.paused)
        await replacement.on_event({'type': 'gameStart', 'game': {'gameId': 'late'}})
        self.assertIsNone(replacement.game)
        self.assertEqual(replacement.api.posts[0][0], '/api/bot/game/late/abort')

    async def test_duplicate_and_out_of_order_commands_cannot_undo_newer_state(self):
        await self.control.command('pause', 2)
        await self.control.command('resume', 3)
        await self.control.command('pause', 2)
        self.assertFalse(self.bot.paused)
        await self.control.command('pause', 4)
        await self.control.command('resume', 3)
        self.assertTrue(self.bot.paused)

    async def test_resume_cannot_bypass_a_fault_stop_or_resource_drain(self):
        for reason in ('fault', 'stopping', 'resource', 'deadline'):
            with self.subTest(reason=reason):
                await self.control.command('pause', 10)
                self.bot.safety.incident = {'id': 'fault'} if reason == 'fault' else None
                self.bot.stopping = reason == 'stopping'
                self.reporter.resource_reported = reason == 'resource'
                self.reporter.stop_requested = reason == 'deadline'
                self.assertIn('refused', await self.control.command('resume', 11))
                self.assertTrue(self.bot.paused)

    async def test_pause_cancels_pending_challenge(self):
        self.bot.matchmaker.pending = ('pending', 'Other', 'blitz', time.monotonic())
        await self.control.command('pause', 1)
        self.assertIsNone(self.bot.matchmaker.pending)
        self.assertIn(('/api/challenge/pending/cancel', None), self.bot.api.posts)

    async def test_resume_refuses_expired_deadline_even_before_next_reporter_tick(self):
        await self.control.command('pause', 1)
        self.reporter.stop_at = time.time() - 1
        self.assertIn('refused', await self.control.command('resume', 2))
        self.assertTrue(self.bot.paused)

    async def test_fics_pause_unseeks_declines_and_resume_restores_configured_seeks(self):
        player = fics.engine_player(None, lambda text: None, policy=fics.play_policy(seeks=['3 0']))
        player.send = AsyncMock()
        control = runner_control('fics', player, self.reporter)
        await control.command('pause', 1)
        await player.on_challenge({'name': 'Other'})
        await player.seek()
        self.assertEqual([call.args[0] for call in player.send.await_args_list], ['unseek', 'decline Other'])
        await control.command('resume', 2)
        self.assertTrue(any(call.args[0].startswith('seek ') for call in player.send.await_args_list))

    async def test_authentication_ignores_other_chats_users_bots_and_forwarded_messages(self):
        listener = self.listener()
        for fields in ({'chat': {'id': -456}}, {'from': {'id': 99}}, {'from': {'id': 11, 'is_bot': True}},
                       {'sender_chat': {'id': -123}}, {'forward_origin': {'type': 'user'}}, {'via_bot': {'id': 33}}):
            self.assertIsNone(await listener.handle(self.update('/maxima all pause', **fields)))
        self.assertFalse(self.bot.paused)

    async def test_mutations_need_explicit_target_and_fresh_message(self):
        listener = self.listener()
        self.assertIn('Maxima commands:', await listener.handle(self.update('/maxima pause')))
        self.assertFalse(self.bot.paused)
        self.assertIn('expired', await listener.handle(self.update('/maxima all pause', date=int(time.time())-121)))
        self.assertFalse(self.bot.paused)
        self.assertIn('paused', await listener.handle(self.update('/maxima all pause')))
        self.assertTrue(self.bot.paused)

    async def test_bare_command_and_optional_bot_suffix(self):
        listener = self.listener()
        listener.username = 'FixtureBot'
        self.assertIn('Listener: lichess', await listener.handle(self.update('/maxima status')))
        self.assertIn('Listener: lichess', await listener.handle(self.update('/maxima@FixtureBot help')))
        self.assertIsNone(await listener.handle(self.update('/maxima@OtherBot all pause')))
        self.assertIsNone(await listener.handle(self.update('ordinary group conversation')))

    async def test_remote_failure_is_visible_without_exposing_configuration(self):
        listener = self.listener()
        listener.targets = {'fics': {'url': 'https://private.invalid/command', 'token': 'private'}}
        with patch('maxima_runner.online_control.post_json', side_effect=OSError('private')):
            text = await listener.handle(self.update('/maxima status'))
        self.assertIn('fics: unreachable', text)
        self.assertNotIn('private', text)

    async def test_endpoint_authentication_and_real_cross_runner_routing(self):
        self.settings.update(MAXIMA_CONTROL_BIND='127.0.0.1:0', MAXIMA_CONTROL_TOKEN='x'*32)
        await self.control.start()
        self.addAsyncCleanup(self.control.close)
        port = self.control.server.sockets[0].getsockname()[1]
        listener = self.listener()
        listener.targets = {'remote': {'url': f'http://127.0.0.1:{port}/command', 'token': 'wrong'*8}}
        self.assertIn('unconfirmed', await listener.route('remote', 'pause', 1))
        self.assertFalse(self.bot.paused)
        listener.targets['remote']['token'] = 'x'*32
        self.assertIn('paused', await listener.route('remote', 'pause', 1))
        self.assertTrue(self.bot.paused)
        self.assertIn('paused', await listener.route('remote', 'status', 2))

    async def test_non_loopback_control_requires_tls(self):
        self.settings.update(MAXIMA_CONTROL_BIND='0.0.0.0:8443', MAXIMA_CONTROL_TOKEN='x'*32)
        with self.assertRaisesRegex(ValueError, 'TLS'):
            await self.control.start()

    def test_remote_urls_and_tokens_are_validated(self):
        for url in ('http://runner.invalid/command', 'https://user:pass@runner.invalid/command',
                    'https://runner.invalid/command?secret', 'https://runner.invalid/other'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                validate_target({'url': url, 'token': 'x'*32})
        validate_target({'url': 'https://runner.invalid/command', 'token': 'x'*32})
        with self.assertRaises(ValueError):
            validate_target({'url': 'https://runner.invalid/command', 'token': 'short'})

    async def test_disabled_by_default_never_polls_telegram(self):
        await self.control.start()
        self.assertIsNone(self.control.task)
        self.assertIsNone(self.control.server)

    async def test_polling_persists_offset_after_reply_and_ignores_processed_update(self):
        listener = self.listener()
        update = self.update('/maxima lichess pause', 10)
        async def api(method, payload):
            if method == 'getMe':
                return {'ok': True, 'result': {'username': 'FixtureBot'}}
            if method == 'getUpdates':
                if payload['offset'] == 11:
                    raise asyncio.CancelledError()
                return {'ok': True, 'result': [update]}
            return {'ok': True, 'result': {}}
        listener.api = AsyncMock(side_effect=api)
        with self.assertRaises(asyncio.CancelledError):
            await listener.run()
        self.assertEqual(listener.cursor.read_text().strip(), '11')
        self.assertTrue(self.bot.paused)
        self.assertEqual(sum(call.args[0] == 'sendMessage' for call in listener.api.await_args_list), 1)
