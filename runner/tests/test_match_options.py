"""Match option validation, explicit model identity, and per-engine isolation."""
import argparse
from contextlib import asynccontextmanager
import io
import json
import sys
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import chess
import chess.engine

from maxima_runner.match_options import (engine_profile, option_assignment,
                                         validated_settings, verify_models)
from maxima_runner.matches import run_match


def option(name, kind, default=None, minimum=None, maximum=None, choices=None):
    return chess.engine.Option(name, kind, default, minimum, maximum, choices)


class match_option_tests(unittest.TestCase):
    def test_assignment_and_protected_policy(self):
        self.assertEqual(option_assignment('Some Name=a=b'), ('Some Name', 'a=b'))
        self.assertEqual(option_assignment('Empty='), ('Empty', ''))
        for text in ('bad', '=x', 'LMR=true\ngo depth 1', 'LMR=\x00',
                     'Hash=2', 'tHrEaDs=8', 'Ponder=true', 'Wild=losers',
                     'UCI_Variant=atomic', 'MultiPV=2', 'UseExperience=true'):
            with self.subTest(text=text), self.assertRaises(argparse.ArgumentTypeError):
                option_assignment(text)

    def test_typed_validation_and_canonical_names(self):
        options = {item.name: item for item in (
            option('LMR', 'check', True), option('Depth', 'spin', 5, 1, 9),
            option('Evaluation', 'combo', 'classical', choices=['classical', 'nnue']),
            option('Text', 'string', ''), option('Clear Hash', 'button'))}
        profile = engine_profile([('lmr', 'FALSE'), ('Depth', '9'),
                                  ('Evaluation', 'nnue'), ('Text', '')], [])
        self.assertEqual(validated_settings(options, {'Threads': 1}, profile),
                         {'LMR': False, 'Depth': 9, 'Evaluation': 'nnue', 'Text': ''})
        for name, value in [('missing', 'x'), ('LMR', 'no'), ('Depth', '10'),
                            ('Depth', '1.5'), ('Evaluation', 'other'), ('Clear Hash', '')]:
            with self.subTest(name=name, value=value), self.assertRaises(ValueError):
                validated_settings(options, {}, engine_profile([(name, value)], []))
        with self.assertRaises(ValueError):
            validated_settings(options, {'Depth': 100}, engine_profile([], []))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            engine_profile([('LMR', 'true'), ('lmr', 'false')], [])

    def test_model_paths_hashes_types_and_mutation(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as directory:
            path = Path(directory) / 'network file.nnue'
            path.write_bytes(b'first network')
            relative = str(path.relative_to(Path.cwd()))
            profile = engine_profile([], [('EvalFile', relative)])
            self.assertEqual(profile['requested_options']['EvalFile'], str(path))
            verify_models(profile)
            settings = validated_settings({'EvalFile': option('EvalFile', 'string')}, {}, profile)
            self.assertEqual(settings['EvalFile'], str(path))
            with self.assertRaisesRegex(ValueError, 'string'):
                validated_settings({'EvalFile': option('EvalFile', 'spin')}, {}, profile)
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                engine_profile([('evalfile', 'x')], [('EvalFile', relative)])
            with self.assertRaisesRegex(ValueError, 'use --model'):
                engine_profile([('EvalFile', relative)], [])
            path.write_bytes(b'changed network')
            with self.assertRaisesRegex(ValueError, 'changed'):
                verify_models(profile)
            with self.assertRaises(OSError):
                engine_profile([], [('EvalFile', str(path) + '.missing')])

    def test_models_are_applied_before_dependent_flags(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'network.nnue'
            path.write_bytes(b'fixture')
            profile = engine_profile([('UseNNUE', 'true')], [('EvalFile', str(path))])
            options = {'UseNNUE': option('UseNNUE', 'check', False),
                       'EvalFile': option('EvalFile', 'string')}
            settings = validated_settings(options, {'UseNNUE': False}, profile)
            self.assertEqual(list(settings), ['EvalFile', 'UseNNUE'])
            self.assertTrue(settings['UseNNUE'])


class match_profile_tests(unittest.IsolatedAsyncioTestCase):
    async def test_shared_tablebases_and_missing_support_before_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(seed=1, pairs=1, seconds=1, increment=0, max_plies=1,
                variant='standard', timeout=1, hash_mb=1,
                profiles=[engine_profile([], []), engine_profile([], [])],
                syzygy_path=Path(directory), json=Path(directory) / 'results.json')
            calls = []
            missing_support = False

            class session:
                identity = ['name fixture']
                def __init__(self, second):
                    options = [option('SyzygyPath', 'string', ''),
                               option('SyzygyProbeLimit', 'spin', 0, 0, 5),
                               option('SyzygyProbeDepth', 'spin', 1, 1, 64)]
                    self.protocol = SimpleNamespace(options={item.name: item for item in options})
                    if missing_support and second:
                        del self.protocol.options['SyzygyPath']
                async def configure(self, settings):
                    calls.append(settings)
                    return settings

            opened = 0
            @asynccontextmanager
            async def open_fixture(*unused):
                nonlocal opened
                opened += 1
                yield session(opened % 2 == 0)

            openings = [('draw', chess.Board('7k/8/8/8/8/8/8/K7 w - - 0 1'))]
            with patch('maxima_runner.uci.open_engine', open_fixture):
                await run_match([['a'], ['b']], openings, args, {'games': []}, io.StringIO())
                self.assertEqual(calls, [dict(SyzygyPath=directory, SyzygyProbeLimit=5,
                                              SyzygyProbeDepth=1)] * 4)
                calls.clear()
                missing_support = True
                with self.assertRaisesRegex(ValueError, 'does not advertise SyzygyPath'):
                    await run_match([['a'], ['b']], openings, args, {'games': []}, io.StringIO())
                self.assertEqual(calls, [])
                missing_support = False
                args.profiles[1] = engine_profile([('SyzygyPath', 'other')], [])
                with self.assertRaisesRegex(ValueError, 'controlled by --syzygy-path'):
                    await run_match([['a'], ['b']], openings, args, {'games': []}, io.StringIO())
                self.assertEqual(calls, [])

    async def test_distinct_profiles_and_validation_before_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / 'net.nnue'
            model.write_bytes(b'fixture')
            profiles = [engine_profile([('LMR', 'true')], []),
                        engine_profile([('LMR', 'false')], [('EvalFile', str(model))])]
            args = SimpleNamespace(seed=1, pairs=1, seconds=1, increment=0, max_plies=1,
                variant='standard', timeout=1, hash_mb=1, profiles=profiles,
                json=Path(directory) / 'results.json')
            calls = []
            sessions = []

            class session:
                identity = ['name fixture']
                def __init__(self):
                    self.protocol = SimpleNamespace(options={item.name: item for item in (
                        option('LMR', 'check', True), option('EvalFile', 'string', ''))})
                    self.settings = None
                async def configure(self, settings):
                    calls.append(settings)
                    self.settings = settings
                    return settings

            @asynccontextmanager
            async def open_fixture(*unused):
                engine = session()
                sessions.append(engine)
                yield engine

            openings = [('draw', chess.Board('7k/8/8/8/8/8/8/K7 w - - 0 1'))]
            with patch('maxima_runner.uci.open_engine', open_fixture):
                report = {'games': []}
                await run_match([['a'], ['b']], openings, args, report, io.StringIO())
                self.assertEqual([settings['LMR'] for settings in calls], [True, False] * 2)
                self.assertNotIn('EvalFile', calls[0])
                self.assertEqual(calls[1]['EvalFile'], str(model))
                self.assertEqual(report['games'][0]['engines'][1]['models'], profiles[1]['models'])
                self.assertEqual(json.loads(args.json.read_text())['games'], report['games'])
                calls.clear()
                args.profiles[1] = engine_profile([('unknown', 'true')], [])
                with self.assertRaisesRegex(ValueError, 'does not advertise'):
                    await run_match([['a'], ['b']], openings, args, {'games': []}, io.StringIO())
                self.assertEqual(calls, [])
                args.profiles[1] = profiles[1] = engine_profile([], [('EvalFile', str(model))])
                model.write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError, 'changed'):
                    await run_match([['a'], ['b']], openings, args, {'games': []}, io.StringIO())
                self.assertEqual(calls, [])

    async def test_model_change_during_game_preserves_failed_record(self):
        from maxima_runner.matches import play_game
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / 'net.nnue'
            model.write_bytes(b'original')
            profile = engine_profile([], [('EvalFile', str(model))])
            args = SimpleNamespace(seed=1, pairs=1, seconds=1, increment=0, max_plies=1,
                variant='standard', timeout=1, hash_mb=1, profiles=[profile, profile],
                json=Path(directory) / 'results.json')
            class session:
                identity = ['name fixture']
                protocol = SimpleNamespace(options={'EvalFile': option('EvalFile', 'string')})
                async def configure(self, settings):
                    return settings
            @asynccontextmanager
            async def open_fixture(*unused):
                yield session()
            async def mutate(*arguments, **keywords):
                result = await play_game(*arguments, **keywords)
                model.write_bytes(b'changed')
                return result
            report = {'games': []}
            stream = io.StringIO()
            with patch('maxima_runner.uci.open_engine', open_fixture), \
                    patch('maxima_runner.matches.play_game', mutate):
                with self.assertRaisesRegex(RuntimeError, 'changed'):
                    await run_match([['a'], ['b']],
                        [('draw', chess.Board('7k/8/8/8/8/8/8/K7 w - - 0 1'))],
                        args, report, stream)
            self.assertEqual(len(report['games']), 1)
            self.assertEqual(report['games'][0]['result'], '*')
            self.assertEqual(report['games'][0]['termination'], 'model identity failure')
            self.assertIn('model identity failure', stream.getvalue())
            self.assertEqual(json.loads(args.json.read_text())['games'], report['games'])

    async def test_uci_receives_boolean_and_absolute_model_path(self):
        from maxima_runner.uci import open_engine
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / 'network with spaces.nnue'
            model.write_bytes(b'network')
            log = root / 'commands.txt'
            script = root / 'engine.py'
            script.write_text("""
import sys
for line in sys.stdin:
    command = line.strip()
    with open(sys.argv[1], 'a') as stream:
        stream.write(command + '\\n')
    if command == 'uci':
        print('id name model fixture')
        print('option name LMR type check default true')
        print('option name EvalFile type string default <empty>')
        print('uciok', flush=True)
    elif command == 'isready':
        print('readyok', flush=True)
    elif command.startswith('go '):
        print('bestmove e2e4', flush=True)
    elif command == 'quit':
        break
""")
            profile = engine_profile([('LMR', 'false')], [('EvalFile', str(model))])
            async with open_engine([sys.executable, str(script), str(log)], root, 2) as engine:
                settings = validated_settings(engine.protocol.options, {}, profile)
                applied = await engine.configure(settings)
                self.assertEqual(applied['LMR'], 'false')
                await engine.search(chess.Board(), chess.engine.Limit(nodes=1))
            lines = log.read_text().splitlines()
            self.assertIn('setoption name LMR value false', lines)
            self.assertIn('setoption name EvalFile value ' + str(model), lines)
