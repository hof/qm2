"""Experience accounting, publication, and real-engine integration checks."""
import asyncio
import os
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest

from maxima_runner import experience
from uci_fixture import engine_session

engine_path = os.environ.get('MAXIMA_ENGINE')

import chess
import chess.polyglot


pgn = '''[Event "fixture"]
[Round "1"]
[White "Maxima"]
[Black "Opponent"]
[WhiteEngine "Maxima fixture"]
[Result "1-0"]

1. e4 {[%eval 0.40,18] [%emt 0:01:00] [%nodes 1200000] [%pv e4 e5 Nf3]} e5 1-0

[Event "fixture"]
[Round "2"]
[White "Opponent"]
[Black "Maxima"]
[Result "1-0"]

1. d4 d5 1-0

[Event "fixture"]
[Round "3"]
[White "Opponent"]
[Black "Maxima"]
[Result "1/2-1/2"]

1. e4 e5 1/2-1/2
'''


def search(engine, command='go depth 3', fen=None):
    engine.send('position fen ' + fen if fen else 'position startpos')
    engine.send(command)
    lines = []
    deadline = time.monotonic() + 5
    while True:
        _, line = engine.receive(deadline)
        lines.append(line)
        if line.startswith('bestmove '):
            return line.split()[1], lines


def configure(engine, command):
    engine.send(command)
    engine.send('isready')
    return engine.wait_for('readyok', 5)


class experience_tests(unittest.TestCase):
    def test_import_both_sides_analysis_and_duplicate_persistence(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.pgn'
            path.write_text(pgn)
            db_path = Path(directory) / 'experience.sqlite'
            db = experience.connect(db_path)
            result = experience.import_games(db, path, {'maxima'}, 32)
            self.assertEqual(result['imported'], 3)
            key = experience.key_of(chess.Board())
            self.assertEqual(db.execute('SELECT wins,draws,losses,own_games,opponent_games FROM moves WHERE key=? AND move=?',
                                        (key, 'e2e4')).fetchone(), (1, 1, 0, 1, 1))
            self.assertEqual(db.execute('SELECT wins,losses,opponent_games FROM moves WHERE key=? AND move=?',
                                        (key, 'd2d4')).fetchone(), (1, 0, 1))
            analysis = db.execute('SELECT depth,score,nodes,think_ms,pv FROM analysis').fetchone()
            self.assertEqual(analysis[:4], (18, 40, 1200000, 60000))
            self.assertEqual(json.loads(analysis[4]), ['e2e4', 'e7e5', 'g1f3'])
            db.close()
            db = experience.connect(db_path)
            duplicate = experience.import_games(db, path, {'maxima'}, 32)
            self.assertEqual(duplicate['duplicates'], 3)
            self.assertEqual(db.execute('SELECT SUM(wins+draws+losses) FROM moves').fetchone()[0], 6)
            # A stronger annotation can be imported without recounting the game.
            path.write_text(pgn.replace('0.40,18', '0.60,24'))
            experience.import_games(db, path, {'maxima'}, 32)
            self.assertEqual(db.execute('SELECT depth,score FROM analysis').fetchone(), (24, 60))
            self.assertEqual(db.execute('SELECT SUM(wins+draws+losses) FROM moves').fetchone()[0], 6)
            db.close()

    def test_repetitions_are_one_game_and_import_horizon_can_grow(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.pgn'
            path.write_text('[White "Maxima"]\n[Black "Opponent"]\n[Result "1-0"]\n\n'
                            '1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 Nf6 1-0\n')
            db = experience.connect(Path(directory) / 'experience.sqlite')
            experience.import_games(db, path, {'maxima'}, 1)
            experience.import_games(db, path, {'maxima'}, 32)
            self.assertEqual(db.execute('SELECT SUM(wins+draws+losses) FROM moves').fetchone()[0], 4)
            db.close()

    def test_unfinished_game_and_rollback(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'games.pgn'
            path.write_text(pgn.split('[Event "fixture"]\n[Round "2"]')[0].replace('1-0', '*'))
            db = experience.connect(Path(directory) / 'experience.sqlite')
            experience.import_games(db, path, {'maxima'}, 32)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM moves').fetchone()[0], 0)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM analysis').fetchone()[0], 1)
            before = db.execute('SELECT COUNT(*) FROM games').fetchone()[0]
            path.write_text(pgn + '\n[White "Maxima"]\n[Black "Opponent"]\n\n1. e5 *\n')
            with self.assertRaises(ValueError):
                experience.import_games(db, path, {'maxima'}, 32)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM games').fetchone()[0], before)
            db.close()

    def test_atomic_snapshot_and_book_protection(self):
        with tempfile.TemporaryDirectory() as directory:
            db = experience.connect(Path(directory) / 'experience.sqlite')
            board = chess.Board()
            with self.assertRaises(ValueError):
                experience.store_analysis(db, board, [chess.Move.from_uci('e2e5')], 20, 0, 0, 1000, 60000, 'fixture')
            experience.store_analysis(db, board, [chess.Move.from_uci('g1f3')], 20, -25, 0, 1000, 60000, 'fixture')
            snapshot = Path(directory) / 'experience.bin'
            self.assertEqual(experience.export_snapshot(db, snapshot), 1)
            self.assertEqual(len(snapshot.read_bytes()), 16 + 80)
            row = experience.record.unpack(snapshot.read_bytes()[16:])
            self.assertEqual(row[0], chess.polyglot.zobrist_hash(board))
            self.assertEqual(row[6], -25)
            book = Path(directory) / 'book.bin'
            book.write_bytes(b'original book bytes')
            with self.assertRaisesRegex(ValueError, 'non-experience'):
                experience.export_snapshot(db, book)
            self.assertEqual(book.read_bytes(), b'original book bytes')
            db.close()

    @unittest.skipUnless(engine_path, 'set MAXIMA_ENGINE for real-engine integration')
    def test_engine_uses_analysis_and_keeps_old_snapshot_on_bad_reload(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            db = experience.connect(directory / 'experience.sqlite')
            experience.store_analysis(db, chess.Board(), [chess.Move.from_uci('g1f3')], 24, 25, 0, 1000000, 60000, 'fixture')
            snapshot = directory / 'experience with spaces.bin'
            experience.export_snapshot(db, snapshot)
            engine = engine_session([engine_path], str(directory))
            try:
                engine.send('uci'); engine.wait_for('uciok', 5)
                configure(engine, 'setoption name BookFile value <empty>')
                lines = configure(engine, 'setoption name ExperienceFile value ' + str(snapshot))
                self.assertIn('info string Experience loaded', lines)
                move, lines = search(engine)
                self.assertEqual(move, 'g1f3')
                self.assertTrue(any(' nodes 1 ' in line for line in lines))
                bad = directory / 'bad.bin'
                content = snapshot.read_bytes()
                invalid = [b'bad header', content[:-1], content + b'x',
                           experience.header.pack(experience.magic, 2000001)]
                values = list(experience.record.unpack(content[16:]))
                for field, value in [(1, 5 << 12), (5, 129), (10, 2), (12, 1), (15, 1)]:
                    changed = values.copy(); changed[field] = value
                    invalid.append(content[:16] + experience.record.pack(*changed))
                invalid.append(experience.header.pack(experience.magic, 2) + content[16:] * 2)
                for data in invalid:
                    bad.write_bytes(data)
                    lines = configure(engine, 'setoption name ExperienceFile value ' + str(bad))
                    self.assertTrue(any('load failed' in line for line in lines))
                    self.assertEqual(search(engine)[0], 'g1f3')
                clock_fen = chess.STARTING_FEN.replace('0 1', '1 1')
                self.assertTrue(any(line.startswith('info depth 3 ') for line in search(engine, fen=clock_fen)[1]))
                configure(engine, 'setoption name ExperienceDepth value 25')
                self.assertTrue(any(line.startswith('info depth 3 ') for line in search(engine)[1]))
                configure(engine, 'setoption name ExperienceDepth value 16')
                configure(engine, 'setoption name OwnBook value false')
                self.assertTrue(any(line.startswith('info depth 3 ') for line in search(engine)[1]))
                configure(engine, 'setoption name OwnBook value true')
                # Solved book positions still wait for stop in infinite mode.
                engine.send('position startpos'); engine.send('go infinite')
                while not engine.receive(time.monotonic() + 5)[1].startswith('info depth 1 '):
                    pass
                engine.send('isready')
                lines = engine.wait_for('readyok', 5)
                self.assertFalse(any(line.startswith('bestmove') for line in lines))
                engine.send('stop')
                while not engine.receive(time.monotonic() + 5)[1].startswith('bestmove '):
                    pass
                # A geometrically illegal stored move must never be played.
                snapshot.write_bytes(experience.header.pack(experience.magic, 1) + experience.record.pack(
                    chess.polyglot.zobrist_hash(chess.Board()), 12 | (36 << 6), 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
                configure(engine, 'setoption name ExperienceFile value ' + str(snapshot))
                move, lines = search(engine)
                self.assertNotEqual(move, 'e2e5')
                self.assertTrue(any(line.startswith('info depth 3 ') for line in lines))
            finally:
                engine.close(); db.close()

    @unittest.skipUnless(engine_path, 'set MAXIMA_ENGINE for real-engine integration')
    def test_post_game_analysis_keeps_evidence_without_inventing_results(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            path = directory / 'game.pgn'
            path.write_text('[White "Maxima"]\n[Black "Opponent"]\n[Result "0-1"]\n\n1. f3 0-1\n')
            db = experience.connect(directory / 'experience.sqlite')
            changes = asyncio.run(experience.analyze_games(db, path, Path(engine_path), 1, 50))
            self.assertEqual(db.execute('SELECT COUNT(*) FROM moves').fetchone()[0], 0)
            records = db.execute('SELECT move,depth,nodes,think_ms,pv FROM analysis').fetchall()
            self.assertGreaterEqual(len(records), 1)
            for move, depth, nodes, think_ms, pv in records:
                self.assertGreater(depth, 0)
                self.assertGreaterEqual(nodes, 0)
                self.assertGreater(think_ms, 0)
                board = chess.Board()
                self.assertEqual(json.loads(pv)[0], move)
                for text in json.loads(pv):
                    move = chess.Move.from_uci(text)
                    self.assertIn(move, board.legal_moves)
                    board.push(move)
            if changes:
                self.assertEqual(changes[0]['played'], 'f2f3')
                self.assertNotEqual(changes[0]['candidate'], 'f2f3')
            db.close()

    @unittest.skipUnless(engine_path, 'set MAXIMA_ENGINE for real-engine integration')
    def test_inferior_analysis_is_not_a_book_recommendation(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            db = experience.connect(directory / 'experience.sqlite')
            for move, score in [('g1f3', 30), ('f2f3', -200)]:
                experience.store_analysis(db, chess.Board(), [chess.Move.from_uci(move)],
                                          24, score, 0, 1000000, 60000, 'fixture')
            snapshot = directory / 'experience.bin'
            experience.export_snapshot(db, snapshot)
            engine = engine_session([engine_path], str(directory))
            try:
                engine.send('uci'); engine.wait_for('uciok', 5)
                configure(engine, 'setoption name BookFile value <empty>')
                configure(engine, 'setoption name ExperienceFile value ' + str(snapshot))
                self.assertEqual(search(engine)[0], 'g1f3')
            finally:
                engine.close(); db.close()

    @unittest.skipUnless(engine_path, 'set MAXIMA_ENGINE for real-engine integration')
    def test_special_move_snapshot_encoding(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            db = experience.connect(directory / 'experience.sqlite')
            cases = [
                ('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1', 'e1g1'),
                ('7k/P7/8/8/8/8/8/7K w - - 0 1', 'a7a8n'),
                ('7k/8/8/3pP3/8/8/8/K7 w - d6 0 1', 'e5d6'),
            ]
            for fen, move in cases:
                experience.store_analysis(db, chess.Board(fen), [chess.Move.from_uci(move)],
                                          24, 0, 0, 1000000, 60000, 'fixture')
            snapshot = directory / 'experience.bin'
            experience.export_snapshot(db, snapshot)
            engine = engine_session([engine_path], str(directory))
            try:
                engine.send('uci'); engine.wait_for('uciok', 5)
                configure(engine, 'setoption name BookFile value <empty>')
                configure(engine, 'setoption name ExperienceFile value ' + str(snapshot))
                for fen, expected in cases:
                    self.assertEqual(search(engine, fen=fen)[0], expected)
            finally:
                engine.close(); db.close()

    @unittest.skipUnless(engine_path, 'set MAXIMA_ENGINE for real-engine integration')
    def test_book_option_and_file_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            key = chess.polyglot.zobrist_hash(chess.Board())
            book = directory / 'reference book.bin'
            content = struct.pack('>QHHI', key, (12 << 6) | 28, 255, 0)
            book.write_bytes(content)
            engine = engine_session([engine_path], str(directory))
            try:
                engine.send('uci'); engine.wait_for('uciok', 5)
                configure(engine, 'setoption name BookFile value ' + str(book))
                self.assertEqual(search(engine)[0], 'e2e4')
                configure(engine, 'setoption name OwnBook value false')
                self.assertTrue(any(line.startswith('info depth 3 ') for line in search(engine)[1]))
                configure(engine, 'setoption name OwnBook value true')
                configure(engine, 'setoption name UCI_AnalyseMode value true')
                self.assertTrue(any(line.startswith('info depth 3 ') for line in search(engine)[1]))
                self.assertEqual(book.read_bytes(), content)
            finally:
                engine.close()


if __name__ == '__main__':
    unittest.main()
