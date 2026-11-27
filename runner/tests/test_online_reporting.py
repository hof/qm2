import json
import io
from collections import Counter
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from maxima_runner.online_reporting import online_reporter


class reporting_tests(unittest.TestCase):
    def test_delivered_report_does_not_expose_credentials_or_local_model_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter = self.reporter(directory)
            reporter.event('login', message='fixture-secret email-secret', engine_details={
                'options': {'Threads': 2, 'Hash': 64, 'EvalFile': '/private/models/network.nnue'}})
            path = next((Path(directory) / 'outbox').glob('*.json'))
            with patch('urllib.request.urlopen', return_value=io.BytesIO(b'{"ok":true}')) as deliver:
                reporter.deliver(path)
            payload = deliver.call_args.args[0].data.decode()
            for private in ('fixture-secret', 'email-secret', '/private/models/network.nnue'):
                self.assertNotIn(private, payload)
            self.assertTrue(json.loads(path.read_text())['sent'])

    def test_recent_game_results_are_from_our_perspective(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter = self.reporter(directory)
            for identity, color, result in [('w','white','1-0'), ('b','black','1-0'), ('d','black','1/2-1/2')]:
                reporter.listen(dict(type='game_end', result=result, headers={
                    'LearningId':identity, 'LearningColor':color, 'White':'White', 'Black':'Black',
                    'WhiteElo':'2500', 'BlackElo':'2600', 'TimeControl':'180+2'}))
            rows = reporter.recent_games()
            self.assertEqual([row['result'] for row in rows], ['D', 'L', 'W'])
            self.assertEqual([row['opponent'] for row in rows], ['White', 'White', 'Black'])
            self.assertEqual([row['rating'] for row in rows], ['2500', '2500', '2600'])

    def reporter(self,root):
        settings={'MAXIMA_REPORTING_ENABLED':'1','TELEGRAM_BOT_TOKEN':'fixture-secret','TELEGRAM_CHAT_ID':'123','RESEND_API_KEY':'email-secret','MAXIMA_ALERT_FROM':'sender@example.invalid','MAXIMA_ALERT_TO':'owner@example.invalid'}
        with patch('maxima_runner.online_reporting.env_value',side_effect=lambda k:settings.get(k)):
            return online_reporter('fixture',root,lambda line:None)

    def test_incident_queues_both_channels_and_redacts_secrets(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter=self.reporter(directory)
            reporter.event('engine_incident',id='incident',platform='fixture',reason='fixture-secret email-secret')
            files=list((Path(directory)/'outbox').glob('*.json'))
            self.assertEqual(len(files),2)
            for p in files:
                self.assertNotIn('fixture-secret',p.read_text())
                self.assertNotIn('email-secret',p.read_text())
            reporter.event('engine_incident',id='incident',reason='repeat')
            self.assertEqual(len(list((Path(directory)/'outbox').glob('*.json'))),2)

    def test_training_alert_queues_both_channels_and_ordinary_notice_only_telegram(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter=self.reporter(directory)
            reporter.event('training_notice',id='budget',message='Self-training paused: waiting for GPU funding.',alert=True)
            files=list((Path(directory)/'outbox').glob('*.json'))
            self.assertEqual(len(files),2)
            reporter.event('training_notice',id='resumed',message='Idle self-play resumed.',alert=False)
            self.assertEqual(len(list((Path(directory)/'outbox').glob('*.json'))),3)

    def test_delivery_failure_keeps_pending_and_does_not_include_token(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter=self.reporter(directory);reporter.event('login',message='connected')
            path=next((Path(directory)/'outbox').glob('*.json'))
            with patch('urllib.request.urlopen',side_effect=OSError('offline')):reporter.deliver(path)
            row=json.loads(path.read_text())
            self.assertFalse(row['sent']);self.assertEqual(row['attempts'],1)

    def test_completed_games_are_counted_once_and_unresolved_separately(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter=self.reporter(directory)
            for identity,result in [('win','1-0'),('draw','1/2-1/2'),('loss','0-1'),('unknown','*'),('win','1-0')]:
                reporter.listen({'type':'game_end','result':result,'headers':{'LearningId':identity,'LearningColor':'white'}})
            self.assertEqual(Counter(row['result'] for row in reporter.recent_games()),
                             Counter({'W': 1, 'D': 1, 'L': 1, '*': 1}))
            self.assertEqual(len(list((Path(directory)/'games').glob('*.json'))), 4)

    def test_titled_opponents_notify_once_per_game_and_only_on_telegram(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter = self.reporter(directory)
            for number, title in enumerate(('FM', 'IM', 'GM')):
                event = dict(type='game_start', game=str(number),
                             opponent=dict(name='Example', title=title, rating=2400, is_engine=False))
                reporter.listen(event)
                reporter.listen(event)
                # A restarted reporter must not repeat an already queued notice.
                self.reporter(directory).listen(event)
            files = list((Path(directory)/'outbox').glob('*.json'))
            self.assertEqual(len(files), 3)
            self.assertTrue(all(json.loads(p.read_text())['channel'] == 'telegram' for p in files))

    def test_untitled_and_computer_opponents_do_not_notify(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter = self.reporter(directory)
            for title, computer in ((None, False), ('BOT', True), ('GM', True)):
                reporter.listen(dict(type='game_start', game='fixture',
                                     opponent=dict(name='Example', title=title, is_engine=computer)))
            self.assertFalse((Path(directory)/'outbox').exists())

    def test_late_fics_title_and_reused_game_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter = self.reporter(directory)
            for started in ('first', 'second'):
                reporter.listen(dict(type='game_start', game=17, game_started_at=started))
                for _ in range(2):
                    reporter.listen(dict(type='opponent', game=17, game_started_at=started,
                                         opponent=dict(name='Example', title='IM', is_engine=False)))
            self.assertEqual(len(list((Path(directory)/'outbox').glob('*.json'))), 2)
