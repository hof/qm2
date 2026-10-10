"""Import game experience and publish an immutable engine snapshot."""
import argparse
import asyncio
from contextlib import closing
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import struct
import tempfile
import os
import sys
from .project import engine_path, project_root


header = struct.Struct('<8sQ')
record = struct.Struct('<QIIIIiiQQIIQIIII')
magic = b'MXEXP001'


def connect(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(str(path), timeout=30)
    if db.execute('PRAGMA user_version').fetchone()[0] not in (0, 1):
        db.close()
        raise ValueError('unsupported experience database version')
    db.executescript('''
        CREATE TABLE IF NOT EXISTS games (
            id TEXT PRIMARY KEY, headers TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS observations (
            game TEXT NOT NULL, key TEXT NOT NULL, move TEXT NOT NULL,
            PRIMARY KEY (game, key, move));
        CREATE TABLE IF NOT EXISTS moves (
            key TEXT NOT NULL, move TEXT NOT NULL, wins INTEGER NOT NULL DEFAULT 0,
            draws INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0,
            own_games INTEGER NOT NULL DEFAULT 0, opponent_games INTEGER NOT NULL DEFAULT 0,
            own_wins INTEGER NOT NULL DEFAULT 0, own_draws INTEGER NOT NULL DEFAULT 0,
            own_losses INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (key, move));
        CREATE TABLE IF NOT EXISTS analysis (
            key TEXT NOT NULL, move TEXT NOT NULL, engine TEXT NOT NULL,
            clock INTEGER NOT NULL, depth INTEGER NOT NULL, score INTEGER NOT NULL,
            score_type INTEGER NOT NULL, nodes INTEGER NOT NULL, think_ms INTEGER NOT NULL,
            pv TEXT NOT NULL, PRIMARY KEY (key, move, engine, clock));
    ''')
    db.execute('PRAGMA user_version=1')
    return db


def key_of(board):
    import chess.polyglot
    return f'{chess.polyglot.zobrist_hash(board):016x}'


def games_from(path):
    import chess.pgn
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


def store_analysis(db, board, pv, depth, score, score_type, nodes, think_ms, engine):
    if not pv or depth <= 0:
        return
    replay = board.copy()
    for move in pv:
        if move not in replay.legal_moves:
            raise ValueError('analysis PV contains an illegal move')
        replay.push(move)
    db.execute('''INSERT INTO analysis VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(key, move, engine, clock) DO UPDATE SET
        depth=excluded.depth, score=excluded.score, score_type=excluded.score_type,
        nodes=excluded.nodes, think_ms=excluded.think_ms, pv=excluded.pv
        WHERE (excluded.depth, excluded.nodes, excluded.think_ms) >
              (analysis.depth, analysis.nodes, analysis.think_ms)''',
        (key_of(board), pv[0].uci(), engine, board.halfmove_clock, depth,
         score, score_type, nodes, think_ms, json.dumps([m.uci() for m in pv])))


def import_games(db, path, players, max_ply):
    imported = duplicates = analyses = 0
    with db:
        for game in games_from(path):
            names = [game.headers.get('Black', ''), game.headers.get('White', '')]
            if not any(name.casefold() in players for name in names):
                continue
            identifier = game_id(game)
            inserted = db.execute('INSERT OR IGNORE INTO games VALUES (?, ?)',
                                  (identifier, json.dumps(dict(game.headers)))).rowcount
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
                observed = outcome != '*' and db.execute(
                    'INSERT OR IGNORE INTO observations VALUES (?, ?, ?)',
                    (identifier, key_of(board), node.move.uci())).rowcount
                if observed:
                    draw = outcome == '1/2-1/2'
                    win = not draw and ((outcome == '1-0') == board.turn)
                    own = names[board.turn].casefold() in players
                    db.execute('''INSERT INTO moves VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                        ON CONFLICT(key, move) DO UPDATE SET
                        wins=wins+excluded.wins, draws=draws+excluded.draws,
                        losses=losses+excluded.losses, own_games=own_games+excluded.own_games,
                        opponent_games=opponent_games+excluded.opponent_games,
                        own_wins=own_wins+excluded.own_wins, own_draws=own_draws+excluded.own_draws,
                        own_losses=own_losses+excluded.own_losses''',
                        (key_of(board), node.move.uci(), int(win), int(draw),
                         int(not win and not draw), int(own), int(not own),
                         int(own and win), int(own and draw), int(own and not win and not draw)))
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
                    store_analysis(db, board, pv, depth,
                                   score.mate() if score.is_mate() else score.score(),
                                   int(score.is_mate()), int(nodes_match.group(1)) if nodes_match else 0,
                                   round((node.emt() or 0) * 1000), engine)
                    analyses += 1
                board.push(node.move)
    return {'imported': imported, 'duplicates': duplicates, 'analyses': analyses}


def export_snapshot(db, path):
    import chess
    if path.exists():
        with path.open('rb') as existing:
            if existing.read(8) != magic:
                raise ValueError('refusing to replace a non-experience file')
    data = {}
    for key, move, wins, draws, losses, own_wins, own_draws, own_losses in db.execute(
            'SELECT key,move,wins,draws,losses,own_wins,own_draws,own_losses FROM moves'):
        data[key, move] = [wins, draws, losses, 0, 0, 0, 0, 0, 0, 0, own_wins, own_draws, own_losses]
    for key, move, engine, clock, depth, score, score_type, nodes, think_ms in db.execute(
            'SELECT key, move, engine, clock, depth, score, score_type, nodes, think_ms FROM analysis'):
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


async def analyze_games(db, path, engine_path, max_ply, movetime):
    from .uci import open_engine
    changes = []
    with tempfile.TemporaryDirectory(prefix='maxima-experience-') as directory:
        async with open_engine([str(engine_path.resolve())], directory,
                               max(10, movetime / 1000 + 10)) as engine:
            identity = '\n'.join('id ' + line for line in engine.identity)
            await engine.configure({'Hash': 64, 'OwnBook': False,
                                    'UCI_AnalyseMode': True, 'SyzygyProbeLimit': 0})
            with db:
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
                            store_analysis(db, board, best['pv'], best['depth'],
                                           score.mate() if score.is_mate() else score.score(),
                                           int(score.is_mate()), best['nodes'], best['think_ms'], identity)
                            if best['pv'][0] != node.move:
                                child = board.copy()
                                child.push(node.move)
                                played = await analyze_position(engine, child, movetime, game_token)
                                if played:
                                    played_score = played['score'].pov(actor)
                                    store_analysis(db, board, [node.move] + played['pv'],
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('import', 'export', 'show', 'analyze'))
    parser.add_argument('--database', type=Path, default=root / 'experience/experience.sqlite')
    parser.add_argument('--output', type=Path, default=root / 'experience/experience.bin')
    parser.add_argument('--pgn', type=Path)
    parser.add_argument('--player', action='append', help='player aliases; default Maxima and BugChess')
    parser.add_argument('--max-ply', type=positive_int, default=32)
    parser.add_argument('--fen', default='rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1')
    parser.add_argument('--engine', type=Path, default=engine_path(root))
    parser.add_argument('--movetime', type=positive_int, default=1000)
    args = parser.parse_args()
    import chess
    try:
        with closing(connect(args.database)) as db:
            if args.command in ('import', 'analyze'):
                if args.pgn is None:
                    parser.error('--pgn is required')
                if args.command == 'import':
                    result = import_games(db, args.pgn,
                                          {p.casefold() for p in (args.player or ['Maxima', 'BugChess'])}, args.max_ply)
                    print(json.dumps(result))
                else:
                    print(json.dumps(asyncio.run(analyze_games(db, args.pgn, args.engine, args.max_ply, args.movetime))))
            if args.command == 'show':
                board = chess.Board(args.fen)
                for move, wins, draws, losses, own, opponent, own_wins, own_draws, own_losses in db.execute(
                        'SELECT move,wins,draws,losses,own_games,opponent_games,own_wins,own_draws,own_losses FROM moves WHERE key=?', (key_of(board),)):
                    games = wins + draws + losses
                    print(f'{move}: {wins}W {draws}D {losses}L, score {(wins + draws / 2) / games:.1%}, '
                          f'{own} own / {opponent} opponent games')
                    if own:
                        print(f'  Maxima: {own_wins}W {own_draws}D {own_losses}L, '
                              f'score {(own_wins + own_draws / 2) / own:.1%}, '
                              f'smoothed {(own_wins + own_draws / 2 + 16) / (own + 32):.1%}')
                    else:
                        print(f'  Opponent-only estimate {(wins + draws / 2 + 32) / (games + 64):.1%}')
                for move, engine, depth, nodes, think_ms, pv in db.execute(
                        'SELECT move,engine,depth,nodes,think_ms,pv FROM analysis WHERE key=? ORDER BY depth DESC',
                        (key_of(board),)):
                    print(f'{move}: analysis depth {depth}, {nodes} nodes, {think_ms}ms, engine {engine}, PV {pv}')
            else:
                print(f'Published {export_snapshot(db, args.output)} entries to {args.output}')
        return 0
    except (OSError, ValueError, sqlite3.Error, struct.error, RuntimeError) as error:
        print(f'Experience failed: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
