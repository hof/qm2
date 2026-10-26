"""Native Polyglot selection and guards, without persistent search experience."""
import os
from pathlib import Path
import struct
import tempfile
import time
import unittest

import chess
import chess.polyglot

from uci_fixture import engine_session

engine_path = os.environ.get('MAXIMA_ENGINE')


def configure(engine, command):
    engine.send(command)
    engine.send('isready')
    return engine.wait_for('readyok', 5)


def search(engine, position='startpos', command='go depth 3'):
    engine.send('position ' + position)
    engine.send(command)
    lines = []
    deadline = time.monotonic() + 5
    while True:
        _, line = engine.receive(deadline)
        lines.append(line)
        if line.startswith('bestmove '):
            return line.split()[1], lines


def write_book(path, rows):
    entries = []
    for board, text, weight in rows:
        move = chess.Move.from_uci(text)
        code = (move.from_square << 6) | move.to_square
        entries.append((chess.polyglot.zobrist_hash(board), code, weight, 0))
    path.write_bytes(b''.join(struct.pack('>QHHI', *entry) for entry in sorted(entries)))


@unittest.skipUnless(engine_path, 'set MAXIMA_ENGINE for native book integration')
class native_book_tests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.book = self.root / 'reference book.bin'
        self.engine = engine_session([engine_path], str(self.root))
        self.addCleanup(self.engine.close)
        self.engine.send('uci')
        self.options = self.engine.wait_for('uciok', 5)
        configure(self.engine, 'setoption name BookFile value ' + str(self.book))

    def assert_searched(self, lines):
        self.assertTrue(any(line.startswith('info depth 3 ') for line in lines), lines)
        self.assertFalse(any('search_result book ' in line for line in lines), lines)

    def test_options_and_passive_historical_files(self):
        self.assertFalse(any('ExperienceFile' in line or 'ExperienceDepth' in line for line in self.options))
        files = {self.root / name: content for name, content in
                 [('experience.bin', b'MXEXP001 historical snapshot'),
                  ('experience.json', b'{"analysis": "historical"}'),
                  ('experience.sqlite', b'historical database')]}
        for path, content in files.items():
            path.write_bytes(content)
        self.assert_searched(search(self.engine)[1])
        for path, content in files.items():
            self.assertEqual(path.read_bytes(), content)

    def test_legal_weight_one_book_move_and_disable_options(self):
        # Native Polyglot keeps weight 1 and rejects a heavy geometrically illegal move.
        write_book(self.book, [(chess.Board(), 'e2e4', 1), (chess.Board(), 'e2e5', 65535),
                               (chess.Board(), 'd2d4', 0)])
        original = self.book.read_bytes()
        move, lines = search(self.engine)
        self.assertEqual(move, 'e2e4')
        self.assertTrue(any('search_result book ' in line for line in lines))
        for option in ('OwnBook value false', 'UCI_AnalyseMode value true'):
            configure(self.engine, 'setoption name ' + option)
            self.assert_searched(search(self.engine)[1])
            configure(self.engine, 'setoption name OwnBook value true')
        self.assertEqual(self.book.read_bytes(), original)

    def test_repetition_draw_clock_and_searchmoves_guards(self):
        board = chess.Board()
        write_book(self.book, [(board, 'e2e4', 100)])
        move, lines = search(self.engine, command='go depth 3 searchmoves d2d4')
        self.assertEqual(move, 'd2d4')
        self.assert_searched(lines)
        self.assert_searched(search(self.engine, 'startpos moves g1f3 g8f6 f3g1 f6g8')[1])
        board.halfmove_clock = 90
        self.assert_searched(search(self.engine, 'fen ' + board.fen())[1])

    def test_original_polyglot_weights(self):
        write_book(self.book, [(chess.Board(), 'e2e4', 1), (chess.Board(), 'd2d4', 9)])
        moves = [search(self.engine, command='go depth 1')[0] for _ in range(200)]
        self.assertEqual(set(moves), {'e2e4', 'd2d4'})
        # Wide bounds distinguish weighted selection from uniform selection.
        self.assertGreater(moves.count('d2d4'), 140)
