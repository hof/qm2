"""Repository-specific paths; generic UCI code does not depend on this module."""
from datetime import datetime
import os
from pathlib import Path
import re


log_days = 7  # log files are deleted after this many days


def project_root():
    configured = os.environ.get("MAXIMA_PROJECT_DIR")
    if configured:
        return Path(configured).resolve()
    for start in (Path.cwd(), Path(__file__).resolve().parent):
        for candidate in (start, *start.parents):
            if (candidate / "CMakePresets.json").is_file() and (candidate / "MAXIMA.md").is_file():
                return candidate
    return Path.cwd()


def env_value(name, root=None):
    """A setting from the environment, else from <project>/.env (git-ignored); never logged."""
    value = os.environ.get(name)
    env = Path(root or project_root()) / ".env"
    if not value and env.is_file():
        for line in env.read_text(encoding="utf-8").splitlines():
            key, _, found = line.partition("=")
            if key.strip() == name:
                value = found.strip().strip('"').strip("'")
    return value or None


def data_root():
    """Books, network, tablebases and experience: MAXIMA_DATA_DIR (e.g. a persistent volume), else the checkout."""
    configured = os.environ.get("MAXIMA_DATA_DIR")
    return Path(configured).expanduser().resolve() if configured else project_root()


def network_path(root=None):
    """The default NNUE network: eval.nnue in the data directory, else one in networks/."""
    root = root or project_root()
    networks = [data_root() / "eval.nnue", *sorted((root / "networks").glob("*.nnue"))]
    network = next((path for path in networks if path.is_file()), None)
    if network is None:
        raise ValueError("no network found; run ./maxima init")
    return network


def engine_path(root=None):
    root = root or project_root()
    for mode in ("release", "debug"):
        path = root / "build/cpp26" / mode / "src/maxima2"
        if path.is_file() and os.access(path, os.X_OK):
            return path
    return root / "build/cpp26/release/src/maxima2"


def log_line(folder, line, name="lichess"):
    """Keep a console line with its time in one file per day; the session never depends on it."""
    now = datetime.now()
    try:
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / f"{name}-{now:%Y-%m-%d}.log"
        if not path.exists():  # a new day: drop what is too old to matter, so logs cannot fill the disk
            for old in folder.glob("*.log"):
                if now.timestamp() - old.stat().st_mtime > log_days * 86400:
                    old.unlink()
        with open(path, "a", encoding="utf-8") as log:
            log.write(f"{now:%H:%M:%S.%f}"[:-3] + " " + re.sub(r"\033\[[\d;]*m", "", line) + "\n")
    except OSError:
        pass
