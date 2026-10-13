"""Minimum logistic Elo decisions with simultaneous opening-pair bounds.

Hoeffding's bounded-mean inequality and a union bound over the declared
maximum pair count allow checking after every pair, including optional stopping.
The guarantee assumes representative independent pair scores in [0, 1].
"""
import math

from .sprt import pentanomial_counts


def validate_min_elo(elo, maximum_pairs, alpha):
    if not math.isfinite(elo) or not 0 <= elo <= 200:
        raise ValueError('minimum gain must be finite and between 0 and 200 logistic Elo')
    if not isinstance(maximum_pairs, int) or maximum_pairs <= 0:
        raise ValueError('minimum-gain test requires a positive maximum pair count')
    if not math.isfinite(alpha) or not 0 < alpha < 1:
        raise ValueError('minimum-gain error budget must be between zero and one')


def min_elo_summary(games, elo, maximum_pairs, alpha=0.05):
    validate_min_elo(elo, maximum_pairs, alpha)
    counts = pentanomial_counts(games)
    pairs = sum(counts)
    if pairs > maximum_pairs:
        raise ValueError('minimum-gain test exceeds its declared pair cap')
    eligible = bool(pairs and len(games) == 2 * pairs)
    target = 1 / (1 + 10 ** (-elo / 400))
    score = sum(index * count for index, count in enumerate(counts)) / (4 * pairs) if pairs else None
    interval = None
    decision = 'continue'
    if eligible:
        radius = math.sqrt((math.log(2) + math.log(maximum_pairs) - math.log(alpha)) / (2 * pairs))
        interval = [max(0, score - radius), min(1, score + radius)]
        if interval[0] > target:
            decision = 'minimum_gain_established'
        elif interval[1] < target:
            decision = 'below_minimum_gain'
    return {'method': 'opening-pair Hoeffding with Bonferroni over every planned pair',
            'elo_model': 'logistic', 'minimum_elo': elo, 'target_score': target,
            'alpha': alpha, 'confidence': 1 - alpha, 'planned_checks': maximum_pairs,
            'completed_pairs': pairs, 'pentanomial': counts, 'a_score': score,
            'score_interval': interval, 'decision_eligible': eligible, 'decision': decision}
