"""Select self-play starting histories before evaluation swings in annotated games."""
import argparse
import hashlib
import json
from pathlib import Path

import chess
import chess.pgn

from . import nnue_data
from .learning_store import atomic_text
from .variants import read_pgn


def our_color(game, players=()):
    saved = game.headers.get('LearningColor')
    if saved in ('white', 'black'):
        return saved == 'white'
    aliases = {name.casefold() for name in players} or {'maxima', 'maxima2', 'maximaengine', 'maxdrone', 'guestmxma'}
    sides = [color for color, name in ((chess.WHITE, 'White'), (chess.BLACK, 'Black'))
             if game.headers.get(name, '').casefold() in aliases]
    return sides[0] if len(sides) == 1 else None


def select(game, *, equal_cp=50, swing_cp=150, min_depth=16, min_ply=16, max_gap=6, stable_observations=2, players=()):
    board = game.board()
    color = our_color(game, players)
    if game.errors or type(board) is not chess.Board or board.chess960 or not board.is_valid() or color is None:
        return []
    initial_fen = board.fen(en_passant='fen')
    history, observations, roots = [], [], {}
    for node in game.mainline():
        if node.move not in board.legal_moves:
            return []
        # Online annotations belong to the search BEFORE the annotated move.
        evaluation = node.eval()
        depth = node.eval_depth()
        if board.turn == color and evaluation is not None and depth is not None and depth >= min_depth:
            score = evaluation.white().score()
            if score is not None and abs(score) <= 1000:
                observations.append((len(history), score, depth))
        roots[len(history)] = (board.fen(en_passant='fen'), not board.is_game_over(claim_draw=True))
        history.append(node.move.uci())
        board.push(node.move)
    identity = game.headers.get('LearningId', '')
    if len(identity) == 64 and all(c in '0123456789abcdef' for c in identity):
        family = identity
    else:
        identity = [initial_fen, history, *(game.headers.get(key, '') for key in
                    ('Site', 'Date', 'UTCTime', 'White', 'Black'))]
        family = hashlib.sha256(json.dumps(identity).encode('utf-8')).hexdigest()
    selected = []
    for index in range(stable_observations - 1, len(observations) - 1):
        before, after = observations[index:index + 2]
        stable = observations[index + 1 - stable_observations:index + 1]
        if any(abs(item[1]) > equal_cp for item in stable):
            continue
        if any(right[0] - left[0] > max_gap for left, right in zip(stable, stable[1:])):
            continue
        ply, score, depth = before
        later, changed, changed_depth = after
        if abs(score) > equal_cp or abs(changed - score) < swing_cp or later - ply > max_gap:
            continue
        for distance in (2, 4):
            start = later - distance
            if start < min_ply or start not in roots or not roots[start][1]:
                continue
            fen = roots[start][0]
            selected.append(dict(initial_fen=initial_fen, moves=history[:start], fen=fen,
                                 source_game=family, site=game.headers.get('Site', ''),
                                 selection=dict(before_ply=ply, after_ply=later, before_cp=score,
                                                after_cp=changed, before_depth=depth, after_depth=changed_depth,
                                                start_ply=start, moves_before=distance // 2)))
    return selected


def scan(paths, *, per_game=4, limit=512, **options):
    candidates, stats = [], dict(games=0, games_selected=0)
    for path in sorted(set(paths)):
        with Path(path).open(encoding='utf-8-sig') as stream:
            while True:
                game = read_pgn(stream)
                if game is None:
                    break
                stats['games'] += 1
                seeds = select(game, **options)
                seeds.sort(key=lambda seed: -abs(seed['selection']['after_cp'] - seed['selection']['before_cp']))
                if seeds:
                    stats['games_selected'] += 1
                    candidates.extend(seeds[:per_game])
    candidates.sort(key=lambda seed: -abs(seed['selection']['after_cp'] - seed['selection']['before_cp']))
    seen, selected = set(), []
    for seed in candidates:
        key = nnue_data.position_key(chess.Board(seed['fen']))
        if key not in seen:
            selected.append(seed)
            seen.add(key)
        if len(selected) >= limit:
            break
    stats['positions'] = len(selected)
    return dict(format='maxima.nnue.starts', version=1, selection=options, stats=stats, starts=selected)


def load(path):
    path = Path(path)
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('starting histories exceed 16 MiB')
    saved = json.loads(path.read_text())
    if saved.get('format') != 'maxima.nnue.starts' or saved.get('version') != 1 or not saved.get('starts'):
        raise ValueError('invalid or empty self-play starting histories')
    if len(saved['starts']) > 10000:
        raise ValueError('too many starting histories')
    for seed in saved['starts']:
        board = chess.Board(seed['initial_fen'])
        if not board.is_valid() or board.chess960:
            raise ValueError('invalid starting board')
        family = seed['source_game']
        if not isinstance(family, str) or len(family) != 64 or any(c not in '0123456789abcdef' for c in family):
            raise ValueError('invalid source game identity')
        for move in seed['moves']:
            board.push_uci(move)
        if board.fen(en_passant='fen') != seed['fen'] or board.is_game_over(claim_draw=True):
            raise ValueError('starting history does not match a playable position')
    return saved['starts']


def main():
    parser = argparse.ArgumentParser(prog='maxima nnue:seeds', description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--player', action='append', default=[])
    parser.add_argument('--equal-cp', type=int, default=50)
    parser.add_argument('--swing-cp', type=int, default=150)
    parser.add_argument('--min-depth', type=int, default=16)
    parser.add_argument('--min-ply', type=int, default=16)
    parser.add_argument('--max-gap', type=int, default=6, help='maximum plies between scored observations')
    parser.add_argument('--stable-observations', type=int, default=2, help='consecutive near-equal logged searches')
    parser.add_argument('--per-game', type=int, default=4)
    parser.add_argument('--limit', type=int, default=512)
    args = parser.parse_args()
    if min(args.swing_cp, args.min_depth, args.max_gap, args.per_game, args.limit, args.stable_observations) <= 0 or min(args.equal_cp, args.min_ply) < 0:
        parser.error('invalid selection bounds')
    if not args.source.exists():
        parser.error('PGN source does not exist')
    paths = args.source.rglob('*.pgn') if args.source.is_dir() else [args.source]
    report = scan(paths, per_game=args.per_game, limit=args.limit, equal_cp=args.equal_cp, swing_cp=args.swing_cp,
                  min_depth=args.min_depth, min_ply=args.min_ply, max_gap=args.max_gap, stable_observations=args.stable_observations, players=args.player)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    atomic_text(args.output, json.dumps(report, indent=2) + '\n')
    print(f"{report['stats']['games']} games scanned, {report['stats']['games_selected']} with swings, "
          f"{report['stats']['positions']} unique starting positions")
    return 0
