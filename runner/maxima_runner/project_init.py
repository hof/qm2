"""Prepare a checkout: toolchain, runner, builds, network, books and tablebases; ready parts are skipped.

Books, network, tablebases and experience go to MAXIMA_DATA_DIR when set, else the checkout."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

from . import download_tablebases
from .project import data_root, project_root

books_url = "https://github.com/ChrisWhittington/polyglot-books/releases/download/v1.0/"
# Pinned downloads: file, archive URL, archive member, size, SHA-256.
books = (
    ("book.bin", books_url + "book.zip", "book.bin", 177973984,
     "5b2f1794c20f095e23364a966f682b1c235afd46ec1f1acbc6e4bf72f1f92bfe"),
    ("fire.bin", books_url + "Daring.zip", "Daring.bin", 306432,
     "168ccb0b467c9280e0e79c9d6309e5fd84d16e229bb28a1903b8ced7c0c2b187"),
)


def file_checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_member(url, member, size, sha256, target):
    """Download a zip, verify one member's size and SHA-256, then replace target atomically."""
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent, prefix=".init-") as temporary:
        archive = Path(temporary) / "archive.zip"
        with urllib.request.urlopen(url, timeout=60) as response, archive.open("wb") as output:
            shutil.copyfileobj(response, output, 1024 * 1024)
        extracted = Path(temporary) / target.name
        digest = hashlib.sha256()
        with zipfile.ZipFile(archive) as bundle:
            if member not in bundle.namelist():
                raise ValueError(f"{url} has no {member}")
            if bundle.getinfo(member).file_size != size:
                raise ValueError(f"{member}: expected {size} bytes")
            with bundle.open(member) as source, extracted.open("wb") as output:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    digest.update(chunk)
                    output.write(chunk)
        if digest.hexdigest() != sha256:
            raise ValueError(f"{member}: SHA-256 does not match the pinned checksum")
        os.replace(extracted, target)


def toolchain_step(root, options):
    from .cli import toolchain_prefix
    if toolchain_prefix(root):
        return "ok", ".tools/cpp26"
    missing = [tool for tool in ("cmake", "ninja", "c++") if not shutil.which(tool)]
    if not missing:
        return "ok", "tools on PATH"
    micromamba = root / ".tools/bootstrap/bin/micromamba"
    if options.check or not micromamba.is_file():
        return "missing", f"no {', '.join(missing)}; see MAXIMA.md chapter 5"
    environment = dict(os.environ, MAMBA_ROOT_PREFIX=str(root / ".tools/mamba"),
                       XDG_CACHE_HOME=str(root / ".tools/cache"))
    status = subprocess.run([str(micromamba), "create", "-y", "-p", str(root / ".tools/cpp26"),
                             "-f", str(root / "cpp26_environment.yml")], cwd=root, env=environment).returncode
    return ("done", "created .tools/cpp26") if status == 0 else ("failed", "micromamba create")


def runner_step(root, options):
    python = root / ".tools/runner/bin/python"
    probe = [str(python), "-c", "import chess"]
    if python.is_file() and subprocess.run(probe, capture_output=True).returncode == 0:
        return "ok", ".tools/runner"
    if not python.is_file():
        try:
            import chess  # noqa: F401
            return "ok", sys.executable
        except ImportError:
            pass
    if options.check:
        return "missing", "python-chess; ./maxima init installs it in .tools/runner"
    if not python.is_file():
        subprocess.run([sys.executable, "-m", "venv", str(root / ".tools/runner")], check=True)
    status = subprocess.run([str(python), "-m", "pip", "install", "-e", str(root / "runner")]).returncode
    return ("done", ".tools/runner") if status == 0 else ("failed", "pip install")


def build_step(mode):
    def step(root, options):
        from . import cli
        preset = f"cpp26_{mode}"
        if (root / "build/cpp26" / mode / "CMakeCache.txt").is_file():
            prefix, environment = cli.toolchain(root)
            dry_run = subprocess.run(prefix + ["cmake", "--build", "--preset", preset, "--", "-n"], cwd=root,
                                     env=environment, capture_output=True, text=True)
            if dry_run.returncode == 0 and "no work to do" in dry_run.stdout:
                return "ok", "up to date"
        if options.check:
            return "missing", f"./maxima build --{mode}"
        status = cli.development_command("build", [f"--{mode}"])
        return ("done", f"build/cpp26/{mode}") if status == 0 else ("failed", f"./maxima build --{mode}")
    return step


def network_step(root, options):
    """Only engines with NNUE ship tools/download_nnue.py; it prints the verified file."""
    script = root / "tools/download_nnue.py"
    target = options.data / "eval.nnue"
    if not script.is_file():
        return "skipped", "this engine has no network"
    if target.is_file():
        return "ok", "eval.nnue"
    if options.check:
        return "missing", "./maxima init downloads it"
    result = subprocess.run([sys.executable, str(script)], cwd=root, capture_output=True, text=True)
    if result.returncode != 0 or not result.stdout.strip():
        return "failed", result.stderr.strip() or "tools/download_nnue.py"
    shutil.copyfile(result.stdout.strip().splitlines()[-1], target)
    return "done", "eval.nnue"


def book_step(name, url, member, size, sha256):
    def step(root, options):
        target = options.data / name
        if target.is_file():
            if file_checksum(target) == sha256:
                return "ok", "verified"
            if not options.force:
                return "ok", "kept your own file (--force replaces it)"
        if options.check:
            return "missing", "./maxima init downloads it"
        download_member(url, member, size, sha256, target)
        return "done", url.rsplit("/", 1)[1]
    return step


def solid_step(root, options):
    from . import solid_book
    if (options.data / "solid.bin").is_file():
        return "ok", "solid.bin"
    if not options.solid:
        return "skipped", "optional: ./maxima init --solid (needs LICHESS_TOKEN)"
    if not solid_book.lichess_token(root):
        return "missing", "set LICHESS_TOKEN (environment or .env)"
    if options.check:
        return "missing", "./maxima init --solid builds it"
    status = subprocess.run([sys.executable, "-m", "maxima_runner", "experience:solid"], cwd=root).returncode
    return ("done", "solid.bin") if status == 0 else ("failed", "./maxima experience:solid")


def syzygy_step(root, options):
    directory = options.data / "tablebases/syzygy"
    if not options.syzygy:
        return "skipped", "--syzygy 0"
    if download_tablebases.installed(directory, options.syzygy):
        return "ok", f"up to {options.syzygy} pieces"
    if options.check:
        return "missing", f"./maxima tablebases:syzygy:download --pieces {options.syzygy}"
    download_tablebases.download(options.syzygy, directory)
    return "done", f"up to {options.syzygy} pieces"


def steps(options):
    yield "runner", runner_step
    if options.build:
        yield "toolchain", toolchain_step
        yield "release build", build_step("release")
        if options.debug:
            yield "debug build", build_step("debug")
    yield "network", network_step
    for name, *download in books:
        yield name, book_step(name, *download)
    yield "solid.bin", solid_step
    yield "syzygy", syzygy_step


def main():
    root = project_root()
    parser = argparse.ArgumentParser(
        prog="maxima init", description=__doc__,
        epilog="Examples: maxima init; maxima init --check; maxima init --no-debug --syzygy 0")
    parser.add_argument("--check", action="store_true",
                        help="only report; change nothing; exit 1 when something is missing (for CI)")
    parser.add_argument("--no-build", dest="build", action="store_false", help="skip both builds")
    parser.add_argument("--no-debug", dest="debug", action="store_false", help="skip the Debug build")
    parser.add_argument("--syzygy", type=int, choices=(0, 3, 4, 5, 6, 7), default=5,
                        help="tablebase pieces to install, 0 skips (default: 5, about 1 GB)")
    parser.add_argument("--solid", action="store_true",
                        help="build solid.bin from Lichess master games (needs LICHESS_TOKEN)")
    parser.add_argument("--force", action="store_true", help="replace books that differ from the pinned files")
    options = parser.parse_args()
    options.data = data_root()
    if not (root / "CMakePresets.json").is_file():
        raise ValueError("init requires a Maxima checkout; set MAXIMA_PROJECT_DIR")
    print(f"data in {options.data} (set MAXIMA_DATA_DIR to move it, e.g. to a persistent volume)\n")
    results = []
    for name, step in steps(options):
        try:
            state, detail = step(root, options)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            state, detail = "failed", str(error)
        results.append((name, state, detail))
        print(f"{name:<14} {state:<8} {detail}", flush=True)
    failed = [name for name, state, _ in results if state in ("missing", "failed")]
    print(f"\nNot ready: {', '.join(failed)}" if failed else "\nReady.")
    return 1 if failed else 0
