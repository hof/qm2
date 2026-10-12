"""Offline paired-opening matches with clocks and variant-preserving records."""
import argparse
import asyncio
from contextlib import AsyncExitStack
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import tempfile

from .match_options import (engine_profile, file_identity, option_assignment,
                            validated_settings, verify_models)


def read_openings(path, variant):
    import chess
    from .variants import royal_losers_board
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or data.get('variant') != variant:
        raise ValueError('opening file must declare the selected variant')
    entries = data.get('openings')
    if not isinstance(entries, list) or not entries:
        raise ValueError('opening file must contain a nonempty openings list')
    boards = []
    identifiers = set()
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get('id'), str):
            raise ValueError('each opening requires a string id')
        if not entry['id'] or any(ord(char) < 32 for char in entry['id']):
            raise ValueError('opening ids must be nonempty single-line strings')
        if entry['id'] in identifiers:
            raise ValueError('opening ids must be unique')
        identifiers.add(entry['id'])
        fen = entry.get('fen', chess.STARTING_FEN)
        if not isinstance(fen, str) or len(fen.split()) != 6:
            raise ValueError('opening fen must be a six-field string')
        board = (royal_losers_board if variant == 'losers' else chess.Board)(
            fen)
        if not board.is_valid():
            raise ValueError(f"invalid opening: {entry['id']}")
        moves = entry.get('moves', [])
        if not isinstance(moves, list) or any(not isinstance(move, str) for move in moves):
            raise ValueError('opening moves must be a list of UCI strings')
        for text in moves:
            if board.is_game_over(claim_draw=True):
                raise ValueError('opening continues after a terminal position')
            board.push_uci(text)
        boards.append((entry['id'], board))
    return boards


def paired_summary(games, requested_pairs=None, *, confidence=0.95):
    """Hoeffding bound over independent opening-pair scores in [0, 1]."""
    if not 0 < confidence < 1:
        raise ValueError('confidence must be between zero and one')
    grouped = {}
    wins = draws = losses = 0
    for game in games:
        if game['result'] == '*':
            continue
        score = {'1-0': 1.0, '1/2-1/2': 0.5, '0-1': 0.0}[game['result']]
        if game['a_color'] == 'black':
            score = 1 - score
        wins += score == 1
        draws += score == 0.5
        losses += score == 0
        grouped.setdefault(game['pair'], []).append(score)
    samples = [sum(scores) / 2 for scores in grouped.values() if len(scores) == 2]
    mean = sum(samples) / len(samples) if samples else None
    complete = (len(samples) == len(grouped) and not any(game['result'] == '*' for game in games)
                and (requested_pairs is None or len(samples) == requested_pairs))
    radius = math.sqrt(math.log(2 / (1 - confidence)) / (2 * len(samples))) if samples and complete else None
    return {'wins': wins, 'draws': draws, 'losses': losses,
            'unfinished': sum(game['result'] == '*' for game in games),
            'completed_pairs': len(samples), 'a_score': mean,
            'confidence': confidence, 'interval_method': 'opening-pair Hoeffding',
            'score_interval': [max(0, mean - radius), min(1, mean + radius)] if radius is not None else None}



def match_summary(games, args):
    cadence = getattr(args, 'sequential_check_pairs', None)
    if cadence is None:
        return paired_summary(games, args.pairs)
    checks = (args.pairs + cadence - 1) // cadence
    checkpoint_confidence = 1 - 0.05 / checks
    summary = paired_summary(games, confidence=checkpoint_confidence)
    pairs = summary['completed_pairs']
    checkpoint = (pairs > 0 and len(games) == 2 * pairs and
                  (pairs % cadence == 0 or pairs == args.pairs))
    if not checkpoint:
        summary['score_interval'] = None
    summary.update(confidence=0.95, checkpoint_confidence=checkpoint_confidence,
                   planned_checks=checks, checkpoint=checkpoint,
                   interval_method='opening-pair Hoeffding with Bonferroni checkpoints')
    return summary


def match_game_type():
    import chess
    import chess.pgn
    from .variants import royal_losers_board

    class match_game(chess.pgn.Game):
        def board(self):
            variant = self.headers.get('Variant', 'Standard')
            if variant not in ('Standard', 'Royal losers'):
                raise ValueError(f'unsupported match PGN variant: {variant}')
            return (royal_losers_board if variant == 'Royal losers' else chess.Board)(
                self.headers.get('FEN', chess.STARTING_FEN))
    return match_game


def read_match_pgn(stream):
    """Read our PGNs without changing python-chess's global variant registry."""
    import chess.pgn
    class match_builder(chess.pgn.GameBuilder):
        def __init__(self):
            super().__init__(Game=match_game_type())

        def begin_headers(self):
            # Headers.variant() is used by python-chess before Game.board().
            class match_headers(chess.pgn.Headers):
                def variant(self):
                    from .variants import royal_losers_board
                    if self.get('Variant') == 'Royal losers':
                        return royal_losers_board
                    return super().variant()
            self.game.headers = match_headers()
            return self.game.headers
    return chess.pgn.read_game(stream, Visitor=match_builder)


async def play_game(engines, board, *, a_white, seconds, increment, max_plies,
                    pair, opening, variant):
    import chess
    import chess.engine
    game = match_game_type()()
    initial = board.root()
    game.headers.update(Event='Maxima offline paired match', Site='offline',
                        Date='????.??.??', Round=str(pair + 1),
                        White='A' if a_white else 'B', Black='B' if a_white else 'A',
                        Variant='Royal losers' if variant == 'losers' else 'Standard',
                        Opening=opening, TimeControl=f'{seconds:g}+{increment:g}')
    if initial.fen() != chess.STARTING_FEN:
        game.headers.update(SetUp='1', FEN=initial.fen(en_passant='fen'))
    node = game
    for move in board.move_stack:
        node = node.add_variation(move)
    clocks = {chess.WHITE: seconds, chess.BLACK: seconds}
    token = object()
    result, reason = '*', 'ply limit'
    failure = None
    for _ in range(max_plies):
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            result, reason = outcome.result(), outcome.termination.name.lower()
            break
        color = board.turn
        engine = engines[0 if color == a_white else 1]
        limit = chess.engine.Limit(white_clock=clocks[chess.WHITE],
                                   black_clock=clocks[chess.BLACK],
                                   white_inc=increment, black_inc=increment)
        # Charge protocol/readiness and runner overhead, not engine-reported time.
        started = asyncio.get_running_loop().time()
        try:
            best, _ = await engine.search(board, limit, game=token)
        except (OSError, ValueError, RuntimeError, chess.engine.EngineError, asyncio.TimeoutError) as error:
            failure = str(error) or type(error).__name__
            reason = 'engine failure'
            break
        elapsed = asyncio.get_running_loop().time() - started
        clocks[color] -= elapsed
        if clocks[color] <= 0:
            winner = not color
            result = ('1/2-1/2' if board.has_insufficient_material(winner)
                      else '1-0' if winner else '0-1')
            reason = 'time forfeit'
            break
        if best.move is None or not board.is_legal(best.move):
            failure = 'engine returned no legal move in a nonterminal position'
            reason = 'engine failure'
            break
        board.push(best.move)
        clocks[color] += increment
        node = node.add_variation(best.move)
        remaining = clocks[color]
        hours, rest = divmod(remaining, 3600)
        minutes, rest = divmod(rest, 60)
        node.comment = f'[%clk {int(hours)}:{int(minutes):02}:{rest:06.3f}]'
    else:
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            result, reason = outcome.result(), outcome.termination.name.lower()
    game.headers.update(Result=result, Termination=reason)
    return game, {'pair': pair, 'opening': opening, 'a_color': 'white' if a_white else 'black',
                  'result': result, 'termination': reason, 'error': failure, 'plies': len(board.move_stack),
                  'clocks': {'white': clocks[chess.WHITE], 'black': clocks[chess.BLACK]}}


async def run_match(commands, openings, args, report, pgn_stream):
    from .uci import open_engine
    profiles = getattr(args, 'profiles', [engine_profile([], []), engine_profile([], [])])
    selected = random.Random(args.seed).sample(openings, args.pairs)
    report['selected_openings'] = [identifier for identifier, _ in selected]
    for pair, (identifier, position) in enumerate(selected):
        for a_white in (True, False):
            async with AsyncExitStack() as stack:
                engines = []
                for command in commands:
                    directory = stack.enter_context(tempfile.TemporaryDirectory(prefix='maxima-match-'))
                    engines.append(await stack.enter_async_context(open_engine(command, directory, args.timeout)))
                metadata = []
                configurations = []
                for engine, profile in zip(engines, profiles):
                    verify_models(profile)
                    settings = {'Hash': args.hash_mb, 'Threads': 1, 'OwnBook': False,
                                'MultiPV': 1, 'Ponder': False, 'DrawContempt': 0,
                                'UCI_AnalyseMode': False, 'SyzygyProbeLimit': 0,
                                'UseExperience': False,
                                'Wild': 'losers' if args.variant == 'losers' else 'standard'}
                    if args.variant == 'losers':
                        option = engine.protocol.options.get('Wild')
                        if option is None or option.type != 'combo' or 'losers' not in option.var:
                            raise ValueError('engine does not support royal losers through Wild=losers')
                    configurations.append(validated_settings(engine.protocol.options, settings, profile))
                # Check both engines before configuring either one.
                for engine, settings, profile in zip(engines, configurations, profiles):
                    applied = await engine.configure(settings)
                    metadata.append({'id': engine.identity, 'options': applied,
                                     'models': profile['models'],
                                     'advertised_defaults': {name: option.default for name, option
                                                             in engine.protocol.options.items()}})
                game, record = await play_game(
                    engines, position.copy(), a_white=a_white, seconds=args.seconds,
                    increment=args.increment, max_plies=args.max_plies, pair=pair,
                    opening=identifier, variant=args.variant)
                for label, engine in zip(('EngineA', 'EngineB'), engines):
                    game.headers[label] = '; '.join(engine.identity)
                record['engines'] = metadata
                try:
                    for profile in profiles:
                        verify_models(profile)
                except (OSError, ValueError) as error:
                    record['error'] = str(error)
                    record['result'] = '*'
                    record['termination'] = 'model identity failure'
                    game.headers.update(Result='*', Termination=record['termination'])
                report['games'].append(record)
                pgn_stream.write(str(game) + '\n\n')
                pgn_stream.flush()
                report['summary'] = match_summary(report['games'], args)
                interval = report['summary']['score_interval']
                if (getattr(args, 'sequential_check_pairs', None) is not None and
                        interval is not None and (interval[0] > 0.5 or interval[1] < 0.5)):
                    report['stop_reason'] = 'a_stronger' if interval[0] > 0.5 else 'b_stronger'
                    report['stopped_early'] = pair + 1 < args.pairs
                args.json.write_text(json.dumps(report, indent=2) + '\n')
                if record['error'] is not None:
                    raise RuntimeError(record['error'])
                print(f"Pair {pair + 1} A={'White' if a_white else 'Black'} "
                      f"{record['result']} ({record['termination']})", flush=True)
                if report.get('stop_reason') in ('a_stronger', 'b_stronger'):
                    return


def positive_float(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return value


def nonnegative_float(text):
    value = float(text)
    if not math.isfinite(value) or value < 0:
        raise argparse.ArgumentTypeError('must be finite and nonnegative')
    return value


def main():
    from .run_suite import positive_int
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-a', required=True, type=Path)
    parser.add_argument('--engine-b', required=True, type=Path)
    for label in ('a', 'b'):
        parser.add_argument(f'--option-{label}', action='append', type=option_assignment,
                            default=[], metavar='NAME=VALUE', help='repeatable per-engine UCI option')
        parser.add_argument(f'--model-{label}', action='append', type=option_assignment,
                            default=[], metavar='OPTION=PATH', help='set and hash an external model file')
    parser.add_argument('--variant', choices=('standard', 'losers'), default='standard')
    parser.add_argument('--openings', required=True, type=Path, help='variant-labeled JSON opening file')
    parser.add_argument('--pairs', type=positive_int, default=10)
    parser.add_argument('--sequential-check-pairs', type=positive_int, metavar='N',
                        help='check significance every N pairs with a 95%% joint Bonferroni bound')
    parser.add_argument('--seed', type=int, default=2026)
    parser.add_argument('--seconds', type=positive_float, default=10)
    parser.add_argument('--increment', type=nonnegative_float, default=0.1)
    parser.add_argument('--max-plies', type=positive_int, default=600)
    parser.add_argument('--timeout', type=positive_float, default=30)
    parser.add_argument('--hash', type=positive_int, default=64, dest='hash_mb')
    parser.add_argument('--pgn', required=True, type=Path)
    parser.add_argument('--json', required=True, type=Path)
    args = parser.parse_args()
    report = None
    try:
        openings = read_openings(args.openings, args.variant)
        if args.sequential_check_pairs is not None and args.sequential_check_pairs > args.pairs:
            raise ValueError('sequential checkpoint cadence cannot exceed maximum pairs')
        if args.pairs > len(openings):
            raise ValueError('pairs cannot exceed distinct openings (sampling without replacement)')
        args.profiles = [engine_profile(args.option_a, args.model_a),
                         engine_profile(args.option_b, args.model_b)]
        commands = [[str(path.resolve())] for path in (args.engine_a, args.engine_b)]
        paths = [args.openings.resolve(), args.engine_a.resolve(), args.engine_b.resolve(),
                 args.pgn.resolve(), args.json.resolve()]
        model_paths = [Path(identity['path']) for profile in args.profiles
                       for identity in profile['models'].values()]
        if paths[-1] == paths[-2] or any(path in paths[:3] + model_paths for path in paths[-2:]):
            raise ValueError('output paths must be distinct from inputs and each other')
        if args.pgn.exists() or args.json.exists():
            raise ValueError('output files already exist; choose new paths')
        report = {'variant': args.variant, 'seed': args.seed, 'pairs': args.pairs,
                  'seconds': args.seconds, 'increment': args.increment,
                  'max_plies': args.max_plies, 'timeout': args.timeout,
                  'engines': [{**file_identity(command[0]), **profile}
                              for command, profile in zip(commands, args.profiles)],
                  'openings_sha256': hashlib.sha256(args.openings.read_bytes()).hexdigest(),
                  'games': [], 'complete': False, 'stopped_early': False, 'stop_reason': None,
                  'sequential_check_pairs': args.sequential_check_pairs}
        # Refuse to overwrite an earlier match record.
        with args.pgn.open('x') as stream, args.json.open('x') as output:
            output.write(json.dumps(report, indent=2) + '\n')
            output.flush()
            asyncio.run(run_match(commands, openings, args, report, stream))
        report['complete'] = (all(game['result'] != '*' for game in report['games']) and
                              (len(report['games']) == 2 * args.pairs or report['stopped_early']))
        if report['stop_reason'] is None and report['complete']:
            report['stop_reason'] = 'maximum_pairs'
        args.json.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], indent=2))
        return 0 if report['complete'] else 1
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Match failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
