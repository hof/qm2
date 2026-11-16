"""Optional CPU training and export verification for MaxDrone's small NNUE models."""
import argparse
import copy
import hashlib
import json
import math
import os
import tempfile
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
        board = row.get('board')
        if board is None:
            board = chess.Board(row["fen"])
        for color in [board.turn, not board.turn]:
            offsets.append(len(indices))
            indices.extend(nnue_format.features(board, color, architecture))
    return (torch.tensor(indices, dtype=torch.long), torch.tensor(offsets, dtype=torch.long))


def result_tensor(torch, rows):
    return torch.tensor([row.get("result_stm") if row.get("result_stm") is not None else math.nan
                         for row in rows], dtype=torch.float64)


def target_probability(torch, labels, results=None, search_weight=1.0):
    scale = math.log(10) / 400
    target = torch.sigmoid(labels * scale)
    if results is not None:
        target = torch.where(torch.isfinite(results),
                             search_weight * target + (1 - search_weight) * torch.nan_to_num(results), target)
    return target


def loss(torch, scores, labels, results=None, search_weight=1.0):
    target = target_probability(torch, labels, results, search_weight)
    return (torch.sigmoid(scores * (math.log(10) / 400)) - target).square().mean()


def constant_baselines(torch, train, validation, search_weight):
    """Fit a constant on training targets only, then measure both splits."""
    labels = torch.tensor([row["score_cp"] for row in train], dtype=torch.float64)
    mean = target_probability(torch, labels, result_tensor(torch, train), search_weight).mean()
    epsilon = torch.finfo(torch.float64).eps
    mean = mean.clamp(epsilon, 1 - epsilon).item()
    fitted_cp = math.log(mean / (1 - mean)) * 400 / math.log(10)
    baselines = {}
    for name, cp in [("zero_cp", 0.0), ("train_mean", fitted_cp)]:
        entry = {"score_cp": cp}
        for split, rows in [("train", train), ("validation", validation)]:
            if not rows:
                entry[split] = None
                continue
            labels = torch.tensor([row["score_cp"] for row in rows], dtype=torch.float64)
            predictions = torch.full_like(labels, cp)
            entry[split] = {"positions": len(rows),
                            "loss": loss(torch, predictions, labels, result_tensor(torch, rows),
                                         search_weight).item(),
                            "mae_cp": (predictions - labels).abs().mean().item()}
        baselines[name] = entry
    return baselines


def metrics(torch, model, rows, search_weight=1.0, batch_size=256):
    if not rows:
        return None
    totals = dict.fromkeys(("loss", "search_loss", "mae_cp", "quantization_mae_cp"), 0.0)
    with torch.no_grad():
        for start in range(0, len(rows), batch_size):
            selected = rows[start:start + batch_size]
            inputs = batch(torch, selected, model.architecture)
            labels = torch.tensor([row["score_cp"] for row in selected], dtype=torch.float64)
            scores = model(*inputs)
            floating = model(*inputs, quantized=False)
            values = {"loss": loss(torch, scores, labels, result_tensor(torch, selected), search_weight),
                      "search_loss": loss(torch, scores, labels),
                      "mae_cp": (scores - labels).abs().mean(),
                      "quantization_mae_cp": (scores - floating).abs().mean()}
            for name, value in values.items():
                totals[name] += value.item() * len(selected)
    return dict(positions=len(rows), **{name: value / len(rows) for name, value in totals.items()})


def verify_export(torch, model, rows, serialized, batch_size=256, start=0, progress=None):
    integer = nnue_format.integer_network(serialized)
    with torch.no_grad():
        for start in range(start, len(rows), batch_size):
            selected = rows[start:start + batch_size]
            predictions = model(*batch(torch, selected, model.architecture)).tolist()
            for row, predicted in zip(selected, predictions):
                if integer.evaluate(chess.Board(row["fen"]))[1] != predicted:
                    raise ValueError(f"quantized model/export mismatch for {row['fen']}")
            checked = start + len(selected)
            if progress is not None and not progress(checked):
                return False
            if checked % 1024 == 0 or checked == len(rows):
                print(f"Export checked: {checked}/{len(rows)} positions", flush=True)
    return True


def atomic_write(path, writer):
    """Replace only files owned by this run; incomplete writes stay unpublished."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=path.parent, prefix=f".{path.name}.",
                                         delete=False) as stream:
            temporary = Path(stream.name)
            writer(stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def training_contract(args, torch):
    # File contents, not locations, identify immutable inputs and implementation.
    sources = ["nnue_training.py", "nnue_model.py", "nnue_format.py", "nnue_warm_start.py",
               "nnue_data.py", "nnue_binary.py", "shortfen.py"]
    config = {name: getattr(args, name) for name in (
        "epochs", "batch_size", "metrics_batch_size", "architecture", "width", "hidden",
        "seed", "learning_rate", "search_weight")}
    config.update(dataset_sha256=nnue_data.file_hash(args.dataset), torch=str(torch.__version__),
                  warm_start_sha256=nnue_data.file_hash(args.warm_start) if args.warm_start else None,
                  implementation={name: nnue_data.file_hash(Path(__file__).with_name(name))
                                  for name in sources})
    digest = hashlib.sha256(json.dumps(config, sort_keys=True, allow_nan=False).encode()).hexdigest()
    return config, digest


def main():
    parser = argparse.ArgumentParser(prog="maxima nnue:train", description=__doc__)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--output", required=True, type=Path, help="new artifact directory")
    parser.add_argument("--resume", action="store_true", help="resume the same output and training settings")
    parser.add_argument("--checkpoint-every", type=int, default=100, help="checkpoint every N batches")
    parser.add_argument("--max-batches", type=int,
                        help="pause after N training/export batches in this invocation")
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--metrics-batch-size", type=int, default=256)
    parser.add_argument("--width", type=int, default=8)
    parser.add_argument("--hidden", type=int, default=8)
    parser.add_argument("--architecture", choices=("halfkp", "halfkav2_hm"), default="halfkp")
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--learning-rate", type=float,
                        help="Adam learning rate (default .003, or .0001 with --warm-start)")
    parser.add_argument("--warm-start", type=Path,
                        help="approximate initialization from legacy HalfKP; requires full HalfKAv2_hm")
    parser.add_argument("--search-weight", type=float, default=0.7,
                        help="search-score share of the target; remaining share is final game result")
    args = parser.parse_args()
    if args.learning_rate is None:
        args.learning_rate = 0.0001 if args.warm_start else 0.003
    if args.warm_start and (args.architecture != "halfkav2_hm" or args.width != 256 or args.hidden != 32):
        parser.error("--warm-start requires --architecture halfkav2_hm --width 256 --hidden 32")
    if (min(args.epochs, args.batch_size, args.metrics_batch_size, args.checkpoint_every) <= 0
            or (args.max_batches is not None and args.max_batches <= 0) or not 1 <= args.width <= 256
            or not 1 <= args.hidden <= 32 or not math.isfinite(args.learning_rate) or args.learning_rate <= 0
            or not math.isfinite(args.search_weight) or not 0 <= args.search_weight <= 1):
        parser.error("invalid dimensions, epoch/batch count or learning rate")
    if args.output.exists() and not args.resume:
        raise ValueError(f"output already exists: {args.output}")
    checkpoint_path = args.output / "training.pt"
    if args.resume and not checkpoint_path.is_file():
        raise ValueError("--resume requires an existing training.pt in --output")
    header, rows = nnue_data.read_dataset(args.dataset)
    if header.get("purpose") == "heldout_evaluation":
        raise ValueError("reserved evaluation datasets cannot be used for training")
    train = [row for row in rows if row["split"] == "train"]
    validation = [row for row in rows if row["split"] == "validation"]
    if not train or not validation:
        raise ValueError("dataset must contain training and held-out positions")
    torch, model_type = training_libraries()
    torch.set_num_threads(1)
    torch.manual_seed(args.seed)
    torch.use_deterministic_algorithms(True)
    model = model_type(args.width, args.hidden, args.architecture).double()
    contract, contract_hash = training_contract(args, torch)
    warm_start = None
    if args.warm_start and not args.resume:
        from .nnue_warm_start import initialize_from_halfkp
        warm_start = initialize_from_halfkp(torch, model, args.warm_start.read_bytes())
    optimizer = torch.optim.Adam(model.parameters(), lr=args.learning_rate)
    if args.resume:
        state = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
        if (state.get("format") != "maxima.nnue.training" or state.get("version") != 1
                or state.get("contract_sha256") != contract_hash or state.get("contract") != contract):
            raise ValueError("training checkpoint input/config/implementation mismatch")
        model.ft_bound = state["ft_bound"]
        model.load_state_dict(state["model"])
        optimizer.load_state_dict(state["optimizer"])
        torch.set_rng_state(state["rng"])
        if state["phase"] == "complete":
            print(f"Run already complete: {args.output}", flush=True)
            return 0
    else:
        baselines = constant_baselines(torch, train, validation, args.search_weight)
        initial = {"train": metrics(torch, model, train, args.search_weight, args.metrics_batch_size),
                   "validation": metrics(torch, model, validation, args.search_weight, args.metrics_batch_size)}
        state = {"format": "maxima.nnue.training", "version": 1, "contract": contract,
                 "contract_sha256": contract_hash, "ft_bound": model.ft_bound,
                 "warm_start": warm_start, "baselines": baselines, "initial": initial,
                 "history": [], "best_state": copy.deepcopy(model.state_dict()) if warm_start else None,
                 "best_epoch": 0, "best_loss": initial["validation"]["loss"] if warm_start else math.inf,
                 "phase": "training", "epoch": 0, "order": None, "next_batch": 0,
                 "export_checked": 0}
        args.output.mkdir(parents=True, exist_ok=False)

    def save_checkpoint():
        state.update(model=model.state_dict(), optimizer=optimizer.state_dict(), rng=torch.get_rng_state())
        atomic_write(checkpoint_path, lambda stream: torch.save(state, stream))

    batches_done = 0

    def progress():
        nonlocal batches_done
        batches_done += 1
        paused = args.max_batches is not None and batches_done >= args.max_batches
        if paused or batches_done % args.checkpoint_every == 0:
            save_checkpoint()
        if paused:
            print(f"Paused {state['phase']}: epoch {state['epoch']}, next batch {state['next_batch']}, "
                  f"export checked {state['export_checked']}/{len(rows)}; resume with --resume", flush=True)
        return not paused

    if not args.resume:
        save_checkpoint()
    while state["phase"] == "training" and state["epoch"] < args.epochs:
        if state["order"] is None:
            state["order"] = torch.randperm(len(train)).tolist()
        for start in range(state["next_batch"], len(train), args.batch_size):
            selected = [train[i] for i in state["order"][start:start + args.batch_size]]
            labels = torch.tensor([row["score_cp"] for row in selected], dtype=torch.float64)
            optimizer.zero_grad(set_to_none=True)
            objective = loss(torch, model(*batch(torch, selected, model.architecture)), labels,
                             result_tensor(torch, selected), args.search_weight)
            if not torch.isfinite(objective):
                raise ValueError("nonfinite training loss")
            objective.backward()
            optimizer.step()
            model.constrain()
            state["next_batch"] = start + len(selected)
            if not progress():
                return 0
        scores = {"epoch": state["epoch"] + 1,
                  "train": metrics(torch, model, train, args.search_weight, args.metrics_batch_size),
                  "validation": metrics(torch, model, validation, args.search_weight, args.metrics_batch_size)}
        if not all(math.isfinite(value) for split in ("train", "validation")
                   for value in scores[split].values()):
            raise ValueError("nonfinite epoch metrics")
        state["history"].append(scores)
        if scores["validation"]["loss"] < state["best_loss"]:
            state.update(best_loss=scores["validation"]["loss"], best_epoch=scores["epoch"],
                         best_state=copy.deepcopy(model.state_dict()))
        print(f"Epoch {scores['epoch']}: train {scores['train']['loss']:.6f}; "
              f"validation {scores['validation']['loss']:.6f}", flush=True)
        state.update(epoch=scores["epoch"], order=None, next_batch=0)
        save_checkpoint()
    if state["phase"] == "training":
        model.load_state_dict(state["best_state"])
        state["phase"] = "export"
        save_checkpoint()

    config = {"format": "maxima.nnue.prototype", "version": 1, "architecture": args.architecture,
              "width": args.width, "hidden": args.hidden, "seed": args.seed,
              "dataset_sha256": contract["dataset_sha256"], "torch": str(torch.__version__),
              "epochs": args.epochs, "batch_size": args.batch_size,
              "metrics_batch_size": args.metrics_batch_size, "learning_rate": args.learning_rate,
              "search_weight": args.search_weight, "teacher": header,
              "warm_start": state["warm_start"], "ft_bound": model.ft_bound,
              "initial": state["initial"], "history": state["history"], "exported_epoch": state["best_epoch"],
              "constant_baselines": state["baselines"], "contract_sha256": contract_hash}
    baseline_loss = min(entry["validation"]["loss"] for entry in state["baselines"].values())
    config["beats_constant_baselines"] = state["best_loss"] < baseline_loss
    if not config["beats_constant_baselines"]:
        print("Selected model does not beat the constant validation baselines.", flush=True)
    serialized = nnue_format.serialize(model.integer_weights(), model.architecture)
    network_hash = hashlib.sha256(serialized).hexdigest()
    if state.get("network_sha256", network_hash) != network_hash:
        raise ValueError("resumed export network hash mismatch")
    state["network_sha256"] = network_hash

    def export_progress(checked):
        state["export_checked"] = checked
        return progress()

    if not verify_export(torch, model, rows, serialized, args.metrics_batch_size,
                         state["export_checked"], export_progress):
        return 0
    config.update(export_checked_positions=len(rows), network_sha256=network_hash)
    # Final artifacts are published only after every export comparison passes.
    atomic_write(args.output / "model.pt",
                 lambda stream: torch.save({"config": config, "state_dict": model.state_dict()}, stream))
    atomic_write(args.output / "network.nnue", lambda stream: stream.write(serialized))
    report = (json.dumps(config, indent=2, allow_nan=False) + "\n").encode()
    atomic_write(args.output / "report.json", lambda stream: stream.write(report))
    state["phase"] = "complete"
    save_checkpoint()
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
    "7K/8/8/8/8/8/8/k7 w - - 0 1",
    "7K/8/8/8/8/8/8/k7 b - - 0 1",
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
