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


def board_after(text):
    board = chess.Board()
    for move in text.split():
        board.push_uci(move)
    return board


class selection_tests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.book = self.root / "book.bin"
        polyglot_book(self.book, [(chess.Board(), "e2e4", 100), (chess.Board(), "d2d4", 100),
                                  (chess.Board(), "g2g4", 1)])

    def opening(self, records=(), seed=7, **options):
        options.setdefault("opponent", "John")
        options.setdefault("group", "humans")
        options.setdefault("game_type", "blitz")
        options.setdefault("book_path", self.book)
        book = learning.opening_book(list(records), random_source=__import__("random").Random(seed), **options)
        book.search_ply = None
        return book

    def picks(self, records=(), trials=400, board=None, **options):
        book = self.opening(records, **options)
        counts, reasons = {}, set()
        for _ in range(trials):
            move, reason = book.choose(board or chess.Board(), 5000)
            counts[move.uci()] = counts.get(move.uci(), 0) + 1
            reasons.add(reason.split(":")[0].split()[0])
        return counts, reasons

    def test_books_ignore_do_not_play_weights(self):
        board = board_after("d2d4 d7d5")
        polyglot_book(self.book, [(board, "c2c4", 1), (board, "h2h3", 1), (board, "g1f3", 0)])
        self.assertEqual(learning.opening_book.read_book(self.book, board), {})
        self.assertIsNone(self.opening().choose(board, 5000))

    def test_without_experience_it_plays_book_by_weight_and_skips_rare_moves(self):
        counts, reasons = self.picks()
        self.assertEqual(reasons, {"book"})
        self.assertEqual(set(counts), {"e2e4", "d2d4"})
        move, reason = self.opening().choose(chess.Board(), 5000)
        self.assertEqual(reason, "book 50%: book 100%")

    def test_good_line_against_opponent_grows_but_book_keeps_its_floor(self):
        few, _ = self.picks([record([[start_key, "e2e4", 1.0]]) for _ in range(2)])
        many, reasons = self.picks([record([[start_key, "e2e4", 1.0]]) for _ in range(30)])
        self.assertGreater(many["e2e4"], few["e2e4"])
        self.assertGreater(many.get("d2d4", 0), 40)  # book.bin's 45% still gives d4 its half
        self.assertIn("learned", reasons)
        _, reason = self.opening([record([[start_key, "e2e4", 1.0]]) for _ in range(30)]).choose(chess.Board(), 5000)
        self.assertRegex(reason, r"^(book|learned) \d+%: book \d+%, global \d+%, humans \d+%, John \d+%$")

    def test_proven_losers_drop_even_in_book(self):
        counts, _ = self.picks([record([[start_key, "e2e4", 0.0]]) for _ in range(8)])
        self.assertLess(counts.get("e2e4", 0), 25)

    def test_moves_only_experience_knows_are_tried(self):
        board = board_after("e2e4")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        polyglot_book(self.book, [(board, "e7e5", 100)])
        counts, _ = self.picks([record([[key, "c7c5", 1.0]], opponent="Other") for _ in range(6)], board=board)
        self.assertGreater(counts.get("c7c5", 0), 0)
        self.assertGreater(counts["e7e5"], counts["c7c5"])

    def test_layers_shrink_toward_the_layer_below(self):
        records = [record([[start_key, "e2e4", 1.0]], opponent="Other") for _ in range(4)]
        records.append(record([[start_key, "e2e4", 0.0]]))
        john = self.opening(records)
        stranger = self.opening(records, opponent="Nobody")
        rate, games = john.estimate(start_key, "e2e4")
        self.assertEqual(games, 5)
        self.assertLess(rate, stranger.estimate(start_key, "e2e4")[0])
        self.assertGreater(rate, 0.5)
        guest = self.opening([record([[start_key, "e2e4", 1.0]], group=None)])
        self.assertEqual(guest.layers[0][(start_key, "e2e4")], [0.5, 0.5])
        self.assertEqual(guest.layers[1], {})
        other_type = self.opening([record([[start_key, "e2e4", 1.0]])], game_type="standard")
        self.assertEqual(other_type.layers[0][(start_key, "e2e4")], [0.5, 0.5])

    def test_evals_weigh_more_than_results(self):
        book = self.opening()
        self.assertAlmostEqual(book.value(0.47, 1.0), 0.8 * 0.47 + 0.2)
        self.assertEqual(book.value(None, 1.0), 1.0)
        self.assertEqual(book.value(0.6, None), 0.6)
        self.assertAlmostEqual(self.opening(contempt=-100).value(None, 0.5), 0.05)

    def test_contempt_prefers_drawing_lines_against_stronger_and_sharp_ones_against_weaker(self):
        drawish = [record([[start_key, "d2d4", 0.5]]) for _ in range(12)]
        sharp = [record([[start_key, "e2e4", value]]) for value in (1.0, 0.0) * 6]
        stronger, _ = self.picks(drawish + sharp, contempt=100)
        weaker, _ = self.picks(drawish + sharp, contempt=-100)
        self.assertGreater(stronger.get("d2d4", 0), stronger.get("e2e4", 0))
        self.assertGreater(weaker.get("e2e4", 0), weaker.get("d2d4", 0))

    def test_deeper_analysis_supersedes_and_is_recalled_within_budget(self):
        board = board_after("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        records = [record([[key, "f3g5", 1.0]], analyses=[[key, "f3g5", 13, 90, 0, 5000, board.halfmove_clock]])
                   for _ in range(3)]
        records.append(record([[key, "h2h4", 0.5]], analyses=[[key, "h2h4", 20, 40, 0, 60000,
                                                                  board.halfmove_clock]]))
        book = self.opening(records, book_path=None)
        self.assertTrue(book.superseded(book.analyses[key], chess.Move.from_uci("f3g5")))
        self.assertEqual({book.choose(board, 5000)[0].uci() for _ in range(50)}, {"h2h4"})
        fresh = self.opening(records[:3] + [record([], analyses=[[key, "h2h4", 20, 40, 0, 60000,
                                                                 board.halfmove_clock]])], book_path=None)
        self.assertEqual(fresh.choose(board, 5000), (chess.Move.from_uci("h2h4"), "recalled d20 60.0s"))
        self.assertIsNone(fresh.choose(board, 90000))

    def test_out_of_book_a_learned_move_needs_evidence(self):
        board = board_after("e2e4 e7e5 g1f3 b8c6")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        quick = [record([[key, "f1b5", 0.6]], analyses=[[key, "f1b5", 8, 30, 0, 300, 2]])]
        self.assertIsNone(self.opening(quick).choose(board, 5000))
        proven = self.opening(quick * 3).choose(board, 5000)
        self.assertEqual(proven[0].uci(), "f1b5")
        self.assertTrue(proven[1].startswith("learned"))

    def test_out_of_horizon_and_repeated_positions_are_searched(self):
        book = self.opening()
        self.assertIsNone(book.choose(board_after("g1f3 g8f6 f3g1 f6g8"), 5000))

    def test_fire_tolerance_follows_rating_gap_and_rated_games(self):
        self.assertEqual(learning.fire_tolerance(None, False), 50)
        self.assertEqual(learning.fire_tolerance(-10, True), 15)
        self.assertEqual(learning.fire_tolerance(-60, True), 32.5)
        self.assertEqual(learning.fire_tolerance(-100, True), 50)
        self.assertEqual(learning.fire_tolerance(100, True), 10)
        self.assertEqual(learning.fire_tolerance(-100, True, {"weaker": 30}), 30)

    def test_checked_style_moves_are_vetoed_by_tolerance(self):
        board = board_after("e2e4 e7e5")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        polyglot_book(self.book, [(board, "g1f3", 100)])
        fire = self.root / "fire.bin"
        polyglot_book(fire, [(board, "f2f4", 100)])
        checks = {f"{key} f2f4 g1f3": 40}
        guest = self.opening(rated=False, style_books={"fire": fire}, checks=checks)
        self.assertIsNone(guest.needs_check(board, chess.Move.from_uci("f2f4")))
        self.assertIn(chess.Move.from_uci("f2f4"), guest.distribution(board, key, {})[0])
        equal = self.opening(contempt=-60, style_books={"fire": fire}, checks=checks)
        self.assertNotIn(chess.Move.from_uci("f2f4"), equal.distribution(board, key, {})[0])
        fresh = self.opening(rated=False, style_books={"fire": fire})
        self.assertEqual(fresh.needs_check(board, chess.Move.from_uci("f2f4")), chess.Move.from_uci("g1f3"))
        self.assertIsNone(fresh.needs_check(board, chess.Move.from_uci("g1f3")))
        self.assertTrue(fresh.record_check(board, chess.Move.from_uci("f2f4"), chess.Move.from_uci("g1f3"), 20))

    def test_search_layer_leaves_the_book_once_per_game(self):
        book = self.opening(profiles={name: dict(weights, search=100)
                                      for name, weights in learning.layer_profiles.items()})
        book.search_ply = 0
        self.assertIsNone(book.choose(chess.Board(), 5000))
        self.assertIsNotNone(book.choose(chess.Board(), 5000))
        def profiles(search):
            return {name: dict(weights, search=search) for name, weights in learning.layer_profiles.items()}
        always = [learning.opening_book([], random_source=__import__("random").Random(seed),
                                        profiles=profiles(100)).search_ply for seed in range(20)]
        self.assertTrue(all(ply is not None and 0 <= ply < learning.horizon_plies for ply in always))
        never = [learning.opening_book([], random_source=__import__("random").Random(seed),
                                       profiles=profiles(0)).search_ply for seed in range(20)]
        self.assertEqual(set(never), {None})


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
        self.after_e5 = board_after("e2e4 e7e5")
        polyglot_book(self.main, [(self.after_e5, "g1f3", 90), (self.after_e5, "b1c3", 10)])

    def test_repertoire_holds_only_our_side_and_encodes_castling(self):
        with chess.polyglot.open_reader(str(self.fire)) as reader:
            castle = board_after("e2e4 e7e5 g1f3 b8c6 f1c4 g8f6")
            self.assertEqual([entry.move.uci() for entry in reader.find_all(castle)], ["e1g1"])
            self.assertEqual([entry.move.uci() for entry in reader.find_all(chess.Board())], ["e2e4"])
            self.assertEqual([entry.move.uci() for entry in reader.find_all(board_after("e2e4"))], ["c7c5"])
            self.assertEqual({entry.move.uci(): entry.weight for entry in reader.find_all(self.after_e5)},
                             {"g1f3": 100, "f2f4": 100})

    def test_profiles_follow_rating_gap_and_unrated_games_play_fire_and_experiment(self):
        profiles = learning.layer_profiles
        self.assertEqual(learning.game_profile(-10, True), profiles["equal"])
        self.assertEqual(learning.game_profile(-100, True), profiles["weaker"])
        self.assertEqual(learning.game_profile(-60, True)["fire"], 12.5)
        self.assertEqual(learning.game_profile(100, True)["solid"], 25)
        self.assertEqual(learning.game_profile(None, True), profiles["equal"])
        unrated = learning.game_profile(-100, False)
        self.assertEqual(unrated, profiles["unrated"])
        self.assertGreaterEqual(unrated["fire"], unrated["book"])
        for weights in profiles.values():
            self.assertEqual(sum(weights[name] for name in learning.layer_names), 100)

    def shares(self, **options):
        book = learning.opening_book([], book_path=self.main, style_books={"fire": self.fire}, **options)
        book.search_ply = None
        moves, in_book = book.distribution(self.after_e5, "", {})
        self.assertTrue(in_book)
        return {move.uci(): round(probability, 3) for move, (probability, _) in moves.items()}, book

    def test_style_books_pull_but_book_bin_keeps_its_share(self):
        self.assertEqual(self.shares(contempt=-10)[0], {"g1f3": 0.9, "b1c3": 0.1})
        weaker, book = self.shares(contempt=-100)
        self.assertEqual(set(book.paths), {"book", "fire"})
        self.assertEqual(weaker, {"g1f3": round((20 * 0.9 + 25 * 0.5) / 45, 3), "b1c3": round(2 / 45, 3),
                                  "f2f4": round(12.5 / 45, 3)})
        self.assertEqual(set(self.shares(contempt=100)[1].paths), {"book"})
        guest, book = self.shares(rated=False)
        self.assertEqual(guest["f2f4"], 0.25)
        moves, _ = book.distribution(self.after_e5, "", {})
        self.assertEqual(book.describe(*moves[chess.Move.from_uci("g1f3")]), "book 70%: book 64%, fire 36%")

if __name__ == "__main__":
    unittest.main()
