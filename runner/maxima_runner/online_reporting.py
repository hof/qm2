"""Private online event outbox; notification delivery never runs on the move path."""
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import socket
import time
import urllib.request
import uuid

from .learning_store import atomic_text, writer_lock
from .project import env_value


def service_notify(message):
    address = os.environ.get('NOTIFY_SOCKET')
    if address:
        if address.startswith('@'):
            address = '\0' + address[1:]
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as connection:
            connection.sendto(message.encode(), address)


class online_reporter:
    def __init__(self, platform, root, on_line=print):
        self.platform, self.root, self.on_line = platform, Path(root), on_line
        self.root.mkdir(parents=True, exist_ok=True)
        self.task = self.heartbeat = None
        self.on_failure = None
        self.stop_at = float(env_value('MAXIMA_STOP_AT') or 0)
        self.stop_requested = False
        enabled = env_value('MAXIMA_REPORTING_ENABLED') == '1'
        self.settings = {key: env_value(key) if enabled else None for key in (
            'TELEGRAM_BOT_TOKEN', 'TELEGRAM_CHAT_ID', 'RESEND_API_KEY',
            'MAXIMA_ALERT_FROM', 'MAXIMA_ALERT_TO')}
        self.zone = timezone.utc
        name = env_value('MAXIMA_REPORT_TIMEZONE') or 'UTC'
        if name != 'UTC':
            from zoneinfo import ZoneInfo
            self.zone = ZoneInfo(name)
        cursor = self.root / 'summary_day'
        self.day = cursor.read_text().strip() if cursor.exists() else datetime.now(self.zone).date().isoformat()
        if not cursor.exists():
            atomic_text(cursor, self.day+'\n')

    def event(self, kind, **fields):
        values = dict(fields)
        values.update(platform=self.platform, kind=kind, at=time.time())
        # Never serialize environment, authorization headers or raw request URLs.
        text = json.dumps(values, default=str)
        for key in ('TELEGRAM_BOT_TOKEN', 'RESEND_API_KEY'):
            if self.settings[key]:
                text = text.replace(self.settings[key], '[redacted]')
        values = json.loads(text)
        identity = values.get('id') or uuid.uuid4().hex
        if kind == 'game_end':
            headers = fields.get('headers') or {}
            identity = headers.get('LearningId')
            if not identity:
                return
            path = self.root / 'games' / (identity + '.json')
            path.parent.mkdir(exist_ok=True)
            if not path.exists():
                atomic_text(path, json.dumps(values) + '\n')
            return
        if kind not in ('login','shutdown','engine_incident','engine_recovered','recovery_failed','quarantined','daily_summary','notification_test'):
            return
        channels = []
        if self.settings['TELEGRAM_BOT_TOKEN'] and self.settings['TELEGRAM_CHAT_ID']:
            channels.append('telegram')
        if kind in ('engine_incident','quarantined','notification_test') and all(self.settings[k] for k in ('RESEND_API_KEY','MAXIMA_ALERT_FROM','MAXIMA_ALERT_TO')):
            channels.append('email')
        for channel in channels:
            path = self.root / 'outbox' / (str(identity) + '-' + kind + '-' + channel + '.json')
            path.parent.mkdir(exist_ok=True)
            if not path.exists():
                atomic_text(path, json.dumps(dict(event=values,channel=channel,sent=False,attempts=0,next_attempt=0)) + '\n')
        self.on_line('-- '+self.platform+' '+kind+': '+str(values.get('message',values.get('reason',''))))

    def listen(self, event):
        if event['type'] == 'login':
            self.event('login', message=event['message'])
        elif event['type'] == 'game_end':
            self.event('game_end', **{k:v for k,v in event.items() if k not in ('type','v','pgn')})

    def summary(self, day):
        wins = draws = losses = unresolved = 0
        for path in (self.root/'games').glob('*.json'):
            row = json.loads(path.read_text())
            if datetime.fromtimestamp(row['at'],self.zone).date().isoformat() != day:
                continue
            headers = row.get('headers') or {}
            result = row.get('result')
            color = headers.get('LearningColor')
            if result == '*' or color not in ('white','black'):
                unresolved += 1
            elif result == '1/2-1/2':
                draws += 1
            elif result == ('1-0' if color == 'white' else '0-1'):
                wins += 1
            else:
                losses += 1
        return f'{day} W-D-L: {wins}-{draws}-{losses}; aborted/unresolved: {unresolved}'

    def deliver(self, path):
        with writer_lock(self.root / '.delivery.lock'):
            self._deliver(path)

    def _deliver(self, path):
        row = json.loads(path.read_text())
        if row['sent'] or row['next_attempt'] > time.time():
            return
        event = row['event']
        message = f"[{self.platform}] {event['kind']}\n" + json.dumps({k:v for k,v in event.items() if k not in ('platform','kind')},ensure_ascii=False,indent=2)
        headers = {'Content-Type':'application/json','User-Agent':'MaximaMonitor/1.0'}
        if row['channel'] == 'telegram':
            url = 'https://api.telegram.org/bot'+self.settings['TELEGRAM_BOT_TOKEN']+'/sendMessage'
            payload = {'chat_id':self.settings['TELEGRAM_CHAT_ID'],'text':message[:4000]}
        else:
            url = 'https://api.resend.com/emails'
            headers.update(Authorization='Bearer '+self.settings['RESEND_API_KEY'],
                           **{'Idempotency-Key':hashlib.sha256(path.name.encode()).hexdigest()})
            payload = {'from':self.settings['MAXIMA_ALERT_FROM'],
                       'to':[v.strip() for v in self.settings['MAXIMA_ALERT_TO'].split(',') if v.strip()],
                       'subject':f'Maxima {self.platform}: {event["kind"]}', 'text':message}
        try:
            request = urllib.request.Request(url,json.dumps(payload).encode(),headers,method='POST')
            with urllib.request.urlopen(request,timeout=3) as response:
                answer=json.load(response)
            if row['channel']=='telegram' and not answer.get('ok'):
                raise RuntimeError('notification rejected')
            row['sent']=True
            row['accepted_id']=answer.get('id') or (answer.get('result') or {}).get('message_id')
        except Exception as error:
            self.on_line('-- notification pending: '+type(error).__name__)
            row['last_error'] = type(error).__name__ + ' ' + str(getattr(error, 'code', ''))
            row['attempts']+=1
            row['next_attempt']=time.time()+min(1800,30*2**min(row['attempts']-1,6))
        atomic_text(path,json.dumps(row)+'\n')

    async def run(self):
        while True:
            service_notify('WATCHDOG=1')
            if self.stop_at and time.time() >= self.stop_at and not self.stop_requested:
                self.stop_requested = True
                self.event('shutdown', message='Configured stop time reached; finishing current game')
                if self.on_failure is not None:
                    self.on_failure()
            day = datetime.now(self.zone).date().isoformat()
            if day != self.day:
                self.event('daily_summary',id=self.platform+'-'+self.day,message=self.summary(self.day))
                self.day=day
                atomic_text(self.root/'summary_day', day+'\n')
            for path in (self.root/'outbox').glob('*.json'):
                service_notify('WATCHDOG=1')
                await asyncio.get_running_loop().run_in_executor(None,self.deliver,path)
            await asyncio.sleep(5)

    async def health(self):
        while True:
            service_notify('WATCHDOG=1')
            await asyncio.sleep(5)

    def start(self):
        self.task=asyncio.ensure_future(self.run())
        self.heartbeat=asyncio.ensure_future(self.health())
        def failed(task):
            if not task.cancelled() and task.exception() is not None:
                self.on_line('-- reporting failed: '+type(task.exception()).__name__)
                if self.on_failure is not None:
                    self.on_failure()
        self.task.add_done_callback(failed)
        service_notify('READY=1')

    async def close(self):
        tasks=[task for task in (self.task,self.heartbeat) if task is not None]
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks,return_exceptions=True)
        # Pending messages remain durable if delivery is unavailable at shutdown.
        for path in (self.root/'outbox').glob('*.json'):
            await asyncio.get_running_loop().run_in_executor(None,self.deliver,path)


def main():
    import argparse
    from .project import data_root
    parser=argparse.ArgumentParser(description='Deliver pending private online reports without connecting a chess client.')
    parser.add_argument('--platform',choices=('lichess','fics'),required=True)
    parser.add_argument('--test',action='store_true',help='Send an explicitly labelled notification test')
    parser.add_argument('--service-failure',action='store_true',help='Record an unexpected service exit and notify operators')
    args=parser.parse_args()
    reporter=online_reporter(args.platform,data_root()/'runtime'/args.platform)
    if args.service_failure:
        from .online_safety import engine_safety
        engine_safety(args.platform, reporter.root, reporter.event).fail(RuntimeError('Runner service failed; inspect the service journal'))
    if args.test:
        reporter.event('notification_test',message='Notification delivery test; no engine failure has occurred.')
    today=datetime.now(reporter.zone).date().isoformat()
    if reporter.day != today:
        reporter.event('daily_summary',id=reporter.platform+'-'+reporter.day,message=reporter.summary(reporter.day))
        atomic_text(reporter.root/'summary_day',today+'\n')
    for path in (reporter.root/'outbox').glob('*.json'):
        reporter.deliver(path)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
