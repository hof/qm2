"""Logistic GSPRT port of fast-chess's SPRT::getLLR_logistic.

Copyright (c) 2023 disservin. MIT license: see LICENSE.
Source revision and numerical changes: see README.md.
"""
import math


def logistic_llr(elo0, elo1, counts):
    regularized = [count if count else 1e-3 for count in counts]
    total = sum(regularized)
    probabilities = [count / total for count in regularized]
    scores = [index / 4 for index in range(5)]

    def constrained_mle(target):
        offsets = [score - target for score in scores]
        lower = -1 / offsets[-1] + 1e-9
        upper = -1 / offsets[0] - 1e-9

        def secular(theta):
            return sum(probability * offset / (1 + theta * offset)
                       for probability, offset in zip(probabilities, offsets))

        if secular(lower) <= 0 or secular(upper) >= 0:
            raise ValueError('SPRT likelihood root is not bracketed')
        # The secular equation is strictly decreasing. Bisection replaces
        # upstream ITP; tighten convergence for parity with Fishtest/Brent.
        for _ in range(100):
            theta = (lower + upper) / 2
            if secular(theta) > 0:
                lower = theta
            else:
                upper = theta
            if upper - lower < 2e-14:
                break
        else:
            raise ValueError('SPRT likelihood root did not converge')
        theta = (lower + upper) / 2
        distribution = [probability / (1 + theta * offset)
                        for probability, offset in zip(probabilities, offsets)]
        if (abs(sum(distribution) - 1) > 1e-6 or
                abs(sum(score * probability for score, probability in
                        zip(scores, distribution)) - target) > 1e-6):
            raise ValueError('SPRT likelihood constraints did not converge')
        return distribution

    distributions = [constrained_mle(1 / (1 + 10 ** (-elo / 400)))
                     for elo in (elo0, elo1)]
    return total * sum(probability * (math.log(p1) - math.log(p0))
                       for probability, p0, p1 in
                       zip(probabilities, distributions[0], distributions[1]))
