import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from maxima_runner.online_reporting import online_reporter, event_text


class reporting_tests(unittest.TestCase):
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

    def test_training_budget_pause_queues_both_channels_without_engine_failure_text(self):
        with tempfile.TemporaryDirectory() as directory:
            reporter=self.reporter(directory)
            reporter.event('training_notice',id='budget',message='Self-training paused: waiting for GPU funding.',alert=True)
            files=list((Path(directory)/'outbox').glob('*.json'))
            self.assertEqual(len(files),2)
            event=json.loads(files[0].read_text())['event']
            message=event_text(event,detailed=True)
            self.assertIn('waiting for GPU funding',message)
            self.assertNotIn('New games blocked',message)
            self.assertNotIn('Incident:',message)
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
            self.assertIn('1-1-1 (tracked); aborted/unresolved: 1',reporter.summary(reporter.day))

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


class message_tests(unittest.TestCase):
    def test_titled_game_message_is_concise_and_includes_the_link(self):
        message = event_text(dict(platform='lichess', kind='titled_opponent', title='GM',
                                  name='Example', rating=2500, url='https://lichess.org/fixture'))
        self.assertEqual(message, '♟ Lichess · Playing GM Example (2500)\nhttps://lichess.org/fixture')

    def test_login_shows_only_threads_and_hash(self):
        message = event_text(dict(platform='lichess', kind='login', engine_details={
            'engine': {'name': 'Maxima revision'},
            'options': {'Threads': 4, 'Hash': 128, 'EvalFile': '/private/models/maxima-halfka.nnue'}}))
        self.assertNotIn('Maxima revision', message)
        self.assertNotIn('maxima-halfka.nnue', message)
        self.assertIn('4 threads · 128 MiB hash', message)
        self.assertNotIn('/private', message)

    def test_operator_messages_are_readable_without_debug_json(self):
        message=event_text({'platform':'lichess','kind':'login','message':'Logged in to Lichess as Example','at':12345})
        self.assertIn('Logged in to Lichess as Example',message)
        self.assertNotIn('12345',message)
        self.assertNotIn('{',message)
        self.assertNotIn('"message"',message)
