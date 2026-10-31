"""Learning digests: eval and result values per move, index cache, player stats, and selection."""
import json
from pathlib import Path
import tempfile
import unittest

import chess
import chess.engine
import chess.pgn

from maxima_runner import learning

line = ("e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3 e8g8 h2h3 c6a5 b3c2 c7c5 "
        "d2d4").split()


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
        self.assertEqual(record["analyses"][0][1:], ["e2e4", 12, 0, 0, None, 0, None, None])

    def test_unfinished_games_keep_evals_only_and_book_tails_keep_results_only(self):
        record = learning.digest(make_game([0] * 4, result="*"))
        self.assertEqual({result for *_, result in record["values"]}, {None})
        played = learning.digest(make_game([0] * 4, result="1-0"))
        self.assertEqual({(evaluated, result) for _, move, evaluated, result in played["values"]
                          if line.index(move) % 2 == 0 and line.index(move) >= 8}, {(None, 1.0)})

    def test_short_and_thoughtless_games_are_ignored(self):
        self.assertIsNone(learning.digest(make_game([0] * 10, plies=19)))
        self.assertIsNone(learning.digest(make_game([None] * 7 + [0] * 3)))
        self.assertIsNotNone(learning.digest(make_game([None] * 6 + [0] * 4, plies=20)))

    def test_groups_and_unknown_games(self):
        self.assertEqual(learning.digest(make_game([0] * 4, ours="black", opponent_headers={"WhiteTitle": "GM"}))["group"],
                         "masters")
        self.assertEqual(learning.digest(make_game([0] * 4, opponent_headers={"BlackType": "program"}))["group"], "engines")
        self.assertIsNone(learning.digest(make_game([0] * 4, opponent_headers={"BlackAccount": "unregistered"}))["group"])
        self.assertEqual(learning.digest(make_game([0] * 4))["group"], "humans")
        self.assertIsNone(learning.digest(make_game([0] * 4, opponent_headers={"BlackType": "program",
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
                other = make_game([20, 80, 320, 400])
                other.headers["UTCTime"] = "20:00:01"
                stream.write(str(other) + "\n\n")
            summary = learning.build(root)
            self.assertEqual((summary["games"], summary["players"]), (2, 1))
            stats = json.loads((root / "players" / "freechess.org" / "beta.json").read_text())
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
    def test_move_results_count_games_without_weights_or_repetitions(self):
        repeated = record([[start_key, "e2e4", 1.0]] * 2, group="masters", game_type="rapid")
        records = [repeated, record([[start_key, "e2e4", 0.5]], group="masters"),
                   record([[start_key, "e2e4", 0.0]], group="engines"),
                   record([[start_key, "e2e4", None]], group="masters")]
        records[-1]["values"][0][2] = 0.6
        for item in records:
            item.update(color="white", moves=[[start_key, "e2e4"]])
        book = self.opening(records, group="masters")
        pair = (start_key, "e2e4")
        self.assertEqual(book.results[0][pair], [1, 1, 1])
        self.assertEqual(book.results[1][pair], [1, 1, 0])
        self.assertEqual(book.results[2][pair], [1, 1, 1])
        board = chess.Board()
        for _ in range(20):
            found = book.choose(board)
            if found and found[0].uci() == "e2e4":
                break
        self.assertEqual(book.choice_results, {"global": (1, 1, 1), "group": (1, 1, 0), "opponent": (1, 1, 1)})
        book.choose(chess.Board("8/8/8/4k3/8/8/8/4K3 w - - 0 1"))
        self.assertEqual(book.choice_results, {})

    def test_displayed_results_only_count_moves_played_by_maxima(self):
        records = [learning.digest(make_game([0] * 10, ours=color, result=result))
                   for color in ("white", "black") for result in ("1-0", "1/2-1/2", "0-1")]
        for item in records:
            item["opponent"] = "John"
        book = self.opening(records)
        first = (start_key, "e2e4")
        reply = (f"{chess.polyglot.zobrist_hash(board_after('e2e4')):016x}", "e7e5")
        for results in book.results:
            self.assertEqual(results[first], [1, 1, 1])
            self.assertEqual(results[reply], [1, 1, 1])
        # Opponents' moves still contribute to the learned move selection.
        self.assertEqual(book.layers[0][first][1], 6)
        self.assertEqual(book.layers[0][reply][1], 6)
        only_opponents = self.opening(records[3:])
        self.assertNotIn(first, only_opponents.results[0])

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
            move, reason = book.choose(board or chess.Board()) or (None, "search")
            name = move.uci() if move else "search"
            counts[name] = counts.get(name, 0) + 1
            reasons.add(reason.split(":")[0].split()[0])
        return counts, reasons

    def test_lost_moves_wait_behind_untried_moves_until_those_lost_too(self):
        board, key = chess.Board(), "start"
        e4, d4 = chess.Move.from_uci("e2e4"), chess.Move.from_uci("d2d4")
        book = self.opening()
        book.results[2][(key, "e2e4")] = [0, 0, 1]
        moves, _, _ = book.distribution(board, key)
        self.assertLess(moves[e4][0], moves[d4][0] / 5)
        book.results[2][(key, "d2d4")] = [0, 0, 1]
        moves, _, _ = book.distribution(board, key)
        self.assertAlmostEqual(moves[e4][0], moves[d4][0])

    def test_the_first_eval_judges_the_last_book_moves_most(self):
        def record(centipawns):
            return {"color": "white", "variant": "standard", "type": "blitz", "group": "humans", "opponent": "X",
                    "moves": [["a", "e2e4"], ["b", "e7e5"], ["c", "g1f3"], ["d", "b8c6"], ["e", "f1c4"]],
                    "values": [], "score": 0.5, "analyses": [["e", "f1c4", 20, centipawns]]}

        book = learning.opening_book([record(-70)])
        last, early = book.comfort("c", chess.Move.from_uci("g1f3")), book.comfort("a", chess.Move.from_uci("e2e4"))
        self.assertLess(last, early)
        self.assertLess(early, 1.0)
        self.assertEqual(book.comfort("c", chess.Move.from_uci("d2d4")), 1.0)
        self.assertGreater(learning.opening_book([record(100)]).comfort("c", chess.Move.from_uci("g1f3")), 1.0)

    def test_a_drawn_line_is_level_comfort_at_any_contempt(self):
        record = {"color": "white", "variant": "standard", "type": "blitz", "group": "humans", "opponent": "X",
                  "moves": [["a", "e2e4"], ["b", "e7e5"]], "values": [], "score": 0.5, "contempt": -10,
                  "analyses": [["b", "e7e5", 20, -10]]}
        self.assertEqual(learning.opening_book([record]).comfort("a", chess.Move.from_uci("e2e4")), 1.0)
        self.assertEqual(learning.game_contempt({"DrawContempt": "22"}, chess.WHITE), 22)
        self.assertEqual(learning.game_contempt({"WhiteElo": "2872", "BlackElo": "2982"}, chess.WHITE), 18)
        self.assertEqual(learning.game_contempt({}, chess.BLACK), -10)  # older games played at the old default

    def test_a_draw_against_a_stronger_opponent_is_repeated(self):
        board, key = chess.Board(), "start"
        e4 = chess.Move.from_uci("e2e4")
        stronger, equal = self.opening(contempt=24), self.opening(contempt=0)
        for book in (stronger, equal):
            book.results[2][(key, "e2e4")] = [0, 1, 0]
        self.assertGreaterEqual(stronger.distribution(board, key)[0][e4][0], stronger.weights["repeat"] / 100)
        self.assertLess(equal.distribution(board, key)[0][e4][0], equal.weights["repeat"] / 100)

    def test_books_ignore_do_not_play_weights(self):
        board = board_after("d2d4 d7d5")
        polyglot_book(self.book, [(board, "c2c4", 1), (board, "h2h3", 1), (board, "g1f3", 0)])
        self.assertEqual(learning.opening_book.read_book(self.book, board), {})
        self.assertIsNone(self.opening().choose(board))

    def test_without_experience_it_plays_book_by_weight_and_skips_rare_moves(self):
        counts, reasons = self.picks()
        self.assertEqual(reasons, {"book"})
        self.assertEqual(set(counts), {"e2e4", "d2d4"})
        move, reason = self.opening().choose(chess.Board())
        self.assertEqual(reason, "book 50% selection: book 100%")

    def test_good_line_against_opponent_grows_but_book_keeps_its_floor(self):
        few, _ = self.picks([record([[start_key, "e2e4", 1.0]]) for _ in range(2)])
        many, reasons = self.picks([record([[start_key, "e2e4", 1.0]]) for _ in range(30)])
        self.assertGreater(many["e2e4"], few["e2e4"])
        self.assertGreater(many.get("d2d4", 0), 40)  # book.bin's 45% still gives d4 its half
        self.assertIn("learned", reasons)
        _, reason = self.opening([record([[start_key, "e2e4", 1.0]]) for _ in range(30)]).choose(chess.Board())
        self.assertRegex(reason, r"^(book|learned) \d+% selection: book \d+%, global \d+%, humans \d+%, John \d+%$")

    def test_signature_line_comes_back_against_the_same_opponent(self):
        def game(score, evaluated=0.55, opponent="John"):
            played = record([[start_key, "e2e4", score]], opponent=opponent)
            played.update(values=[[start_key, "e2e4", evaluated, score]], moves=[[start_key, "e2e4"]], color="white")
            return played

        counts, reasons = self.picks([game(1.0) for _ in range(3)])
        self.assertIn("signature", reasons)
        self.assertTrue(240 < counts["e2e4"] < 320, counts)  # about 70%: some variety stays
        for records in ([game(1.0)] * 2, [game(1.0)] * 3 + [game(0.0)], [game(1.0, evaluated=0.3)] * 3):
            self.assertNotIn("signature", self.picks(records)[1])
        self.assertNotIn("signature", self.picks([game(1.0)] * 3, opponent="Mary")[1])
        # One bad game resets the streak; new convincing wins bring the line back.
        self.assertIn("signature", self.picks([game(0.0)] + [game(1.0)] * 3)[1])

    def test_revenge_plays_the_opponents_winning_line_back_until_it_wins(self):
        board = board_after("e2e4 e7e5")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"

        def game(score, color, opponent="John"):
            played = record([[key, "f2f4", 1 - score if color == "black" else score]], opponent=opponent)
            moves = [[start_key, "e2e4"], [f"{chess.polyglot.zobrist_hash(board_after('e2e4')):016x}", "e7e5"],
                     [key, "f2f4"]]
            played.update(moves=moves, color=color, score=score)
            return played

        polyglot_book(self.book, [(board, "g1f3", 100)])  # book.bin plays 3.Nf3 here
        lost = game(0.0, "black")  # John beat us with the King's Gambit
        counts, reasons = self.picks([lost], board=board, rated=False)
        self.assertIn("revenge", reasons)
        self.assertGreater(counts.get("f2f4", 0), 160, counts)  # at least 50% as a guest game, less noise
        rated, rated_reasons = self.picks([lost], board=board)
        self.assertIn("revenge", rated_reasons)
        self.assertGreater(rated.get("f2f4", 0), 85, rated)  # at least 30% when rated and equal, less noise
        self.assertNotIn("revenge", self.picks([lost], board=board, opponent="Mary", rated=False)[1])
        # Our own win with White serves the revenge.
        self.assertNotIn("revenge", self.picks([lost, game(1.0, "white")], board=board, rated=False)[1])

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

    def test_learning_groups_keep_global_group_and_opponent_feedback(self):
        for group in ("humans", "masters", "engines"):
            with self.subTest(group=group):
                records = [record([[start_key, "e2e4", 1.0]], group=group)]
                book = self.opening(records, group=group)
                self.assertTrue(all(layer for layer in book.layers))
                other = self.opening(records, group="other", opponent="Nobody")
                self.assertTrue(other.layers[0])
                self.assertFalse(other.layers[1])
                self.assertFalse(other.layers[2])

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

    def test_depth_differences_preserve_alternatives(self):
        board = chess.Board()
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        records = [record([[key, "e2e4", 1.0], [key, "d2d4", 0.5]],
                          analyses=[[key, "d2d4", 24, 40, 0, 60000, 0]])]
        book = self.opening(records)
        moves, _, _ = book.distribution(board, key)
        self.assertGreater(moves[chess.Move.from_uci("e2e4")][0], 0)
        self.assertGreater(moves[chess.Move.from_uci("d2d4")][0], 0)

    def test_saved_analysis_is_passive(self):
        from unittest.mock import Mock
        rows = [[start_key, move, depth, cp, mate, 60000, 0, nodes, 1000]
                for move, depth, cp, mate, nodes in
                (("e2e4", 12, 40, 0, 2000), ("d2d4", 30, 2000, 1, 90000))]
        book = self.opening([record([], analyses=rows)], book_path=None)
        book.search_ply = None
        book.random.choices = Mock()
        self.assertIsNone(book.choose(chess.Board()))
        book.random.choices.assert_not_called()
        self.assertFalse(hasattr(book, "analyses"))
        self.assertFalse(hasattr(book, "reuse"))

    def test_out_of_book_a_learned_move_needs_evidence(self):
        board = board_after("e2e4 e7e5 g1f3 b8c6")
        key = f"{chess.polyglot.zobrist_hash(board):016x}"
        quick = [record([[key, "f1b5", 0.6]], analyses=[[key, "f1b5", 8, 30, 0, 300, 2]])]
        self.assertIsNone(self.opening(quick).choose(board))
        proven = [self.opening(quick * 3, seed=seed).choose(board) for seed in range(20)]
        self.assertEqual({pick[0].uci() for pick in proven if pick}, {"f1b5"})
        self.assertTrue(all(pick[1].startswith("learned") for pick in proven if pick))
        self.assertIn(None, proven)

    def test_out_of_horizon_and_repeated_positions_are_searched(self):
        book = self.opening()
        self.assertIsNone(book.choose(board_after("g1f3 g8f6 f3g1 f6g8")))

    def test_search_layer_leaves_the_book_once_per_game(self):
        book = self.opening(profiles={name: dict(weights, search=100)
                                      for name, weights in learning.layer_profiles.items()})
        book.search_ply = 0
        self.assertIsNone(book.choose(chess.Board()))
        self.assertIsNotNone(book.choose(chess.Board()))
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
        self.assertEqual(learning.game_profile(0, True), profiles["equal"])
        self.assertEqual(learning.game_profile(-100, True), profiles["weaker"])
        self.assertEqual(learning.game_profile(-25, True)["fire"], 0)
        self.assertEqual(learning.game_profile(-25, True)["book"], 45)
        self.assertEqual(learning.game_profile(-26, True)["fire"], 21)
        self.assertEqual(learning.game_profile(30, True), profiles["stronger"])
        self.assertEqual(learning.game_profile(100, True)["draw"], 35)
        self.assertEqual(learning.game_profile(None, True), profiles["equal"])
        unrated = learning.game_profile(-100, False)
        self.assertEqual(unrated, profiles["unrated"])
        self.assertGreaterEqual(unrated["fire"], unrated["book"])
        self.assertEqual(learning.game_profile(None, False), profiles["unrated"])
        self.assertEqual(learning.game_profile(-25, False, bot=True)["fire"], 0)
        self.assertEqual(learning.game_profile(None, False, bot=True)["fire"], 0)
        self.assertEqual(learning.game_profile(-26, False, bot=True), profiles["unrated"])
        for weights in profiles.values():
            self.assertEqual(sum(weights[name] for name in learning.layer_names), 100)

    def shares(self, **options):
        book = learning.opening_book([], book_path=self.main, style_books={"fire": self.fire}, **options)
        book.search_ply = None
        moves, in_book, search = book.distribution(self.after_e5, "")
        self.assertEqual(search, 0.0)
        self.assertTrue(in_book)
        return {move.uci(): round(probability, 3) for move, (probability, _) in moves.items()}, book

    def test_style_books_pull_but_book_bin_keeps_its_share(self):
        self.assertEqual(self.shares(contempt=0)[0], {"g1f3": 0.9, "b1c3": 0.1})
        weaker, book = self.shares(contempt=-100)
        self.assertEqual(set(book.paths), {"book", "fire"})
        self.assertEqual(weaker, {"g1f3": round((20 * 0.9 + 25 * 0.5) / 45, 3), "b1c3": round(2 / 45, 3),
                                  "f2f4": round(12.5 / 45, 3)})
        self.assertEqual(set(self.shares(contempt=100)[1].paths), {"book"})
        guest, book = self.shares(rated=False)
        self.assertEqual(guest["f2f4"], 0.25)
        moves, _, _ = book.distribution(self.after_e5, "")
        self.assertEqual(book.describe(*moves[chess.Move.from_uci("g1f3")]), "book 70% selection: book 64%, fire 36%")

    def test_draw_and_experiment_layers_contribute_legal_moves(self):
        for layer, options in (("draw", {"contempt": 100}), ("experiment", {"rated": False})):
            with self.subTest(layer=layer):
                book = learning.opening_book([], book_path=self.main,
                                             style_books={layer: self.fire}, **options)
                book.search_ply = None
                moves, in_book, _ = book.distribution(self.after_e5, "")
                self.assertTrue(in_book)
                self.assertGreater(moves[chess.Move.from_uci("f2f4")][1][layer], 0)
                self.assertTrue(all(self.after_e5.is_legal(move) for move in moves))

    def test_mood_changes_with_time_and_joins_the_book(self):
        moods = ["dutch", "parham", "traxler"]
        self.assertTrue(all(learning.pick_mood("auto", moods, "parham") != "parham" for _ in range(50)))
        self.assertEqual(learning.pick_mood("auto", ["parham"], "parham"), "parham")
        self.assertEqual(learning.pick_mood("parham", moods), "parham")
        self.assertIsNone(learning.pick_mood("off", moods))
        self.assertIsNone(learning.pick_mood("missing", moods))
        (self.root / "moods").mkdir()
        polyglot_book(self.root / "moods" / "parham.bin", [(self.after_e5, "d1h5", 100)])
        library = learning.learner(self.root, book_path=self.main, mood="parham", moods_dir=self.root / "moods")
        book = library.for_game(None, None, None, rated=False)
        self.assertEqual(book.mood, "parham")
        moves, _, _ = book.distribution(self.after_e5, "")
        self.assertIn("mood", book.describe(*moves[chess.Move.from_uci("d1h5")]))
        polyglot_book(self.root / "moods" / "dutch.bin", [(self.after_e5, "f7f5", 100)])
        library = learning.learner(self.root, book_path=self.main, moods_dir=self.root / "moods")
        first = library.today_mood(now=0)
        self.assertEqual(library.today_mood(now=learning.mood_hours * 3600 - 1), first)
        self.assertNotEqual(library.today_mood(now=learning.mood_hours * 3600), first)

    def test_the_mood_survives_a_lost_line_and_tries_its_other_lines_first(self):
        mood = self.root / "mood.bin"
        polyglot_book(mood, [(self.after_e5, "d1h5", 100)])
        books = {"mood": mood}
        lost = chess.Move.from_uci("d1h5")

        def share():
            book = learning.opening_book([], book_path=self.main, style_books=books, rated=False,
                                         random_source=__import__("random").Random(1))
            book.personal_score = lambda key, move: 0.0 if move == lost else None
            return book.distribution(self.after_e5, "")[0][lost][0]

        self.assertGreater(share(), 0.05)
        polyglot_book(mood, [(self.after_e5, "d1h5", 100), (self.after_e5, "f1c4", 100)])
        self.assertLess(share(), 0.05)

    def test_where_book_bin_has_nothing_its_share_goes_to_search(self):
        board = board_after("e2e4 e7e5 f2f4")
        polyglot_book(self.fire, [(board, "e5f4", 100)])
        book = learning.opening_book([], book_path=self.main, style_books={"fire": self.fire}, rated=False,
                                     random_source=__import__("random").Random(3))
        book.search_ply = None
        moves, in_book, search = book.distribution(board, "")
        self.assertTrue(in_book)
        self.assertEqual((round(moves[chess.Move.from_uci("e5f4")][0], 3), search), (0.5, 0.5))
        picks = [book.choose(board) for _ in range(200)]
        searched = sum(pick is None for pick in picks)
        self.assertTrue(60 < searched < 140, searched)
        self.assertEqual({pick[0].uci() for pick in picks if pick}, {"e5f4"})

if __name__ == "__main__":
    unittest.main()
