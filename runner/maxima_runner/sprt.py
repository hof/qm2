"""Whole-opening-pair logistic GSPRT decisions for engine A versus B."""
import math

from .third_party.fastchess.llr import logistic_llr


def validate_sprt(elo0, elo1, alpha, beta):
    if not all(math.isfinite(value) for value in (elo0, elo1, alpha, beta)):
        raise ValueError('SPRT parameters must be finite')
    if not elo0 < elo1:
        raise ValueError('SPRT requires elo0 < elo1 (logistic Elo)')
    if not (0 < alpha < 1 and 0 < beta < 1 and alpha + beta < 1):
        raise ValueError('SPRT requires positive alpha/beta with sum less than one')


def pentanomial_counts(games):
    pairs = {}
    for game in games:
        color = game['a_color']
        if color not in ('white', 'black'):
            raise ValueError('invalid A color in SPRT record')
        pair = pairs.setdefault(game['pair'], {})
        if color in pair:
            raise ValueError('duplicate color in SPRT opening pair')
        result = game['result']
        if result not in ('1-0', '1/2-1/2', '0-1', '*'):
            raise ValueError('invalid result in SPRT record')
        pair[color] = None
        if result != '*' and not game.get('error'):
            score = {'1-0': 2, '1/2-1/2': 1, '0-1': 0}[result]
            pair[color] = score if color == 'white' else 2 - score
    counts = [0] * 5
    for pair in pairs.values():
        if len(pair) == 2 and all(score is not None for score in pair.values()):
            counts[sum(pair.values())] += 1
    return counts


def sprt_summary(games, elo0, elo1, alpha=0.05, beta=0.05):
    validate_sprt(elo0, elo1, alpha, beta)
    counts = pentanomial_counts(games)
    complete_pairs = sum(counts)
    llr = logistic_llr(elo0, elo1, counts) if complete_pairs else 0.0
    if not math.isfinite(llr):
        raise ValueError('nonfinite SPRT likelihood')
    lower = math.log(beta / (1 - alpha))
    upper = math.log((1 - beta) / alpha)
    eligible = bool(complete_pairs and len(games) == 2 * complete_pairs)
    decision = ('accept_h1' if llr >= upper else 'accept_h0' if llr <= lower
                else 'continue') if eligible else 'continue'
    return {'method': 'pentanomial logistic GSPRT', 'elo0': elo0, 'elo1': elo1,
            'alpha': alpha, 'beta': beta, 'pentanomial': counts,
            'completed_pairs': complete_pairs, 'llr': llr,
            'lower_bound': lower, 'upper_bound': upper,
            'decision_eligible': eligible, 'decision': decision}
