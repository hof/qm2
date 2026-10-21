"""Command ownership, dependency isolation, and build failure propagation."""
import contextlib
import io
import os
import json
import signal
import subprocess
import tempfile
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from maxima_runner import cli


class cli_tests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / 'CMakePresets.json').write_text('{}')
        self.root_patch = patch.object(cli, 'project_root', return_value=self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def configure_build(self):
        build = self.root / 'build/cpp26/release'
        (build / 'src').mkdir(parents=True)
        (build / 'CMakeCache.txt').write_text('BUILD_TESTING:BOOL=ON\n')
        (build / 'src/maxima2').touch()

    def test_help_and_build_do_not_import_chess(self):
        with patch.dict(sys.modules, {'chess': None}), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(cli.main(['--help']), 0)
            self.assertEqual(cli.main(['help']), 0)
            self.assertIn('experience:import', output.getvalue())
            self.assertNotIn('\033[', output.getvalue())
            with self.assertRaises(SystemExit) as result:
                cli.main(['help', 'build'])
            self.assertEqual(result.exception.code, 0)
            with patch.object(cli, 'run_command', return_value=0) as run:
                self.assertEqual(cli.main(['build', '--debug']), 0)
        self.assertEqual(run.call_args_list[0].args[0], ['cmake', '--preset', 'cpp26_debug'])
        self.assertEqual(run.call_args_list[1].args[0],
                         ['cmake', '--build', '--preset', 'cpp26_debug', '--parallel'])

    def test_configure_failure_prevents_build(self):
        with patch.object(cli, 'run_command', return_value=7) as run:
            self.assertEqual(cli.main(['build']), 7)
        self.assertEqual(run.call_count, 1)

    def test_build_failure_propagates(self):
        with patch.object(cli, 'run_command', side_effect=[0, 9]):
            self.assertEqual(cli.main(['build', '--release']), 9)

    def test_missing_build_has_actionable_error(self):
        with contextlib.redirect_stderr(io.StringIO()) as output:
            self.assertEqual(cli.main(['test']), 1)
        self.assertIn('./maxima build --release', output.getvalue())

    def test_engine_tests_need_no_chess(self):
        self.configure_build()
        with patch.dict(sys.modules, {'chess': None}), patch.object(cli, 'run_command', return_value=0) as run:
            self.assertEqual(cli.main(['test', '--engine-only']), 0)
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[0], ['ctest', '--preset', 'cpp26_release', '--no-tests=error'])

    def test_subcommand_help_needs_no_chess(self):
        for command in ('uci', 'suite:run', 'match:run', 'experience:show', 'tablebases:syzygy:download',
                        'fics'):
            with patch.dict(sys.modules, {'chess': None}), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as result:
                    cli.main([command, '--help'])
                self.assertEqual(result.exception.code, 0)

    def test_failed_engine_checks_prevent_runner_checks(self):
        self.configure_build()
        with patch.object(cli, 'run_command', return_value=11) as run:
            self.assertEqual(cli.main(['test']), 11)
        self.assertEqual(run.call_count, 1)

    def test_runner_tests_receive_explicit_engine_and_propagate_failure(self):
        self.configure_build()
        with patch.object(cli, 'run_command', return_value=13) as run:
            self.assertEqual(cli.main(['test', '--runner-only']), 13)
        self.assertEqual(run.call_count, 1)
        command, root, environment = run.call_args.args
        self.assertIn('unittest', command)
        self.assertEqual(environment['MAXIMA_ENGINE'], str(root / 'build/cpp26/release/src/maxima2'))

    def test_local_toolchain_and_caches(self):
        binary = self.root / '.tools/bootstrap/bin/micromamba'
        binary.parent.mkdir(parents=True)
        binary.touch()
        binary.chmod(0o755)
        (self.root / '.tools/cpp26').mkdir()
        with patch.dict(os.environ, {}, clear=True), patch.object(cli, 'run_command', return_value=0) as run:
            self.assertEqual(cli.main(['build']), 0)
        self.assertEqual(run.call_args.args[0][:4],
                         [str(binary), 'run', '-p', str(self.root / '.tools/cpp26')])
        self.assertEqual(run.call_args.args[2]['XDG_CACHE_HOME'], str(self.root / '.tools/cache'))

    def test_unknown_command_returns_usage_failure(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
            cli.main(['no-such-command'])
        self.assertEqual(result.exception.code, 2)

    def test_uci_default_selection_and_forwarding(self):
        self.configure_build()
        binary = self.root / 'build/cpp26/release/src/maxima2'
        binary.chmod(0o755)
        with patch.dict(sys.modules, {'chess': None}), patch.object(cli.os, 'execv') as execute:
            self.assertIsNone(cli.main(['uci', '--', '--help', 'argument with spaces']))
        execute.assert_called_once_with(str(binary),
                                        [str(binary), '--help', 'argument with spaces'])

    def test_uci_launch_errors_keep_stdout_clean(self):
        for path in (self.root / 'missing', self.root / 'CMakePresets.json'):
            with contextlib.redirect_stdout(io.StringIO()) as output, \
                    contextlib.redirect_stderr(io.StringIO()) as errors:
                self.assertEqual(cli.main(['uci', '--engine', str(path)]), 1)
            self.assertEqual(output.getvalue(), '')
            self.assertIn(str(path), errors.getvalue())
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
            cli.main(['uci', 'unseparated-argument'])
        self.assertEqual(result.exception.code, 2)

    @unittest.skipUnless(os.name == 'posix', 'POSIX exec and signal behavior')
    def test_uci_exec_preserves_pid_streams_directory_arguments_and_signals(self):
        fixture = self.root / 'engine.py'
        fixture.write_text(
            'import json, os, sys\n'
            'print(json.dumps([os.getpid(), os.getcwd(), sys.argv[1:]]), flush=True)\n'
            'print("engine stderr", file=sys.stderr, flush=True)\n'
            'for line in sys.stdin:\n'
            '    if line.strip() == "uci": print("uciok", flush=True)\n')
        environment = dict(os.environ, PYTHONPATH=str(Path(cli.__file__).resolve().parents[1]))
        process = subprocess.Popen(
            [sys.executable, '-m', 'maxima_runner', 'uci', '--engine', sys.executable,
             '--', '-u', str(fixture), '--help', 'argument with spaces'],
            cwd=self.root, env=environment, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            # Terminate only after the exec'd fixture answers the UCI request.
            import selectors
            with selectors.DefaultSelector() as ready:
                ready.register(process.stdout, selectors.EVENT_READ)
                self.assertTrue(ready.select(timeout=5), 'engine did not start')
            pid, directory, arguments = json.loads(process.stdout.readline())
            self.assertEqual(pid, process.pid)
            self.assertEqual(directory, str(self.root))
            self.assertEqual(arguments, ['--help', 'argument with spaces'])
            process.stdin.write('uci\n')
            process.stdin.flush()
            with selectors.DefaultSelector() as ready:
                ready.register(process.stdout, selectors.EVENT_READ)
                self.assertTrue(ready.select(timeout=5), 'engine did not answer')
            self.assertEqual(process.stdout.readline(), 'uciok\n')
            process.terminate()
            output, errors = process.communicate(timeout=5)
            self.assertEqual(output, '')
            self.assertEqual(errors, 'engine stderr\n')
            self.assertEqual(process.returncode, -signal.SIGTERM)
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)
