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
    bounds = getattr(args, 'sprt', None)
    minimum = getattr(args, 'min_elo', None)
    hybrid = getattr(args, 'sprt_min_elo_stop', False)
    if minimum is not None or hybrid:
        from .strength import min_elo_summary
        alpha = min(args.sprt_alpha, args.sprt_beta) / 2 if hybrid else 0.05
        strength = min_elo_summary(games, bounds[1] if hybrid else minimum, args.pairs, alpha)
        summary = paired_summary(games, args.pairs)
        summary.update(confidence=strength['confidence'], score_interval=strength['score_interval'],
                       interval_method=strength['method'], a_score=strength['a_score'],
                       minimum_gain=strength)
        if hybrid:
            from .sprt import sprt_summary
            summary['sprt'] = sprt_summary(games, *bounds, args.sprt_alpha / 2, args.sprt_beta / 2)
        return summary
    if bounds is not None:
        from .sprt import sprt_summary
        summary = paired_summary(games, args.pairs)
        summary.update(confidence=None, score_interval=None, interval_method=None,
                       sprt=sprt_summary(games, *bounds, args.sprt_alpha, args.sprt_beta))
        return summary
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


class win_adjudicator:
    """Require consecutive, exact main-line scores from both players."""
    def __init__(self, threshold_cp, plies, min_move):
        if threshold_cp <= 0 or plies < 2 or min_move < 1:
            raise ValueError('win adjudication requires positive cp/min-move and at least two plies')
        self.threshold_cp = threshold_cp
        self.plies = plies
        self.min_move = min_move
        self.winner = None
        self.evidence = []

    def observe(self, info, color, move_number, ply):
        score = info.get('score')
        value = (score.white().score(mate_score=100000) if score is not None
                 and not info.get('lowerbound') and not info.get('upperbound') else None)
        winner = (value > 0 if value is not None and abs(value) >= self.threshold_cp
                  and move_number >= self.min_move else None)
        if winner is None or winner != self.winner:
            self.evidence = []
        self.winner = winner
        if winner is None:
            return None
        self.evidence.append({'ply': ply, 'engine_color': 'white' if color else 'black',
                              'score_white_cp': score.white().score(),
                              'mate_white': score.white().mate()})
        if len(self.evidence) >= self.plies and len({entry['engine_color'] for entry in self.evidence}) == 2:
            return {'winner': 'white' if winner else 'black', 'threshold_cp': self.threshold_cp,
                    'required_plies': self.plies, 'min_move': self.min_move,
                    'evidence': list(self.evidence)}
        return None


async def play_game(engines, board, *, a_white, seconds, increment, max_plies,
                    pair, opening, variant, adjudicate_win_cp=None,
                    adjudicate_win_plies=6, adjudicate_min_move=20):
    import chess
    import chess.engine
    adjudicator = None
    if adjudicate_win_cp is not None:
        if variant != 'standard':
            raise ValueError('win adjudication currently supports standard chess only')
        adjudicator = win_adjudicator(adjudicate_win_cp, adjudicate_win_plies, adjudicate_min_move)
    adjudication = None
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
    search_work = {color: {'nodes': 0, 'searches': 0, 'reported_searches': 0,
                           'wall_seconds': 0.0} for color in (chess.WHITE, chess.BLACK)}
    search_samples = []
    result, reason = '*', 'ply limit'
    failure = None
    for _ in range(max_plies):
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            result, reason = outcome.result(), outcome.termination.name.lower()
            break
        color = board.turn
        move_number = board.fullmove_number
        search_info = {}
        search_nodes = None
        search_data = {}

        def collect_info(info, elapsed_ms):
            nonlocal search_nodes
            for name in ('depth', 'seldepth', 'tbhits', 'time'):
                if name in info:
                    search_data[name] = info[name]
            if info.get('string', '').startswith('search_result '):
                search_data['search_result'] = info['string']
            if 'nodes' in info:
                search_nodes = info['nodes']
            if 'score' in info:
                search_info.clear()
                search_info.update(info)

        engine = engines[0 if color == a_white else 1]
        limit = chess.engine.Limit(white_clock=clocks[chess.WHITE],
                                   black_clock=clocks[chess.BLACK],
                                   white_inc=increment, black_inc=increment)
        # Charge protocol/readiness and runner overhead, not engine-reported time.
        started = asyncio.get_running_loop().time()
        try:
            best, _ = await engine.search(board, limit, game=token, on_info=collect_info)
        except (OSError, ValueError, RuntimeError, chess.engine.EngineError, asyncio.TimeoutError) as error:
            failure = str(error) or type(error).__name__
            reason = 'engine failure'
            break
        elapsed = asyncio.get_running_loop().time() - started
        work = search_work[color]
        work['searches'] += 1
        work['wall_seconds'] += elapsed
        search_samples.append(dict(search_data, ply=len(board.move_stack) + 1,
                                   engine='A' if color == a_white else 'B',
                                   color='white' if color else 'black',
                                   nodes=search_nodes, wall_seconds=elapsed))
        if search_nodes is not None:
            work['nodes'] += search_nodes
            work['reported_searches'] += 1
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
        if search_nodes is not None:
            node.comment += f' [%nodes {search_nodes}]'
        if 'depth' in search_data:
            node.comment += f" [%depth {search_data['depth']}]"
        if adjudicator:
            outcome = board.outcome(claim_draw=True)
            if outcome is not None:
                result, reason = outcome.result(), outcome.termination.name.lower()
                break
            adjudication = adjudicator.observe(search_info, color, move_number, len(board.move_stack))
            if adjudication is not None:
                result = '1-0' if adjudication['winner'] == 'white' else '0-1'
                reason = 'win adjudication'
                node.comment += (f' Win adjudication: both engines agreed for '
                                 f'{adjudicate_win_plies} plies at >= {adjudicate_win_cp} cp.')
                break
    else:
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            result, reason = outcome.result(), outcome.termination.name.lower()
    game.headers.update(Result=result, Termination=reason)
    record = {'pair': pair, 'opening': opening, 'a_color': 'white' if a_white else 'black',
                  'result': result, 'termination': reason, 'error': failure, 'plies': len(board.move_stack),
                  'clocks': {'white': clocks[chess.WHITE], 'black': clocks[chess.BLACK]}}
    record['search_work'] = {'white': search_work[chess.WHITE], 'black': search_work[chess.BLACK]}
    record['search_samples'] = search_samples
    if adjudicator:
        game.headers['WinAdjudication'] = (f'cp={adjudicate_win_cp}; plies={adjudicate_win_plies}; '
                                          f'min_move={adjudicate_min_move}')
        record['adjudication'] = adjudication
    return game, record


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
                    if getattr(args, 'syzygy_path', None) is not None:
                        if any(name.lower() in ('syzygypath', 'syzygyprobelimit', 'syzygyprobedepth')
                               for name in profile['requested_options']):
                            raise ValueError('Syzygy options are controlled by --syzygy-path')
                        if not all(name in engine.protocol.options for name in ('SyzygyPath', 'SyzygyProbeLimit')):
                            raise ValueError('engine does not advertise SyzygyPath and SyzygyProbeLimit')
                        settings.update(SyzygyPath=str(args.syzygy_path),
                                        SyzygyProbeLimit=5, SyzygyProbeDepth=1)
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
                    opening=identifier, variant=args.variant,
                    adjudicate_win_cp=getattr(args, 'adjudicate_win_cp', None),
                    adjudicate_win_plies=getattr(args, 'adjudicate_win_plies', 6),
                    adjudicate_min_move=getattr(args, 'adjudicate_min_move', 20))
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
                sprt = report['summary'].get('sprt')
                strength = report['summary'].get('minimum_gain')
                if strength is not None and strength['decision'] != 'continue':
                    report['stop_reason'] = strength['decision']
                    report['stopped_early'] = pair + 1 < args.pairs
                if sprt is not None and sprt['decision'] != 'continue':
                    report['stop_reason'] = report.get('stop_reason') or sprt['decision']
                    report['stopped_early'] = pair + 1 < args.pairs
                args.json.write_text(json.dumps(report, indent=2) + '\n')
                if record['error'] is not None:
                    raise RuntimeError(record['error'])
                print(f"Pair {pair + 1} A={'White' if a_white else 'Black'} "
                      f"{record['result']} ({record['termination']})", flush=True)
                if report.get('stop_reason') in ('a_stronger', 'b_stronger', 'accept_h0', 'accept_h1',
                                               'minimum_gain_established', 'below_minimum_gain'):
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
    stopping = parser.add_mutually_exclusive_group()
    stopping.add_argument('--sprt', type=float, nargs=2, metavar=('ELO0', 'ELO1'),
                          help='paired logistic GSPRT bounds for engine A versus B')
    stopping.add_argument('--min-elo', type=float, metavar='ELO',
                          help='stop when simultaneous 95%% bounds establish A above or below this minimum gain')
    stopping.add_argument('--sequential-check-pairs', type=positive_int, metavar='N',
                        help='check significance every N pairs with a 95%% joint Bonferroni bound')
    parser.add_argument('--sprt-alpha', type=float, default=0.05, help='SPRT false-positive target')
    parser.add_argument('--sprt-beta', type=float, default=0.05, help='SPRT false-negative target')
    parser.add_argument('--sprt-min-elo-stop', action='store_true',
                        help='also check confidence bounds against ELO1; reserve half each error budget for SPRT')
    parser.add_argument('--seed', type=int, default=2026)
    parser.add_argument('--seconds', type=positive_float, default=10)
    parser.add_argument('--increment', type=nonnegative_float, default=0.1)
    parser.add_argument('--max-plies', type=positive_int, default=600)
    parser.add_argument('--adjudicate-win-cp', type=positive_int, metavar='CP',
                        help='enable early wins when both engines consistently agree at this cp threshold')
    parser.add_argument('--adjudicate-win-plies', type=positive_int, default=6,
                        help='consecutive agreeing plies required (default 6, minimum 2)')
    parser.add_argument('--adjudicate-min-move', type=positive_int, default=20,
                        help='earliest full move eligible for win adjudication (default 20)')
    parser.add_argument('--timeout', type=positive_float, default=30)
    parser.add_argument('--hash', type=positive_int, default=64, dest='hash_mb')
    parser.add_argument('--syzygy-path', type=Path,
                        help='shared directory of standard WDL tables; enables up to five-piece engine probing')
    parser.add_argument('--pgn', required=True, type=Path)
    parser.add_argument('--json', required=True, type=Path)
    args = parser.parse_args()
    report = None
    try:
        if args.syzygy_path is not None:
            args.syzygy_path = args.syzygy_path.resolve()
            if args.variant != 'standard':
                raise ValueError('Syzygy tables require standard chess')
            if not args.syzygy_path.is_dir() or not any(args.syzygy_path.glob('*.rtbw')):
                raise ValueError('Syzygy path must be a directory containing WDL (.rtbw) tables')
        if args.adjudicate_win_cp is not None:
            if args.variant != 'standard':
                raise ValueError('win adjudication currently supports standard chess only')
            win_adjudicator(args.adjudicate_win_cp, args.adjudicate_win_plies, args.adjudicate_min_move)
        elif args.adjudicate_win_plies != 6 or args.adjudicate_min_move != 20:
            raise ValueError('win adjudication settings require --adjudicate-win-cp')
        if args.sprt is not None:
            from .sprt import validate_sprt
            validate_sprt(*args.sprt, args.sprt_alpha, args.sprt_beta)
            if args.sprt_min_elo_stop:
                from .strength import validate_min_elo
                validate_min_elo(args.sprt[1], args.pairs, min(args.sprt_alpha, args.sprt_beta) / 2)
        elif args.sprt_alpha != 0.05 or args.sprt_beta != 0.05:
            raise ValueError('SPRT error targets require --sprt')
        if args.sprt_min_elo_stop and args.sprt is None:
            raise ValueError('--sprt-min-elo-stop requires --sprt')
        if args.min_elo is not None:
            from .strength import validate_min_elo
            validate_min_elo(args.min_elo, args.pairs, 0.05)
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
                  'syzygy_path': str(args.syzygy_path) if args.syzygy_path is not None else None,
                  'seconds': args.seconds, 'increment': args.increment,
                  'max_plies': args.max_plies, 'timeout': args.timeout,
                  'win_adjudication': {'threshold_cp': args.adjudicate_win_cp,
                                       'plies': args.adjudicate_win_plies,
                                       'min_move': args.adjudicate_min_move}
                                      if args.adjudicate_win_cp is not None else None,
                  'engines': [{**file_identity(command[0]), **profile}
                              for command, profile in zip(commands, args.profiles)],
                  'openings_sha256': hashlib.sha256(args.openings.read_bytes()).hexdigest(),
                  'games': [], 'complete': False, 'stopped_early': False, 'stop_reason': None,
                  'sequential_check_pairs': args.sequential_check_pairs,
                  'minimum_gain': {'minimum_elo': args.sprt[1] if args.sprt_min_elo_stop else args.min_elo,
                                   'alpha': min(args.sprt_alpha, args.sprt_beta) / 2 if args.sprt_min_elo_stop else 0.05,
                                   'planned_checks': args.pairs, 'elo_model': 'logistic'}
                                  if args.min_elo is not None or args.sprt_min_elo_stop else None,
                  'stopping_mode': 'sprt_or_minimum_gain' if args.sprt_min_elo_stop else
                                   'minimum_gain' if args.min_elo is not None else
                                   'sprt' if args.sprt is not None else
                                   'sequential_checkpoints' if args.sequential_check_pairs else 'fixed_sample',
                  'sprt': {'elo0': args.sprt[0], 'elo1': args.sprt[1],
                           'alpha': args.sprt_alpha / 2 if args.sprt_min_elo_stop else args.sprt_alpha,
                           'beta': args.sprt_beta / 2 if args.sprt_min_elo_stop else args.sprt_beta,
                           'requested_alpha': args.sprt_alpha, 'requested_beta': args.sprt_beta,
                           'elo_model': 'logistic', 'upstream': 'Disservin/fast-chess',
                           'upstream_revision': 'ebfd64cb7fac80d4822c50b07dca4fe934779d21'}
                           if args.sprt is not None else None}
        # Refuse to overwrite an earlier match record.
        with args.pgn.open('x') as stream, args.json.open('x') as output:
            output.write(json.dumps(report, indent=2) + '\n')
            output.flush()
            asyncio.run(run_match(commands, openings, args, report, stream))
        report['complete'] = (all(game['result'] != '*' for game in report['games']) and
                              (len(report['games']) == 2 * args.pairs or report['stopped_early']))
        if report['stop_reason'] is None and report['complete']:
            report['stop_reason'] = 'maximum_pairs'
            if report['summary'].get('sprt') is not None:
                report['summary']['sprt']['decision'] = 'inconclusive'
            if report['summary'].get('minimum_gain') is not None:
                report['summary']['minimum_gain']['decision'] = 'inconclusive'
        args.json.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], indent=2))
        return 0 if report['complete'] else 1
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Match failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
