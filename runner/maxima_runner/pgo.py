"""Profile-guided Release build: instrument the engine, train it on searches, rebuild."""
import shutil

from .project import network_path

# Varied middlegames. Training needs representative search work, not results.
TRAINING_POSITIONS = (
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N2N2/PP2BPPP/R2QKB1R w KQ - 0 9",
    "r1bq1rk1/pp2ppbp/2n3p1/3p4/3P1B2/2P2NP1/P3PPBP/R2Q1RK1 w - - 0 1",
    "r1bq1rk1/pp2ppbp/3p1np1/8/2PB4/1P2P3/P3BPPP/RN1Q1RK1 b - - 0 1",
    "r2q1rk1/p3ppbp/2p2np1/3p1b2/3P1B2/2N3P1/PP2PPBP/R2Q1RK1 b - - 0 1",
    "r1bq1rk1/ppp2ppp/2n5/4p3/1b6/2PP1N2/P1PBBPPP/R2Q1RK1 b - - 0 1",
    "r1bq1rk1/1p2bppp/p2ppn2/8/2PQP3/2N3P1/PP3PBP/R1B2RK1 w - - 0 1",
    "r2qr1k1/1b1nbppp/p2p1n2/1p2p3/4P3/1BN1BN1P/PPP2PP1/R2QR1K1 w - - 0 13",
    "2r2rk1/pp1bqppp/2n1pn2/3p4/2PP4/P1NBPN2/1P3PPP/2RQ1RK1 b - - 0 13",
)
SECONDS_PER_POSITION = 5


def train(binary, root):
    import chess
    import chess.engine
    engine = chess.engine.SimpleEngine.popen_uci(str(binary), cwd=str(root))
    try:
        engine.configure({"EvalFile": str(network_path(root)), "OwnBook": False, "Hash": 64})
        for fen in TRAINING_POSITIONS:
            engine.analyse(chess.Board(fen), chess.engine.Limit(time=SECONDS_PER_POSITION), game=object())
    finally:
        engine.quit()  # A clean exit writes the profiles.


def build(root, configure, build_command, run):
    profiles = root / "build/cpp26/profiles"
    shutil.rmtree(profiles, ignore_errors=True)
    profiles.mkdir(parents=True)
    configure = configure + [f"-Dmaxima_pgo_dir={profiles}"]
    status = run(configure + ["-Dmaxima_pgo_mode=generate"]) or run(build_command + ["--target", "maxima2"])
    if status:
        return status
    print("Training the instrumented engine...", flush=True)
    train(root / "build/cpp26/release/src/maxima2", root)
    return run(configure + ["-Dmaxima_pgo_mode=use"]) or run(build_command)
