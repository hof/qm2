"""Select self-play starting histories before evaluation swings in annotated games."""
import argparse
import hashlib
import json
import random
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


def game_family(game, initial_fen):
    identity = game.headers.get('LearningId', '')
    if len(identity) == 64 and all(c in '0123456789abcdef' for c in identity):
        family = identity
    else:
        identity = [initial_fen, [move.uci() for move in game.mainline_moves()], *(game.headers.get(key, '') for key in
                    ('Site', 'Date', 'UTCTime', 'White', 'Black'))]
        family = hashlib.sha256(json.dumps(identity).encode('utf-8')).hexdigest()
    return family


def select(game, *, equal_cp=50, swing_cp=150, min_depth=16, min_ply=16, max_gap=6, stable_observations=2, players=(),
           max_pieces=32, preceding_plies=(2, 4), score_position='before'):
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
                observations.append((len(history) + (score_position == 'after'), score, depth))
        roots[len(history)] = (board.fen(en_passant='fen'), not board.is_game_over(claim_draw=True),
                               len(board.piece_map()))
        history.append(node.move.uci())
        board.push(node.move)
    roots[len(history)] = (board.fen(en_passant='fen'), not board.is_game_over(claim_draw=True),
                           len(board.piece_map()))
    family = game_family(game, initial_fen)
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
        if later not in roots or roots[later][2] > max_pieces:
            continue
        for distance in preceding_plies:
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


def position_starts(game, *, min_ply=16, max_pieces=32, preceding_plies=()):
    """Replay real histories and retain positions matching configurable board bounds."""
    board = game.board()
    if game.errors or type(board) is not chess.Board or not board.is_valid() or board.chess960:
        return []
    initial, history, roots, selected = board.fen(en_passant='fen'), [], {}, set()
    for node in game.mainline():
        if node.move not in board.legal_moves:
            return []
        if board.is_game_over(claim_draw=True):
            break
        roots[len(history)] = board.fen(en_passant='fen')
        if board.ply() >= min_ply and len(board.piece_map()) <= max_pieces:
            selected.add(len(history))
            selected.update(max(0, len(history) - distance) for distance in preceding_plies)
        history.append(node.move.uci())
        board.push(node.move)
    family = game_family(game, initial)
    return [dict(initial_fen=initial, moves=history[:ply], fen=roots[ply], source_game=family)
            for ply in sorted(selected) if ply in roots]


def prepare(source, *, selection='positions', limit=512, per_game=4, seed=1, exclude=(), **options):
    """Prepare a bounded, reproducible pool; supplied scores never become labels."""
    source = Path(source)
    source_hash = nnue_data.file_hash(source) if source.is_file() else None
    if (type(limit) is not int or not 0 < limit <= 10000 or type(per_game) is not int or per_game <= 0
            or selection not in ('positions', 'critical')):
        raise ValueError('invalid starting-pool selection or bounds')
    excluded = set()
    for path in exclude:
        _, rows = nnue_data.read_dataset(path)
        excluded.update(nnue_data.position_key(chess.Board(row['fen'])) for row in rows)
    rng = random.Random(seed)
    if source.suffix.lower() in ('.fen', '.epd'):
        if selection != 'positions':
            raise ValueError('critical selection requires annotated PGN input')
        reservoir, total = [], 0
        with source.open(encoding='utf-8-sig') as stream:
            for line in stream:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                total += 1
                if len(reservoir) < limit:
                    reservoir.append(line)
                else:
                    index = rng.randrange(total)
                    if index < limit:
                        reservoir[index] = line
        candidates = []
        for line in reservoir:
            board = chess.Board()
            fields = line.split()
            if len(fields) >= 6 and fields[4].isdigit() and fields[5].isdigit():
                board = chess.Board(' '.join(fields[:6]))
            elif source.suffix.lower() == '.epd':
                board.set_epd(line)
            else:
                board = chess.Board(line)
            if (not board.is_valid() or board.chess960 or board.is_game_over(claim_draw=True)
                    or len(board.piece_map()) > options.get('max_pieces', 32)):
                continue
            fen = board.fen(en_passant='fen')
            family = hashlib.sha256(('position:' + nnue_data.position_key(board)).encode()).hexdigest()
            candidates.append(dict(initial_fen=fen, fen=fen, moves=[], source_game=family))
    elif source.suffix.lower() == '.json':
        candidates = list(load(source))
        rng.shuffle(candidates)
    else:
        paths = sorted(source.rglob('*.pgn')) if source.is_dir() else [source]
        candidates, total = [], 0
        for path in paths:
            with path.open(encoding='utf-8-sig') as stream:
                while True:
                    game = read_pgn(stream)
                    if game is None:
                        break
                    if selection == 'critical':
                        starts = select(game, **options)
                    else:
                        starts = position_starts(game, **{key: value for key, value in options.items()
                            if key in ('min_ply', 'max_pieces', 'preceding_plies')})
                    rng.shuffle(starts)
                    for item in starts[:per_game]:
                        total += 1
                        if len(candidates) < limit:
                            candidates.append(item)
                        else:
                            index = rng.randrange(total)
                            if index < limit:
                                candidates[index] = item
        rng.shuffle(candidates)
    seen, starts = set(excluded), []
    for item in candidates:
        key = nnue_data.position_key(chess.Board(item['fen']))
        if key not in seen:
            seen.add(key)
            starts.append(item)
        if len(starts) >= limit:
            break
    if not starts:
        raise ValueError('no playable, unreserved starting positions were selected')
    if source_hash is not None and nnue_data.file_hash(source) != source_hash:
        raise ValueError('starting-position source changed while selecting the pool')
    return dict(format='maxima.nnue.starts', version=1, starts=starts,
                selection=dict(kind=selection, seed=seed, **options),
                source_sha256=source_hash)


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
    parser.add_argument('--selection', choices=('critical', 'positions'), default='critical')
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--max-pieces', type=int, default=32)
    parser.add_argument('--preceding-plies', type=int, nargs='+', default=[2, 4])
    parser.add_argument('--score-position', choices=('before', 'after'), default='before')
    parser.add_argument('--exclude-dataset', type=Path, action='append', default=[])
    args = parser.parse_args()
    if (min(args.swing_cp, args.min_depth, args.max_gap, args.per_game, args.limit, args.stable_observations) <= 0
            or min(args.equal_cp, args.min_ply, *args.preceding_plies) < 0
            or not 2 <= args.max_pieces <= 32):
        parser.error('invalid selection bounds')
    if not args.source.exists():
        parser.error('PGN source does not exist')
    report = prepare(args.source, selection=args.selection, limit=args.limit, per_game=args.per_game,
                     seed=args.seed, exclude=args.exclude_dataset, equal_cp=args.equal_cp, swing_cp=args.swing_cp,
                     min_depth=args.min_depth, min_ply=args.min_ply, max_gap=args.max_gap,
                     stable_observations=args.stable_observations, players=args.player,
                     max_pieces=args.max_pieces, preceding_plies=args.preceding_plies,
                     score_position=args.score_position)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    atomic_text(args.output, json.dumps(report, indent=2) + '\n')
    print(f"{len(report['starts'])} playable starting histories selected")
    return 0
