"""Learning digests: JDrone's decision point, asymmetric credit, index cache, and player stats."""
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


class decision_tests(unittest.TestCase):
    def test_score_bands_match_jdrone(self):
        self.assertEqual([learning.bucket(cp) for cp in (300, 299, 150, 149, 50, 49, -49, -50, -149, -150, -299, -300)],
                         [3, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -3])

    def test_detects_loss_draw_and_win(self):
        self.assertEqual(learning.find_decision([(1, 0), (3, -2), (5, -3), (7, -3)], 30), ("loss", 6, -3))
        self.assertEqual(learning.find_decision([(ply, 0) for ply in range(0, 40, 2)], 60)[0], "draw")
        self.assertEqual(learning.find_decision([(0, 1), (2, 2), (4, 3)], 6), ("win", 4, 3))
        self.assertEqual(learning.find_decision([(0, 1), (2, 2), (4, 3)], 20), (None, None, None))


class digest_tests(unittest.TestCase):
    def test_loss_credits_opponent_moves_as_wins(self):
        record = learning.digest(make_game([0, -60, -200, -400, -500, -600, -700, -800], result="0-1"))
        self.assertEqual(record["decision"], "loss")
        ours = {credit[2] for credit, (key, move) in zip(record["credited"], record["moves"]) if line.index(move) % 2 == 0}
        theirs = {credit[2] for credit, (key, move) in zip(record["credited"], record["moves"]) if line.index(move) % 2}
        self.assertEqual((ours, theirs), ({0.0}, {1.0}))
        self.assertEqual(record["score"], 0.0)

    def test_win_credits_only_our_moves_and_stops_after_easy_moves(self):
        record = learning.digest(make_game([20, 80, 320, 400, 500, 600, 700, 800, 900]))
        self.assertEqual(record["decision"], "win")
        credited = [move for _, move, _ in record["credited"]]
        self.assertEqual(credited, ["e2e4", "g1f3", "f1b5", "b5a4"])
        self.assertEqual({value for _, _, value in record["credited"]}, {1.0})

    def test_draw_credits_both_sides_and_records_analysis(self):
        record = learning.digest(make_game([0] * 9, result="1/2-1/2"))
        self.assertEqual(record["decision"], "draw")
        self.assertEqual({value for _, _, value in record["credited"]}, {0.5})
        self.assertEqual(record["analyses"][0][1:], ["e2e4", 12, 0, 0, 2500, 0])

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
