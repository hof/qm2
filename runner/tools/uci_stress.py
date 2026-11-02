"""Random UCI command order and timing against a real engine: fails when it stops answering."""
import argparse
import queue
import random
import subprocess
import threading
import time

answer_limit = 5  # seconds for readyok or bestmove once a search must end
fens = ("startpos", "fen r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2QK2R w KQ - 0 9",
        "fen 8/8/8/4k3/8/8/4P3/4K3 w - - 0 1", "fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")


class engine:
    def __init__(self, path):
        self.process = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, bufsize=1)
        self.lines = queue.Queue()
        self.sent = []
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            self.lines.put(line.strip())
        self.lines.put(None)

    def send(self, line):
        self.sent = (self.sent + [line])[-40:]
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def wait(self, word, limit=answer_limit):
        """Lines up to the one starting with word; raises when the engine goes quiet or dies."""
        deadline, seen = time.monotonic() + limit, []
        while True:
            try:
                line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                raise AssertionError(f"no {word} in {limit}s after: {self.sent[-12:]}") from None
            if line is None:
                raise AssertionError(f"engine exited (status {self.process.wait()}) after: {self.sent[-12:]}")
            seen.append(line)
            if line.startswith(word):
                return seen


def round_of_commands(chosen, subject):
    """One random burst; returns nothing, but leaves the engine proven responsive."""
    searching = 0  # searches that still owe a bestmove
    for _ in range(chosen.randrange(1, 14)):
        action = chosen.choice(("stop", "ponderhit", "isready", "position", "newgame", "go", "ponder", "infinite",
                                "movetime", "nodes", "option", "sleep"))
        if action in ("go", "ponder", "infinite", "movetime", "nodes"):
            if searching:  # a search on top of a search is a client error; end the old one first
                subject.send("stop")
                subject.wait("bestmove")
                searching = 0
            subject.send(f"position {chosen.choice(fens)}")
            subject.send({
                "go": f"go wtime {chosen.randrange(0, 4000)} btime {chosen.randrange(1, 4000)}",
                "ponder": "go ponder wtime 2000 btime 2000", "infinite": "go infinite",
                "movetime": f"go movetime {chosen.randrange(1, 60)}", "nodes": f"go nodes {chosen.randrange(1, 20000)}",
            }[action])
            searching = 1
        elif action == "position":
            if not searching:
                subject.send(f"position {chosen.choice(fens)}")
        elif action == "newgame":
            if searching:
                subject.send("stop")
                subject.wait("bestmove")
                searching = 0
            subject.send("ucinewgame")
        elif action == "option":
            if searching:
                subject.send("stop")
                subject.wait("bestmove")
                searching = 0
            subject.send(f"setoption name Hash value {chosen.choice((1, 16, 64))}")
        elif action == "sleep":
            time.sleep(chosen.random() * 0.03)
        else:
            subject.send(action)
    subject.send("stop")
    if searching:
        subject.wait("bestmove")
    subject.send("isready")
    subject.wait("readyok")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--seed", type=int, default=1)
    options = parser.parse_args()
    chosen = random.Random(options.seed)
    subject = engine(options.engine)
    subject.send("uci")
    subject.wait("uciok", 30)
    subject.send("setoption name OwnBook value false")
    subject.send("isready")
    subject.wait("readyok", 30)
    rounds, ended = 0, time.monotonic() + options.seconds
    while time.monotonic() < ended:
        round_of_commands(chosen, subject)
        rounds += 1
    subject.send("quit")
    if subject.process.wait(timeout=10) != 0:
        raise SystemExit(f"engine exit status {subject.process.returncode}")
    print(f"{rounds} rounds passed, seed {options.seed}")


if __name__ == "__main__":
    main()
