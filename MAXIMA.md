# Maxima project documentation

This is the main technical documentation for Maxima. Keep it current as the
engine evolves. `AGENTS.md` is the quick entry for contributors and coding agents;
optional private context lives in `.context/agents.md`, outside Git.

## Contents

1. [Engine and goals](#1-engine-and-goals)
2. [Engineering conventions](#2-engineering-conventions)
3. [Code layout](#3-code-layout)
4. [Search correctness and caches](#4-search-correctness-and-caches)
5. [Build and debugging](#5-build-and-debugging)
6. [Testing and performance](#6-testing-and-performance)
7. [CLI and local tablebases](#7-cli-and-local-tablebases)
8. [Roadmap](#8-roadmap)

Keep chapter numbers stable; append new chapters rather than renumbering existing
ones. Use descriptive headings and keep each chapter understandable independently.
Until a dedicated documentation reader exists, list chapters with
`rg '^## ' MAXIMA.md` and locate topics with `rg -n '<term>' MAXIMA.md`.

## 1. Engine and goals

Maxima2 is a C++26 UCI chess engine with bitboard move generation, classical
search and evaluation, and experimental losers-chess support.

### Goals

- Improve playing strength, correctness, performance, and reliability.
- Preserve the engine's focus on speed: efficient chess algorithms, high search
  throughput, and compilation tuned for the target CPU.
- Explore a learned evaluator, such as NNUE, with correctness tests and measured
  playing-strength gains guiding integration into search.
- Strengthen endgame play with verified KPK integration and optional tablebases
  that respect draw rules and work with partial, user-selected coverage.
- Add multithreaded search with explicit ownership of worker state, safe shared
  caches, and reliable cancellation.
- Make the code cleaner through focused refactoring: reduce global mutable
  state, encapsulate invariants, clarify interfaces, and remove misleading or
  unused behavior.
- Preserve chess correctness and existing functionality while improving the
  engine.

These goals guide development; they do not select a parallel search algorithm
or require every task to advance every goal.

## 2. Engineering conventions

- Keep code clean, minimal, and readable. Prefer small, explicit functions and
  simple data structures; introduce abstractions only when they simplify the
  current implementation.
- Use snake_case for new project-defined identifiers and filenames. Preserve
  externally defined names, such as UCI commands and options.
- Prioritize missing engine fundamentals and substantial measured strength gains
  over complex changes aimed at marginal Elo improvements.
- Keep heuristics understandable and independently testable. Justify added
  complexity with measured benefits.
- Treat performance as a first-class requirement. Preserve useful low-level
  techniques, including intrinsics and assembly; use profiling and benchmarks
  to guide hot-path changes and compiler choices. Compare Release builds with
  identical positions, limits, and compiler flags, checking NPS, time to depth,
  and playing strength rather than assuming any one metric proves improvement.
- Prefer incremental changes with clear ownership and explicit dependencies.
- Keep board representations, hashes, ply counters, and search stacks consistent
  through operations that own their invariants.
- Validate external input before changing engine state. Assertions supplement
  validation; they do not replace it.
- Use atomics or locks for shared mutable state. `volatile` is not thread
  synchronization.
- Support search and evaluation improvements with correctness checks and
  relevant performance or playing-strength evidence. Distinguish measured
  results from assumptions.

## 3. Code layout

- `src/`: engine, UCI handling, board state, move generation, search, evaluation,
  hash tables, time management, and Polyglot opening-book support.
- `src/w17/`: losers-chess search and evaluation.
- `tests/`: tests for bit operations, move generation, search, static exchange
  evaluation, Polyglot keys, hash tables, evaluation, board flipping, and mates.
- `gen/`: KPK bitbase generator.

CMake generates `src/version.h` from `src/version.h.in`. Do not edit the generated
header manually.

- `src/tablebase.*`: Maxima's Syzygy adapter and probing policies.
- `third_party/fathom/`: pinned, unmodified MIT-licensed table decoder.
- `tools/`: development utilities, including the tablebase downloader.
- `tests/data/syzygy/`: small offline tablebase test fixtures.
- `tablebases/syzygy/`: downloaded user tables, ignored by Git.

## 4. Search correctness and caches

Search owns its configuration and pre-root repetition history. Real game history
requires three occurrences; cycles strictly inside search use one earlier match
as a search heuristic. Repetition identity excludes en passant squares when no
legal capture is available. Irreversible moves and null moves bound history scans.
Checkmate takes precedence over a fifty-move draw.

The transposition table uses 24-byte entries: a full 64-bit position signature,
a 64-bit context fingerprint, and packed move, score, depth, age, and halfmove
clock-range metadata. Move hints can survive a context mismatch, but score
cutoffs require compatible context and clock ranges. Material and pawn caches
and the transposition table still need synchronization before parallel search.

KPK is an embedded win/draw bitbase. Proven draws stop search with the configured
draw score; winning positions retain search because KPK has no distance-to-zero
information. Tests independently verify all legal normalized KPK states.

## 5. Build and debugging

The default build uses C++26 and requires a compiler that supports that mode
(tested with GCC 16), CMake 3.30 or newer, Ninja, and Python 3.7 or newer
for lifecycle/download tests. Position-suite tests need Python 3.8+ and the
optional tooling dependency described in chapter 6.1.
Release builds retain `-O3 -DNDEBUG -march=native`; Debug builds retain assertions
and debug symbols. Fathom is compiled separately as C11; a C compiler is also required.
Native CPU tuning means the binary targets the build machine.

With those tools on your PATH:

```bash
cmake --preset cpp26_release
cmake --build --preset cpp26_release --parallel
ctest --preset cpp26_release
./build/cpp26/release/src/maxima2
```

Use `cpp26_debug` for the corresponding Debug configure, build, and test commands.
Its executable is `build/cpp26/debug/src/maxima2`.

For an isolated Linux toolchain, install [micromamba](https://mamba.readthedocs.io/en/latest/installation/micromamba-installation.html), then:

```bash
export MAMBA_ROOT_PREFIX="$PWD/.tools/mamba"
export XDG_CACHE_HOME="$PWD/.tools/cache"
micromamba create -y -p .tools/cpp26 -f cpp26_environment.yml
micromamba run -p .tools/cpp26 cmake --preset cpp26_release
micromamba run -p .tools/cpp26 cmake --build --preset cpp26_release --parallel
micromamba run -p .tools/cpp26 ctest --preset cpp26_release
micromamba run -p .tools/cpp26 ./build/cpp26/release/src/maxima2
```

This workspace's local micromamba executable is `.tools/bootstrap/bin/micromamba`.
The compiler environment lives under `.tools/` and is excluded from Git.
The isolated build links to the C++ runtime in `.tools/cpp26/lib`.
C++26 is required by default; CMake will fail rather than silently select an older
standard. An explicit compatibility build can select `-DCMAKE_CXX_STANDARD=17`.

You can use [Visual Studio Code](https://code.visualstudio.com/) to develop and debug Maxima2 by installing the
[Remote Development Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.vscode-remote-extensionpack) and [Docker](https://www.docker.com/).

![Image of VS Code](vscode.png)

## 6. Testing and performance

Tests must return a nonzero exit code when a check fails, so both direct execution
and CTest detect failures. Detailed test output is available in
`build/cpp26/debug/Testing/Temporary/LastTest.log`. Use `cpp26_release` for
Release checks. When using the local toolchain, prefix these commands with
`.tools/bootstrap/bin/micromamba run -p .tools/cpp26`.

- Run checks appropriate to the change. For engine behavior changes, cover
  relevant move generation, make/unmake behavior, UCI commands, and search
  lifecycle scenarios.
- For concurrency changes, check cancellation, repeated searches, race
  conditions, and scaling across worker counts.
- Use sanitizer builds when investigating memory safety or undefined behavior;
  inspect diagnostics as well as test exit status.
- For documentation-only changes, review the content and diff; a build is not
  necessary.

Use identical Release compiler flags, positions, search limits, and hash size
for performance comparisons. Track nodes, NPS, and time to depth; only match
testing establishes a playing-strength gain. Test score bounds and cache reuse
as well as best moves. Keep regular tests offline and deterministic.

### 6.1 Position suites through UCI

Position benchmarks live outside the engine. `tools/run_suite.py` communicates
through ordinary UCI commands; the `maxima` shell launcher dispatches tooling.
C++ correctness tests link the engine library, while lifecycle and suite runners
exercise the executable. No suite-specific UCI command or search hook is needed.

```bash
./maxima suite:run --suite bratko-kopec --movetime 1000
./maxima suite:run --suite bratko-kopec --nodes 200000 --json /tmp/bk.json
./maxima suite:run --suite /path/to/positions.epd --engine /path/to/engine
./maxima suite:run --suite /path/to/positions.epd --syzygy-path tablebases/syzygy
```

The default is the bundled 24-position Bratko–Kopec suite, one second per
position, and 64 MiB hash. The positions and historical expected moves were
recovered from `tests/test_bratkokopec.cpp` before commit `be07fda` (2014).
Treat these expected moves as benchmark targets, not proofs that alternatives
are wrong. Review targets before using individual positions as correctness tests.

Suite parsing requires Python 3.8+ and the pinned `chess` package from
[python-chess](https://python-chess.readthedocs.io/en/latest/core.html).
Install it in the ignored tooling environment:

```bash
python3 -m venv .tools/suites
.tools/suites/bin/python -m pip install -r tools/requirements.txt
```

On systems missing `ensurepip`, but with system pip available, use
`python3 -m venv --without-pip --system-site-packages .tools/suites` instead of
the first command. The `suite:run` launcher and CTest select this environment
when present; otherwise they use the normal Python interpreter, which must have
the package installed. Reconfigure CMake after creating the environment.
`--help`, engine execution, and the downloader do not import this dependency.

The runner accepts standard EPD with one or more `bm` moves in **SAN** (such as
`Nf3`, `Bxe6+`, `O-O`, or `a8=N`), and still accepts legal UCI coordinate moves.
Moves are resolved against the position and normalized to UCI for comparison.
Ambiguous or illegal moves and invalid positions are rejected with a
line-numbered error before the engine starts. Castling uses the letter `O`.
Blank lines and lines beginning with `#` are ignored.

EPD operations can appear in any order. Quoted `id` labels are optional;
`hmvc` and `fmvn` preserve the halfmove clock and fullmove number, defaulting to
zero and one. Other operations are parsed and retained in the original EPD
report field but do not affect scoring: a nonempty `bm` is required, so
`am`-only suites are not supported. JSON includes canonical `expected_san` and
normalized `expected_moves`, along with the original `epd`. For example:

```text
8/8/8/8/8/8/4P3/K6k w - - bm e3 e4; id "pawn advance";
```

Each position starts a fresh process in an empty temporary working directory,
so Maxima's automatic book and previous caches cannot affect results. Supported
options are set to hash 64 MiB (or `--hash`), one thread, one PV, no pondering,
no opening book, zero draw contempt, and disabled Syzygy by default for a
search/evaluation baseline. To test the complete engine with local probing, pass
`--syzygy-path /path/to/tables`. The runner resolves the path before changing
working directories, requires WDL files, sets `SyzygyPath`, and sets the probe
limit to five when advertised. Engines without `SyzygyPath` are rejected when
probing is requested. Partial table coverage remains valid; uncovered positions
use normal search. Per-position output and JSON include `tbhits` (zero when no
hit was reported). The JSON report records engine identity and the options
actually applied. Other engines may have additional
book/tablebase options; inspect their configuration when comparing engines.

Only the final `bestmove` determines solved/missed. The report also records the
first observed expected PV move, latest primary-PV score/depth, engine-reported
nodes/time, and wall time until `bestmove`. Reported depth/score can describe the
last completed iteration rather than the final interrupted search. Time/node
limits follow the engine's UCI implementation. Maxima currently reports visited
plus pruned nodes but enforces `go nodes` against visited nodes; reported totals
can therefore exceed the requested budget. Avoid equating node totals across
engines. Time comparisons also depend on hardware and compiler flags.

`--timeout` bounds response waits (default 30 seconds, extended to cover the
requested time budget). Protocol errors/timeouts return nonzero and clean up the
process. Unsolved positions are normal benchmark results and return success.
`--json FILE` saves the complete report after a successful run. Suite results do
not establish Elo; use engine matches for playing strength.

CTest exercises the runner's parsing, protocol handling, final-move scoring,
isolation, and timeout cleanup offline. The BK strength benchmark is deliberately
run manually rather than enforced as a build check.

## 7. CLI and local tablebases

### 7.1 Download command

Download complete Syzygy WDL and DTZ sets through the project command:

```bash
./maxima tablebases:syzygy:download --pieces 4
./maxima tablebases:syzygy:download --pieces 5 --dry-run
```

`--pieces` accepts 3, 4, 5, 6, or 7, including kings; each selection includes
smaller tables needed for captures and promotions. The default is 4. The complete
three- and four-piece set is 70 files, about 4.14 MiB. `--dry-run` lists total
storage without downloading table files. `--directory /path/to/tables` selects
another destination; the default `tablebases/syzygy/` is ignored by Git.
Downloads check the mirror's published SHA-256 hashes, replace files atomically,
and verify/reuse completed files when rerun. Python 3.7 or newer is needed only
for this download command and lifecycle/download tests, not to run the compiled
engine. Position-suite parsing has its own dependency; see chapter 6.1.

### 7.2 Engine and UCI configuration

After a Release build, `./maxima` starts the engine (falls back to the Debug build
if Release is absent). Enable local tables with UCI commands:

```text
setoption name SyzygyPath value /absolute/path/to/tablebases/syzygy
setoption name SyzygyProbeLimit value 5
setoption name SyzygyProbeDepth value 1
isready
```

The path can contain several directories separated by `:` on Linux. An empty
path (`<empty>`) or a probe limit of 0 disables tablebase use. The initial engine
integration supports up to five pieces; six/seven-piece downloads are available
for later work. Missing tables fall back to normal search, and changing the path
stops the active search and clears cached search scores.

### 7.3 Probing policies

WDL probing supplies proven draw results and win/loss cutoffs at zero halfmove
clock. The depth setting limits probes at the highest available piece count;
smaller covered positions may be probed at leaves. Checked nodes retain normal
mate/evasion search. Root DTZ probing respects the current halfmove clock and
selects moves that make progress; ambiguous rounded results at the fifty-move
boundary fall back to search. With known reversible game history, the root also
keeps normal search because tablebases do not encode repetition history.
Tablebase wins/losses use large centipawn scores, not invented mate distances;
`info` output reports successful probes as `tbhits`. A root tablebase result
still waits for `stop` during infinite analysis or `ponderhit`/`stop` while
pondering before publishing `bestmove`.

### 7.4 Decoder dependency

The probing decoder is the MIT-licensed Fathom library, pinned in
`third_party/fathom/`. Keep upstream sources unchanged so updates remain
reviewable. Maxima's adapter, configuration, score handling, and search policy
live in `src/tablebase.*` and `src/search.*` and follow project conventions.

## 8. Roadmap

- Add incremental CLI documentation lookup (`docs:list`, `docs:search`,
  `docs:section`) using the stable chapters in this file. These commands are
  planned and are not implemented yet.
- Measure tablebase probing cost and strength before adding more probe heuristics.
- Extend root tablebase handling across reversible repetition history.
- Explore selective asynchronous online probes separately from local probing.
- Prepare shared caches and worker state for safe multithreaded search.
- Evaluate learned evaluation after the search and testing foundation is sound.

No speed or Elo gain should be claimed without the corresponding measurements.
