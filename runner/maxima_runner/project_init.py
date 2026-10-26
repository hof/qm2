"""Prepare a checkout: toolchain, runner, builds, network, books and tablebases; ready parts are skipped.

Books, network, tablebases and experience go to MAXIMA_DATA_DIR when set, else the checkout."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

from . import download_tablebases
from .project import data_root, project_root

# Chris Whittington's polyglot-books collection: Noomen's Rebel book and his gambit-minded Daring.
polyglot_books = "https://github.com/ChrisWhittington/polyglot-books/releases/download/v1.0/"
polyglot_credit = "Chris Whittington's polyglot-books collection"
# Our own books, built by maxima books:draw and books:fire (MAXIMA.md 9.7).
maxima_books = "https://github.com/hof/qm2/releases/download/books-v1/"
# Pinned downloads: file, archive URL, archive member, size, SHA-256, source.
books = (
    ("book.bin", polyglot_books + "book.zip", "book.bin", 177973984,
     "5b2f1794c20f095e23364a966f682b1c235afd46ec1f1acbc6e4bf72f1f92bfe", polyglot_credit),
    ("fire.bin", maxima_books + "fire.zip", "fire.bin", 312768,
     "a44687218c01a4f29c42fcee1359e1b489b87e7db45cce91f8df04696f64f34f",
     "Maxima's books-v1 release: Daring by Jeroen Noomen, extended with Lichess masters moves"),
    ("draw.bin", maxima_books + "draw.zip", "draw.bin", 19904,
     "d8aeb8ebee8281a5a59435c16bcbcf5517118dc3837ce972aa3481d2b8f8daca", "Maxima's books-v1 release"),
)
# Static micromamba for Linux x86-64, used to create .tools/cpp26 when no C++26 toolchain is found.
micromamba_release = ("https://github.com/mamba-org/micromamba-releases/releases/download/2.9.0-0/micromamba-linux-64",
                      18292808, "366cd9cd8be14df1ab8ed50352a82111082a36686b2d389fdb79a92c3fafb3e3")


def file_checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def show_progress(received, total):
    if sys.stdout.isatty() and total:
        amount = f"{total / 1024**2:.0f} MiB" if total >= 1024**2 else f"{total / 1024:.0f} KiB"
        print(f"\r  {received * 100 // total:3d}% of {amount}", end="", flush=True)
        if received >= total:
            print()


def fetch(url, path, progress=None):
    with urllib.request.urlopen(url, timeout=60) as response, path.open("wb") as output:
        total = int(getattr(response, "headers", {}).get("Content-Length") or 0)
        received = 0
        for chunk in iter(lambda: response.read(1024 * 1024), b""):
            output.write(chunk)
            received += len(chunk)
            if progress:
                progress(received, total)


def download_file(url, size, sha256, target, progress=None):
    """Download one file, verify its size and SHA-256, then replace target atomically."""
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent, prefix=".init-") as temporary:
        download = Path(temporary) / target.name
        fetch(url, download, progress)
        if download.stat().st_size != size or file_checksum(download) != sha256:
            raise ValueError(f"{url}: size or SHA-256 does not match the pinned file")
        os.replace(download, target)


def download_member(url, member, size, sha256, target, progress=None):
    """Download a zip, verify one member's size and SHA-256, then replace target atomically."""
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent, prefix=".init-") as temporary:
        archive = Path(temporary) / "archive.zip"
        fetch(url, archive, progress)
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


def tool_version(command):
    """First major.minor in a tool's version output, or None when it does not run."""
    try:
        output = subprocess.run(command, capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    found = re.search(r"(\d+)\.(\d+)", output)
    return (int(found[1]), int(found[2])) if found else None


def path_toolchain():
    """None when PATH has GCC 16+, CMake 3.30+ and Ninja; otherwise what is missing."""
    if not shutil.which("ninja"):
        return "ninja"
    cmake = tool_version(["cmake", "--version"])
    if cmake is None or cmake < (3, 30):
        return "CMake 3.30+"
    compiler = shutil.which("c++") and subprocess.run(["c++", "--version"], capture_output=True, text=True).stdout
    gcc = tool_version(["c++", "-dumpfullversion"])
    if not compiler or "clang" in compiler.lower() or gcc is None or gcc < (16, 0):
        return "GCC 16+"
    return None


def toolchain_step(root, options):
    from .cli import toolchain_prefix
    if toolchain_prefix(root):
        return "ok", ".tools/cpp26"
    missing = path_toolchain()
    if missing is None:
        return "ok", "tools on PATH"
    micromamba = root / ".tools/bootstrap/bin/micromamba"
    if options.check:
        return "missing", f"no {missing} on PATH; ./maxima init creates .tools/cpp26"
    if not micromamba.is_file():
        if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
            return "missing", f"no {missing}; install micromamba, see MAXIMA.md chapter 5"
        print("micromamba: downloading the pinned release", flush=True)
        download_file(*micromamba_release, micromamba, show_progress)
        micromamba.chmod(0o755)
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
    """Only engines with NNUE ship tools/download_nnue.py; it prints the verified file.

    The build's tests need networks/ and the engine needs eval.nnue, so both must exist."""
    script = root / "tools/download_nnue.py"
    target = options.data / "eval.nnue"
    if not script.is_file():
        return "skipped", "this engine has no network"
    if target.is_file() and any((root / "networks").glob("*.nnue")):
        return "ok", "eval.nnue"
    if options.check:
        return "missing", "./maxima init downloads it"
    result = subprocess.run([sys.executable, str(script)], cwd=root, capture_output=True, text=True)
    if result.returncode != 0 or not result.stdout.strip():
        return "failed", result.stderr.strip() or "tools/download_nnue.py"
    shutil.copyfile(result.stdout.strip().splitlines()[-1], target)
    return "done", "eval.nnue"


def book_step(name, url, member, size, sha256, source):
    def step(root, options):
        target = options.data / name
        if target.is_file():
            if file_checksum(target) == sha256:
                return "ok", "verified"
            if not options.force:
                return "ok", "kept your own file (--force replaces it)"
        if options.check:
            return "missing", "./maxima init downloads it"
        print(f"{name}: {member} from {source}", flush=True)
        download_member(url, member, size, sha256, target, show_progress)
        return "done", url.rsplit("/", 1)[1]
    return step


def draw_step(root, options):
    """Rebuild draw.bin from Lichess master games instead of downloading the published one (MAXIMA.md 9.7)."""
    from . import master_books
    if not master_books.lichess_token(root):
        return "missing", "set LICHESS_TOKEN (environment or .env)"
    if options.check:
        return "missing", "./maxima init --draw builds it"
    status = subprocess.run([sys.executable, "-m", "maxima_runner", "books:draw"], cwd=root).returncode
    return ("done", "draw.bin") if status == 0 else ("failed", "./maxima books:draw")


def repertoire_step(root, options):
    """experiment.bin and the moods, the fun style books, are built from books/ in the checkout."""
    from . import learning
    books = root / "books"
    pairs = [(books / "experiment.pgn", options.data / "experiment.bin")]
    pairs += learning.mood_sources(books, options.data)
    stale = [(source, target) for source, target in pairs if source.is_file() and (
        not target.is_file() or target.stat().st_mtime < source.stat().st_mtime)]
    if not any(source.is_file() for source, _ in pairs):
        return "skipped", "no repertoire PGNs in books/"
    if not stale:
        return "ok", "up to date"
    if options.check:
        return "missing", f"./maxima init builds {', '.join(target.name for _, target in stale)}"
    for source, target in stale:
        target.parent.mkdir(parents=True, exist_ok=True)
        learning.build_repertoire(source, target)
    return "done", ", ".join(target.name for _, target in stale)


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


def skipped(reason):
    return lambda root, options: ("skipped", reason)


def steps(options):
    yield "runner", runner_step
    # Configuring the build needs the network: its tests are registered against networks/.
    yield "network", network_step
    if options.build:
        yield "toolchain", toolchain_step
        yield "release build", build_step("release")
        if options.debug:
            yield "debug build", build_step("debug")
    for name, *download in books:
        if not options.books:
            yield name, skipped("--no-books")
        else:
            yield name, draw_step if name == "draw.bin" and options.draw else book_step(name, *download)
    yield "experiment+moods", repertoire_step if options.books else skipped("--no-books")
    yield "syzygy", syzygy_step


def main():
    root = project_root()
    parser = argparse.ArgumentParser(
        prog="maxima init", description=__doc__,
        epilog="Examples: maxima init; maxima init --check; "
               "maxima init --no-debug --no-books --syzygy 0 (CI)")
    parser.add_argument("--check", action="store_true",
                        help="only report; change nothing; exit 1 when something is missing (for CI)")
    parser.add_argument("--no-build", dest="build", action="store_false", help="skip both builds")
    parser.add_argument("--no-debug", dest="debug", action="store_false", help="skip the Debug build")
    parser.add_argument("--no-books", dest="books", action="store_false",
                        help="skip opening and style books (about 180 MB; tests do not need them)")
    parser.add_argument("--syzygy", type=int, choices=(0, 3, 4, 5, 6, 7), default=5,
                        help="tablebase pieces to install, 0 skips (default: 5, about 1 GB)")
    parser.add_argument("--draw", action="store_true",
                        help="rebuild draw.bin from Lichess master games instead of downloading it (needs LICHESS_TOKEN)")
    parser.add_argument("--force", action="store_true", help="replace books that differ from the pinned files")
    options = parser.parse_args()
    options.data = data_root()
    if not (root / "CMakePresets.json").is_file():
        raise ValueError("init requires a Maxima checkout; set MAXIMA_PROJECT_DIR")
    where = ("the project root; set MAXIMA_DATA_DIR to keep them elsewhere"
             if options.data == root else "MAXIMA_DATA_DIR")
    print(f"books, network, tablebases and experience in {options.data}\n({where})\n")
    results = []
    for name, step in steps(options):
        try:
            state, detail = step(root, options)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            state, detail = "failed", str(error)
        results.append((name, state, detail))
        print(f"{name:<17} {state:<8} {detail}", flush=True)
    failed = [name for name, state, _ in results if state in ("missing", "failed")]
    print(f"\nNot ready: {', '.join(failed)}" if failed else "\nReady.")
    return 1 if failed else 0
