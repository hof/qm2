import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from maxima_runner.online_reporting import online_reporter


class reporting_tests(unittest.TestCase):
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
            self.assertIn('1-1-1; aborted/unresolved: 1',reporter.summary(reporter.day))
