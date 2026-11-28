"""Private online event outbox; notification delivery never runs on the move path."""
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import shutil
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


def event_text(event, detailed=False):
    label = {'lichess':'Lichess', 'fics':'FICS', 'telegram':'Monitor'}.get(event['platform'], event['platform'])
    kind = event['kind']
    if kind == 'login':
        text = '🟢 ' + event.get('message', 'Logged in to ' + label)
        details = event.get('engine_details') or {}
        options = details.get('options') or {}
        facts = []
        if options.get('Threads') is not None:
            facts.append(str(options['Threads']) + ' threads')
        if options.get('Hash') is not None:
            facts.append(str(options['Hash']) + ' MiB hash')
        if facts:
            text += '\n' + ' · '.join(facts)
    elif kind == 'training_notice':
        text = ('⏸ ' if event.get('alert') else '🧠 ') + event.get('message', 'Self-training update')
    elif kind == 'notification_test':
        text = '✅ Maxima Monitor notification test. No engine failure has occurred.'
    elif kind == 'engine_incident':
        text = '🔴 ' + label + ' runner incident: ' + event.get('reason', 'unexpected engine failure')
        text += '\nNew games blocked. Recovery is limited to the current game.'
        if event.get('game') is not None:
            text += '\nGame: ' + str(event['game'])
    elif kind == 'engine_recovered':
        text = '🟠 ' + label + ' engine recovered. Finishing the current game, then staying offline for review.'
    elif kind == 'recovery_failed':
        text = '🔴 ' + label + ' recovery failed. Ending play and staying offline for review.'
    elif kind == 'quarantined':
        text = '⛔ ' + label + ' is offline and awaiting review.'
    elif kind == 'daily_summary':
        message = event.get('message', '')
        previous = re.fullmatch(r'(\d{2}/\d{2}/\d{4}) · W-D-L: (\d+)-(\d+)-(\d+) \(tracked\); aborted/unresolved: \d+', message)
        if previous:
            day, wins, draws, losses = previous.groups()
            day = datetime.strptime(day, '%d/%m/%Y').strftime('%d/%m/%y')
            message = f'{day} +{wins} ={draws} -{losses}'
        text = label + ' ' + message
    elif kind == 'titled_opponent':
        text = '♟ ' + label + ' · Playing ' + event['title'] + ' ' + event['name']
        if event.get('rating') is not None:
            text += ' (' + str(event['rating']) + ')'
        if event.get('url'):
            text += '\n' + event['url']
    else:
        text = '⚪ ' + label + ' · ' + event.get('message', 'Session ended')
    if detailed and kind != 'training_notice':
        for key, title in [('id','Incident'),('error','Error type'),('fen','Position')]:
            if event.get(key):
                text += '\n' + title + ': ' + str(event[key])
        engine = (event.get('engine_details') or {}).get('engine') or {}
        if engine.get('name'):
            text += '\nEngine: ' + engine['name']
    return text


class online_reporter:
    def __init__(self, platform, root, on_line=print, daily=True):
        self.platform, self.root, self.on_line = platform, Path(root), on_line
        self.daily = daily
        self.root.mkdir(parents=True, exist_ok=True)
        self.task = self.heartbeat = None
        self.on_failure = self.on_stop = None
        self.minimum_free = int(env_value("MAXIMA_MIN_FREE_BYTES") or 0)
        self.resource_reported = False
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
        if kind not in ('login','shutdown','engine_incident','engine_recovered','recovery_failed','quarantined','daily_summary','notification_test','operator_notice','titled_opponent','training_notice'):
            return
        channels = []
        if self.settings['TELEGRAM_BOT_TOKEN'] and self.settings['TELEGRAM_CHAT_ID']:
            channels.append('telegram')
        if (kind in ('engine_incident','quarantined','notification_test') or kind == 'training_notice' and fields.get('alert')) and all(self.settings[k] for k in ('RESEND_API_KEY','MAXIMA_ALERT_FROM','MAXIMA_ALERT_TO')):
            channels.append('email')
        for channel in channels:
            path = self.root / 'outbox' / (str(identity) + '-' + kind + '-' + channel + '.json')
            path.parent.mkdir(exist_ok=True)
            if not path.exists():
                atomic_text(path, json.dumps(dict(event=values,channel=channel,sent=False,attempts=0,next_attempt=0)) + '\n')
        self.on_line('-- '+self.platform+' '+kind+': '+str(values.get('message',values.get('reason',''))))

    def listen(self, event):
        if event['type'] == 'login':
            self.event('login', message=event['message'], engine_details=event.get('engine_details'))
        elif event['type'] == 'game_end':
            self.event('game_end', **{k:v for k,v in event.items() if k not in ('type','v','pgn')})
        elif event['type'] in ('game_start', 'opponent'):
            opponent = event.get('opponent') or {}
            if (opponent.get('title') not in ('FM', 'IM', 'GM') or not opponent.get('name')
                    or opponent.get('is_engine') or event.get('game') is None):
                return
            key = json.dumps([self.platform, event['game'], event.get('game_started_at')])
            self.event('titled_opponent', id=hashlib.sha256(key.encode()).hexdigest(),
                       name=opponent['name'], title=opponent['title'], rating=opponent.get('rating'),
                       game=event['game'], url=event.get('url'))

    def summary(self, day):
        totals = {}
        for path in (self.root/'games').glob('*.json'):
            row = json.loads(path.read_text())
            if datetime.fromtimestamp(row['at'],self.zone).date().isoformat() != day:
                continue
            headers = row.get('headers') or {}
            result = row.get('result')
            color = headers.get('LearningColor')
            if result not in ('1-0','0-1','1/2-1/2') or color not in ('white','black'):
                continue
            variant = headers.get('Variant', 'Standard')
            label = ('W17' if variant.casefold() in ('royal losers','fics losers','losers','wild/17') else
                     'Chess' if variant.casefold() in ('standard','normal','chess') else variant)
            counts = totals.setdefault(label, [0, 0, 0])
            outcome = 1 if result == '1/2-1/2' else 0 if result == ('1-0' if color == 'white' else '0-1') else 2
            counts[outcome] += 1
        date = datetime.fromisoformat(day).strftime('%d/%m/%y')
        if self.platform == 'fics' and any(label != 'Chess' for label in totals):
            return date + ''.join(f'\n{label}: +{counts[0]} ={counts[1]} -{counts[2]}'
                                  for label, counts in sorted(totals.items(), key=lambda item: (item[0] != 'Chess', item[0])))
        wins, draws, losses = (sum(counts[index] for counts in totals.values()) for index in range(3))
        return f'{date} +{wins} ={draws} -{losses}'

    def recent_games(self, limit=10):
        games = []
        for path in (self.root/'games').glob('*.json'):
            event = json.loads(path.read_text())
            headers = event.get('headers') or {}
            color = headers.get('LearningColor')
            tag = 'Black' if color == 'white' else 'White'
            result = event.get('result', '*')
            outcome = ('D' if result == '1/2-1/2' else
                       'W' if color in ('white','black') and result == ('1-0' if color == 'white' else '0-1') else
                       'L' if color in ('white','black') and result in ('1-0','0-1') else '*')
            games.append(dict(at=event['at'], opponent=headers.get(tag, '?'),
                              title=headers.get(tag+'Title', ''), rating=headers.get(tag+'Elo'), result=outcome,
                              clock=headers.get('TimeControl', '?'), platform=self.platform))
        return sorted(games, key=lambda row: row['at'], reverse=True)[:limit]

    def deliver(self, path):
        with writer_lock(self.root / '.delivery.lock'):
            self._deliver(path)

    def _deliver(self, path):
        row = json.loads(path.read_text())
        if row['sent'] or row['next_attempt'] > time.time():
            return
        event = row['event']
        message = event_text(event, detailed=row['channel'] == 'email')
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
                       'subject': 'Maxima Monitor — ' + ('notification test' if event['kind']=='notification_test' else 'training update' if event['kind']=='training_notice' else self.platform+' incident'), 'text':message}
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
                if self.on_stop is not None:
                    self.on_stop()
            if self.minimum_free and shutil.disk_usage(self.root).free < self.minimum_free and not self.resource_reported:
                self.resource_reported = True
                if self.on_failure is not None:
                    self.on_failure(RuntimeError('free disk below configured safety threshold'))
            day = datetime.now(self.zone).date().isoformat()
            if self.daily and day != self.day:
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
                if self.on_failure is not None:
                    self.on_failure(task.exception())
                print('-- reporting failed: '+type(task.exception()).__name__, flush=True)
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
    parser.add_argument('--platform',choices=('lichess','fics','telegram'),required=True)
    parser.add_argument('--test',action='store_true',help='Send an explicitly labelled notification test')
    parser.add_argument('--service-failure',action='store_true',help='Record an unexpected service exit and notify operators')
    args=parser.parse_args()
    reporter=online_reporter(args.platform,data_root()/'runtime'/args.platform,daily=args.platform!='telegram')
    if args.service_failure:
        from .online_safety import engine_safety
        engine_safety(args.platform, reporter.root, reporter.event).fail(RuntimeError('Runner service failed: '+os.environ.get('MONITOR_SERVICE_RESULT','unknown')+'; inspect the service journal'))
    if args.test:
        reporter.event('notification_test',message='Notification delivery test; no engine failure has occurred.')
    today=datetime.now(reporter.zone).date().isoformat()
    if reporter.daily and reporter.day != today:
        reporter.event('daily_summary',id=reporter.platform+'-'+reporter.day,message=reporter.summary(reporter.day))
        atomic_text(reporter.root/'summary_day',today+'\n')
    for path in (reporter.root/'outbox').glob('*.json'):
        reporter.deliver(path)
    return 0


if __name__=='__main__':
    raise SystemExit(main())
