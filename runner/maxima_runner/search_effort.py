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
