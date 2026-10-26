"""Import game experience and publish an immutable engine snapshot."""
import argparse
import asyncio
from contextlib import closing
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
import os
import sys
from .project import data_root, engine_path, project_root


header = struct.Struct('<8sQ')
record = struct.Struct('<QIIIIiiQQIIQIIII')
magic = b'MXEXP001'


def open_store(path):
    from .experience_store import open_store as load_store
    return load_store(path)


def key_of(board):
    import chess.polyglot
    return f'{chess.polyglot.zobrist_hash(board):016x}'


def games_from(path):
    import chess.pgn
    if path.is_dir():
        paths = sorted(path.rglob('*.pgn'))
        if not paths:
            raise ValueError(f'no PGN files found in {path}')
        for source in paths:
            yield from games_from(source)
        return
    with path.open(encoding='utf-8-sig') as stream:
        while True:
            game = chess.pgn.read_game(stream)
            if game is None:
                return
            if game.errors or type(game.board()) is not chess.Board or not game.board().is_valid() or game.board().chess960:
                raise ValueError('invalid or unsupported PGN game')
            yield game


def game_id(game):
    tags = ('Event', 'Site', 'Date', 'Time', 'UTCDate', 'UTCTime', 'Round',
            'White', 'Black', 'Result', 'TimeControl')
    content = [game.board().fen(), [move.uci() for move in game.mainline_moves()],
               {tag: game.headers.get(tag, '') for tag in tags}]
    return hashlib.sha256(json.dumps(content, sort_keys=True).encode()).hexdigest()


def store_analysis(store, board, pv, depth, score, score_type, nodes, think_ms, engine):
    if not pv or depth <= 0:
        return
    replay = board.copy()
    for move in pv:
        if move not in replay.legal_moves:
            raise ValueError('analysis PV contains an illegal move')
        replay.push(move)
    row = [key_of(board), pv[0].uci(), engine, board.halfmove_clock, depth,
           score, score_type, nodes, think_ms, [move.uci() for move in pv]]
    identity = json.dumps(row[:4])
    old = store.data['analysis'].get(identity)
    if old is None or (depth, nodes, think_ms) > (old[4], old[7], old[8]):
        store.data['analysis'][identity] = row


def import_games(store, path, players, max_ply):
    imported = duplicates = analyses = 0
    with store:
        for game in games_from(path):
            names = [game.headers.get('Black', ''), game.headers.get('White', '')]
            aliases = players
            if aliases is None:
                aliases = {'maxima', 'bugchess', 'maximaengine'}
                color = game.headers.get('LearningColor')
                if color in ('white', 'black'):
                    aliases.add(game.headers.get(color.title(), '').casefold())
            if not any(name.casefold() in aliases for name in names):
                continue
            identifier = game_id(game)
            inserted = identifier not in store.data['games']
            store.data['games'].setdefault(identifier, dict(game.headers))
            observations = set(store.data['observations'].get(identifier, []))
            imported += inserted
            duplicates += not inserted
            board = game.board()
            for ply, node in enumerate(game.mainline()):
                if ply >= max_ply:
                    break
                if node.move not in board.legal_moves:
                    raise ValueError('illegal game move')
                outcome = game.headers.get('Result', '*')
                if outcome not in ('1-0', '0-1', '1/2-1/2', '*'):
                    raise ValueError('invalid game result')
                move_key = key_of(board) + '/' + node.move.uci()
                observed = outcome != '*' and move_key not in observations
                if observed:
                    observations.add(move_key)
                    draw = outcome == '1/2-1/2'
                    win = not draw and ((outcome == '1-0') == board.turn)
                    own = names[board.turn].casefold() in aliases
                    counts = [int(win), int(draw), int(not win and not draw), int(own), int(not own),
                              int(own and win), int(own and draw), int(own and not win and not draw)]
                    row = store.data['moves'].setdefault(move_key, [key_of(board), node.move.uci()] + [0] * 8)
                    row[2:] = [before + after for before, after in zip(row[2:], counts)]
                evaluation, depth = node.eval(), node.eval_depth()
                if evaluation is not None and depth:
                    pv_match = re.search(r'\[%pv\s+([^\]]+)\]', node.comment)
                    pv = []
                    replay = board.copy()
                    for token in (pv_match.group(1).split() if pv_match else [node.move.uci()]):
                        move = replay.parse_san(token)
                        if not move:
                            raise ValueError('null move in analysis')
                        pv.append(move)
                        replay.push(move)
                    score = evaluation.pov(board.turn)
                    nodes_match = re.search(r'\[%nodes\s+(\d+)\]', node.comment)
                    engine = game.headers.get('WhiteEngine' if board.turn else 'BlackEngine',
                                              names[board.turn])
                    store_analysis(store, board, pv, depth,
                                   score.mate() if score.is_mate() else score.score(),
                                   int(score.is_mate()), int(nodes_match.group(1)) if nodes_match else 0,
                                   round((node.emt() or 0) * 1000), engine)
                    analyses += 1
                board.push(node.move)
            store.data['observations'][identifier] = sorted(observations)
    return {'imported': imported, 'duplicates': duplicates, 'analyses': analyses}


def export_snapshot(store, path):
    import chess
    if path.exists():
        with path.open('rb') as existing:
            if existing.read(8) != magic:
                raise ValueError('refusing to replace a non-experience file')
    data = {}
    for key, move, wins, draws, losses, own, opponent, own_wins, own_draws, own_losses in store.data['moves'].values():
        data[key, move] = [wins, draws, losses, 0, 0, 0, 0, 0, 0, 0, own_wins, own_draws, own_losses]
    for key, move, engine, clock, depth, score, score_type, nodes, think_ms, pv in store.data['analysis'].values():
        row = data.setdefault((key, move), [0] * 13)
        if (depth, nodes, think_ms) > (row[3], row[5], row[6]):
            row[3:10] = [depth, score, nodes, think_ms, clock, score_type,
                       int.from_bytes(hashlib.sha256(engine.encode()).digest()[:8], 'little')]
    entries = []
    for (key, text), row in data.items():
        move = chess.Move.from_uci(text)
        promotion = 0 if move.promotion is None else move.promotion - 1
        code = move.from_square | move.to_square << 6 | promotion << 12
        wins, draws, losses, depth, score, nodes, think_ms, clock, score_type, engine, own_wins, own_draws, own_losses = row
        entries.append((int(key, 16), code, wins, draws, losses, min(depth, 128),
                        score, nodes, think_ms, clock, score_type, engine, own_wins, own_draws, own_losses, 0))
    entries.sort(key=lambda row: row[:2])
    if len(entries) > 2000000:
        raise ValueError('experience snapshot exceeds two million entries')
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(header.pack(magic, len(entries)))
            for row in entries:
                stream.write(record.pack(*row))
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return len(entries)


async def analyze_position(engine, board, movetime, game_token):
    import chess.engine
    latest = None

    def observe(info, elapsed_ms):
        nonlocal latest
        if (info.get('pv') and info.get('depth') and 'score' in info
                and not info.get('lowerbound') and not info.get('upperbound')):
            latest = {'pv': info['pv'], 'depth': info['depth'],
                      'nodes': info.get('nodes', 0), 'score': info['score']}

    best, elapsed_ms = await engine.search(
        board, chess.engine.Limit(time=movetime / 1000), game=game_token, on_info=observe)
    if latest and latest['pv'][0] == best.move:
        latest['think_ms'] = round(elapsed_ms)
        return latest
    return None


async def analyze_games(store, path, engine_path, max_ply, movetime):
    from .uci import open_engine
    changes = []
    with tempfile.TemporaryDirectory(prefix='maxima-experience-') as directory:
        async with open_engine([str(engine_path.resolve())], directory,
                               max(10, movetime / 1000 + 10)) as engine:
            identity = '\n'.join('id ' + line for line in engine.identity)
            await engine.configure({'Hash': 64, 'OwnBook': False,
                                    'UCI_AnalyseMode': True, 'SyzygyProbeLimit': 0})
            with store:
                for game in games_from(path):
                    board = game.board()
                    game_token = object()
                    for ply, node in enumerate(game.mainline()):
                        if ply >= max_ply:
                            break
                        actor = board.turn
                        best = await analyze_position(engine, board, movetime, game_token)
                        if best:
                            score = best['score'].pov(actor)
                            store_analysis(store, board, best['pv'], best['depth'],
                                           score.mate() if score.is_mate() else score.score(),
                                           int(score.is_mate()), best['nodes'], best['think_ms'], identity)
                            if best['pv'][0] != node.move:
                                child = board.copy()
                                child.push(node.move)
                                played = await analyze_position(engine, child, movetime, game_token)
                                if played:
                                    played_score = played['score'].pov(actor)
                                    store_analysis(store, board, [node.move] + played['pv'],
                                                   played['depth'] + 1,
                                                   played_score.mate() if played_score.is_mate() else played_score.score(),
                                                   int(played_score.is_mate()), played['nodes'], played['think_ms'], identity)
                                    changes.append({'ply': ply + 1, 'played': node.move.uci(),
                                                    'candidate': best['pv'][0].uci(), 'depth': best['depth'],
                                                    'best_score': str(score), 'played_score': str(played_score),
                                                    'estimated_loss_cp': score.score(mate_score=30000)
                                                        - played_score.score(mate_score=30000)})
                        board.push(node.move)
    return changes


def positive_int(text):
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return value


def main():
    root = project_root()
    descriptions = {
        'import': 'Import PGN results and annotations, then export experience.bin.',
        'analyze': 'Find better PGN continuations with engine analysis, then export experience.bin.',
        'export': 'Export saved game statistics and analysis to experience.bin.',
        'show': 'Inspect saved move statistics and analysis for a position.',
    }
    command = sys.argv[1] if len(sys.argv) > 1 else ''
    parser = argparse.ArgumentParser(prog=f'maxima experience:book:{command}',
                                     description=descriptions.get(command, __doc__))
    if command not in descriptions:
        parser.error('expected import, analyze, export or show')
    pgn_input = command in ('import', 'analyze')
    default_store = data_root() / 'experience/experience.json'
    default_source = data_root() / 'experience/games' if pgn_input else default_store
    parser.add_argument('--source', type=Path, default=default_source,
                        help=f'{"PGN file or folder" if pgn_input else "saved experience data"}; default: {default_source}')
    parser.add_argument('--store', type=Path,
                        help=f'working data file; default: {default_store}' if pgn_input else argparse.SUPPRESS)
    parser.add_argument('--output', type=Path, default=data_root() / 'experience/experience.bin',
                        help='binary export path; default: experience/experience.bin' if command != 'show' else argparse.SUPPRESS)
    parser.add_argument('--player', action='append',
                        help='our player aliases; default: Maxima/BugChess/MaximaEngine and saved LearningColor' if command == 'import' else argparse.SUPPRESS)
    parser.add_argument('--max-ply', type=positive_int, default=32,
                        help='opening horizon in plies; default: 32 (16 full moves)' if pgn_input else argparse.SUPPRESS)
    parser.add_argument('--fen', default='rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
                        help='position to inspect; default: starting position' if command == 'show' else argparse.SUPPRESS)
    parser.add_argument('--engine', type=Path, default=engine_path(root),
                        help='UCI engine; default: Release Maxima' if command == 'analyze' else argparse.SUPPRESS)
    parser.add_argument('--movetime', type=positive_int, default=1000,
                        help='milliseconds per search; up to two searches per position; default: 1000' if command == 'analyze' else argparse.SUPPRESS)
    args = parser.parse_args(sys.argv[2:])
    args.store = args.store or (default_store if pgn_input else args.source)
    import chess
    try:
        legacy = args.store if args.store.suffix in ('.sqlite', '.db') else args.store.with_suffix('.sqlite')
        if args.store == legacy:
            args.store = legacy.with_suffix('.json')
        if not args.store.exists() and legacy.exists():
            from .experience_migration import migrate_legacy
            migrate_legacy(legacy, args.store)
            print(f'Converted legacy experience to {args.store}; original retained at {legacy}')
        with closing(open_store(args.store)) as store:
            if pgn_input:
                source = args.source
                if command == 'import':
                    result = import_games(store, source,
                                          {p.casefold() for p in args.player} if args.player else None, args.max_ply)
                    print(json.dumps(result))
                else:
                    print(json.dumps(asyncio.run(analyze_games(store, source, args.engine, args.max_ply, args.movetime))))
            if command == 'show':
                board = chess.Board(args.fen)
                for key, move, wins, draws, losses, own, opponent, own_wins, own_draws, own_losses in store.data['moves'].values():
                    if key != key_of(board):
                        continue
                    games = wins + draws + losses
                    print(f'{move}: {wins}W {draws}D {losses}L, score {(wins + draws / 2) / games:.1%}, '
                          f'{own} own / {opponent} opponent games')
                    if own:
                        print(f'  Maxima: {own_wins}W {own_draws}D {own_losses}L, '
                              f'score {(own_wins + own_draws / 2) / own:.1%}, '
                              f'smoothed {(own_wins + own_draws / 2 + 16) / (own + 32):.1%}')
                    else:
                        print(f'  Opponent-only estimate {(wins + draws / 2 + 32) / (games + 64):.1%}')
                for key, move, engine, clock, depth, score, score_type, nodes, think_ms, pv in sorted(
                        store.data['analysis'].values(), key=lambda row: row[4], reverse=True):
                    if key != key_of(board):
                        continue
                    print(f'{move}: analysis depth {depth}, {nodes} nodes, {think_ms}ms, engine {engine}, PV {pv}')
            else:
                print(f'Published {export_snapshot(store, args.output)} entries to {args.output}')
        return 0
    except (OSError, ValueError, struct.error, RuntimeError) as error:
        print(f'Experience failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
