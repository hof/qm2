"""Optional CPU training and export verification for MaxDrone's small NNUE models."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import subprocess

import chess

from . import nnue_data, nnue_format


def training_libraries():
    try:
        import torch
        from .nnue_model import halfkp_model
    except ImportError as error:
        raise RuntimeError("NNUE training needs the optional PyTorch environment; see MAXIMA.md") from error
    return torch, halfkp_model


def batch(torch, rows, architecture="halfkp"):
    indices, offsets = [], []
    for row in rows:
        board = chess.Board(row["fen"])
        for color in [board.turn, not board.turn]:
            offsets.append(len(indices))
            indices.extend(nnue_format.features(board, color, architecture))
    return (torch.tensor(indices, dtype=torch.long), torch.tensor(offsets, dtype=torch.long))


def loss(torch, scores, labels):
    scale = math.log(10) / 400
    return (torch.sigmoid(scores * scale) - torch.sigmoid(labels * scale)).square().mean()


def metrics(torch, model, rows):
    if not rows:
        return None
    with torch.no_grad():
        inputs = batch(torch, rows, model.architecture)
        labels = torch.tensor([row["score_cp"] for row in rows], dtype=torch.float64)
        scores = model(*inputs)
        floating = model(*inputs, quantized=False)
        return {"positions": len(rows), "loss": loss(torch, scores, labels).item(),
                "mae_cp": (scores - labels).abs().mean().item(),
                "quantization_mae_cp": (scores - floating).abs().mean().item()}


def main():
    parser = argparse.ArgumentParser(prog="maxima nnue:train", description=__doc__)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--output", required=True, type=Path, help="new artifact directory")
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--width", type=int, default=8)
    parser.add_argument("--hidden", type=int, default=8)
    parser.add_argument("--architecture", choices=("halfkp", "halfkav2_hm"), default="halfkp")
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--learning-rate", type=float, default=0.003)
    args = parser.parse_args()
    if (min(args.epochs, args.batch_size) <= 0 or not 1 <= args.width <= 256
            or not 1 <= args.hidden <= 32 or not math.isfinite(args.learning_rate) or args.learning_rate <= 0):
        parser.error("invalid dimensions, epoch/batch count or learning rate")
    if args.output.exists():
        raise ValueError(f"output already exists: {args.output}")
    header, rows = nnue_data.read_dataset(args.dataset)
    train = [row for row in rows if row["split"] == "train"]
    validation = [row for row in rows if row["split"] == "validation"]
    if not train or not validation:
        raise ValueError("dataset must contain training and held-out positions")
    torch, model_type = training_libraries()
    torch.set_num_threads(1)
    torch.manual_seed(args.seed)
    torch.use_deterministic_algorithms(True)
    model = model_type(args.width, args.hidden, args.architecture).double()
    optimizer = torch.optim.Adam(model.parameters(), lr=args.learning_rate)
    initial = {"train": metrics(torch, model, train), "validation": metrics(torch, model, validation)}
    history, best_state, best_epoch, best_loss = [], None, 0, math.inf
    for epoch in range(args.epochs):
        order = torch.randperm(len(train)).tolist()
        for start in range(0, len(train), args.batch_size):
            selected = [train[i] for i in order[start:start + args.batch_size]]
            labels = torch.tensor([row["score_cp"] for row in selected], dtype=torch.float64)
            optimizer.zero_grad(set_to_none=True)
            objective = loss(torch, model(*batch(torch, selected, model.architecture)), labels)
            if not torch.isfinite(objective):
                raise ValueError("nonfinite training loss")
            objective.backward()
            optimizer.step()
            model.constrain()
        scores = {"epoch": epoch + 1, "train": metrics(torch, model, train),
                  "validation": metrics(torch, model, validation)}
        history.append(scores)
        if scores["validation"]["loss"] < best_loss:
            best_loss = scores["validation"]["loss"]
            best_epoch, best_state = epoch + 1, copy.deepcopy(model.state_dict())
        print(f"Epoch {epoch + 1}: train {scores['train']['loss']:.6f}; "
              f"validation {scores['validation']['loss']:.6f}", flush=True)
    config = {"format": "maxima.nnue.prototype", "version": 1, "architecture": args.architecture,
              "width": args.width, "hidden": args.hidden, "seed": args.seed,
              "dataset_sha256": nnue_data.file_hash(args.dataset), "torch": str(torch.__version__),
              "epochs": args.epochs, "batch_size": args.batch_size, "learning_rate": args.learning_rate,
              "teacher": header, "initial": initial, "history": history, "exported_epoch": best_epoch}
    model.load_state_dict(best_state)
    serialized = nnue_format.serialize(model.integer_weights(), model.architecture)
    integer = nnue_format.integer_network(serialized)
    with torch.no_grad():
        predictions = model(*batch(torch, rows, model.architecture)).tolist()
    for row, predicted in zip(rows, predictions):
        if integer.evaluate(chess.Board(row["fen"]))[1] != predicted:
            raise ValueError(f"quantized model/export mismatch for {row['fen']}")
    config["export_checked_positions"] = len(rows)
    config["network_sha256"] = hashlib.sha256(serialized).hexdigest()
    # Artifacts are experiments: never overwrite a run or activate its network.
    args.output.mkdir(parents=True, exist_ok=False)
    torch.save({"config": config, "state_dict": model.state_dict()}, args.output / "model.pt")
    (args.output / "network.nnue").write_bytes(serialized)
    (args.output / "report.json").write_text(json.dumps(config, indent=2, allow_nan=False) + "\n")
    print(f"Saved model and verified export to {args.output}")
    return 0


verification_fens = [
    chess.STARTING_FEN,
    "r3k2r/ppp2ppp/2n5/3pp3/3PP3/2N5/PPP2PPP/R3K2R w KQkq - 0 1",
    "r3k2r/ppp2ppp/2n5/3pp3/3PP3/2N5/PPP2PPP/R3K2R b KQkq - 0 1",
    "7k/8/8/3pP3/8/8/8/K7 w - d6 0 1",
    "7k/8/8/8/3Pp3/8/8/K7 b - d3 0 1",
    "7k/P7/8/8/8/8/8/K7 w - - 0 1",
    "7k/8/8/8/8/8/p7/7K b - - 0 1",
    "7k/8/8/8/8/8/P7/3K4 w - - 0 1",
    "3k4/7p/8/8/8/8/8/K7 b - - 0 1",
]


def verify_main():
    parser = argparse.ArgumentParser(prog="maxima nnue:verify", description="Compare exported inference with Maxima.")
    parser.add_argument("network", type=Path)
    parser.add_argument("--probe", required=True, type=Path, help="built tests/nnue_probe executable")
    parser.add_argument("--dataset", type=Path)
    parser.add_argument("--positions", type=int, default=16)
    args = parser.parse_args()
    if args.positions <= 0:
        parser.error("positions must be positive")
    integer = nnue_format.integer_network(args.network.read_bytes())
    fens = list(verification_fens)
    if args.dataset:
        _, rows = nnue_data.read_dataset(args.dataset)
        fens.extend(row["fen"] for row in rows[:args.positions])
    expected = []
    for fen in fens:
        board = chess.Board(fen)
        expected.append(board.fen(en_passant="fen"))
        for move in board.legal_moves:
            board.push(move)
            expected.append(board.fen(en_passant="fen"))
            board.pop()
    result = subprocess.run([str(args.probe.resolve()), str(args.network.resolve())],
                            input="\n".join(fens) + "\n", capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise ValueError(f"NNUE probe failed: {result.stderr.strip()}")
    output = result.stdout.splitlines()
    if len(output) != len(expected):
        raise ValueError("NNUE probe returned an incomplete evaluation set")
    # Move generation order differs: compare the entire position/score multiset.
    from collections import Counter
    actual = Counter()
    for line in output:
        fen, raw, cp = line.split("|")
        actual[(chess.Board(fen).fen(en_passant="fen"), int(raw), int(cp))] += 1
    wanted = Counter((fen, *integer.evaluate(chess.Board(fen))) for fen in expected)
    if actual != wanted:
        raise ValueError(f"NNUE inference mismatch: {list((wanted - actual).items())[:3]}")
    print(f"Verified {len(output)} root/child evaluations against Maxima, fresh and incremental")
    return 0
