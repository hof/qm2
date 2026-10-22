"""Build and test Maxima, run position suites, and manage chess data."""
import argparse
import os
import subprocess
import sys

from .project import engine_path, project_root


command_groups = (
    ("Engine", (
        ("uci", "Run a UCI engine (Maxima2 by default)"),
    )),
    ("Development", (
        ("build", "Build Maxima2 (Release by default)"),
        ("test", "Run engine and runner checks"),
        ("suite:run", "Evaluate a position suite"),
        ("match:run", "Run offline paired-opening matches"),
    )),
    ("Chess data", (
        ("experience:import", "Import games into experience"),
        ("experience:export", "Export an engine experience snapshot"),
        ("experience:show", "Inspect experience"),
        ("experience:analyze", "Analyze experience positions"),
        ("tablebases:syzygy:download", "Download selected Syzygy tablebases"),
    )),
    ("Online play", (
        ("fics", "Play on FICS as MaxDrone (seek, play, save PGN)"),
    )),
)


def show_help(all_commands=False):
    use_color = sys.stdout.isatty() and "NO_COLOR" not in os.environ

    def styled(value, code):
        return f"\033[{code}m{value}\033[0m" if use_color else value

    print(styled(".       .\n|\\ /^\\ /|\n| V   V |\n'-------'", "33"))
    print(styled("M A X I M A", "1"))
    print("Chess engine & ops\n")
    print("Usage: maxima <command> [options]")
    print("       maxima                  Run the native UCI engine (from a GUI or pipe)")
    print("       maxima uci              Run it in this terminal\n")
    for title, commands in command_groups:
        visible = commands if all_commands else tuple(
            item for item in commands if item[0] in (
                "uci", "build", "test", "suite:run", "match:run", "experience:show", "tablebases:syzygy:download",
                "fics"))
        print(styled("-" * 72, "36"))
        print(styled(title, "1"))
        print(styled("-" * 72, "36"))
        for command, description in visible:
            print(f"  {command:<28} {description}")
        print()
    if not all_commands:
        print("Run maxima help to list every command.")
    print("Run maxima <command> --help for usage and options.")
    print("You can also use maxima help <command>.\n")


def toolchain_prefix(root):
    micromamba = root / ".tools/bootstrap/bin/micromamba"
    environment = root / ".tools/cpp26"
    if micromamba.is_file() and os.access(micromamba, os.X_OK) and environment.is_dir():
        return [str(micromamba), "run", "-p", str(environment)]
    return []


def run_command(command, root, env=None):
    return subprocess.run(command, cwd=root, env=env).returncode


def uci_command(arguments):
    parser = argparse.ArgumentParser(
        prog="maxima uci", description="Replace Maxima's CLI process with a UCI engine.",
        epilog="Examples: maxima uci; maxima uci --engine /path/to/engine -- --engine-option")
    parser.add_argument("--engine", metavar="PATH",
                        help="engine executable path (default: Release, then Debug)")
    parser.add_argument("engine_arguments", nargs=argparse.REMAINDER,
                        help="engine arguments following --")
    # Parse CLI options separately so everything after -- belongs to the engine,
    # including --help. Unseparated engine arguments are errors.
    separator = arguments.index("--") if "--" in arguments else len(arguments)
    args = parser.parse_args(arguments[:separator])
    if args.engine_arguments:
        parser.error("engine arguments must follow --")
    binary = (os.path.abspath(os.path.expanduser(args.engine)) if args.engine
              else str(engine_path(project_root())))
    if not os.path.isfile(binary):
        raise ValueError(f"engine executable not found: {binary}; use --engine PATH or ./maxima build")
    if not os.access(binary, os.X_OK):
        raise ValueError(f"engine is not executable: {binary}")
    os.execv(binary, [binary, *arguments[separator + 1:]])


def development_command(command, arguments):
    parser = argparse.ArgumentParser(prog=f"maxima {command}")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--debug", action="store_true", help="use the Debug build")
    modes.add_argument("--release", action="store_true", help="use the Release build (default)")
    parser.description = ("Configure and build Maxima2." if command == "build"
                          else "Run checks against an existing Maxima2 build.")
    parser.epilog = (f"Examples: maxima {command}; maxima {command} --debug")
    if command == "test":
        checks = parser.add_mutually_exclusive_group()
        checks.add_argument("--engine-only", action="store_true", help="run only native engine checks")
        checks.add_argument("--runner-only", action="store_true", help="run only Python runner checks")
    args = parser.parse_args(arguments)
    root = project_root()
    if not (root / "CMakePresets.json").is_file():
        raise ValueError("build/test requires a Maxima checkout; set MAXIMA_PROJECT_DIR")
    mode = "debug" if args.debug else "release"
    preset = f"cpp26_{mode}"
    prefix = toolchain_prefix(root)
    environment = dict(os.environ)
    if prefix:
        environment.setdefault("MAMBA_ROOT_PREFIX", str(root / ".tools/mamba"))
        environment.setdefault("XDG_CACHE_HOME", str(root / ".tools/cache"))
    if command == "build":
        status = run_command(prefix + ["cmake", "--preset", preset], root, environment)
        if status:
            return status
        return run_command(prefix + ["cmake", "--build", "--preset", preset, "--parallel"], root, environment)

    build_dir = root / "build/cpp26" / mode
    binary = build_dir / "src/maxima2"
    if not (build_dir / "CMakeCache.txt").is_file() or not binary.is_file():
        raise ValueError(f"missing {mode} build; run ./maxima build --{mode}")
    if not args.runner_only:
        status = run_command(prefix + ["ctest", "--preset", preset, "--no-tests=error"], root, environment)
        if status:
            return status
    if args.engine_only:
        return 0

    # Fail explicitly rather than silently skipping dependency-dependent tests.
    import chess
    environment = dict(os.environ, MAXIMA_ENGINE=str(binary), MAXIMA_PROJECT_DIR=str(root))
    source = str(root / "runner")
    environment["PYTHONPATH"] = source + os.pathsep + environment.get("PYTHONPATH", "")
    return run_command([sys.executable, "-m", "unittest", "discover", "-s",
                        str(root / "runner/tests"), "-v"], root, environment)


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(prog="maxima", description=__doc__)
    parser.add_argument("command", nargs="?", help=(
        "uci, build, test, suite:run, match:run, experience:import|export|show|analyze, "
        "tablebases:syzygy:download, fics (no command: run engine)"))
    if not arguments and sys.stdin.isatty() and sys.stdout.isatty():
        arguments = ["--help"]
    if arguments and arguments[0] in ("--help", "-h"):
        show_help()
        return 0
    if arguments and arguments[0] == "help":
        if len(arguments) == 1:
            show_help(all_commands=True)
            return 0
        if len(arguments) != 2:
            parser.error("usage: maxima help [command]")
        arguments = [arguments[1], "--help"]
    try:
        if not arguments:
            binary = engine_path()
            if not binary.is_file():
                raise ValueError("build Maxima first: ./maxima build")
            os.execv(str(binary), [str(binary)])
        command, remaining = arguments[0], arguments[1:]
        if command == "uci":
            return uci_command(remaining)
        if command in ("build", "test"):
            return development_command(command, remaining)
        if command == "suite:run":
            from . import run_suite
            entrypoint = run_suite.main
        elif command == "match:run":
            from . import matches
            entrypoint = matches.main
        elif command in ("experience:import", "experience:export", "experience:show", "experience:analyze"):
            from . import experience
            remaining.insert(0, command.split(":", 1)[1])
            entrypoint = experience.main
        elif command == "tablebases:syzygy:download":
            from . import download_tablebases
            entrypoint = download_tablebases.main
        elif command == "fics":
            from . import fics
            entrypoint = fics.main
        else:
            parser.error(f"unknown command: {command}")
        # Existing subcommands retain their argparse interfaces.
        original = sys.argv
        try:
            sys.argv = [f"maxima {command}", *remaining]
            return entrypoint() or 0
        finally:
            sys.argv = original
    except ImportError as error:
        print(f"Runner dependency missing: {error}. Install with: python -m pip install ./runner",
              file=sys.stderr)
        return 1
    except (OSError, ValueError, RuntimeError) as error:
        print(f"Maxima failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
