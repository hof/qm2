"""Known terminal games certify filtering, pre-move labels, WDL and resume integrity."""
import asyncio
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

import chess
import chess.engine
import chess.pgn

from maxima_runner import nnue_data, nnue_selfplay


class fools_mate_engine:
    async def search(self, board, limit, *, game, on_info):
        move = chess.Move.from_uci(["f2f3", "e7e5", "g2g4", "d8h4"][board.ply()])
        score = chess.engine.Mate(1) if board.ply() == 3 else chess.engine.Cp(10)
        on_info({"depth": 3, "nodes": 10000, "pv": [move],
                 "score": chess.engine.PovScore(score, board.turn)}, 1)
        on_info({"string": "search_result complete completed_depth 3 attempted_depth 3"}, 1)
        return SimpleNamespace(move=move), 1


class nnue_selfplay_tests(unittest.TestCase):
    def config(self, max_plies=20):
        return {"seed": 1, "nodes": 10000, "random_plies": 0, "max_plies": max_plies,
                "book_plies": 0, "min_ply": 0, "score_limit": 1000, "network_sha256": "test"}

    def test_completed_game_labels_have_correct_root_and_final_result_perspective(self):
        text, rows, filters, plies = asyncio.run(nnue_selfplay.play_game(
            fools_mate_engine(), self.config(), 1, set()))
        self.assertEqual(plies, 4)
        self.assertIn('[Result "0-1"]', text)
        self.assertEqual([row["result_stm"] for row in rows], [0, 1, 0])
        self.assertEqual([row["source"]["ply"] for row in rows], [0, 1, 2])
        self.assertEqual(rows[0]["fen"], chess.STARTING_FEN)
        self.assertEqual(rows[0]["source"]["sha256"], hashlib.sha256(text.encode()).hexdigest())
        self.assertEqual(filters["no_exact_cp"], 1)
        self.assertEqual(len({row["split"] for row in rows}), 1)

    def test_excluded_position_is_removed_before_either_split_is_published(self):
        config = self.config()
        # Position identity ignores clocks, including for reserved evaluation rows.
        reserved = chess.Board()
        reserved.fullmove_number = 42
        row = {"fen": reserved.fen(), "score_cp": 0, "result_stm": 0.5,
               "split": nnue_data.position_split(reserved),
               "source": {"sha256": "b" * 64, "game": 1, "ply": 0}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reserved.mxdata"
            nnue_data.save_dataset(path, {"variant": "standard", "score_pov": "side_to_move",
                                          "purpose": "heldout_evaluation"}, [row])
            excluded, hashes = nnue_selfplay.dataset_exclusions([path, path])
            self.assertEqual(hashes, [nnue_data.file_hash(path)])
            _, rows, filters, _ = asyncio.run(nnue_selfplay.play_game(
                fools_mate_engine(), config, 1, excluded))
        self.assertEqual([row["source"]["ply"] for row in rows], [1, 2])
        self.assertEqual(filters["duplicate"], 1)
        self.assertTrue(all(nnue_data.position_key(chess.Board(row["fen"])) not in excluded for row in rows))

    def test_unfinished_game_does_not_invent_wdl_samples(self):
        text, rows, filters, _ = asyncio.run(nnue_selfplay.play_game(
            fools_mate_engine(), self.config(max_plies=3), 1, set()))
        self.assertIn('[Result "*"]', text)
        self.assertEqual(rows, [])
        self.assertEqual(filters["unfinished_samples_discarded"], 3)

    def test_quiet_filter_rejects_checks_captures_promotions_and_duplicates(self):
        label = {"score_cp": 20, "depth": 9, "nodes": 10000}
        for fen, move, expected in [
            ("7k/8/8/8/8/8/P7/K7 w - - 0 9", "a2a3", None),
            ("7k/8/8/8/8/1p6/P7/K7 w - - 0 9", "a2b3", "tactical"),
            ("7k/P7/8/8/8/8/8/K7 w - - 0 9", "a7a8q", "tactical"),
            ("7k/8/8/8/8/8/r7/K7 w - - 0 9", "a1b1", "check"),
        ]:
            board = chess.Board(fen)
            self.assertEqual(nnue_selfplay.usable_position(board, chess.Move.from_uci(move), label,
                                                          min_ply=16, score_limit=1000, seen=set()), expected)
        board = chess.Board("7k/8/8/8/8/8/P7/K7 w - - 0 9")
        self.assertEqual(nnue_selfplay.usable_position(board, chess.Move.from_uci("a2a3"), label,
                                                      min_ply=16, score_limit=1000,
                                                      seen={nnue_data.position_key(board)}), "duplicate")

    def test_resume_validates_whole_game_artifacts(self):
        config = self.config()
        text, rows, _, _ = asyncio.run(nnue_selfplay.play_game(fools_mate_engine(), config, 1, set()))
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            game = run / "game-000001"
            game.mkdir()
            (game / "game.pgn").write_text(text)
            nnue_data.save_dataset(game / "positions.mxdata",
                                   {"variant": "standard", "score_pov": "side_to_move", "split_method": "game"}, rows)
            report = {"game": 1, "config": config, "positions": len(rows),
                      "pgn_sha256": nnue_data.file_hash(game / "game.pgn"),
                      "data_sha256": nnue_data.file_hash(game / "positions.mxdata")}
            (game / "report.json").write_text(json.dumps(report))
            restored, seen, games = nnue_selfplay.load_run(run, config)
            self.assertEqual(len(restored), 3)
            self.assertEqual(len(seen), 3)
            self.assertEqual(len(games), 1)
            with self.assertRaisesRegex(ValueError, "excluded position"):
                nnue_selfplay.load_run(run, config, {nnue_data.position_key(chess.Board(rows[0]["fen"]))})
            (game / "game.pgn").write_text("damaged")
            with self.assertRaises(ValueError):
                nnue_selfplay.load_run(run, config)

    def test_random_opening_is_reproducible_and_legal(self):
        first = nnue_selfplay.opening_moves(2026, 1, 8)
        self.assertEqual(first, nnue_selfplay.opening_moves(2026, 1, 8))
        board = chess.Board()
        for move in first:
            self.assertIn(move, board.legal_moves)
            board.push(move)
        self.assertEqual(len(first), 8)

    def test_book_guidance_leaves_actual_random_exploration(self):
        class forced_book:
            def __init__(self):
                self.calls = 0

            def find_all(self, board):
                self.calls += 1
                return [SimpleNamespace(move=next(iter(board.legal_moves)), weight=1)]

        book = forced_book()
        first = nnue_selfplay.opening_moves(2026, 1, 8, book, book_plies=4)
        self.assertEqual(book.calls, 4)
        second = nnue_selfplay.opening_moves(2026, 2, 8, forced_book(), book_plies=4)
        self.assertEqual(first[:4], second[:4])
        self.assertNotEqual(first[4:], second[4:])
