"""Bounded, reproducible standard-chess datasets for MaxDrone NNUE training."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

import chess
import chess.engine
import chess.pgn

from .project import engine_path, network_path, project_root
from . import nnue_binary, shortfen


data_format = "maxima.nnue.data"


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def position_key(board):
    # Ignore both clocks: transpositions must not cross the held-out boundary.
    return " ".join(board.fen(en_passant="legal").split()[:4])


def position_split(board):
    digest = hashlib.sha256(position_key(board).encode("ascii")).digest()
    return "validation" if int.from_bytes(digest[:8], "big") % 10 == 0 else "train"


def game_split(source):
    identity = f"{source['sha256']}:{source['game']}"
    digest = hashlib.sha256(identity.encode("ascii")).digest()
    return "validation" if int.from_bytes(digest[:8], "big") % 10 == 0 else "train"


def completed_label(infos):
    """Use an exact completed iteration, never an interrupted root's bound."""
    exact, completed, nodes = {}, None, 0
    for info in infos:
        nodes = max(nodes, info.get("nodes", 0))
        text = info.get("string", "")
        if text.startswith("search_result ") and "completed_depth " in text:
            fields = text.split()
            completed = int(fields[fields.index("completed_depth") + 1])
        if ("score" in info and "depth" in info and not info.get("lowerbound")
                and not info.get("upperbound")):
            exact[info["depth"]] = info["score"].relative.score()
    depth = completed if completed is not None else max(exact, default=0)
    score = exact.get(depth)
    if depth <= 0 or score is None or abs(score) >= 10000:
        return None
    return {"score_cp": score, "depth": depth, "nodes": nodes}


def candidates(paths, limit, stride=8, min_ply=16, split_method="game"):
    """Replay complete games; sample outside check, never reuse PGN eval labels."""
    found, seen = [], set()
    for path in paths:
        path = Path(path)
        digest = file_hash(path)
        with path.open(encoding="utf-8-sig") as source:
            game_number = 0
            while (game := chess.pgn.read_game(source)) is not None:
                game_number += 1
                if game.errors:
                    raise ValueError(f"invalid PGN in {path}, game {game_number}")
                board = game.board()
                if type(board) is not chess.Board or board.chess960:
                    continue
                if not board.is_valid() or len(board.piece_map()) > 32:
                    raise ValueError(f"unsupported board in {path}, game {game_number}")
                game_rows = []
                result = game.headers.get("Result", "*")
                for ply, move in enumerate(game.mainline_moves(), 1):
                    if move not in board.legal_moves:
                        raise ValueError(f"illegal PGN move in {path}, game {game_number}, ply {ply}")
                    board.push(move)
                    # Alternating one-ply offsets avoid sampling only White with even strides.
                    offset = (ply - min_ply) // stride % 2 if stride % 2 == 0 else 0
                    if (ply < min_ply or (ply - min_ply) % stride != offset or board.is_check()
                            or board.is_game_over(claim_draw=True)):
                        continue
                    key = position_key(board)
                    if key in seen:
                        continue
                    seen.add(key)
                    white_result = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}.get(result)
                    source_info = {"sha256": digest, "game": game_number, "ply": ply}
                    game_rows.append({"fen": board.fen(), "split": game_split(source_info)
                                      if split_method == "game" else position_split(board),
                                      "result_stm": (white_result if board.turn else 1 - white_result)
                                      if white_result is not None else None,
                                      "source": source_info})
                found.extend(game_rows[:limit - len(found)])
                if len(found) == limit:
                    return found
    return found


def publish_jsonl(path, header, rows):
    """Publish a complete file atomically; never replace an existing artifact."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=f".{path.name}.", delete=False) as stream:
            temporary = Path(stream.name)
            for item in [header, *rows]:
                stream.write(json.dumps(item, allow_nan=False, sort_keys=True) + "\n")
        os.link(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def read_text_dataset(path):
    with Path(path).open(encoding="utf-8") as source:
        header = json.loads(next(source, "{}"))
        if (header.get("format") != data_format or header.get("version") not in (1, 2)
                or header.get("variant") != "standard" or header.get("score_pov") != "side_to_move"):
            raise ValueError("unsupported NNUE dataset format, variant or score perspective")
        compact = header["version"] == 2
        if compact and header.get("position_encoding") != "shortfen":
            raise ValueError("unsupported NNUE position encoding")
        rows = []
        for line in source:
            row = json.loads(line)
            if compact:
                board = shortfen.decode(row["position"])
                row = dict(row, fen=board.fen(en_passant="fen"))
            else:
                board = chess.Board(row["fen"])
            rows.append(row)
    return header, rows


def read_dataset(path):
    with Path(path).open("rb") as stream:
        binary = stream.read(len(nnue_binary.magic)) == nnue_binary.magic
    if binary:
        with nnue_binary.binary_dataset(path) as data:
            header = data.header
            if header["variant"] != "standard" or header["variant_state_size"]:
                raise ValueError("the current trainer requires standard chess without extra variant state")
            rows = []
            for stored in data:
                board = stored.pop("board")
                if board.chess960 or not board.is_valid() or len(board.piece_map()) > 32:
                    raise ValueError("invalid or unsupported board for the current standard trainer")
                stored.pop("variant_state")
                rows.append(dict(stored, fen=board.fen(en_passant="fen", promoted=True)))
    else:
        header, rows = read_text_dataset(path)
    method = header.get("split_method", "position")
    if method not in ("position", "game"):
        raise ValueError("unsupported NNUE dataset split method")
    seen = set()
    for row in rows:
        board = chess.Board(row["fen"])
        key, score = position_key(board), row.get("score_cp")
        expected_split = game_split(row["source"]) if method == "game" else position_split(board)
        result = row.get("result_stm")
        if (not board.is_valid() or len(board.piece_map()) > 32 or key in seen
                or isinstance(score, bool) or not isinstance(score, int) or abs(score) >= 10000
                or isinstance(result, bool) or result not in (None, 0, 0.5, 1)
                or row.get("split") != expected_split):
            raise ValueError("invalid or duplicate NNUE dataset position/label/split")
        seen.add(key)
    if not rows or len(rows) != header.get("positions"):
        raise ValueError("empty or incomplete NNUE dataset")
    return header, rows


def save_dataset(path, header, rows, encoding="binary"):
    metadata = {key: value for key, value in header.items() if key not in (
        "format", "version", "position_encoding", "board_encoding", "sources", "record_size",
        "store_promoted", "variant_state_size", "variant_state_schema")}
    metadata["positions"] = len(rows)
    if encoding == "binary":
        promoted = any(chess.Board(row["fen"]).promoted for row in rows)
        nnue_binary.write_dataset(path, metadata, rows, store_promoted=promoted)
    elif encoding == "shortfen":
        stored = []
        for row in rows:
            item = dict(row)
            board = chess.Board(item.pop("fen"))
            if board.promoted:
                raise ValueError("shortfen export does not encode promoted-origin markers")
            item["position"] = shortfen.encode(board)
            stored.append(item)
        publish_jsonl(path, dict(metadata, format=data_format, version=2, position_encoding="shortfen"), stored)
    else:
        raise ValueError("unsupported NNUE dataset output encoding")


def convert_main():
    parser = argparse.ArgumentParser(prog="maxima nnue:convert", description="Convert a standard NNUE dataset.")
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--format", choices=("binary", "shortfen"), default="binary")
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError(f"output already exists: {args.output}")
    header, rows = read_dataset(args.dataset)
    save_dataset(args.output, header, rows, args.format)
    print(f"Saved {len(rows)} positions as {args.format} to {args.output}")
    return 0


def main():
    parser = argparse.ArgumentParser(prog="maxima nnue:data", description=__doc__)
    parser.add_argument("pgn", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--format", choices=("binary", "shortfen"), default="binary")
    parser.add_argument("--positions", type=int, default=256)
    parser.add_argument("--nodes", type=int, default=2000)
    parser.add_argument("--stride", type=int, default=8)
    parser.add_argument("--min-ply", type=int, default=16)
    parser.add_argument("--engine", type=Path, default=engine_path())
    parser.add_argument("--network", type=Path, default=network_path(project_root()))
    args = parser.parse_args()
    if min(args.positions, args.nodes, args.stride) <= 0 or args.min_ply < 0:
        parser.error("positions, nodes and stride must be positive; min-ply must be nonnegative")
    if args.output.exists():
        raise ValueError(f"output already exists: {args.output}")
    rows = candidates(args.pgn, args.positions, args.stride, args.min_ply)
    if not rows:
        raise ValueError("no eligible standard-chess positions")
    header = {"format": data_format, "version": 2, "variant": "standard",
              "position_encoding": "shortfen", "split_method": "game",
              "label_source": "completed_exact_iteration",
              "score_pov": "side_to_move", "engine_sha256": file_hash(args.engine),
              "network_sha256": file_hash(args.network), "requested_nodes": args.nodes,
              "stride": args.stride, "min_ply": args.min_ply}
    labeled = []
    with chess.engine.SimpleEngine.popen_uci(str(args.engine.resolve()), timeout=30) as engine:
        engine.configure({"Threads": 1, "Hash": 32, "OwnBook": False, "UCI_AnalyseMode": True,
                          "EvalFile": str(args.network.resolve()), "DrawContempt": 0,
                          "ShortDrawOffset": 0})
        header["engine"] = engine.id
        for row in rows:
            with engine.analysis(chess.Board(row["fen"]), chess.engine.Limit(nodes=args.nodes), game=object()) as analysis:
                label = completed_label(analysis)
            if label is not None:
                labeled.append(dict(row, **label))
    if not labeled:
        raise ValueError("no ordinary centipawn labels produced")
    header["positions"] = len(labeled)
    save_dataset(args.output, header, labeled, args.format)
    print(f"Saved {len(labeled)} positions to {args.output}; "
          f"{sum(row['split'] == 'validation' for row in labeled)} held out")
    return 0
