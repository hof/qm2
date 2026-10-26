"""Search effort stored in PGN comments, independently of server clocks."""
import math
import re


def annotation(node, name):
    match = re.search(r"\[%" + name + r" ([0-9]+(?:\.[0-9]+)?)\]", node.comment)
    value = float(match[1]) if match else None
    return value if value is not None and math.isfinite(value) else None


def effort(node):
    return annotation(node, "nodes"), annotation(node, "nps"), annotation(node, "searchtime")


def analysis_rank(depth, cp, mate, duration, nodes):
    # Preserve winning mates; otherwise prefer measured work, then depth and time.
    return (bool(mate and cp > 0), nodes or 0, depth or 0, duration or 0)


class reuse_monitor:
    """Stop only when a complete live PV agrees and saved work covers this turn."""
    def __init__(self, move, saved, budget_ms, nps=None, margin=0.40):
        self.move, self.saved = move, saved
        self.seconds = budget_ms / 1000 if budget_ms is not None else None
        self.nps = nps if nps is not None and math.isfinite(nps) and nps > 0 else None
        self.margin = margin
        self.first = None
        self.measured = False
        self.stopped = False

    def __call__(self, info):
        if (self.stopped or info.get("multipv", 1) != 1 or info.get("lowerbound") or info.get("upperbound")
                or not all(key in info for key in ("nodes", "time", "pv", "score", "depth"))
                or not info["pv"] or info["score"] is None or info["depth"] <= 0):
            return False
        nodes, seconds = info["nodes"], info["time"]
        if nodes < 0 or seconds < 0 or not math.isfinite(seconds):
            return False
        if self.first is None:
            self.first = (nodes, seconds)
        elif nodes > self.first[0] and seconds - self.first[1] >= 0.100 - 1e-9:
            measured = (nodes - self.first[0]) / (seconds - self.first[1])
            self.nps = max(self.nps or 0, measured)
            self.measured = True
            self.first = (nodes, seconds)
        saved_nodes = self.saved[5]
        mate = self.saved[2] and self.saved[1] > 0
        enough = (self.measured and self.nps is not None and self.seconds is not None and saved_nodes is not None
                  and saved_nodes >= self.nps * self.seconds * (1 + self.margin))
        if (info["pv"][0] == self.move and (mate or enough)
                and (mate or nodes <= saved_nodes)):
            self.stopped = True
            return True
        return False


def validate_setting(name, value):
    if name == "learning_nps" and value is None:
        return value
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value)
            or value < 0 or name == "learning_nps" and value == 0):
        raise ValueError(f"{name} must be a finite {'positive' if name == 'learning_nps' else 'nonnegative'} number")
    return value
