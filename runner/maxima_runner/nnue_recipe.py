"""Portable, resumable self-play recipes with accepted-position source quotas."""
import argparse
from concurrent.futures import FIRST_EXCEPTION, ThreadPoolExecutor, wait
from contextlib import closing
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import signal
import subprocess
import sys
import tarfile
import threading
import time
import uuid

import chess

from . import nnue_data, nnue_seeds, nnue_shards, nnue_selfplay
from .learning_store import atomic_text, writer_lock
from .nnue_binary import binary_dataset


max_rpc_bytes = 32 * 1024 * 1024
name_pattern = re.compile(r'[a-zA-Z0-9_-]{1,96}')


def save(path, value):
    atomic_text(path, json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n')


def digest_json(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def positive(value, name, maximum=2**63-1):
    if type(value) is not int or not 0 < value <= maximum:
        raise ValueError('invalid ' + name)
    return value


def command(value):
    if not isinstance(value, list) or not value or any(not isinstance(v, str) or not v or '\x00' in v for v in value):
        raise ValueError('expected a nonempty command argument array')
    return value


def path_at(base, value):
    return (base / Path(value).expanduser()).resolve()


class recipe:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.config = json.loads(self.path.read_text())
        cfg, base = self.config, self.path.parent
        allowed = {'version', 'output', 'store', 'block', 'campaign', 'engine', 'network', 'nodes',
                   'workers', 'job_positions', 'job_games', 'job_seconds', 'stop_at', 'exclude', 'sources',
                   'admission_command', 'admission_refresh', 'adopt_existing', 'existing_sources', 'notify_command',
                   'stop_file', 'result_path', 'heartbeat_path', 'max_empty_jobs', 'require_coordinator',
                   'previous_target', 'engines'}
        if not isinstance(cfg, dict) or cfg.get('version') != 1 or set(cfg) - allowed:
            raise ValueError('unsupported recipe configuration')
        self.root = path_at(base, cfg['output'])
        self.store = path_at(base, cfg.get('store', str(self.root / 'store')))
        self.block = cfg['block']
        if not isinstance(self.block, str) or not name_pattern.fullmatch(self.block):
            raise ValueError('invalid block name')
        if not isinstance(cfg.get('campaign'), str) or not cfg['campaign']:
            raise ValueError('recipe requires a unique campaign namespace')
        positive(cfg.get('nodes', 5000), 'node budget', 2**64-1)
        positive(cfg.get('workers', 2), 'worker count', 256)
        positive(cfg.get('job_positions', 10000), 'job positions', 100000)
        positive(cfg.get('job_games', 1000), 'job games', 10000)
        positive(cfg.get('job_seconds', 300), 'job duration', 86400)
        positive(cfg.get('admission_refresh',60),'admission refresh',86400)
        if cfg.get('stop_at') is not None and (type(cfg['stop_at']) not in (int, float) or not 0 < cfg['stop_at'] < float('inf')):
            raise ValueError('invalid recipe deadline')
        if type(cfg.get('require_coordinator',True)) is not bool:raise ValueError('require_coordinator must be boolean')
        for key in ('admission_command', 'notify_command'):
            if key in cfg:
                command(cfg[key])
        self.sources = cfg['sources']
        if not isinstance(self.sources, list) or not self.sources:
            raise ValueError('recipe requires sources')
        identifiers = set()
        for source in self.sources:
            if not isinstance(source, dict) or set(source) - {'id','positions','book','diversity','input','selection','options','limit','per_game','seed'}:
                raise ValueError('unsupported recipe source')
            identity = source['id']
            if not isinstance(identity, str) or not name_pattern.fullmatch(identity) or identity in identifiers:
                raise ValueError('source names must be unique path-safe identifiers')
            identifiers.add(identity)
            positive(source['positions'], 'source quota')
            if source.get('book') and source.get('input'):
                raise ValueError('a source selects an opening book or supplied starting positions, not both')
            if type(source.get('diversity', False)) is not bool:
                raise ValueError('source diversity must be boolean')
        if cfg.get('adopt_existing') is not None and cfg['adopt_existing'] not in identifiers:
            raise ValueError('existing positions need a declared adoption source')
        credits = cfg.get('existing_sources', {})
        if not isinstance(credits, dict) or any(not re.fullmatch('[0-9a-f]{64}', k) or v not in identifiers for k,v in credits.items()):
            raise ValueError('invalid existing input/source attribution')
        self.target = sum(s['positions'] for s in self.sources)
        positive(self.target, 'total quota')
        if 'previous_target' in cfg:
            positive(cfg['previous_target'], 'previous block target')
            if cfg['previous_target'] >= self.target:
                raise ValueError('previous target must be smaller than the recipe total')
        self.identity = digest_json({k:v for k,v in cfg.items() if k not in ('workers','admission_command','admission_refresh','notify_command')})
        self.stop_path = path_at(base,cfg.get('stop_file',str(self.root/'STOP')))
        positive(cfg.get('max_empty_jobs',200),'empty-job limit')
        self.state_path = self.root / 'state.json'
        self.lock = self.root / '.recipe.lock'

    def add_asset(self, path):
        path = Path(path)
        digest = nnue_data.file_hash(path)
        destination = self.root / 'assets' / digest
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name('.' + uuid.uuid4().hex)
            shutil.copyfile(path, temporary)
            if nnue_data.file_hash(temporary) != digest:
                temporary.unlink()
                raise ValueError('recipe input changed while freezing an asset')
            temporary.replace(destination)
        elif nnue_data.file_hash(destination) != digest:
            raise ValueError('immutable recipe asset changed')
        self.state['assets'][digest] = dict(bytes=destination.stat().st_size)
        return digest

    def initialize(self):
        self.root.mkdir(parents=True, exist_ok=True)
        if self.state_path.exists():
            self.state = json.loads(self.state_path.read_text())
            if self.state.get('recipe_sha256') != self.identity:
                raise ValueError('recipe changed; use a new output or explicitly migrate the saved state')
            return
        self.state = dict(version=1, recipe_sha256=self.identity, assets={}, sources={}, counts={},
                          jobs={}, next_job=0, cursor=0, admission=None, hold=None, notice=None,empty_jobs=0,
                          generator_sha256=nnue_data.file_hash(nnue_selfplay.__file__))
        for name in ('engine', 'network'):
            self.state[name] = self.add_asset(path_at(self.path.parent, self.config[name]))
        self.state['engines'] = [self.state['engine']]
        for path in self.config.get('engines', []):
            digest = self.add_asset(path_at(self.path.parent, path))
            if digest not in self.state['engines']:self.state['engines'].append(digest)
        excluded = [path_at(self.path.parent, p) for p in self.config.get('exclude', [])]
        self.state['exclude'] = [self.add_asset(p) for p in excluded]
        for source in self.sources:
            entry = dict(quota=source['positions'], diversity=source.get('diversity', False))
            if source.get('book'):
                entry['book'] = self.add_asset(path_at(self.path.parent, source['book']))
            if source.get('input'):
                prepared = nnue_seeds.prepare(path_at(self.path.parent, source['input']),
                    selection=source.get('selection','positions'), limit=source.get('limit',10000),
                    per_game=source.get('per_game',4), seed=source.get('seed',1), exclude=excluded,
                    **source.get('options',{}))
                frozen = self.root / ('starts-' + source['id'] + '.json')
                save(frozen, prepared)
                nnue_seeds.load(frozen)
                entry['starts'] = self.add_asset(frozen)
            self.state['sources'][source['id']] = entry
            self.state['counts'][source['id']] = 0
        existing = (self.store / 'index.sqlite3').exists()
        if existing:
            if 'previous_target' in self.config:
                nnue_shards.extend_block(self.store, self.block,
                    previous_target=self.config['previous_target'], target=self.target)
            with closing(sqlite3.connect('file:'+str(self.store/'index.sqlite3')+'?mode=ro',uri=True,timeout=60)) as db:
                row = db.execute('SELECT positions,sealed,target FROM blocks WHERE name=?',(self.block,)).fetchone()
                if row and (row[1] or row[2] != self.target):
                    raise ValueError('existing block is sealed or has another target')
                if row and row[0] and not self.config.get('adopt_existing'):
                    raise ValueError('nonempty block requires explicit adopt_existing source')
        nnue_shards.ingest(self.store,self.block,[],target=self.target,exclude=() if existing else excluded)
        with closing(sqlite3.connect('file:'+str(self.store/'index.sqlite3')+'?mode=ro',uri=True,timeout=60)) as db:
            for rowid, name, count in db.execute('SELECT rowid,path,positions FROM shards WHERE block=? ORDER BY rowid',(self.block,)):
                with binary_dataset(self.store / name) as data:
                    source = self.config.get('existing_sources',{}).get(data.header.get('input_sha256'),self.config.get('adopt_existing'))
                if source not in self.state['counts']:
                    raise ValueError('existing shard has no explicit source attribution')
                self.state['counts'][source] += count
                self.state['cursor'] = rowid
        if any(self.state['counts'][key] > entry['quota'] for key,entry in self.state['sources'].items()):
            raise ValueError('existing positions exceed their source quota')
        save(self.state_path,self.state)

    def reconcile(self):
        with closing(sqlite3.connect('file:'+str(self.store/'index.sqlite3')+'?mode=ro',uri=True,timeout=60)) as db:
            for rowid,name,count in db.execute('SELECT rowid,path,positions FROM shards WHERE block=? AND rowid>? ORDER BY rowid',(self.block,self.state['cursor'])):
                with binary_dataset(self.store / name) as data:
                    source = data.header.get('generation_source')
                    if data.header.get('generation_recipe') != self.identity or source not in self.state['counts']:
                        raise ValueError('unexpected external writer changed the recipe block')
                self.state['counts'][source] += count
                self.state['cursor'] = rowid
            count = db.execute('SELECT positions FROM blocks WHERE name=?',(self.block,)).fetchone()[0]
        if count != sum(self.state['counts'].values()) or any(self.state['counts'][s] > e['quota'] for s,e in self.state['sources'].items()):
            raise ValueError('recipe/source count integrity failure')
        return count

    def admitted(self):
        stamp = time.time()
        if self.stop_path.exists() or (self.store / 'STOP').exists():
            return False
        if stamp >= self.config.get('stop_at',float('inf')):
            return False
        if 'admission_command' not in self.config:
            return True
        saved = self.state.get('admission')
        if saved is None or stamp - saved['at'] >= self.config.get('admission_refresh',60):
            try:
                result = json.loads(subprocess.check_output(self.config['admission_command'],text=True,stderr=subprocess.DEVNULL,timeout=45))
                allowed = result.get('allowed',result.get('funded')) is True
            except (OSError,ValueError,subprocess.SubprocessError):
                allowed = False
            if (saved is None and not allowed) or (saved is not None and saved['allowed']!=allowed):
                self.state['notice']=dict(message='Self-play generation resumed.' if allowed else
                    'Self-play generation paused by its configured admission check.',alert=not allowed)
            saved = dict(at=stamp,allowed=allowed)
            self.state['admission'] = saved
        return saved['allowed']

    def source(self):
        return next((s['id'] for s in self.sources if self.state['counts'][s['id']] < s['positions']),None)

    def status(self):
        count = self.reconcile()
        complete = count == self.target
        stopped = (self.stop_path.exists() or (self.store/'STOP').exists()
                   or time.time()>=self.config.get('stop_at',float('inf')))
        from .idle_work import alive
        owner=self.state.get('owner')
        owned=(not self.config.get('require_coordinator',True)
               or bool(owner and time.time()-owner['at']<30 and alive(owner)))
        allowed = not complete and not stopped and owned and not self.state.get('hold') and self.admitted()
        if complete:
            manifest = nnue_shards.seal(self.store,self.block)
            self.state['manifest'] = dict(path=str(manifest.resolve()),sha256=nnue_data.file_hash(manifest))
        return dict(complete=complete,stopped=stopped,allowed=allowed,positions=count,target=self.target,
                    counts=self.state['counts'],quotas={s['id']:s['positions'] for s in self.sources},
                    source=self.source(),hold=self.state.get('hold'),manifest=self.state.get('manifest'),notice=self.state.get('notice'),
                    expires_at=time.time()+90)

    def handle(self, request):
        if not isinstance(request,dict):
            raise ValueError('recipe requests must be JSON objects')
        with writer_lock(self.lock):
            self.initialize()
            op = request.get('op','status')
            if op=='tick':
                from .idle_work import birth
                self.state['owner']=dict(pid=os.getpid(),birth=birth(os.getpid()),at=time.time())
            status = self.status()
            if op == 'asset':
                digest = request['sha256']
                if digest not in self.state['assets']:
                    raise ValueError('asset is not part of this recipe')
                offset = request.get('offset',0)
                if type(offset) is not int or not 0 <= offset <= self.state['assets'][digest]['bytes']:
                    raise ValueError('invalid asset offset')
                with (self.root/'assets'/digest).open('rb') as stream:
                    stream.seek(offset);data = stream.read(1024*1024)
                result = dict(data=base64.b64encode(data).decode(),bytes=self.state['assets'][digest]['bytes'])
            elif op == 'claim':
                worker = request['worker']
                if not isinstance(worker,str) or not name_pattern.fullmatch(worker):
                    raise ValueError('invalid worker identifier')
                engine=request.get('engine_sha256',self.state['engine'])
                if engine not in self.state['engines']:
                    raise ValueError('worker engine is not an approved recipe artifact')
                job = next((j for j in self.state['jobs'].values() if j['worker']==worker and j['status']=='claimed'),None)
                if job is not None and job.get('engine',self.state['engine'])!=engine:
                    raise ValueError('worker engine changed during an assignment')
                if not status['allowed']:
                    result = dict(status=status,job=None)
                else:
                    if job is None:
                        index = self.state['next_job'];self.state['next_job'] += 1
                        identity = f'job_{index:09d}'
                        job = dict(id=identity,worker=worker,engine=engine,source=status['source'],status='claimed',
                                   seed=int.from_bytes(hashlib.sha256((self.config['campaign']+':'+self.identity+':'+identity).encode()).digest()[:8],'big'))
                        self.state['jobs'][identity] = job
                    job['lease_until'] = time.time()+90
                    assets = {name:self.state[name] for name in ('engine','network')}
                    assets['engine']=job['engine']
                    assets.update({name:value for name,value in self.state['sources'][job['source']].items() if name in ('starts','book')})
                    result = dict(status=status,job=dict(job,recipe_sha256=self.identity,assets=assets,
                        exclude=self.state['exclude'],asset_sizes=self.state['assets'],
                        diversity=self.state['sources'][job['source']]['diversity'],nodes=self.config.get('nodes',5000),
                        positions=self.config.get('job_positions',10000),games=self.config.get('job_games',1000),
                        generator_sha256=self.state['generator_sha256'],
                        seconds=self.config.get('job_seconds',300),stop_at=self.config.get('stop_at')))
            elif op in ('heartbeat','submit'):
                identity=request['job']
                if not isinstance(identity,str) or not name_pattern.fullmatch(identity):raise ValueError('invalid job identifier')
                receipt=self.root/'receipts'/(identity+'.json')
                job=json.loads(receipt.read_text()) if receipt.exists() else self.state['jobs'][identity]
                if request['worker'] != job['worker']:
                    raise ValueError('job belongs to another worker')
                job['lease_until'] = time.time()+90
                if op=='submit' and job['status']=='done' and request['sha256']!=job['chunk']:
                    raise ValueError('completed job cannot be replaced by different data')
                if op == 'submit' and job['status'] != 'done':
                    raw = base64.b64decode(request['data'],validate=True)
                    if len(raw)>8*1024*1024 or hashlib.sha256(raw).hexdigest()!=request['sha256']:
                        raise ValueError('invalid or oversized completed chunk')
                    inbox=self.root/'chunks';inbox.mkdir(exist_ok=True);path=inbox/(request['sha256']+'.mxdata')
                    if not path.exists():
                        temporary=path.with_name('.'+uuid.uuid4().hex);temporary.write_bytes(raw);temporary.replace(path)
                    with binary_dataset(path) as data:
                        source_options=self.state['sources'][job['source']]
                        if (data.header.get('generation_recipe')!=self.identity or data.header.get('generation_source')!=job['source']
                                or data.header.get('network_sha256')!=self.state['network']
                                or data.header.get('engine_sha256')!=job['engine']
                                or data.header.get('nodes')!=self.config.get('nodes',5000)
                                or data.header.get('label_source')!='completed_exact_iteration'
                                or data.header.get('generator_sha256')!=self.state['generator_sha256']
                                or data.header.get('book_sha256')!=source_options.get('book')
                                or data.header.get('start_positions_sha256')!=source_options.get('starts')
                                or data.header.get('diversity')!=(dict(nnue_selfplay.diversity_recipe) if source_options['diversity'] else None)):
                            raise ValueError('completed chunk provenance differs from its assignment')
                        excluded=set()
                        for digest in self.state['exclude']:
                            _,reserved=nnue_data.read_dataset(self.root/'assets'/digest)
                            excluded.update(nnue_data.position_key(chess.Board(r['fen'])) for r in reserved)
                        for row in data:
                            if (nnue_shards.validate_row(row,data.header) in excluded or row['nodes']>data.header['nodes']
                                    or bool(row['source'].get('seed_game'))!=bool(source_options.get('starts'))):
                                raise ValueError('completed chunk contains reserved data or exceeds its search budget')
                    remaining=self.state['sources'][job['source']]['quota']-self.state['counts'][job['source']]
                    archive_digest=None
                    if request.get('archive') is not None:
                        archive=base64.b64decode(request['archive'],validate=True)
                        archive_digest=hashlib.sha256(archive).hexdigest()
                        if len(archive)>8*1024*1024 or archive_digest!=request.get('archive_sha256') or data.header.get('generation_archive')!=archive_digest:
                            raise ValueError('game archive checksum or size mismatch')
                        folder=self.root/'games';folder.mkdir(exist_ok=True)
                        archive_path=folder/(archive_digest+'.tar.gz')
                        if not archive_path.exists():
                            temporary=archive_path.with_name('.'+uuid.uuid4().hex);temporary.write_bytes(archive);temporary.replace(archive_path)
                    before=sum(self.state['counts'].values())
                    if remaining and not status['complete']:
                        nnue_shards.ingest(self.store,self.block,[path],target=self.target,
                                           accepted_limit=sum(self.state['counts'].values())+remaining)
                    job['status']='done';job['chunk']=request['sha256'];job['archive']=archive_digest
                    after=self.reconcile()
                    if after>before:self.state['empty_jobs']=0
                    elif data.header.get('generation_games',0)>0:self.state['empty_jobs']+=1
                    if self.state['empty_jobs']>=self.config.get('max_empty_jobs',200):
                        self.state['hold']=dict(id=uuid.uuid4().hex,reason='no_new_positions')
                        self.state['notice']=dict(message='Self-play produced no new usable positions; generation is paused for review.',alert=True)
                if job['status']=='done':
                    receipt.parent.mkdir(exist_ok=True)
                    if not receipt.exists():save(receipt,job)
                    self.state['jobs'].pop(identity,None)
                result=dict(status=self.status(),archive_sha256=job.get('archive'))
                if op=='heartbeat' and job['status']=='done':result['status']=dict(result['status'],allowed=False)
                if op=='heartbeat' and self.state['counts'][job['source']]>=self.state['sources'][job['source']]['quota']:
                    result['status']=dict(result['status'],allowed=False)
            elif op == 'incident':
                job=self.state['jobs'][request['job']]
                if request.get('worker')!=job['worker']:raise ValueError('incident belongs to another worker')
                self.state['hold']=dict(id=uuid.uuid4().hex,reason='worker_failure',worker=request.get('worker'))
                self.state['notice']=dict(message='A self-play worker failed; generation is paused for review.',alert=True)
                result=dict(status=self.status())
            elif op in ('status','tick'):
                result=dict(status=status)
            else:
                raise ValueError('unknown recipe operation')
            save(self.state_path,self.state)
            for key in ('result_path','heartbeat_path'):
                if self.config.get(key):
                    current=result.get('status',status)
                    save(path_at(self.path.parent,self.config[key]),dict(at=time.time(),accepted=current['positions'],
                        target=self.target,outcome='complete' if current['complete'] else 'stopped' if current.get('stopped') else 'running',
                        source_counts=current['counts']))
            return result


    def fail(self, error):
        with writer_lock(self.lock):
            self.initialize()
            if not self.state.get('hold'):
                self.state['hold']=dict(id=uuid.uuid4().hex,reason=type(error).__name__)
            self.state['notice']=dict(message='Self-play coordinator stopped; generation is paused for review.',alert=True)
            save(self.state_path,self.state)
        self.deliver_notice()

    def deliver_notice(self):
        if not self.config.get('notify_command'):
            return
        with writer_lock(self.lock):
            self.initialize()
            notice=self.state.get('notice')
            if not notice:return
            try:
                subprocess.run(self.config['notify_command'],input=json.dumps(notice),text=True,
                               stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=True)
            except (OSError,subprocess.SubprocessError):return
            self.state['notice']=None
            save(self.state_path,self.state)


def rpc_call(config, request):
    raw=subprocess.check_output(command(config['coordinator']),input=json.dumps(request).encode(),stderr=subprocess.DEVNULL,timeout=60)
    if len(raw)>max_rpc_bytes:
        raise ValueError('oversized coordinator response')
    return json.loads(raw)


def terminate_generation(process, force=False):
    """Terminate only the process tree created for this background assignment."""
    try:
        if os.name=='posix':
            os.killpg(process.pid,signal.SIGKILL if force else signal.SIGTERM)
        elif os.name=='nt':
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],
                           stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=5,check=False)
        else:
            process.kill() if force else process.terminate()
    except ProcessLookupError:
        pass


class generation_worker:
    def __init__(self, config, stopping=None):
        self.config=config
        self.stopping=stopping or threading.Event()
        self.root=Path(config['output']).resolve();self.root.mkdir(parents=True,exist_ok=True)
        self.identity=config['id']
        if not isinstance(self.identity,str) or not name_pattern.fullmatch(self.identity):
            raise ValueError('invalid worker identifier')
        self.call=lambda request:rpc_call(config,request)

    def asset(self, digest, job, executable=False):
        cache=self.root/'assets';cache.mkdir(exist_ok=True)
        path=cache/(digest+('.exe' if executable and os.name=='nt' else ''))
        with writer_lock(cache/(digest+'.lock')):
            if path.exists() and nnue_data.file_hash(path)==digest:
                return path
            supplied=next((Path(self.config[k]) for k in ('engine','network','book') if self.config.get(k)
                           and nnue_data.file_hash(self.config[k])==digest),None)
            temporary=path.with_name('.'+uuid.uuid4().hex)
            if supplied is not None:
                shutil.copyfile(supplied,temporary)
            else:
                if executable:
                    raise ValueError('worker engine differs from the recipe; configure the matching local engine')
                with temporary.open('wb') as stream:
                    offset=0;size=job['asset_sizes'][digest]['bytes']
                    while offset<size:
                        response=self.call(dict(op='asset',sha256=digest,offset=offset))
                        data=base64.b64decode(response['data'],validate=True)
                        if not data or len(data)>1024*1024 or offset+len(data)>size:
                            raise ValueError('invalid asset transfer')
                        stream.write(data);offset+=len(data)
                    stream.flush();os.fsync(stream.fileno())
            if nnue_data.file_hash(temporary)!=digest:
                temporary.unlink();raise ValueError('downloaded asset checksum mismatch')
            temporary.chmod(0o700 if executable else 0o600);temporary.replace(path)
        return path

    def work(self, job):
        if nnue_data.file_hash(nnue_selfplay.__file__)!=job['generator_sha256']:
            raise ValueError('worker generator differs from the frozen recipe runtime')
        directory=self.root/'runs'/job['recipe_sha256']/job['id'];directory.mkdir(parents=True,exist_ok=True)
        pid_file=directory/'process.json'
        if (directory/'chunk.mxdata').exists():
            return self.submit_completed(directory, job)
        if pid_file.exists():
            from .idle_work import alive
            previous=json.loads(pid_file.read_text())
            if previous.get('birth') is not None and alive(previous):
                if previous.get('uid')!=os.getuid():raise ValueError('saved process belongs to another user')
                try:os.killpg(previous['pid'],signal.SIGTERM)
                except ProcessLookupError:pass
                end=time.monotonic()+5
                while alive(previous) and time.monotonic()<end:time.sleep(.1)
                if alive(previous):
                    os.killpg(previous['pid'],signal.SIGKILL)
        assets={name:self.asset(digest,job,name=='engine') for name,digest in job['assets'].items()}
        excluded=[self.asset(digest,job) for digest in job['exclude']]
        args=[sys.executable,'-u','-m','maxima_runner','nnue:selfplay','--output',str(directory/'run'),
              '--engine',str(assets['engine']),'--network',str(assets['network']),
              '--positions',str(job['positions']),'--games-per-run',str(job['games']),
              '--nodes',str(job['nodes']),'--seed',str(job['seed']),'--stop-file',str(directory/'STOP')]
        settings=directory/'run/settings.json'
        deadline=time.time()+job['seconds']
        if job.get('stop_at') is not None:deadline=min(deadline,job['stop_at'])
        args.extend(['--stop-at',str(deadline)])
        if assets.get('starts'):args.extend(['--start-positions',str(assets['starts']),'--min-ply','0'])
        args.extend(['--book',str(assets['book'])] if assets.get('book') else ['--no-book'])
        if job['diversity']:args.append('--diversity')
        for path in excluded:args.extend(['--exclude-dataset',str(path)])
        env=dict(os.environ)
        if os.name=='posix':args=['nice','-n',str(self.config.get('nice',10))]+args
        if self.config.get('idle_control'):env['MAXIMA_GENERATION_IDLE_CONTROL']=self.config['idle_control']
        (directory/'STOP').unlink(missing_ok=True)
        process=None
        try:
            with (directory/'generation.log').open('ab') as log:
                process=subprocess.Popen(args,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=os.name=='posix')
                from .idle_work import birth
                save(pid_file,dict(pid=process.pid,birth=birth(process.pid),uid=os.getuid() if os.name=='posix' else None))
                stopped=None
                next_heartbeat=0
                status=dict(allowed=False,expires_at=0)
                while process.poll() is None:
                    if time.monotonic()>=next_heartbeat:
                        try:status=self.call(dict(op='heartbeat',worker=self.identity,job=job['id']))['status']
                        except (OSError,ValueError,subprocess.SubprocessError):status=dict(allowed=False,expires_at=0)
                        next_heartbeat=time.monotonic()+10
                    allowed=status.get('allowed') is True and time.time()<status.get('expires_at',0)
                    if self.config.get('admission_file'):
                        save(Path(self.config['admission_file']),dict(allowed=allowed,expires_at=status.get('expires_at',0)))
                    if self.stopping.is_set() or not allowed or time.time()>deadline+30:
                        (directory/'STOP').touch();stopped=stopped or time.monotonic()
                        if time.monotonic()-stopped>30:break
                    time.sleep(.5)
                if process.poll() is None:
                    terminate_generation(process)
                    process.wait(timeout=5)
                if process.returncode and stopped is None:
                    raise RuntimeError('generation process failed; completed game files are retained')
        finally:
            if process is not None and process.poll() is None:
                terminate_generation(process,force=True)
                process.wait()
        cfg=json.loads(settings.read_text());rows,_,games=nnue_selfplay.load_run(directory/'run',cfg)
        archive=directory/'games.tar.gz'
        with tarfile.open(archive,'w:gz') as bundle:
            bundle.add(settings,arcname='settings.json',recursive=False)
            for game in sorted((directory/'run').glob('game-[0-9]*')):
                for name in ('game.pgn','report.json'):bundle.add(game/name,arcname=game.name+'/'+name,recursive=False)
        archive_bytes=archive.read_bytes();archive_digest=hashlib.sha256(archive_bytes).hexdigest()
        header=dict(cfg,variant='standard',score_pov='side_to_move',split_method='game',
                    label_source='completed_exact_iteration',generation_recipe=job['recipe_sha256'],generation_source=job['source'],generation_games=len(games),generation_archive=archive_digest)
        output=directory/'chunk.mxdata'
        if not output.exists():nnue_data.save_dataset(output,header,rows)
        return self.submit_completed(directory, job)

    def submit_completed(self, directory, job):
        """Retry the identical finished assignment before starting another engine."""
        output=directory/'chunk.mxdata'
        archive=directory/'games.tar.gz'
        archive_bytes=archive.read_bytes();archive_digest=hashlib.sha256(archive_bytes).hexdigest()
        raw=output.read_bytes()
        result=self.call(dict(op='submit',worker=self.identity,job=job['id'],sha256=hashlib.sha256(raw).hexdigest(),
                              data=base64.b64encode(raw).decode(),archive=base64.b64encode(archive_bytes).decode(),archive_sha256=archive_digest))
        if result.get('archive_sha256')!=archive_digest:raise ValueError('coordinator did not verify the game archive')
        save(directory/'receipt.json',result)
        if (directory/'run').exists():shutil.rmtree(directory/'run')
        output.unlink()
        archive.unlink()
        (directory/'process.json').unlink(missing_ok=True)
        return result

    def run(self, once=False):
        with writer_lock(self.root/'.worker.lock'):
            self.run_locked(once)

    def run_locked(self, once):
        while not self.stopping.is_set() and time.time()<self.config.get('stop_at',float('inf')):
            try:
                response=self.call(dict(op='claim',worker=self.identity,
                    engine_sha256=nnue_data.file_hash(self.config['engine'])))
            except (OSError,ValueError,subprocess.SubprocessError):
                if self.config.get('admission_file'):save(Path(self.config['admission_file']),dict(allowed=False,expires_at=0))
                if once:raise
                time.sleep(5)
                continue
            if response['status']['complete'] or response['status'].get('stopped'):return
            if response['job'] is None:
                if once:return
                time.sleep(5);continue
            try:
                self.work(response['job'])
            except BaseException:
                self.stopping.set()
                try:self.call(dict(op='incident',worker=self.identity,job=response['job']['id']))
                except Exception:pass
                raise
            if once:return


def main():
    parser=argparse.ArgumentParser(prog='maxima nnue:generate',description=__doc__)
    group=parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--config',type=Path,help='generation recipe; relative paths use its directory')
    group.add_argument('--worker-config',type=Path,help='worker identity, local paths and coordinator command')
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--status',action='store_true')
    mode.add_argument('--check',action='store_true',help='validate inputs without starting generation')
    mode.add_argument('--rpc',action='store_true',help='authenticated transport entry point; one JSON request on stdin')
    mode.add_argument('--coordinator',action='store_true',help='coordinate external workers without local producers')
    parser.add_argument('--clear-hold',metavar='INCIDENT_ID',help='clear an explicitly reviewed generation incident')
    parser.add_argument('--once',action='store_true',help='perform one worker assignment or coordinator status poll')
    args=parser.parse_args()
    if args.worker_config:
        if args.status or args.rpc or args.coordinator or args.check or args.clear_hold:
            parser.error('worker mode accepts only --once')
        config=json.loads(args.worker_config.read_text());base=args.worker_config.resolve().parent
        if (not isinstance(config,dict) or config.get('version')!=1
                or set(config)-{'version','id','output','coordinator','engine','network','book','idle_control','admission_file','slots','nice','stop_at'}):
            raise ValueError('unsupported worker configuration')
        if type(config.get('nice',10)) is not int or not 0<=config.get('nice',10)<=19:raise ValueError('invalid worker priority')
        for key in ('output','engine','network','book','idle_control','admission_file'):
            if config.get(key):config[key]=str(path_at(base,config[key]))
        command(config['coordinator']);slots=positive(config.get('slots',1),'worker slots',256)
        stopping=threading.Event()
        prior_term=signal.signal(signal.SIGTERM,lambda *_:stopping.set())
        def run(slot):
            local=dict(config,id=config['id']+'_'+str(slot),output=str(Path(config['output'])/str(slot)))
            generation_worker(local,stopping).run(args.once)
        with ThreadPoolExecutor(max_workers=slots) as pool:
            futures=[pool.submit(run,slot) for slot in range(slots)]
            try:
                wait(futures,return_when=FIRST_EXCEPTION)
                for future in futures:
                    if future.done():future.result()
            except BaseException:
                stopping.set()
                raise
            finally:signal.signal(signal.SIGTERM,prior_term)
        return 0
    controller=recipe(args.config)
    if args.clear_hold:
        with writer_lock(controller.lock):
            controller.initialize()
            if (controller.state.get('hold') or {}).get('id')!=args.clear_hold:raise ValueError('incident ID does not match')
            controller.state['hold']=None;controller.state['empty_jobs']=0
            save(controller.state_path,controller.state)
        return 0
    if args.check:
        for key in ('engine','network'):nnue_data.file_hash(path_at(controller.path.parent,controller.config[key]))
        for path in controller.config.get('engines',[]):nnue_data.file_hash(path_at(controller.path.parent,path))
        excluded=[path_at(controller.path.parent,p) for p in controller.config.get('exclude',[])]
        for path in excluded:nnue_data.read_dataset(path)
        for source in controller.sources:
            if source.get('book'):nnue_data.file_hash(path_at(controller.path.parent,source['book']))
            if source.get('input'):nnue_seeds.prepare(path_at(controller.path.parent,source['input']),
                selection=source.get('selection','positions'),limit=source.get('limit',10000),
                per_game=source.get('per_game',4),seed=source.get('seed',1),exclude=excluded,**source.get('options',{}))
        print(json.dumps(dict(target=controller.target,sources=[s['id'] for s in controller.sources],valid=True)))
        return 0
    if args.rpc:
        raw=sys.stdin.buffer.read(max_rpc_bytes+1)
        if len(raw)>max_rpc_bytes:raise ValueError('oversized recipe request')
        request=json.loads(raw)
        if request.get('op')=='tick':raise ValueError('coordinator heartbeat is local-only')
        result=controller.handle(request)
        controller.deliver_notice()
        print(json.dumps(result))
        return 0
    if args.status or args.once:
        print(json.dumps(controller.handle(dict(op='status'))['status'],indent=2))
        return 0
    controller.handle(dict(op='tick'))
    prior_term=signal.signal(signal.SIGTERM,lambda *_:controller.stop_path.touch())
    if args.coordinator:
        try:
            while True:
                status=controller.handle(dict(op='tick'))['status']
                if status['complete'] or status.get('stopped'):print(json.dumps(status));return 0
                controller.deliver_notice()
                time.sleep(5)
        except BaseException as error:
            controller.stop_path.touch()
            if isinstance(error,Exception):controller.fail(error)
            raise
        finally:signal.signal(signal.SIGTERM,prior_term)
    config=dict(output=str(controller.root/'workers'),engine=str(path_at(controller.path.parent,controller.config['engine'])),
                network=str(path_at(controller.path.parent,controller.config['network'])),
                coordinator=[sys.executable,'-m','maxima_runner','nnue:generate','--config',str(controller.path),'--rpc'])
    stopping=threading.Event()
    def run(slot):
        generation_worker(dict(config,id='local_'+str(slot),output=str(controller.root/'workers'/str(slot))),stopping).run()
    with ThreadPoolExecutor(max_workers=controller.config.get('workers',2)) as pool:
        futures=[pool.submit(run,slot) for slot in range(controller.config.get('workers',2))]
        try:
            while not all(future.done() for future in futures):
                controller.handle(dict(op='tick'))
                controller.deliver_notice()
                for future in futures:
                    if future.done():future.result()
                time.sleep(.5)
            for future in futures:future.result()
        except BaseException as error:
            stopping.set()
            controller.stop_path.touch()
            if isinstance(error,Exception):controller.fail(error)
            raise
        finally:signal.signal(signal.SIGTERM,prior_term)
    print(json.dumps(controller.handle(dict(op='status'))['status']))
    return 0
