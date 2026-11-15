"""Resumable standard self-play with searched labels, final results and quiet filtering."""
import argparse
import asyncio
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import tempfile
import time

import chess
import chess.engine
import chess.pgn
import chess.polyglot

from . import nnue_data
from .learning_store import writer_lock
from .project import data_root, engine_path, network_path, project_root
from .uci import open_engine


diversity_recipe = dict(version=1, opening_min_plies=8, opening_max_plies=12,
                        book_weight_power=0.5, explore_rate=0.02, explore_max_attempts=3,
                        explore_max_loss_cp=80, explore_max_ply=160)


def usable_position(board, move, label, *, min_ply, score_limit, seen):
    if board.ply() < min_ply:
        return "opening"
    if board.is_check():
        return "check"
    if board.is_capture(move) or move.promotion:
        return "tactical"
    if label is None:
        return "no_exact_cp"
    if score_limit is not None and abs(label["score_cp"]) > score_limit:
        return "decided"
    if nnue_data.position_key(board) in seen:
        return "duplicate"
    return None


def opening_moves(seed, index, plies, book=None, book_plies=4, weight_power=1):
    generator = random.Random(f"{seed}:{index}")
    board, moves = chess.Board(), []
    for ply in range(plies):
        if board.is_game_over(claim_draw=True):
            break
        entries = list(book.find_all(board)) if book is not None and ply < book_plies else []
        if entries:
            moves_and_weights = [(entry.move, entry.weight) for entry in entries
                                 if entry.weight > 0 and entry.move in board.legal_moves]
        else:
            moves_and_weights = []
        move = (generator.choices([item[0] for item in moves_and_weights],
                                  [item[1] ** weight_power for item in moves_and_weights])[0]
                if moves_and_weights else generator.choice(list(board.legal_moves)))
        board.push(move)
        moves.append(move)
    return moves


async def alternative_move(engine, board, move, label, config, game_id):
    """Probe the best alternative without replacing the unrestricted root label."""
    candidates = [candidate for candidate in board.legal_moves if candidate != move]
    if not candidates:
        return None
    infos = []
    best, _ = await engine.search(board, chess.engine.Limit(nodes=config['nodes']), game=game_id,
                                  root_moves=candidates, on_info=lambda info, elapsed: infos.append(info))
    other = nnue_data.completed_label(infos)
    if other is not None and other['nodes'] > config['nodes']:
        raise RuntimeError('exploration search exceeded its node budget')
    exact = [info for info in infos if other is not None and info.get('depth') == other['depth']
             and 'score' in info and not info.get('lowerbound') and not info.get('upperbound')]
    pv = exact[-1].get('pv', []) if exact else []
    if (other is None or best.move not in candidates or not pv or pv[0] != best.move
            or abs(label['score_cp'] - other['score_cp']) > config['diversity']['explore_max_loss_cp']):
        return None
    return best.move, other


async def play_game(engine, config, index, seen, book=None):
    board, game, rows, local_seen = chess.Board(), chess.pgn.Game(), [], set()
    counters = collections.Counter()
    game_id = f"maxima-selfplay-{config['seed']}-{index}"
    game.headers.update(Event="Maxima NNUE self-play", Site="local", Round=str(index),
                        White="Maxima teacher", Black="Maxima teacher", GameId=game_id,
                        Variant="Standard", NodesPerMove=str(config["nodes"]),
                        EvalFileSHA256=config["network_sha256"])
    node = game
    diversity = config.get('diversity')
    if diversity:
        game.headers['DiversityRecipe'] = f"bounded-v{diversity['version']}"
    generator = random.Random(f"{config['seed']}:{index}:diversity")
    plies = (generator.randint(diversity['opening_min_plies'], diversity['opening_max_plies'])
             if diversity else config['random_plies'])
    for move in opening_moves(config["seed"], index, plies, book, config["book_plies"],
                              diversity['book_weight_power'] if diversity else 1):
        board.push(move)
        node = node.add_variation(move)
        node.comment = "opening diversity"
        counters["opening"] += 1
    attempts = 0
    while not board.is_game_over(claim_draw=True) and board.ply() < config["max_plies"]:
        infos = []
        best, _ = await engine.search(board, chess.engine.Limit(nodes=config["nodes"]), game=game_id,
                                      on_info=lambda info, elapsed: infos.append(info))
        move = best.move
        if move is None or move not in board.legal_moves:
            raise RuntimeError("self-play engine returned no legal move")
        label = nnue_data.completed_label(infos)
        if label is not None and label["nodes"] > config["nodes"]:
            raise RuntimeError("self-play engine exceeded its node budget")
        explored = None
        if (diversity and label is not None and not board.is_check()
                and board.ply() <= diversity['explore_max_ply']
                and attempts < diversity['explore_max_attempts']
                and generator.random() < diversity['explore_rate']):
            attempts += 1
            counters['exploration_attempts'] += 1
            explored = await alternative_move(engine, board, move, label, config, game_id)
            if explored is not None:
                move = explored[0]
                counters['exploration_accepted'] += 1
        reason = usable_position(board, move, label, min_ply=config["min_ply"],
                                 score_limit=config["score_limit"], seen=seen)
        if reason is None and nnue_data.position_key(board) in local_seen:
            reason = "duplicate"
        if explored is not None:
            reason = 'exploration'
        if reason is None:
            # Also filter the best move of the completed iteration used for the label.
            exact = [info for info in infos if info.get("depth") == label["depth"] and "score" in info
                     and not info.get("lowerbound") and not info.get("upperbound")]
            pv = exact[-1].get("pv", []) if exact else []
            if not pv or board.is_capture(pv[0]) or pv[0].promotion:
                reason = "tactical_label"
        if reason is None:
            rows.append(dict(label, fen=board.fen(en_passant="fen"), ply=board.ply()))
            local_seen.add(nnue_data.position_key(board))
            counters["usable"] += 1
        else:
            counters[reason] += 1
        mover = board.turn
        board.push(move)
        node = node.add_variation(move)
        if label is not None:
            node.set_eval(chess.engine.PovScore(chess.engine.Cp(label["score_cp"]), mover), label["depth"])
            node.comment += f" [%nodes {label['nodes']}]"
        if explored is not None:
            node.comment += f" [%explore_cp {explored[1]['score_cp']}] [%explore_nodes {explored[1]['nodes']}]"
    outcome = board.outcome(claim_draw=True)
    game.headers["Result"] = outcome.result() if outcome is not None else "*"
    game.headers["Termination"] = outcome.termination.name if outcome is not None else "ply limit"
    text = str(game) + "\n\n"
    source_hash = hashlib.sha256(text.encode("utf-8")).hexdigest()
    if outcome is None:
        counters["unfinished_samples_discarded"] = len(rows)
        rows = []
    else:
        result = 0.5 if outcome.winner is None else float(outcome.winner)
        for row in rows:
            color = chess.Board(row["fen"]).turn
            row["result_stm"] = result if color else 1 - result
            row["source"] = {"sha256": source_hash, "game": index, "ply": row.pop("ply")}
            row["split"] = nnue_data.game_split(row["source"])
    return text, rows, dict(counters), board.ply()


def dataset_exclusions(paths):
    seen, hashes = set(), set()
    for path in paths:
        _, rows = nnue_data.read_dataset(path)
        hashes.add(nnue_data.file_hash(path))
        seen.update(nnue_data.position_key(chess.Board(row["fen"])) for row in rows)
    return seen, sorted(hashes)


def load_run(directory, config, excluded=()):
    rows, seen, games = [], set(excluded), []
    for path in sorted(directory.glob("game-[0-9]*")):
        report = json.loads((path / "report.json").read_text())
        if report["game"] != len(games) + 1 or report["config"] != config:
            raise ValueError("inconsistent self-play game sequence or teacher configuration")
        if nnue_data.file_hash(path / "game.pgn") != report["pgn_sha256"]:
            raise ValueError("self-play PGN hash mismatch")
        if nnue_data.file_hash(path / "positions.mxdata") != report["data_sha256"]:
            raise ValueError("self-play shard hash mismatch")
        if report["positions"]:
            _, saved = nnue_data.read_dataset(path / "positions.mxdata")
            if len(saved) != report["positions"]:
                raise ValueError("self-play shard count mismatch")
            for row in saved:
                key = nnue_data.position_key(chess.Board(row["fen"]))
                if key in seen:
                    raise ValueError("duplicate or excluded position in self-play shards")
                seen.add(key)
            rows.extend(saved)
        games.append(report)
    return rows, seen, games


def generation_stopped(args):
    stopped = (any(path.exists() for path in getattr(args, 'stop_file', []))
            or (getattr(args, 'stop_at', None) is not None and time.time() >= args.stop_at))
    if not stopped and getattr(args, 'control_store', None) is not None:
        from . import nnue_campaign
        try:
            nnue_campaign.allocation(args.control_store, args.control_lease, 'cpu', args.control_block)
        except ValueError:
            return True
    return stopped


async def generate(args, config, rows, seen, games):
    book = chess.polyglot.open_reader(args.book) if args.book else None
    try:
        with tempfile.TemporaryDirectory() as working:
            async with open_engine(str(args.engine.resolve()), working, timeout=30) as engine:
                await engine.configure({"Threads": 1, "Hash": 32, "OwnBook": False,
                                        "Ponder": False, "UCI_AnalyseMode": True,
                                        "DrawContempt": 0, "ShortDrawOffset": 0,
                                        "SyzygyPath": "<empty>", "EvalFile": str(args.network.resolve())})
                await engine.ready()
                for _ in range(args.games_per_run):
                    if len(rows) >= args.positions or generation_stopped(args):
                        break
                    index = len(games) + 1
                    text, produced, counters, plies = await play_game(engine, config, index, seen, book)
                    staging = Path(tempfile.mkdtemp(prefix=".pending-", dir=args.output))
                    try:
                        (staging / "game.pgn").write_text(text, encoding="utf-8")
                        header = dict(config, variant="standard", score_pov="side_to_move", split_method="game",
                                      label_source="completed_exact_iteration", engine=engine.protocol.id)
                        nnue_data.save_dataset(staging / "positions.mxdata", header, produced)
                        report = {"game": index, "config": config, "positions": len(produced), "plies": plies,
                                  "filters": counters, "pgn_sha256": nnue_data.file_hash(staging / "game.pgn"),
                                  "data_sha256": nnue_data.file_hash(staging / "positions.mxdata")}
                        (staging / "report.json").write_text(json.dumps(report, indent=2) + "\n")
                        os.rename(staging, args.output / f"game-{index:06d}")
                    finally:
                        if staging.exists():
                            shutil.rmtree(staging)
                    rows.extend(produced)
                    seen.update(nnue_data.position_key(chess.Board(row["fen"])) for row in produced)
                    games.append(report)
                    print(f"Game {index}: {plies} plies, {len(produced)} usable; total {len(rows)}/{args.positions}", flush=True)
    finally:
        if book is not None:
            book.close()


def main():
    parser = argparse.ArgumentParser(prog="maxima nnue:selfplay", description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="new or matching resumable run directory")
    parser.add_argument("--exclude-dataset", action="append", type=Path, default=[],
                        help="exclude every position in this dataset from both splits; repeatable")
    parser.add_argument("--control-store", type=Path, help="campaign store for per-game allocation checks")
    parser.add_argument("--control-lease")
    parser.add_argument("--control-block")
    parser.add_argument("--stop-file", type=Path, action="append", default=[],
                        help="finish the current game then stop when any named file exists")
    parser.add_argument("--stop-at", type=float, help="Unix deadline: stop scheduling games after this time")
    parser.add_argument("--positions", type=int, default=10000)
    parser.add_argument("--nodes", type=int, default=10000)
    parser.add_argument("--games-per-run", type=int, default=5)
    parser.add_argument("--max-plies", type=int, default=512)
    parser.add_argument("--random-plies", type=int, default=8)
    parser.add_argument("--diversity", action="store_true",
                        help="bounded-v1: 8-12 opening plies, flatter book weights, bounded alternative moves")
    parser.add_argument("--book-plies", type=int, default=4,
                        help="book-guided prefix within the opening plies; the remainder are random")
    parser.add_argument("--min-ply", type=int, default=16)
    parser.add_argument("--score-limit", type=int,
                        help="optional absolute centipawn sampling limit; default: no score filter")
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--engine", type=Path, default=engine_path())
    parser.add_argument("--network", type=Path, default=network_path(project_root()))
    parser.add_argument("--book", type=Path, default=(data_root() / "book.bin") if (data_root() / "book.bin").is_file() else None)
    parser.add_argument("--no-book", action="store_true", help="disable automatic opening-book lookup")
    args = parser.parse_args()
    if args.diversity:
        args.random_plies = diversity_recipe['opening_max_plies']
    if args.no_book:
        args.book = None
    if ((args.stop_at is not None and not math.isfinite(args.stop_at))
            or min(args.positions, args.nodes, args.games_per_run) <= 0
            or (args.score_limit is not None and args.score_limit <= 0)
            or not 0 <= args.book_plies <= args.random_plies <= args.min_ply < args.max_plies <= 65535):
        parser.error("invalid generation bounds")
    config = {"format": "maxima.nnue.selfplay", "version": 1, "nodes": args.nodes,
              "generator_sha256": nnue_data.file_hash(__file__),
              "seed": args.seed, "random_plies": args.random_plies, "min_ply": args.min_ply,
              "book_plies": args.book_plies,
              "max_plies": args.max_plies, "score_limit": args.score_limit,
              "engine_sha256": nnue_data.file_hash(args.engine), "network_sha256": nnue_data.file_hash(args.network),
              "book_sha256": nnue_data.file_hash(args.book) if args.book else None}
    if args.diversity:
        config['diversity'] = dict(diversity_recipe)
    excluded, exclusion_hashes = dataset_exclusions(args.exclude_dataset)
    if exclusion_hashes:
        config["excluded_dataset_sha256"] = exclusion_hashes
    args.output.mkdir(parents=True, exist_ok=True)
    with writer_lock(args.output / ".lock"):
        settings = args.output / "settings.json"
        if settings.exists():
            if json.loads(settings.read_text()) != config:
                raise ValueError("self-play resume settings differ from the saved run")
        else:
            if any(args.output.glob("game-*")):
                raise ValueError("self-play games exist without run settings")
            nnue_data.publish_jsonl(settings, config, [])
        rows, seen, games = load_run(args.output, config, excluded)
        if len(rows) < args.positions and not generation_stopped(args):
            asyncio.run(generate(args, config, rows, seen, games))
        if len(rows) >= args.positions:
            path = args.output / f"dataset-{args.positions}.mxdata"
            if not path.exists():
                nnue_data.save_dataset(path, dict(config, variant="standard", score_pov="side_to_move",
                                                  split_method="game", label_source="completed_exact_iteration"),
                                       rows[:args.positions])
            print(f"Ready: {path}")
        else:
            print(f"Checkpoint: {len(games)} games, {len(rows)} positions; repeat the command to resume")
    return 0
