"""Repository-specific paths; generic UCI code does not depend on this module."""
import os
from pathlib import Path


def project_root():
    configured = os.environ.get("MAXIMA_PROJECT_DIR")
    if configured:
        return Path(configured).resolve()
    for start in (Path.cwd(), Path(__file__).resolve().parent):
        for candidate in (start, *start.parents):
            if (candidate / "CMakePresets.json").is_file() and (candidate / "MAXIMA.md").is_file():
                return candidate
    return Path.cwd()


def data_root():
    """Books, network, tablebases and experience: MAXIMA_DATA_DIR (e.g. a persistent volume), else the checkout."""
    configured = os.environ.get("MAXIMA_DATA_DIR")
    return Path(configured).expanduser().resolve() if configured else project_root()


def engine_path(root=None):
    root = root or project_root()
    for mode in ("release", "debug"):
        path = root / "build/cpp26" / mode / "src/maxima2"
        if path.is_file() and os.access(path, os.X_OK):
            return path
    return root / "build/cpp26/release/src/maxima2"
