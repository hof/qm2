"""Learning digests: eval and result values per move, index cache, player stats, and selection."""
import json
from pathlib import Path
import tempfile
import unittest

import chess
import chess.engine
import chess.pgn

from maxima_runner import learning

line = "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6a5".split()


def make_game(our_scores, ours="white", result="1-0", opponent_headers=None, plies=len(line)):
    """A game where our searched moves carry the given centipawn scores (our point of view)."""
    game = chess.pgn.Game()
    our_color = chess.WHITE if ours == "white" else chess.BLACK
    prefix, other = ("White", "Black") if ours == "white" else ("Black", "White")
    game.headers.update({"Event": "FICS rated blitz game", "White": "Alpha", "Black": "Beta", "Result": result,
                         "UTCDate": "2026.10.21", "UTCTime": "20:00:00", "TimeControl": "180+0",
                         f"{prefix}Type": "program", f"{prefix}Engine": "Maxima 2.0.0", f"{other}Elo": "1500"})
    game.headers.update(opponent_headers or {})
    node, board, scores = game, chess.Board(), iter(our_scores)
    for text in line[:plies]:
        move = chess.Move.from_uci(text)
        mover = board.turn
        node = node.add_variation(move)
        if mover == our_color:
            score = next(scores, None)
            if score is not None:
                node.set_eval(chess.engine.PovScore(chess.engine.Cp(score), mover), 12)
                node.set_emt(2.5)
        board.push(move)
    return game


class digest_tests(unittest.TestCase):
    def test_expected_score_curve(self):
        self.assertAlmostEqual(learning.expected_score(0), 0.5)
        self.assertAlmostEqual(learning.expected_score(100), 0.64, places=2)
        self.assertAlmostEqual(learning.expected_score(-300), 0.15, places=2)

    def test_each_move_gets_the_following_evals_and_the_result_from_its_movers_view(self):
        record = learning.digest(make_game([0, 100, 100, 100, 100, 300, 300, 300, 300], result="0-1"))
        values = {move: (evaluated, result) for _, move, evaluated, result in record["values"]}
        mean = (0.5 + 3 * learning.expected_score(100)) / 4
        self.assertAlmostEqual(values["e2e4"][0], mean, places=3)
        self.assertAlmostEqual(values["e7e5"][0], 1 - learning.expected_score(100), places=3)
        self.assertAlmostEqual(values["g1f3"][0], learning.expected_score(100), places=3)
        self.assertEqual((values["e2e4"][1], values["e7e5"][1]), (0.0, 1.0))
        self.assertEqual(record["score"], 0.0)
        self.assertEqual(record["analyses"][0][1:], ["e2e4", 12, 0, 0, 2500, 0])

    def test_unfinished_games_keep_evals_only_and_book_tails_keep_results_only(self):
        record = learning.digest(make_game([0, 0], result="*"))
        self.assertEqual({result for *_, result in record["values"]}, {None})
        played = learning.digest(make_game([], result="1-0"))
        self.assertEqual({(evaluated, result) for _, move, evaluated, result in played["values"]
                          if line.index(move) % 2 == 0}, {(None, 1.0)})

    def test_groups_and_unknown_games(self):
        self.assertEqual(learning.digest(make_game([0], ours="black", opponent_headers={"WhiteTitle": "GM"}))["group"],
                         "masters")
        self.assertEqual(learning.digest(make_game([0], opponent_headers={"BlackType": "program"}))["group"], "engines")
        self.assertIsNone(learning.digest(make_game([0], opponent_headers={"BlackAccount": "unregistered"}))["group"])
        self.assertEqual(learning.digest(make_game([0]))["group"], "humans")
        self.assertIsNone(learning.digest(make_game([0], opponent_headers={"BlackType": "program",
                                                                           "BlackEngine": "Other"})))


class index_tests(unittest.TestCase):
    def test_cache_refreshes_when_a_file_grows_and_writes_player_stats(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            games = root / "games" / "fics"
            games.mkdir(parents=True)
            path = games / "2026-10.pgn"
            path.write_text(str(make_game([0] * 9, result="1/2-1/2")) + "\n\n")
            self.assertEqual(learning.build(root)["games"], 1)
            with path.open("a") as stream:
                stream.write(str(make_game([20, 80, 320, 400])) + "\n\n")
            summary = learning.build(root)
            self.assertEqual((summary["games"], summary["players"]), (2, 1))
            stats = json.loads((root / "players" / "beta.json").read_text())
            self.assertEqual((stats["games"], stats["wins"], stats["draws"], stats["rating"]), (2, 1, 1, 1500))
            self.assertEqual(stats["by_type"]["blitz"]["games"], 2)


if __name__ == "__main__":
    unittest.main()


def polyglot_book(path, entries):
    """Write a Polyglot book: entries of (board, uci move, weight)."""
    import struct
    rows = []
    for board, text, weight in entries:
        move = chess.Move.from_uci(text)
        code = (chess.square_file(move.to_square) | chess.square_rank(move.to_square) << 3
                | chess.square_file(move.from_square) << 6 | chess.square_rank(move.from_square) << 9)
        rows.append((chess.polyglot.zobrist_hash(board), code, weight))
    with open(path, "wb") as stream:
        for key, code, weight in sorted(rows):
            stream.write(struct.pack(">QHHI", key, code, weight, 0))


def record(results, opponent="John", group="humans", game_type="blitz", analyses=()):
    """A game record whose moves carry only a result value, so draws follow the contempt."""
    return {"opponent": opponent, "group": group, "type": game_type,
            "values": [[key, move, None, result] for key, move, result in results], "analyses": list(analyses)}


start_key = f"{chess.polyglot.zobrist_hash(chess.Board()):016x}"


class selection_tests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.book = Path(directory.name) / "book.bin"
        polyglot_book(self.book, [(chess.Board(), "e2e4", 100), (chess.Board(), "d2d4", 100),
                                  (chess.Board(), "g2g4", 1)])

    def picks(self, records, trials=300, **options):
        options.setdefault("opponent", "John")
        options.setdefault("group", "humans")
        options.setdefault("game_type", "blitz")
        book = learning.opening_book(records, book_path=self.book, random_source=__import__("random").Random(7),
                                     **options)
        counts = {}
        for _ in range(trials):
            move, reason = book.choose(chess.Board(), 5000)
            counts[move.uci()] = counts.get(move.uci(), 0) + 1
        return counts, reason

    def test_without_experience_it_plays_book_by_weight_and_skips_rare_moves(self):
        counts, reason = self.picks([])
        self.assertEqual(reason, "book")
        self.assertEqual(set(counts), {"e2e4", "d2d4"})

    def test_winning_line_against_opponent_is_repeated_but_others_still_tried(self):
        wins = [record([[start_key, "e2e4", 1.0]]) for _ in range(3)]
        counts, reason = self.picks(wins)
        self.assertTrue(reason.startswith("learned"))
        self.assertGreater(counts["e2e4"], 200)
        self.assertGreater(counts.get("d2d4", 0), 0)
        narrow, _ = self.picks(wins, variety=0.2)
        self.assertGreater(narrow["e2e4"], counts["e2e4"])

    def test_layers_shrink_toward_the_layer_below(self):
        records = [record([[start_key, "e2e4", 1.0]], opponent="Other") for _ in range(4)]
        records.append(record([[start_key, "e2e4", 0.0]]))
        john = learning.opening_book(records, opponent="John", group="humans", game_type="blitz")
        stranger = learning.opening_book(records, opponent="Nobody", group="humans", game_type="blitz")
        rate, _, games = john.estimate(start_key, "e2e4")
        self.assertEqual(games, 5)
        self.assertLess(rate, stranger.estimate(start_key, "e2e4")[0])
        self.assertGreater(rate, 0.5)
        guest = learning.opening_book([record([[start_key, "e2e4", 1.0]], group=None)], game_type="blitz")
        self.assertEqual(guest.layers[0][(start_key, "e2e4")], [0.5, 0.0, 0.5])
        self.assertEqual(guest.layers[1], {})
        other_type = learning.opening_book([record([[start_key, "e2e4", 1.0]])], game_type="standard")
        self.assertEqual(other_type.layers[0][(start_key, "e2e4")], [0.5, 0.0, 0.5])

    def test_contempt_prefers_drawing_lines_against_stronger_and_sharp_ones_against_weaker(self):
        drawish = [record([[start_key, "d2d4", 0.5]]) for _ in range(6)]
        sharp = [record([[start_key, "e2e4", value]]) for value in (1.0, 1.0, 1.0, 0.0, 0.0, 0.0)]
        stronger, _ = self.picks(drawish + sharp, contempt=100)
        weaker, _ = self.picks(drawish + sharp, contempt=-100)
        self.assertGreater(stronger.get("d2d4", 0), stronger.get("e2e4", 0))
        self.assertGreater(weaker.get("e2e4", 0), weaker.get("d2d4", 0))

    def test_deeper_analysis_supersedes_and_is_recalled_within_budget(self):
        board = chess.Board()
        for text in "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6".split():
            board.push_uci(text)
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        records = [record([[key, "f3g5", 1.0]], analyses=[[key, "f3g5", 13, 90, 0, 5000, board.halfmove_clock]])
                   for _ in range(3)]
        records.append(record([[key, "h2h4", 0.5]], analyses=[[key, "h2h4", 20, 40, 0, 60000,
                                                                  board.halfmove_clock]]))
        book = learning.opening_book(records, opponent="John", group="humans", game_type="blitz")
        self.assertTrue(book.superseded(book.analyses[key], chess.Move.from_uci("f3g5")))
        choices = {book.choose(board, 5000)[0].uci() for _ in range(50)}
        self.assertEqual(choices, {"h2h4"})
        fresh = learning.opening_book(records[:3] + [record([], analyses=[[key, "h2h4", 20, 40, 0, 60000,
                                                                          board.halfmove_clock]])],
                                      game_type="blitz")
        self.assertEqual(fresh.choose(board, 5000), (chess.Move.from_uci("h2h4"), "recalled d20 60.0s"))
        self.assertIsNone(fresh.choose(board, 90000))

    def test_out_of_horizon_and_repeated_positions_are_searched(self):
        book = learning.opening_book([], book_path=self.book)
        board = chess.Board()
        for text in "g1f3 g8f6 f3g1 f6g8".split():
            board.push_uci(text)
        self.assertIsNone(book.choose(board, 5000))


class multi_layer_book_tests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        source = self.root / "fire.pgn"
        source.write_text('[Event "Italian"]\n[Repertoire "white"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. O-O *\n\n'
                          '[Event "KG"]\n[Repertoire "white"]\n\n1. e4 e5 2. f4 exf4 *\n\n'
                          '[Event "Najdorf"]\n[Repertoire "black"]\n\n1. e4 c5 2. Nf3 d6 *\n\n')
        self.fire = self.root / "fire.bin"
        self.assertEqual(learning.build_repertoire(source, self.fire), 7)
        self.main = self.root / "book.bin"
        board = chess.Board()
        board.push_uci("e2e4")
        board.push_uci("e7e5")
        polyglot_book(self.main, [(board, "g1f3", 100)])
        self.after_e5 = board

    def test_repertoire_holds_only_our_side_and_encodes_castling(self):
        board = chess.Board()
        for text in "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6".split():
            board.push_uci(text)
        with chess.polyglot.open_reader(str(self.fire)) as reader:
            self.assertEqual([entry.move.uci() for entry in reader.find_all(board)], ["e1g1"])
            self.assertEqual([entry.move.uci() for entry in reader.find_all(chess.Board())], ["e2e4"])
            black = chess.Board()
            black.push_uci("e2e4")
            self.assertEqual([entry.move.uci() for entry in reader.find_all(black)], ["c7c5"])
            self.assertEqual({entry.move.uci(): entry.weight for entry in reader.find_all(self.after_e5)},
                             {"g1f3": 1, "f2f4": 1})

    def test_style_follows_contempt_and_unrated_opponents_experiment(self):
        books = {"fire": self.fire, "solid": self.root / "missing.bin", "experiment": self.root / "none.bin"}
        self.assertEqual(learning.choose_style(-100, books), ("fire", 1.0))
        self.assertEqual(learning.choose_style(-60, books), ("fire", 0.5))
        self.assertEqual(learning.choose_style(-10, books), ("fire", 0.0))
        self.assertEqual(learning.choose_style(100, books), ("solid", 0.0))
        self.assertEqual(learning.choose_style(None, books), ("fire", 0.5))
        self.assertEqual(learning.choose_style(None, dict(books, experiment=self.fire)), ("experiment", 0.5))

    def test_style_book_is_blended_over_book_bin(self):
        books = {"fire": self.fire}
        for contempt, expected in ((-10, {"g1f3": 1.0}), (-60, {"g1f3": 0.75, "f2f4": 0.25}),
                                   (-100, {"g1f3": 0.5, "f2f4": 0.5})):
            book = learning.opening_book([], book_path=self.main, contempt=contempt, style_books=books)
            shares = {move.uci(): share for move, share in book.book_moves(self.after_e5).items() if share}
            self.assertEqual(shares, expected)
        fire = learning.opening_book([], book_path=self.main, contempt=-100, style_books=books)
        reasons = {fire.choose(self.after_e5, 5000)[1] for _ in range(40)}
        self.assertEqual(reasons, {"book fire 100%"})
