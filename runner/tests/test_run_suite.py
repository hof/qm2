"""Offline checks for suite parsing, UCI isolation, and final-move scoring."""
import asyncio
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


from maxima_runner import run_suite as runner
import chess
import chess.engine


def run_position(*args, **kwargs):
    return asyncio.run(runner.run_position(*args, **kwargs))

fake_engine = '''
import sys
commands = []
for line in sys.stdin:
    command = line.strip()
    commands.append(command)
    if command == "uci":
        print("id name fixture")
        print("option name Hash type spin default 16 min 1 max 1024")
        print("option name OwnBook type check default true")
        print("option name SyzygyPath type string default <empty>")
        print("option name SyzygyProbeLimit type spin default 5 min 0 max 5")
        print("uciok", flush=True)
    elif command == "isready":
        print("readyok", flush=True)
    elif command.startswith("go "):
        assert "setoption name Hash value 32" in commands
        assert "setoption name OwnBook value false" in commands
        assert commands.index("ucinewgame") < commands.index("isready")
        assert sum(c.startswith("go ") for c in commands) == 1
        print("info depth 3 score cp 20 nodes 20 time 1 pv e2e4 e7e5")
        enabled = "setoption name SyzygyProbeLimit value 0" not in commands
        if enabled:
            assert any(c.startswith("setoption name SyzygyPath value ") for c in commands)
        print(f"info depth 4 score cp 30 nodes 40 time 2 tbhits {3 if enabled else 0} pv d2d4 d7d5")
        print("bestmove d2d4", flush=True)
    elif command == "quit":
        break
'''


class suite_tests(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "suite.epd"
            path.write_text(text)
            return runner.read_suite(path)

    def test_multiple_moves_and_id(self):
        positions = self.parse('8/8/8/8/8/8/4P3/K6k w - - bm e2e3 e2e4; id "two; choices";')
        self.assertEqual(positions[0]["expected_moves"], ["e2e3", "e2e4"])
        self.assertEqual(positions[0]["id"], "two; choices")
        self.assertTrue(positions[0]["fen"].endswith(" 0 1"))

    def test_san_special_moves_and_disambiguation(self):
        cases = [
            ('r3k2r/8/8/8/8/8/8/R3K2R w KQkq -', 'O-O', 'e1g1'),
            ('r3k2r/8/8/8/8/8/8/R3K2R w KQkq -', 'O-O-O', 'e1c1'),
            ('r3k2r/8/8/8/8/8/8/R3K2R b KQkq -', 'O-O', 'e8g8'),
            ('r3k2r/8/8/8/8/8/8/R3K2R b KQkq -', 'O-O-O', 'e8c8'),
            ('7k/8/8/8/8/8/N1N5/K7 w - -', 'Nab4', 'a2b4'),
            ('7k/8/N7/8/8/8/N7/K7 w - -', 'N2b4', 'a2b4'),
            ('7k/8/8/8/8/1N6/8/KN3N2 w - -', 'Nb1d2', 'b1d2'),
            ('k3r3/8/8/8/8/2N1N3/8/4K3 w - -', 'Nd5', 'c3d5'),
            ('8/8/k7/1p6/2B5/8/8/7K w - -', 'Bxb5+', 'c4b5'),
            ('k7/8/8/3pP3/8/8/8/K7 w - d6', 'exd6', 'e5d6'),
            ('7k/P7/8/8/8/8/8/7K w - -', 'a8=Q+', 'a7a8q'),
            ('7k/P7/8/8/8/8/8/7K w - -', 'a8=N', 'a7a8n'),
            ('1r5k/P7/8/8/8/8/8/7K w - -', 'axb8=Q+', 'a7b8q'),
            ('7k/8/5KQ1/8/8/8/8/8 w - -', 'Qg7#', 'g6g7'),
        ]
        for fen, san, uci in cases:
            with self.subTest(san=san, fen=fen):
                position = self.parse(f'{fen} bm {san};')[0]
                self.assertEqual(position['expected_moves'], [uci])
                self.assertEqual(len(position['expected_san']), 1)

    def test_epd_operations_and_clocks(self):
        position = self.parse('8/8/8/8/8/8/4P3/K6k w - - '
                              'id "two; choices"; hmvc 42; fmvn 17; '
                              'c0 "comment with escaped \"quotes\""; '
                              'bm e3 e2e4; am Ka2; ce 12;')[0]
        self.assertEqual(position['expected_moves'], ['e2e3', 'e2e4'])
        self.assertEqual(position['expected_san'], ['e3', 'e4'])
        self.assertEqual(position['id'], 'two; choices')
        self.assertTrue(position['fen'].endswith(' 42 17'))
        self.assertIn('ce 12;', position['epd'])

    def test_reject_ambiguous_illegal_or_malformed_input(self):
        with self.assertRaisesRegex(ValueError, 'suite is empty'):
            self.parse('')
        for text in ('8/8/8/8/8/8/4P3/K6k w - - bm e5;',
                     '8/8/8/8/8/8/4P3/K6k w - - bm e2e5;',
                     '8/8/8/8/8/8/4P3/K6k w - - bm --;',
                     '8/8/8/8/8/8/4P3/K6k w - - bm 12;',
                     '8/8/8/8/8/8/4P3/K6k w - - bm e4; hmvc -1;',
                     '8/8/8/8/8/8/4P3/K6k w - - am e3;',
                     '8/8/8/8/8/8/4P3/K7 w - - bm e4;',
                     '9/8/8/8/8/8/4P3/K6k w - - bm e4;',
                     '7k/8/8/8/8/8/N1N5/K7 w - - bm Nb4;',
                     'k3r3/8/8/8/8/2N1N3/8/4K3 w - - bm Ned5;',
                     '8/8/8/8/8/8/4P3/K6k w - - bm e4; id 5;'):
            with self.subTest(text=text), self.assertRaisesRegex(ValueError, r'suite.epd:1:'):
                self.parse(text)

    def test_missing_parser_dependency(self):
        with patch.dict(sys.modules, {'chess': None}):
            with self.assertRaisesRegex(RuntimeError, 'runner package'):
                self.parse('8/8/8/8/8/8/4P3/K6k w - - bm e4;')

    def test_bundled_suite(self):
        positions = runner.read_suite(Path(runner.__file__).parent / "data/suites/bratko-kopec.epd")
        self.assertEqual(len(positions), 22)
        ids = {position["id"] for position in positions}
        self.assertEqual(len(ids), 22)
        self.assertNotIn("BK.03", ids)
        self.assertNotIn("BK.09", ids)
        self.assertIn("BK.07", ids)

    def test_report_scores_and_time(self):
        info = runner.report_info({'depth': 5, 'score': chess.engine.PovScore(
            chess.engine.Mate(-3), chess.WHITE), 'upperbound': True, 'nodes': 99,
            'time': 0.123, 'pv': [chess.Move.from_uci('a7a8q')]})
        self.assertEqual(info['score'], {'type': 'mate', 'value': -3, 'bound': 'upperbound'})
        self.assertEqual(info['pv'], ['a7a8q'])
        self.assertEqual(info['time'], 123)

    def test_final_move_and_fresh_engine(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine.py'
            engine.write_text(fake_engine)
            position = {'id': 'fixture', 'fen': chess.STARTING_FEN,
                        'expected_moves': ['e2e4']}
            for _ in range(2):
                result, metadata = run_position([sys.executable, str(engine)], position,
                                                       'nodes 40', 32, 3)
                self.assertFalse(result['solved'])
                self.assertEqual(result['bestmove'], 'd2d4')
                self.assertIsNotNone(result['first_expected_ms'])
                self.assertEqual(result['depth'], 4)
                self.assertEqual(result['nodes'], 40)
                self.assertEqual(metadata['options'], {'Hash': '32', 'OwnBook': 'false', 'SyzygyProbeLimit': '0'})
                self.assertEqual(result['tbhits'], 0)

    def test_syzygy_configuration_and_hits(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine.py'
            engine.write_text(fake_engine)
            tables = Path(directory) / 'tables with spaces'
            tables.mkdir()
            position = {'fen': chess.STARTING_FEN,
                        'expected_moves': ['d2d4']}
            result, metadata = run_position([sys.executable, str(engine)], position,
                                                   'nodes 40', 32, 3, tables)
            self.assertEqual(metadata['options']['SyzygyPath'], str(tables.resolve()))
            self.assertEqual(metadata['options']['SyzygyProbeLimit'], '5')
            self.assertEqual(result['tbhits'], 3)
            engine.write_text(fake_engine.replace(
                '        print("option name SyzygyPath type string default <empty>")', ''))
            with self.assertRaisesRegex(RuntimeError, 'does not advertise SyzygyPath'):
                run_position([sys.executable, str(engine)], position,
                                    'nodes 40', 32, 3, tables)

    def test_model_selection_identity_and_load_order(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine.py'
            model = Path(directory) / 'network file.nnue'
            model.write_bytes(b'fixture weights')
            source = fake_engine.replace('        print("uciok", flush=True)',
                '        print("option name UseNNUE type check default false")\n'
                '        print("option name EvalFile type string default <empty>")\n'
                '        print("uciok", flush=True)')
            source = source.replace('        assert "setoption name Hash value 32" in commands',
                '        assert commands.index(' + repr('setoption name EvalFile value ' + str(model))
                + ') < commands.index("setoption name UseNNUE value true")\n'
                '        assert "setoption name Hash value 32" in commands')
            engine.write_text(source)
            profile = runner.engine_profile([('UseNNUE', 'true')], [('EvalFile', str(model))])
            position = {'fen': chess.STARTING_FEN, 'expected_moves': ['d2d4']}
            result, metadata = run_position([sys.executable, str(engine)], position,
                                           'nodes 40', 32, 3, profile=profile)
            self.assertTrue(result['solved'])
            self.assertEqual(metadata['options']['UseNNUE'], 'true')
            self.assertEqual(metadata['models'], profile['models'])
            # Mutations while searching invalidate the model identity/report.
            engine.write_text(source.replace('        print("bestmove d2d4", flush=True)',
                '        open(' + repr(str(model)) + ', "wb").write(b"changed")\n'
                '        print("bestmove d2d4", flush=True)'))
            with self.assertRaisesRegex(ValueError, 'model file changed'):
                run_position([sys.executable, str(engine)], position,
                             'nodes 40', 32, 3, profile=profile)

    def test_unsupported_requested_option_rejects_suite(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine.py'
            engine.write_text(fake_engine)
            profile = runner.engine_profile([('UseNNUE', 'true')], [])
            with self.assertRaisesRegex(ValueError, 'does not advertise option'):
                run_position([sys.executable, str(engine)],
                             {'fen': chess.STARTING_FEN, 'expected_moves': ['d2d4']},
                             'nodes 40', 32, 3, profile=profile)

    def test_exit_and_timeout_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'engine.py'
            position = {'fen': chess.STARTING_FEN, 'expected_moves': ['e2e4']}
            engine.write_text('pass\n')
            with self.assertRaisesRegex(RuntimeError, 'exited'):
                run_position([sys.executable, str(engine)], position, 'nodes 40', 32, 1)
            engine.write_text('import time\ntime.sleep(30)\n')
            with self.assertRaisesRegex(RuntimeError, 'timed out'):
                run_position([sys.executable, str(engine)], position, 'nodes 40', 32, 0.1)


if __name__ == '__main__':
    unittest.main()
