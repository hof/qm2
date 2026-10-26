"""Conservative online loss evidence, shared by FICS and Lichess."""
from collections import deque
import math
from statistics import median

import chess


def validate_settings(value):
    defaults = {"enabled": True, "loss_cp": -800, "confirmations": 3, "min_depth": 16,
                "rating_margin": 200, "unrated_ceiling": 30}
    if not isinstance(value, dict) or value.keys() - defaults.keys():
        raise ValueError("resignation must be an object with known policy settings")
    settings = dict(defaults, **value)
    for key, item in settings.items():
        if key == "enabled":
            valid = type(item) is bool
        elif key == "unrated_ceiling":
            valid = type(item) in (int, float) and math.isfinite(item) and item > 0
        else:
            valid = type(item) is int and (item < 0 if key == "loss_cp" else item >= (0 if key == "rating_margin" else 1))
        if not valid:
            raise ValueError(f"invalid resignation setting {key}: {item!r}")
    return settings


class loss_policy:
    def __init__(self, settings=None):
        self.settings = validate_settings({} if settings is None else settings)
        self.reset()

    def reset(self):
        self.scores = deque(maxlen=max(4, self.settings["confirmations"]))
        self.matches = deque(maxlen=3)
        self.reply_times = deque(maxlen=3)
        self.pending = None
        self.last_search = None
        self.sent = False

    def ceiling(self, rated):
        if rated is False and len(self.scores) >= 2 and all(score <= -300 for _, score in list(self.scores)[-2:]):
            return self.settings["unrated_ceiling"]
        return None

    def on_reply(self, before, move, seconds):
        expected = self.pending
        self.matches.append(bool(expected and expected == (before.fen(), move)))
        self.pending = None
        if seconds is None or not math.isfinite(seconds) or seconds < 0:
            self.reply_times.clear()
        else:
            self.reply_times.append(seconds)

    def on_search(self, board, move, info, kind):
        key = (board.fen(), tuple(board.move_stack))
        duplicate = key == self.last_search
        self.last_search = key
        self.pending = None
        info = info.get("_resignation", info)
        score = info.get("score")
        valid = (kind == "engine" and not info.get("source") and score is not None
                 and info.get("depth", 0) >= self.settings["min_depth"]
                 and not info.get("lowerbound") and not info.get("upperbound"))
        if not valid:
            self.scores.clear()
            return False
        sample = (board.fullmove_number, score.pov(board.turn).score(mate_score=100000))
        if duplicate and self.scores:
            self.scores[-1] = sample
        else:
            self.scores.append(sample)
        pv = info.get("pv", [])
        replay = board.copy()
        if len(pv) >= 2 and pv[0] == move and replay.is_legal(move):
            replay.push(move)
            if replay.is_legal(pv[1]):
                self.pending = (replay.fen(), pv[1])
        return not duplicate

    def decision(self, *, our_rating, their_rating, bot, speed, ours, theirs, increment, info, board):
        """Return auditable evidence or None. Unknown bot status receives human clock protection."""
        count = self.settings["confirmations"]
        scores = list(self.scores)
        if (not self.settings["enabled"] or self.sent or len(scores) < max(4, count)
                or any(number <= 20 or score > self.settings["loss_cp"] for number, score in scores[-count:])
                or scores[-4][1] - scores[-1][1] < 100
                or any(b[1] - a[1] > 50 for a, b in zip(scores[-4:], scores[-3:]))
                or len(self.matches) < 3 or not all(self.matches)):
            return None
        if (bot is not True and their_rating is not None
                and (our_rating is None or their_rating < our_rating - self.settings["rating_margin"])):
            return None
        if (any(value is None or not math.isfinite(value) or value < 0 for value in (ours, theirs, increment))
                or len(self.reply_times) < 3):
            return None
        reserve = 15 if speed in ("bullet", "ultraBullet", "lightning") else 30 if speed == "blitz" else 60
        reply = median(self.reply_times)
        required = reserve + 20 * max(0, reply - increment)
        if theirs < required or bot is not True and ours >= 2 * theirs and theirs < 2 * required:
            return None
        answer = info.get("tablebase")
        if (board.is_game_over() or answer and answer.get("category") != "loss"
                or info.get("tbhits") and chess.popcount(board.occupied) <= 7):
            return None
        return {"scores": scores, "prediction_matches": list(self.matches), "our_rating": our_rating,
                "their_rating": their_rating, "bot": bot, "speed": speed, "ours": ours, "theirs": theirs,
                "median_reply_seconds": reply, "increment_seconds": increment, "speed_reserve": reserve,
                "required_reserve": required}
