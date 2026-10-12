"""Build a frozen recent-game diagnostic suite with independently searched labels.

Requires the runner environment and an explicit reference-engine binary. No
network access or engine-under-test results participate in position selection.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path

import chess
import chess.engine
import chess.pgn
from maxima_runner.uci import open_engine

sample_plies = (31, 40, 49, 58, 67, 76, 85, 94)
node_budgets = (200_000, 2_000_000, 8_000_000)
minimum_gap_cp = 50
round_ids = {1: "FRGTkE8Z", 7: "vhLqH6fn", 13: "yuTondoz"}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def candidates(sources):
    result = []
    for path in sources:
        with path.open() as stream:
            while True:
                game = chess.pgn.read_game(stream)
                if game is None:
                    break
                if game.errors or game.headers.get("Variant", "Standard") != "Standard":
                    raise ValueError(f"invalid source game in {path}")
                round_number = int(game.headers["Round"].split(".")[0])
                if round_number not in round_ids or not game.headers["Date"].startswith("2026."):
                    raise ValueError(f"unexpected source event/date in {path}")
                board = game.board()
                for ply, move in enumerate(game.mainline_moves(), 1):
                    if move not in board.legal_moves:
                        raise ValueError(f"illegal source move in {path}")
                    board.push(move)
                    if (ply not in sample_plies or board.is_game_over(claim_draw=True)
                            or board.halfmove_clock >= 90 or board.legal_moves.count() < 8):
                        continue
                    result.append({"source": path.name, "round": round_number,
                                   "game_url": game.headers["GameURL"],
                                   "white": game.headers["White"], "black": game.headers["Black"],
                                   "date_as_published": game.headers["Date"],
                                   "ply": ply, "fen": board.fen(en_passant="fen")})
    # Interleave rounds/games and sides; never rank candidates by Maxima's score.
    return sorted(result, key=lambda row: (sample_plies.index(row["ply"]), row["round"], row["game_url"]))


async def analyse(engine, board, nodes):
    await engine.configure({"Clear Hash": None})
    completed = {}
    analysis = await engine.protocol.analysis(board, chess.engine.Limit(nodes=nodes),
                                               multipv=2, game=object())
    async for info in analysis:
        if ("depth" not in info or "score" not in info or not info.get("pv")
                or info.get("lowerbound") or info.get("upperbound")):
            continue
        score = info["score"].pov(board.turn)
        if score.is_mate():
            continue  # Mate cases need a different certificate and scoring rule.
        replay = board.copy(stack=False)
        for move in info["pv"]:
            if move not in replay.legal_moves:
                raise ValueError("reference engine returned an illegal PV")
            replay.push(move)
        completed.setdefault(info["depth"], {})[info.get("multipv", 1)] = {
            "move": info["pv"][0].uci(), "cp": score.score(),
            "pv": [move.uci() for move in info["pv"]]}
    best = await analysis.wait()
    depths = [depth for depth, entries in completed.items() if 1 in entries and 2 in entries]
    if not depths:
        return None
    depth = max(depths)
    top = completed[depth]
    if (best.move is None or best.move.uci() != top[1]["move"]
            or abs(top[1]["cp"]) > 1000 or top[1]["cp"] - top[2]["cp"] < minimum_gap_cp):
        return None
    return {"nodes_budget": nodes, "completed_depth": depth,
            "gap_cp": top[1]["cp"] - top[2]["cp"], "top": [top[1], top[2]]}


async def build(args):
    sources = sorted(args.source)
    source_info = [{"file": path.name, "sha256": sha256(path)} for path in sources]
    engine_hash = sha256(args.engine)
    rows = candidates(sources)
    selected, rejected, games, rounds = [], [], {}, {}
    async with open_engine([str(args.engine.resolve())], args.engine.resolve().parent, timeout=120) as engine:
        if engine.identity[0] != "name Stockfish 19":
            raise ValueError("this corpus freeze requires Stockfish 19")
        options = await engine.configure({"Threads": 1, "Hash": 64, "SyzygyPath": "",
                                          "SyzygyProbeLimit": 0, "UCI_LimitStrength": False})
        for index, row in enumerate(rows):
            if games.get(row["game_url"], 0) >= 2 or rounds.get(row["round"], 0) >= 8:
                continue
            board = chess.Board(row["fen"])
            stages = []
            for nodes in node_budgets:
                stage = await asyncio.wait_for(analyse(engine, board, nodes), 120)
                if stage is None or (stages and stage["top"][0]["move"] != stages[0]["top"][0]["move"]):
                    break
                stages.append(stage)
            if len(stages) != len(node_budgets) or stages[-1]["completed_depth"] < 18:
                rejected.append({**row, "stages": stages})
                if args.audit:
                    args.audit.write_text(json.dumps({"accepted": selected, "rejected": rejected}, indent=2) + "\n")
                print(f"candidate {index + 1}/{len(rows)} rejected", flush=True)
                continue
            move = chess.Move.from_uci(stages[-1]["top"][0]["move"])
            row.update(id=f"MG26.{len(selected) + 1:02d}", expected_move=move.uci(),
                       expected_san=board.san(move), capture=board.is_capture(move),
                       pieces=len(board.piece_map()), verification=stages[1:])
            selected.append(row)
            if args.audit:
                args.audit.write_text(json.dumps({"accepted": selected, "rejected": rejected}, indent=2) + "\n")
            games[row["game_url"]] = games.get(row["game_url"], 0) + 1
            rounds[row["round"]] = rounds.get(row["round"], 0) + 1
            print(f"accepted {row['id']} round {row['round']} ply {row['ply']} "
                  f"{row['expected_san']} depth {stages[-1]['completed_depth']} "
                  f"gap {stages[-1]['gap_cp']}cp", flush=True)
            if len(selected) >= args.count:
                break
        if len(selected) < args.count:
            raise ValueError(f"only {len(selected)} stable positions; requested {args.count}")
        manifest = {"suite": "modern_2026", "generated_on": args.date,
                    "source_license": "CC-BY-SA-4.0", "source_license_url": "https://database.lichess.org/#broadcasts",
                    "sources": source_info,
                    "reference": {"identity": engine.identity, "binary_sha256": engine_hash,
                                  "release_url": "https://github.com/official-stockfish/Stockfish/releases/tag/sf_19",
                                  "options": options, "multipv": 2, "cold_hash_each_pass": True},
                    "selection": {"plies": sample_plies, "screen_nodes": node_budgets[0],
                                  "verify_nodes": node_budgets[1:], "minimum_gap_cp": minimum_gap_cp,
                                  "minimum_final_depth": 18, "maximum_abs_cp": 1000,
                                  "maximum_per_game": 2, "maximum_per_round": 8},
                    "positions": selected}
    if (sha256(args.engine) != engine_hash
            or any(sha256(path) != source["sha256"] for path, source in zip(sources, source_info))):
        raise ValueError("source PGNs or reference executable changed during generation")
    lines = ["# Recent 2026 games; Stockfish 19 targets, see modern_2026.json and README.md."]
    for row in selected:
        board = chess.Board(row["fen"])
        lines.append(board.epd(bm=[chess.Move.from_uci(row["expected_move"])], id=row["id"],
                               hmvc=board.halfmove_clock, fmvn=board.fullmove_number))
    args.output.write_text("\n".join(lines) + "\n")
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    if args.audit:
        args.audit.write_text(json.dumps({"accepted": selected, "rejected": rejected}, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--source", required=True, action="append", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--audit", type=Path)
    parser.add_argument("--date", required=True, help="freeze date, YYYY-MM-DD")
    parser.add_argument("--count", type=int, default=22)
    args = parser.parse_args()
    if args.count <= 0:
        parser.error("count must be positive")
    asyncio.run(build(args))


if __name__ == "__main__":
    main()
