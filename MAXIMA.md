# Maxima project documentation

This is the main technical documentation for Maxima. Keep it current as the
engine evolves and keep it short: describe current behavior, not its history.
`AGENTS.md` is the quick entry for contributors and coding agents; optional
private context lives in `.context/agents.md`, outside Git. Measurement records
and experiment logs belong under `.context/baselines/`; Git history keeps the rest.

## Contents

1. [Engine and goals](#1-engine-and-goals)
2. [Engineering conventions](#2-engineering-conventions)
3. [Code layout](#3-code-layout)
4. [Search and evaluation](#4-search-and-evaluation)
5. [Build and debugging](#5-build-and-debugging)
6. [Testing and performance](#6-testing-and-performance)
7. [CLI and local tablebases](#7-cli-and-local-tablebases)
8. [Roadmap](#8-roadmap)
9. [Opening books and persistent experience](#9-opening-books-and-persistent-experience)
10. [Python runner and project CLI](#10-python-runner-and-project-cli)

Keep chapter numbers stable; append new chapters rather than renumbering existing
ones. List chapters with `rg '^## ' MAXIMA.md` and locate topics with
`rg -n '<term>' MAXIMA.md`.

## 1. Engine and goals

Maxima2 is a C++26 UCI chess engine with bitboard move generation, PVS alpha-beta
search, NNUE evaluation, Syzygy tablebases, and experimental royal-losers support.

### Goals

- Improve playing strength, correctness, performance, and reliability.
- Make clean, understandable code a first-class goal: clear control flow,
  explicit state ownership and coherent interfaces.
- Preserve the engine's focus on speed: efficient chess algorithms, high search
  throughput, and compilation tuned for the target CPU.
- Keep search similar to established strong engines. Every mechanism needs a
  counterpart in that reference set; anything without one is removed rather than
  guarded. Evaluation-related behavior follows the engine that trained the
  current network.
- Standard chess uses NNUE exclusively. Lopsided endgames are a known weakness
  of the borrowed network; tablebases cover only their installed piece counts.
- Add multithreaded search with explicit ownership of worker state, safe shared
  caches, and reliable cancellation.
- Later: train our own networks (standard and royal losers), and investigate play
  that exploits other engines' selective-search weaknesses.

## 2. Engineering conventions

- Keep code clean, understandable, and maintainable. Judge simplicity by how
  easily behavior and invariants can be understood and changed safely. Prefer
  focused functions, explicit dependencies, and purposeful abstractions. Explain
  the reasoning behind heuristics and subtle invariants where it helps readers.
- Use snake_case for new project-defined identifiers and filenames. Preserve
  externally defined names, such as UCI commands and options.
- Prioritize missing engine fundamentals and substantial measured strength gains
  over complex changes aimed at marginal Elo improvements.
- Keep heuristics understandable and independently testable. Justify added
  complexity with measured benefits.
- During the NNUE search baseline work, use simple documented search rules and
  conservative round margins. Apply this to every pruning/reduction/extension
  under review. Establish a sound empirical baseline before adding adaptive
  tweaks or tuning; cite original references and state heuristic limits.
- Before implementing or substantially changing a chess algorithm, read the
  relevant [Chess Programming Wiki](https://chessprogramming.org/) articles.
  Follow their references to original papers and engine implementations when
  assessing technical details. Treat
  [NNUE](https://chessprogramming.org/NNUE) and
  [Late Move Reductions](https://chessprogramming.org/Late_Move_Reductions)
  as required reading for their respective workstreams; validate designs and
  tuning choices in Maxima.
- Treat performance as a first-class requirement. Preserve useful low-level
  techniques, including intrinsics and assembly; use profiling and benchmarks
  to guide hot-path changes and compiler choices. Compare Release builds with
  identical positions, limits, and compiler flags, checking NPS, time to depth,
  and playing strength rather than assuming any one metric proves improvement.
- Always use curly braces for `if` and `else` bodies, including single returns.
- Prefer incremental changes with clear ownership and explicit dependencies.
- Commit each completed, tested increment with its regressions and relevant
  documentation before moving on. Use a one-line Conventional Commit message
  (`type(scope): description`) with no body or footer.
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

- `src/`: engine, UCI handling, board state, move generation, search, NNUE
  evaluation, hash tables, time management, and Polyglot opening-book support.
- `src/w17/`: royal-losers search and material-only evaluation.
- `src/tablebase.*`: Maxima's Syzygy adapter and probing policies.
- `third_party/fathom/`: pinned, unmodified MIT-licensed table decoder.
- `tests/`: native correctness tests and raw-UCI lifecycle tests.
- `tests/data/`: independently generated certificates (SEE, search trees,
  boundaries) and small offline Syzygy fixtures.
- `runner/`: independently packaged Python CLI, UCI orchestration, suites,
  matches, tablebase downloads, experience tooling, and their tests.
- `tools/download_nnue.py`: fetches and verifies the pinned network.
- `networks/`, `tablebases/syzygy/`: downloaded data, ignored by Git.

CMake generates `generated/maxima/version.h` inside each build directory from
`src/version.h.in`. Do not edit generated headers.

## 4. Search and evaluation

Search is fail-soft PVS with quiescence, a shared transposition table, and
worker-owned histories. Every selective rule below is a heuristic: none is a
proven bound, and the round constants are provisional until tuned. Royal losers
(`Wild=losers`) has its own search in `src/w17/` and shares only the root driver;
the rules in this chapter apply to standard chess.

Every search-policy option is a UCI checkbox, default true, snapshotted by the
worker. Changing one stops and joins the worker and clears both TT modes;
unchanged or rejected values keep the cache. The switches are `NullMove`,
`NullVerify`, `BetaPruning`, `FutilityPruning`, `Razoring`, `LMR`, `LMRHistory`,
`LMP`, `HistoryPruning`, `MainSEE`, `IIR`, `ProbCut`, `SingularExtensions`,
`Aspiration`, `UpcomingRepetition`, `CheckEvasions`, `QsearchTT`, `TTEval`,
`TTHistory`, the history tables (`MainHistory`, `ContinuationHistory`,
`LongContinuationHistory`, `PawnHistory`, `CaptureHistory`, `Countermoves`,
`Killers`), ordering context (`ThreatOrdering`, `CheckOrdering`) and correction
history (`CorrectionHistory`, `PieceCorrection`, `ContinuationCorrection`).
`DrawContempt` (default -10 cp), the variant and tablebase settings also
invalidate cached scores.

### 4.1 Nodes and repetition

One node is counted per move made, including quiescence moves; null moves,
re-searches of a made move and pruned moves are not nodes. `go nodes` uses the
same counter.

Search owns its configuration and pre-root repetition history. At the root,
repetition requires three occurrences; below it, the first earlier match in game
or search history scores as a draw. Repetition identity ignores en passant
squares without a legal capture. Irreversible moves and null moves bound history
scans. Checkmate takes precedence over the fifty-move draw at 100 halfmoves.

`UpcomingRepetition`: below the root, when `alpha < draw`, search checks whether a
legal reversible move reaches an earlier position (a two-choice key-difference
table, confirmed by full legality and the resulting key). A confirmed move
raises alpha to the draw score before the TT probe, cuts off if that reaches
beta, and stays a floor for fail-soft returns. A singular search's excluded-move
node skips this check.

### 4.2 Root and iterative deepening

Depth one keeps the picker's legal order; each later iteration reorders the root
list by running the picker with the previous best move as hash move. Root LMR
keeps the first two moves at full depth.

`Aspiration` searches from depth two with a window of
`12 + score * score / 30155` around the previous score. A fail low moves beta to
the window midpoint and alpha to `score - delta`; a fail high moves beta to
`score + delta` and repeats one ply shallower per consecutive nondecisive fail
high (reset by a fail low). Delta grows by a third per retry. Reports keep the
iteration depth.

### 4.3 Interior node order

1. Abort, stack ceiling, mate-distance pruning, draw adjudication, tablebase
   probe, upcoming-repetition floor.
2. TT probe. Only non-PV nodes take score cutoffs: exact scores on either side
   of the window, lower bounds at or above beta, upper bounds at or below alpha.
   Every score needs sufficient depth, matching context and a clock inside the
   entry's validity range. PV nodes use the entry only as a move hint.
3. Static evaluation (section 4.9) and the improving flag: the eval is above
   that of the same side's last evaluated position two plies back, else four
   plies back; with neither, the node counts as improving. Checked nodes are
   never improving.
4. Node pruning, non-PV and outside check only:
   - Razoring: `eval < alpha - 300 * depth` runs qsearch on the null window; a
     nondecisive result at or below alpha returns.
   - Reverse futility, depth 1–8:
     `eval - (100 * depth - 100 * improving_without_threat + |correction| / 2) >= beta`
     returns `beta + (eval - beta) / 3`. A threat is an enemy pawn/minor/rook
     attacking a more valuable piece; a legal quiet TT move vetoes the cut.
   - Null move at expected cut nodes with depth above two and
     `eval + 132 * improving >= beta + 8 * depth`, never twice in a row. The
     reduced depth is `depth - 4 - depth / 3 - min((eval - beta) / 174, 4)`, one
     more with a tactical TT move. From depth 12 a cutoff is confirmed by a
     normal search at the null depth with null moves disabled in its subtree.
5. IIR: at PV or expected cut nodes from depth six without a legal TT move,
   search one ply shallower.
6. ProbCut from depth five: captures and promotions passing
   `see >= (threshold - eval) * 3 / 4` are screened by qsearch against
   `threshold = beta + 250 - 50 * improving`, then verified by a reduced
   null-window search (shallower for larger margins, with a correspondingly
   higher bar). A compatible TT upper bound below the threshold skips it.
7. Singular extensions from depth seven, below twice the root depth, for a legal
   TT move with a compatible lower/exact bound at `depth - 3`. A search at
   `(depth - 1) / 2` excluding that move tests `threshold = value - depth`:
   - below it: extend one ply; non-PV nodes extend two when the margin exceeds
     16 cp and three for quiets beyond 64 cp, within a per-line budget of 12
     multiple extensions;
   - otherwise: a non-PV alternative at or above beta cuts off (multicut); a TT
     value at or above beta reduces the move by `3 - pv` plies; a cut node
     reduces it by two.
   The excluded-move node skips the TT, node pruning, IIR, ProbCut, nested
   singular tests, correction updates and the repetition floor; its children are
   ordinary nodes. Without an alternative it returns alpha.
8. Move loop (section 4.4), then the TT store and correction update.

Node pruning and null moves never run in check, at decisive windows, without own
non-pawn material, or at reversible clock 99, where the next reversible move
draws unless it mates. Static cutoffs verify that a legal move exists, so
stalemate stays a draw.

### 4.4 Move loop: pruning and reductions

All move pruning needs one searched legal alternative and a nondecisive best
score, applies in PV nodes too, and checks legality before recording a skip.
The predicted depth is `depth - estimate`, where the estimate is the unclamped
LMR value for that move.

- LMP: quiets after `(3 + depth^2) / (2 - improving)` searched moves when the
  predicted depth is below nine.
- History pruning: quiets with learned history below `-2048 * depth` when the
  predicted depth is below seven; killers are exempt.
- Futility: quiets need predicted depth below six and
  `eval + 100 * (1 + d) + history / 64 <= alpha`; nonchecking captures need
  depth below eight and `eval + 200 * (1 + d) + victim + capture_history / 64 <= alpha`.
  Promotions, checks and evasions are exempt.
- SEE: captures below `-100 * depth`, quiets below `-50 * depth^2`.

LMR starts at depth three after the first move (two at the root). The base is
`floor(1 + ln(depth) * ln(move + 1) / 3)`; add two for an expected cut node, one
when not improving, one for a legal tactical TT move and `min(alpha_raises / 2, 2)`;
subtract one each for a PV node, a persistent TT-PV flag, giving check, being in
check, a capture/promotion and a quiet killer or countermove, plus the
correction uncertainty `min(|corrected - raw| / 100, 2)` and
`clamp(history / divisor, -2, 2)` (divisor 4096 for captures, 8192 for quiets).
The applied reduction is clamped to `[0, depth - 2]`. A reduced move that beats
alpha is re-searched at full depth and window. `LMRHistory` then rewards (result
at or above beta) or penalizes (at or below alpha) the quiet move's continuation
history.

Moves after the first use a null window; PV nodes re-search in the full window
when the score beats alpha. There are no check extensions.

### 4.5 Quiescence

Qsearch searches captures and promotions, plus all legal evasions in check.
Outside check, stand pat cuts at beta (after verifying a legal move exists),
captures need `see >= -60` and the picker stops at the first noisy move below
that. Futility skips a nonchecking, non-recapture move that does not win
material when `eval + 350 <= alpha`, raising the best score to that bound. In
check, after one evasion, evasions that lose material are skipped; quiet
evasions stay searchable. Stalemate is detected at every depth. Results are
fail-soft. `QsearchTT` stores entry and tactical qsearch bounds in their own TT
modes; scores never cross modes, move hints do.

### 4.6 Move ordering and histories

The picker stages are TT move, good noisy moves, killers, countermove, quiets,
bad noisy moves. `CheckEvasions` restricts generation in check to captures of the
checker, blocks and king moves (king moves only in double check). Quiet scores
combine main, continuation (one and two plies; three and five with
`LongContinuationHistory`), pawn-structure history; captures use capture history
plus MVV/LVA. `ThreatOrdering` adds 4000/8000/12000 for a minor/rook/queen
escaping a lower-value attack and subtracts the same for entering one;
`CheckOrdering` adds 10000 for a quiet check with `see >= 0`. Picker bonuses
never feed history-based decisions. `TTHistory` rewards a validated quiet TT
move whose score caused a cutoff and penalizes the early quiet predecessor that
allowed it. Histories persist across moves; `ucinewgame` resets them.

### 4.7 Correction history

The static evaluation is corrected by learned differences between search results
and the raw evaluation, indexed by pawn structure, non-pawn material per side,
minor and major piece keys (`PieceCorrection`) and the previous two moves
(`ContinuationCorrection`). Updates follow completed searches whose best move is
quiet, outside check and excluded-move nodes, below clock 90, and only when the
bound is informative. The corrected value is clamped below the decisive band.

### 4.8 Transposition table

Entries are 40 bytes: 64-bit position signature, 64-bit search-context
fingerprint, packed move/score/depth/age/clock-range/mode, a 64-bit model
fingerprint with a signed 16-bit raw evaluation, and a TT-PV flag. The context
covers repetition history, draw contempt, the model, and the halfmove-clock
bucket: clocks 0–13 share one bucket, then buckets of eight. Scores are reused
within a bucket; other buckets keep move hints. Every score also carries the clock
range over which its subtree is valid; reuse merges that range into the parent,
so fifty-move boundaries are never crossed. Selective cuts near clock 99 cap
the range at 98.

Replacement prefers empty slots, then move-only entries, then older and
shallower entries; a deeper non-exact entry of the same context survives a
shallower write. Move-only and evaluation writes never evict scores. `TTEval`
stores the raw network score so other workers and later searches skip inference;
it never authorizes a cutoff.

### 4.9 NNUE evaluation

Standard chess uses the Stockfish 13 network (legacy HalfKP: 41024 inputs,
two 256-wide accumulators, 512->32->32->1 integer inference) until Maxima trains
its own. Weights are CC0: `nn-62ef826d1a6d.nnue`, SHA-256
`62ef826d1a6d11b9e814188025aa02a60815c037292e0ef9bbb9bf4f724e5e63`, from
https://github.com/official-stockfish/networks. `python3 tools/download_nnue.py`
fetches and verifies it into `networks/`.

The static evaluation applies the network's own NNUE-path scaling:
`raw * (641 + material / 32 - 4 * clock) / 1024 + 28`, then
`* (100 - clock) / 100`, in network units (208 per pawn), converted to
centipawns. Material counts both sides' non-pawn material plus twice the pawn
value per pawn (126/781/825/1276/2538 for P/N/B/R/Q). Correction history is
added afterwards without clock damping. Scores are side-to-move relative and
clamped below the tablebase range.

The default model is `eval.nnue` beside `book.bin`. Initial `isready` loads it
before the clock starts; `MAXIMA_EVAL_FILE` overrides discovery, otherwise the
working directory, the executable directory and its parent are checked. Missing
or invalid models reject standard searches; a failed replacement keeps the
previous model. Select another compatible model with
`setoption name EvalFile value /absolute/path/to/network.nnue` and `isready`.
Model changes stop the worker and clear incompatible TT entries; each search
holds an immutable model snapshot. Royal losers needs no network.

`src/nnue.cpp` owns the isolated loader and inference. Accumulators are per
search and ply, updated lazily from the nearest ready ancestor; a king move
refreshes its perspective, null moves swap perspectives. The loader checks
size, version and architecture hashes. AVX2 feature accumulation, activation
packing and byte dot products (`vpmaddubsw`/`vpmaddwd`) have scalar fallbacks;
clipping keeps every intermediate exact. A per-search cache keyed by position
holds raw network scores; clock-dependent scaling is applied on every read.
The GPL reference probe used for exact inference tests is recorded in
`third_party/nnue_reference/README.md`. There is no network I/O or Python
dependency in the engine.

### 4.10 UCI boundary and time management

The UCI boundary validates positions and moves before committing the new FEN and
repetition history. `position fen` requires six fields, bounded nonnegative
counters, one king and at most eight pawns per side, valid rank widths, and
consistent castling/en passant metadata. Invalid move sequences reject the
entire command, including `forward`.
Moves must match a generated legal move; losers mode also enforces compulsory
legal captures. Rejected input reports an `info string` diagnostic and leaves the
previous position/history intact.
Material evaluation also bounds its pawn-count adjustment lookup for internal
boards constructed without UCI validation. Promotions remain supported.

`setoption` requires the `name` header and validates integers against advertised
spin bounds, boolean values, and the supported `Wild` selections before changing
options. Invalid `go` numeric limits are rejected without starting another search;
clock values must fit the engine's integer clock representation, and depth is
bounded by its search stack. Time-budget arithmetic avoids signed overflow for
large valid clock values. These checks validate supported input; they do not prove
that an arbitrary FEN is reachable from the initial position.

The engine supports orthodox castling and does not advertise `UCI_Chess960`.
Requests to set that option report `info string Unsupported option: UCI_Chess960`
and preserve options and the position/history. `go mate` limits are unsupported:
the whole request reports `info string Unsupported go limit: mate`, including
when the limit follows `searchmoves`. Rejected requests preserve the previous
search settings and do not start a replacement search. As with other `go` and
`setoption` commands, an active search is stopped and joined first.

UCI output serializes complete lines from the command and search threads, so
readiness responses and search reports cannot merge into a malformed line.
Protocol logging is buffered and synchronized. Output reaches stdout before
logging; `bestmove` then flushes the log, and a new game resets it after stopping
the prior worker. Per-line file opens/flushes are avoided. Timed UCI searches
start their monotonic budget when `go` enters the command handler, before
logging, parsing, prior-worker joining, allocation, and search setup.
The UCI `Move Overhead` option reserves 50 ms by default (range 0–5000 ms)
from each allocated clock budget for interface and transport delays. Adjust it
in the GUI engine options; for example, `setoption name Move Overhead value 100`.
Budgets saturate at zero when the reserve exceeds the allocation. Explicit
`go movetime` limits retain their requested duration. When UCI supplies
`movestogo 1`, clock-based searches also cap both budgets to leave 5% of the
remaining own clock, capped at 1000 ms, or `Move Overhead` if larger. This
reserve replaces rather than adds to the interface reserve at that cap, and
cannot grant time from an increment before the move is delivered. It scales
with the remaining clock rather than assuming a particular period length:
21363 ms leaves 1000 ms, 7338 ms leaves 366 ms and 5000 ms leaves 250 ms
with a 50 ms interface reserve. Other period moves and sudden death retain
their current allocation. The boundary follows UCI's remaining-move count,
not the board's move number; no next-period time is available until the GUI
credits it. Clock polling tightens
from 5000 visits to 512 within
250 ms of the deadline and to 64 within 50 ms; scheduling or blocking I/O can
still exceed those intervals, so these counts are not a hard real-time guarantee.
Both centipawn and mate scores retain lower/upper-bound qualifiers. NPS uses
reported nodes per second with the reported monotonic elapsed milliseconds; a
zero-millisecond sample uses a one-millisecond denominator. Rates retain unsigned
64-bit range and saturate if that range is exceeded. Reported nodes count moves
made in the search, including quiescence, using the same counter as `go nodes`;
null moves, re-searches of an already made move and pruned moves are not counted. Cancellation suppresses subsequent
full-window re-searches once the first pass has stopped.

Each finished search emits `info string search_result SOURCE completed_depth N
attempted_depth N interrupted 0|1 pruned_moves N`. Sources are `complete`,
`partial`, `fallback`, `book`, `terminal`, or `tablebase`. Completed depth counts
fully finished root iterations; attempted depth can include interrupted work.
The final score and PV depth belong to the selected result, which can remain from
a completed earlier iteration or come from a partially searched root. Partial
root scores use lower bounds; a fail-low score for only a subset of root moves
cannot bound the whole root and is omitted. Only completed root iterations and
terminal/tablebase shortcuts publish exact final scores. Book and legal fallback
moves report depth zero without a score; book selection reports zero nodes.
The engine result snapshot carries the same provenance, bounds and counters.
`interrupted` describes search work, independently of the selected result source;
for example, interruption before evaluating a new root move can leave the previous
completed iteration selected.

Search deadlines and UCI elapsed times use a monotonic wall clock. Time spent
preparing the search or waiting to run its worker counts toward the budget; a
process pause does not extend the deadline. Pondering suppresses deadline
cancellation until `ponderhit`, preserving elapsed time from the search start.
Explicit zero clocks and `movetime 0` retain a time limit and return a legal
move without requiring `stop`; absent time limits retain untimed search behavior.
`go searchmoves` accepts a nonempty list of legal root moves, including losers'
compulsory-capture rule. It restricts both search and book/experience selection;
illegal or empty lists reject the command. Other supported `go` options can
appear before or after the move list. A search interrupted before evaluating its
first root move returns a legal fallback from that permitted set, rather than
`0000`. This fallback is not a completed evaluation.

In losers chess, king-only wins, checkmate, and stalemate precede draw scoring.
These terminal rules also apply at the UCI root before book lookup: finished
positions report an exact terminal score and `bestmove 0000`. The side to move
wins when both sides have only their kings, matching interior search precedence.
Pondering retains a completed result until `ponderhit` or `stop`; explicit
`infinite` analysis retains it until `stop`, even after `ponderhit`. Depth, node,
and time limits may finish search work but do not authorize these results.
Argument order does not change the limits or infinite intent. This behavior
applies to ordinary searches, terminal roots, and book/tablebase shortcuts.
A bare `go` also waits for `stop`; an explicit depth limit, including 128,
remains bounded. Ordinary bounded searches publish their result when work finishes.
Playable positions reach the fifty-move threshold at 100 halfmoves, including
positions in check; checkmate does not become a draw at higher clock counts.

## 5. Build and debugging

The default build uses C++26 and requires a compiler that supports that mode
(tested with GCC 16), CMake 3.30 or newer, and Ninja. Engine lifecycle tests need
Python 3.7+ with its standard library. Configure with `-DBUILD_TESTING=OFF`
to build the engine without Python or test targets. The separately packaged
runner needs Python 3.8+; see chapters 6.1 and 10. Standard-chess tests require
the pinned network (download with `python3 tools/download_nnue.py`), or an
explicit `-Dmaxima_nnue_test_network=/absolute/path/to/network.nnue`. CTest
supplies `MAXIMA_EVAL_FILE` to isolate model discovery from its working directory.
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

### Windows GUI with the WSL build

The local Windows GUI bundle lives at `C:\dev\maxima`. Add
`C:\dev\maxima\maxima.exe` in En Croissant's Engines page. This executable
is a Windows UCI bridge: it launches the bundled ordinary Linux Release engine
through WSL distribution `Ubuntu`. WSL remains required; this is not a native
standalone Windows engine. The bundle is tied to this directory and the CPU
where the engine was built, but does not require the source checkout to run.

The bundle includes the NNUE weights, opening book, immutable experience file,
290 installed three/four/five-piece Syzygy WDL/DTZ tables (about 984 MB), and GCC runtime libraries.
The bridge translates single Windows drive paths for `EvalFile`, `BookFile`,
`ExperienceFile` and `SyzygyPath` to Linux `/mnt/<drive>/...` paths.
`eval.nnue` beside `book.bin` loads automatically; `EvalFile` selects alternative
networks. Use `C:\\dev\\maxima\\syzygy` for `SyzygyPath`, with
`SyzygyProbeLimit=5` and `SyzygyProbeDepth=1`. The complete five-piece set is bundled.
`BookFile` is `C:\dev\maxima\book.bin` and `ExperienceFile` is
`C:\dev\maxima\experience\experience.bin`. The frozen HCE baseline remains available through its separate launcher. The bundle's README lists the configuration.

Launcher source/build artifacts remain local under ignored `.tools/windows/`.
Copied assets are SHA-256 verified in the bundle's `SHA256.json`. Checks through
the copied Windows executable loaded NNUE/book/experience/Syzygy using Windows
paths, completed a legal depth-three search, and reported a Syzygy root hit.
GUI interaction itself requires the user's Windows desktop.

### 5.1 Optional Release LTO and GCC PGO

Ordinary builds keep their existing flags. Enable LTO explicitly with
`-Dmaxima_lto=ON`; CMake checks compiler/linker IPO support. LTO applies to
Release targets and allows optimization across source files. GCC PGO is
selected with `-Dmaxima_pgo_mode=generate` or `use` (default `off`) and
`-Dmaxima_pgo_dir=/absolute/path`. PGO requires Release and GCC for both C and
C++. It instruments the engine and Fathom, uses atomic profile counters, and
supplies the profiling runtime to executables linking the engine library.

Start with an isolated LTO build:

```bash
cmake -S . -B build/cpp26/lto -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= \
  -Dmaxima_lto=ON
cmake --build build/cpp26/lto --parallel
ctest --test-dir build/cpp26/lto --output-on-failure
```

Use the toolchain prefix and cache environment from chapter 5 when needed.
Each configuration owns its generated version header in its build directory,
so configuring Debug, Release, or profiling builds cannot overwrite another
configuration's revision/build-type metadata. Freeze each playing binary with
its hash and build settings. Revision metadata is captured at configure time;
reconfigure from the intended clean revision before freezing a baseline binary.

PGO has three steps: build an instrumented engine, train it with representative
searches, then rebuild with the recorded profile. For example, from the repository
root with the compiler environment active:

```bash
cmake -S . -B build/cpp26/pgo -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= \
  -Dmaxima_pgo_mode=generate -Dmaxima_pgo_dir="$PWD/build/cpp26/profiles"
cmake --build build/cpp26/pgo --target maxima2 --parallel
./maxima suite:run --engine build/cpp26/pgo/src/maxima2 \
  --suite /path/to/training.epd --nodes 500000 --timeout 60
cmake -S . -B build/cpp26/pgo -Dmaxima_pgo_mode=use
cmake --build build/cpp26/pgo --parallel
ctest --test-dir build/cpp26/pgo --output-on-failure
```

The suite runner requires its Python environment. Its standard-chess EPD input
requires a legal `bm`; these moves are suite targets, not instructions selecting
compiler optimizations. A bounded UCI corpus driver can train other variants
without that EPD scoring requirement. Training must wait for each search result
and quit cleanly so GCC writes the `.gcda` profiles. Do not use correctness tests
as the training workload. Configuration rejects an empty profile directory;
compilation rejects missing and incompatible profiles rather than silently
falling back to untrained optimization.

Include openings, early and late middlegames (quiet and tactical), queen, rook,
minor-piece, pawn, and promotion endgames, with both sides to move. Include
royal-losers searches if that variant will use the optimized binary. Keep books,
experience, and tablebases disabled when training search/evaluation code, and use
fixed hash/search options. This search-only workload does not exercise tablebase
decoder probing paths; assess tablebase performance separately before adopting a
production profile. Keep a separate held-out corpus with the same stage coverage
for measurement.

Keep the same source, compiler, build directory, and ordinary optimization
settings between profile generation and use. Save the training corpus and build
metadata with the profiles; start with a fresh profile directory when changing
source/compiler/LTO settings. To test PGO+LTO, repeat both PGO steps in another
build/profile directory with `-Dmaxima_lto=ON` from the start. Do not reuse the
plain PGO profile as evidence for the combined configuration.

Compare ordinary Release, LTO, PGO, and PGO+LTO only after builds and tests stop.
Pin identical C/C++ flags in all four configurations: compiler environment
activation can supply extra flags, while the ordinary presets clear them. The
examples above match the preset's empty additional flag settings.
Use repeated fixed-work searches and alternate execution order, preserving wall
times, search results, and variation by game stage and variant. Instrumented
engines are training tools, not performance candidates. Node/NPS reporting has
the limitations described in chapter 6.1; compiler optimization alone establishes
neither a speedup nor an Elo gain. Keep these build modes opt-in until measured.

See [GCC profiling](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)
and [GCC optimization](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
for the underlying flags.

An LTO/PGO comparison (2026-10-17) found no stable gain beyond noise; both stay opt-in.

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

For a small sanity check of pruning/reduction changes, build first and run:

```bash
ctest --preset cpp26_debug -R '^(test_search_features|test_uci_input)$'
```

`test_search_features` holds one minimal trigger per search feature: a position
and window where the feature must fire, plus one case where it must not. Feature
tests never assert node counts or restate tuning formulas. The full preset
additionally covers lifecycle, evaluation, board rules and search contracts.

Use identical Release compiler flags, positions, search limits, and hash size
for performance comparisons. Track nodes, NPS, and time to depth; only match
testing establishes a playing-strength gain. Test score bounds and cache reuse
as well as best moves. Keep regular tests offline and deterministic.

### 6.1 Position suites through UCI

Position benchmarks live outside the engine. The Python runner uses python-chess
through ordinary UCI commands; the `maxima` shell launcher dispatches tooling.
C++ correctness tests link the engine library, while lifecycle and suite runners
exercise the executable. No suite-specific UCI command or search hook is needed.

```bash
./maxima suite:run --movetime 1000
./maxima suite:run --nodes 200000 --json /tmp/modern.json
./maxima suite:run --suite /path/to/positions.epd --engine /path/to/engine
./maxima suite:run --suite /path/to/positions.epd --syzygy-path tablebases/syzygy
./maxima suite:run --suite bratko-kopec --nodes 200000 \
  --model EvalFile=networks/nn-62ef826d1a6d.nnue
```

Repeatable `--option NAME=VALUE` and `--model OPTION=PATH` select the evaluator
or search settings. Models are loaded before enabling evaluator options, hashed
in the report, and checked for changes before and after each search. Requested
options must be advertised by the engine. Use identical budgets and settings
for comparisons; use the frozen HCE executable with its legacy `UseNNUE=false`
option when comparing against HCE. Pass `--model EvalFile=...` explicitly so
the runner records the model hash, including when using the default network.

Use `--suite bratko-kopec` explicitly for the historical suite, now 22 positions:
BK.03 and BK.09 were removed following their disputed targets in
[this analysis](https://talkchess.com/forum3/viewtopic.php?f=6&t=57166).
Original IDs are retained; the old 24-position totals are historical.
All expected moves remain diagnostic targets from finite analysis, not proofs
that every alternative is wrong or mandatory engine correctness assertions.
Position scores do not establish Elo; paired matches remain the strength gate.

The runner requires Python 3.8+ and pins the `chess` package from
[python-chess](https://python-chess.readthedocs.io/en/latest/core.html).
Install the package and its dependency in the ignored runner environment:

```bash
python3 -m venv .tools/runner
.tools/runner/bin/python -m pip install -e ./runner
```

On systems missing `ensurepip`, but with system pip available, use
`python3 -m venv --without-pip --system-site-packages .tools/runner` instead of
the first command. The launcher prefers this environment, falls back to the
legacy `.tools/suites` environment, then uses `python3`. The runner is also
installable with `python -m pip install ./runner`, providing a `maxima` command.
Help, build helpers, and the downloader do not import python-chess. Engine
execution through `./maxima` or the binary needs no Python.

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

The bundled suite is package data under `runner/maxima_runner/data/suites/`.
Each position starts a fresh process in an empty temporary working directory,
so Maxima's automatic book and previous caches cannot affect results. Supported
options are set to hash 64 MiB (or `--hash`), one thread, one PV, no pondering,
no opening book, disabled analysis mode, zero draw contempt, and disabled Syzygy by default for a
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
nodes/time, and wall time until `bestmove`. Wall timing includes the initial
new-game readiness exchange; streaming observations use the runner clock. Reported depth/score can describe the
last completed iteration rather than the final interrupted search. Time/node
limits follow the engine's UCI implementation. Maxima reports visited nodes for
both telemetry and `go nodes`, and reports pruned moves separately in result metadata. Avoid equating node totals across
engines. Time comparisons also depend on hardware and compiler flags.

`--timeout` bounds response waits (default 30 seconds, extended to cover the
requested time budget). Protocol errors/timeouts return nonzero and clean up the
process. Unsolved positions are normal benchmark results and return success.
`--json FILE` saves the complete report after a successful run. Suite results do
not establish Elo; use engine matches for playing strength.

Runner tests exercise parsing, protocol handling, final-move scoring,
isolation, and timeout cleanup offline; they are separate from engine CTest.
The BK strength benchmark is deliberately run manually rather than enforced
as a build check.

### Offline paired-opening matches

```bash
./maxima match:run --engine-a /path/to/baseline --engine-b /path/to/candidate \
    --openings openings.json --pairs 2 --seed 2026 \
    --seconds 10 --increment 0.1 --pgn games.pgn --json results.json
```

Matches keep Syzygy disabled by default. `--syzygy-path /path/to/tables` enables
the same standard-chess table directory for both engines, with probe limit five
and probe depth one where supported. The directory must contain WDL tables,
and both engines must advertise `SyzygyPath` and `SyzygyProbeLimit`. Unsupported
engines or conflicting per-engine Syzygy overrides are rejected before either
engine is configured. The report records the resolved path and applied options.
This enables engine probing; it does not adjudicate games directly from tables.

Use `--variant losers` with royal-losers openings and two engines advertising
`Wild=losers`. Standard is the default. The opening file has this format:

```json
{"variant": "standard", "openings": [
  {"id": "open-game", "moves": ["e2e4", "e7e5"]},
  {"id": "queen-pawn", "moves": ["d2d4", "d7d5"]}
]}
```

Each entry may also specify a six-field `fen`; moves then start from that FEN.
Positions and move histories are validated before launching engines. Select at
most the number of distinct entries with `--pairs`. The fixed seed samples
without replacement; each opening is played twice with engine A assigned each
color. Fixed selection is reproducible, but clock-based searches and results
can vary. Both engines start fresh for every game in an empty temporary directory.
Applied options request one thread/PV, no pondering/book/analysis mode/contempt,
64 MiB hash (adjust with `--hash`), and no Syzygy probing or experience lookup when advertised. The report records
advertised options actually applied, identities, binary hashes, opening-file
hash, selected opening order, clocks, results, and terminations. Other engines
may require additional book/learning/tablebase configuration; inspect the
recorded options before accepting a baseline.

Use repeatable per-engine overrides to compare search settings or evaluation
backends with the same executable:

```bash
./maxima match:run --engine-a build/cpp26/release/src/maxima2 \
    --engine-b build/cpp26/release/src/maxima2 \
    --option-a LMR=true --option-b LMR=false \
    --openings openings.json --pairs 2 --seconds 10 --increment 0.1 \
    --pgn lmr.pgn --json lmr.json
```

Assignments use `NAME=VALUE`; quote assignments containing spaces. Option names
are case-insensitive, duplicate names are rejected, and explicit overrides must
be advertised by that engine. Checks accept only `true`/`false`, spins must meet
advertised integer bounds, and combos must match an advertised choice. Button
options are not supported. Both profiles are validated before either engine is
configured in each game. Hash, threads, PV count, pondering, variant, book,
contempt, analysis mode, Syzygy probe limit and experience controls belong to the
shared match policy; use `--hash` for the common hash size. Optional shared
settings are applied only when advertised, and their values are validated too.

For an external network, use `--model-b EvalFile=/path/to/network.nnue` (or
`--model-a`), rather than a plain `--option` assignment. This repeatable flag
accepts any advertised string option, sets an absolute file path before entering
the temporary engine directory, and records its SHA-256. `EvalFile` and
`EvalFileSmall` overrides require this model flag. File identities are checked
before configuration and after every game; a changed or missing model stops the
match, and a change during a game preserves an unfinished PGN/error record.
Output paths must also differ from model inputs. The JSON records requested
profiles and model identities at match level, plus applied settings, model
identities and advertised defaults per game. The runner cannot prove that an
external engine loaded the requested weights; use the engine's own load/parity
checks before strength testing. Embedded networks are identified by the engine
binary hash. Other default external files are not automatically discovered or
hashed; explicitly select them with model flags for reproducible comparisons.

For a baseline refresh after correctness fixes, finish the relevant native,
lifecycle, sanitizer and runner checks first, then freeze a clean revision with
its binary hash and build settings. Retain the previous frozen binary for
comparison. Freeze distinct variant-specific openings and match clocks before
running both color assignments; longer clocks can reduce short-clock artifacts.
Replay every PGN independently and inspect terminations, applied options and
pair uncertainty before using the corrected binary to judge strength candidates.
A completed small baseline match is not a strength promotion result.

Clocks use seconds plus a per-move increment. Wall time from the search request
through its response is charged, including readiness and runner overhead;
initial process startup/configuration is excluded. A late move loses on time,
except when the opponent has insufficient material under the selected variant.
Legal moves are verified before being pushed. Terminal outcomes and available
threefold/fifty-move claims are adjudicated by the independent board. Optional
standard-chess win adjudication is enabled with `--adjudicate-win-cp 1000`.
Starting at full move 20, it requires six consecutive plies of exact main-line
scores at or above that threshold for the same winning color: both engines
must agree, after converting their scores to White's perspective. Missing,
bounded, smaller, or disagreeing scores reset the sequence. Mate scores count
with their winning sign. Configure the sequence and earliest full move with
`--adjudicate-win-plies` (minimum 2, default 6) and `--adjudicate-min-move`
(default 20). Natural terminal/claimable outcomes, clock forfeits and engine
failures take precedence; errors remain unfinished and stop the match. Reports
record the policy and deciding scores, and PGNs mark `Termination "win adjudication"`
with the policy and a final comment. This is an estimated result and can conceal
conversion weaknesses; retain unadjudicated conversion checks separately.
Adjudication is disabled by default, preserving earlier baseline protocols.
`--max-plies` (default 600 searched plies, excluding the opening) leaves a
nonterminal game unfinished, with result `*`.
`--timeout` bounds each protocol response (default 30 seconds); increase it for
long clocks. Protocol/illegal-move failures preserve the current game's partial
PGN and error record, stop the match, and return nonzero. Completed game records
are flushed as they finish; abrupt interruption may lose the active game.
Output paths must be new and distinct from each other and inputs.

Each game also records reported search nodes, search counts, reporting coverage,
and charged wall time separately for White and Black in JSON `search_work`.
Played-move PGN comments include `[%nodes N]` when the engine reports a count.
New match records also retain per-move `search_samples`: reported depth,
selective depth, engine time, tablebase hits, nodes and charged wall time where
reported, plus Maxima's raw `search_result` metadata. PGN comments include
`[%depth D]`. The reported depth may describe an interrupted iteration; use
`completed_depth` from `search_result` when measuring completed search depth,
and count direct tablebase/book/terminal returns separately. Older reports,
including the completed 66-game strength gate, do not contain these samples.
Missing node reports are tracked through `reported_searches`, not treated as
measured zero work. Counts include searched quiescence and exclude pruned moves
for Maxima; external engines may use other counting conventions.

PGNs preserve opening history, setup FEN, clock comments, result, termination,
and `Variant "Royal losers"` for losers. Use
`maxima_runner.matches.read_match_pgn(stream)` to replay these PGNs with the
correct board without changing python-chess's global variant registry. Generic
PGN readers may reject this variant; do not import these into standard experience.

For strength testing, use paired-result SPRT with bounds selected before play:

```bash
./maxima match:run --engine-a /path/to/candidate --engine-b /path/to/baseline \
    --openings openings.json --pairs 1000 --sprt 0 5 \
    --seconds 10 --increment 0.1 --pgn sprt.pgn --json sprt.json
```

`--sprt ELO0 ELO1` uses logistic Elo for A relative to B, with nominal false-positive
and false-negative targets of 0.05 each (`--sprt-alpha` and `--sprt-beta`). The
supported bounds satisfy `-200 <= ELO0 < ELO1 <= 200`; error targets must be
positive with sum less than one. Declare all parameters and the maximum number
of pairs before results. `--pairs` is a cap, and requires at least that many
unique opening entries. Each completed color-swapped pair contributes to one
of five buckets: LL, LD/DL, DD/WL/LW, WD/DW, WW, from A's perspective. Incomplete
pairs, engine failures and a lone first game cannot trigger a decision.

The logistic generalized SPRT estimates the paired outcome distribution under
each hypothesis. It stops after a whole pair when the log-likelihood ratio
crosses `log(beta/(1-alpha))` (accept H0) or `log((1-beta)/alpha)` (accept H1).
At the cap, a test without a crossing is explicitly inconclusive. H1 supports
the higher hypothesis against the lower; it does not certify a gain of at least
ELO1. H0 likewise is not proof of equal strength or regression. Error targets
are nominal model-based rates, not distribution-free finite-sample guarantees.
Generalized SPRT has asymptotic error control and assumes independent sampled
opening pairs and stable testing conditions. Larger independent confirmation
at longer clocks remains necessary before promoting a playing change.

To stop on either a SPRT decision or evidence of a minimum gain, predeclare
`--sprt 0 10 --sprt-min-elo-stop`. The additional rule checks a simultaneous
confidence interval after every complete pair and compares it with the logistic
score corresponding to ELO1 (about 51.44% for +10 Elo). It stops with
`minimum_gain_established` when the lower bound is strictly above that score,
or `below_minimum_gain` when the upper bound is below it. The first condition
establishes at least the requested gain at the reported confidence level;
the second establishes a smaller gain, which can still be a useful improvement.
Whichever this rule or SPRT decides first ends the run. If both decide on the
same pair, the minimum-gain decision determines the stop reason. Each test's
own decision remains visible; `accept_h1` alone still does not certify ELO1.

Adding a rule requires allocating the error budget. Hybrid mode gives SPRT
half the requested alpha and beta, and the confidence bound an error budget
of `min(alpha, beta) / 2`. At defaults this is 0.025 for each SPRT target and
97.5% simultaneous coverage for the bound. By a union bound, positive errors
at ELO0 and negative errors at ELO1 retain the requested nominal overall targets;
the SPRT contribution remains asymptotic/model-based. Tightening its thresholds
can lengthen small-gain tests compared with plain SPRT. This is not a promise
of universally fastest stopping.

For a positive decision that must establish a minimum gain, use `--min-elo 10`
instead. It uses simultaneous 95% coverage and never accepts an ordinary SPRT
decision. All stopping modes are exclusive except the explicit SPRT hybrid flag.
Minimum gains must be finite within [0, 200] logistic Elo, always A relative
to B; put the candidate in A. Reports identify the mode, target score, minimum
Elo, allocated budgets, declared number of checks, bounds and stop reason.
A cap without a crossing is inconclusive; incomplete/failing pairs cannot stop.

The confidence rule uses pair scores in [0, 1] and radius
`sqrt(log(2 * maximum_pairs / bound_alpha) / (2 * completed_pairs))`.
Hoeffding plus Bonferroni controls all declared pair checkpoints, including
optional stopping. It requires representative random opening-pair sampling
and stable, independent playing outcomes; two games in a pair need not be
independent. Predeclare the cap and rules before fresh games, do not extend
the cap after inspecting results. The bound is conservative for small gains;
SPRT and confidence-bound decisions answer different questions.
See [Hoeffding's original bounded-sum inequality](https://doi.org/10.1080/01621459.1963.10500830).

For optional early stopping, predeclare `--sequential-check-pairs N`. `--pairs`
then sets the maximum number of opening pairs; checks occur after every N whole
pairs and at the maximum. Each check uses a two-sided Hoeffding interval with
error budget `0.05 / ceil(maximum_pairs / N)`. The union bound (Bonferroni)
provides at least 95% coverage across the planned checkpoints under the same
opening-pair sampling assumptions as fixed-sample matches. Stop when the
adjusted interval excludes 50%; otherwise finish the maximum sample. This is a
conservative checkpoint rule, not SPRT, and does not establish equivalence when
inconclusive. For example, `--pairs 50 --sequential-check-pairs 10` allows five
checks over at most100games; each checkpoint interval has99% coverage. Reports
identify the checkpoint confidence, total confidence, stop reason and whether
stopping was early. A successful early stop has `complete=true` although fewer
than the requested maximum games were played. Interim noncheckpoint reports
have no interval. Omit the flag for the existing fixed-sample95% workflow; do not
use those unadjusted intervals to decide when to stop.
The bound follows [Hoeffding's inequality](https://www.stat.cmu.edu/~larry/=stat705/Lecture2.pdf)
with an equal allocation of error across the predeclared checks.

JSON counts A's wins/draws/losses and unfinished games. The score estimate is the
mean of complete opening-pair scores, each game scored 1/0.5/0. The final 95%
confidence interval uses the conservative Hoeffding bound over pairs, retaining
within-pair dependence. It assumes independent pairs representative of the
opening population; narrow or biased opening sets limit conclusions. No interval
is reported until every requested pair finishes, avoiding interpretation of
partial or selectively completed runs as fixed-sample evidence. Small samples
have broad bounds; in fixed-sample mode these are score intervals, not Elo estimates or a sequential
promotion test. Unfinished games or engine failures return nonzero.

### 6.2 Correctness tests and sanitizers

`test_uci_input` covers malformed FENs and moves, atomic preservation of position
and repetition history (including unsupported pawn counts), invalid option/search
values, unsupported Chess960/mate requests, special moves, compulsory losers
captures, and large-clock arithmetic. Lifecycle tests verify Chess960 is absent
from advertised options, ordinary castling still works after rejection, and mate
limits in either argument order preserve readiness and do not start searches.
An unsupported mate request stops an active search with exactly one prior result
and allows a subsequent supported bounded search.
Keep confirmed bug fixes accompanied
by focused regressions; passing the suite is evidence for its covered cases,
not a claim that the engine has no remaining bugs.
`test_uci_output` verifies that concurrent output preserves every message as one
complete line, including scheduling delays between payloads and newlines. It
also checks positive/negative cp and mate score bounds, zero/short elapsed-time
NPS, and unsigned 64-bit node/rate formatting. Deterministic root-reporting
checks cover interruption before/after a child evaluation, previous completed
iterations, partial aspiration bounds for both signs of cp/mate scores, and
visited/pruned counter separation. Raw-UCI checks cover standard/losers node
budgets, result provenance, fallback scores and book result authorization.
Search ownership checks cover preparation/worker delays; lifecycle checks cover
ordinary and zero clocks, bounded ponder/infinite requests in either argument
order, exactly one authorized bestmove, standard/royal-losers terminal roots,
book/tablebase shortcuts, and pause/resume deadlines on platforms with SIGSTOP/SIGCONT.
`test_leaf_search` checks both colors' mate/stalemate at quiescence depths 0,
-1, and -2, terminal score windows, captures reaching stalemate, quiet playable
positions, board/hash restoration, and losers terminal root score signs.

`test_search_ceiling` checks both colors' mate/stalemate and playable checked
and quiet positions at the final stack slot, clocks 0/99/100/101, full/null
windows, standard PVS/quiescence and every losers mode, cold/seeded/disabled TT,
legal mate transitions, interrupted restarts, and board/hash/history restoration.
Synthetic placement isolates the boundary without claiming reachable histories
of that length; playable ceiling evaluations remain horizon approximations.

`test_search_boundary` uses independently generated python-chess certificates
for legal move sets, mating moves and clock-100 child results. It checks both
colors, full and null windows, mate distance, production defaults and individual
heuristics, cold/warm/disabled TT, parent clock-range propagation, interruption
and restart, and board/hash/history restoration. The certified trees terminate
after one reversible move; they do not establish general selective-search
equivalence or quiescence accuracy before a legal move reaches the draw clock.
Native checks require no python-chess installation. Regenerate or verify the
certificates with `python tests/data/search/generate_boundary.py [--check]` in
the runner environment, and review regenerated data before committing it.

`test_search_tree` checks PVS against independently generated two-ply legal
and minimax certificates, including arbitrary stable nonterminal leaf scores.
Its test subclass supplies the explicit leaf contract through the virtual PVS
boundary, stopping extensions at two plies. Null, beta, futility and LMR pruning
are disabled: this isolates window/bound handling rather than asserting accuracy
of the classical evaluator, quiescence or selective search. It compares every
certified legal move set, crosses cold/warm/disabled TT with full and bounded
windows, and checks state restoration and interrupted interior re-search restarts.
Verify certificates with `python tests/data/search/generate_tree.py --check` in
the runner environment; native checks need no python-chess installation.

Verify `selectivity.inc` with `python tests/data/search/generate_selectivity.py --check`.

`test_search_horizon` exercises real quiescence at depths 0/-1/-2 and PVS at
depths 0/1 on independently certified capture, recapture, promotion, en passant,
check-evasion and mate trees, including reversible clock 99. Its exhaustive
reference uses certified legal/tactical move sets and terminal scores, Maxima's
static stand-pat labels. It has no alpha-beta pruning, TT or move ordering; it
does not independently validate evaluation or SEE. Full/null windows, selective options enabled and
disabled, cold/warm/disabled TT, incompatible deep entries at quiescence entry,
real repetition histories, node-budget restart and state restoration are
checked. With selective search enabled, a one-node reverse-futility cutoff is
checked against its documented static margin rather than the unpruned oracle.
This is bounded search-integration coverage, not a general
proof of selective search. Verify certificates with
`python tests/data/search/generate_horizon.py --check` in the runner environment.
Native checks require no python-chess installation.

`test_search_review` checks independently certified both-color castling,
checking castles, attacked transit squares and check evasions at quiescence
0/-1/-2. Every legal child reaches the fifty-move draw boundary; production
stand-pat labels are an explicit shared dependency. It also checks exact-TT
PV truncation/legal text for both variants, missing-move entries, signed root
ordering biases, counters above 32 bits, saturation, sorting and complexity.
Production-default tactical fixture searches check exact warm reuse, disabled
TT, node-budget cancellation, fresh-state restarts and board/hash/history
restoration. Deterministic comparisons use identical selective settings;
they do not assert selective search equals exhaustive minimax. Verify the
castling certificates with `python tests/data/search/generate_review.py --check`.

`test_w17_tt_context` compares every royal-losers mate-search mode pair with
cold and disabled tables, both colors, reversible clocks, and repetition history.
It checks exact/lower/upper bounds, legal move hints, matching-mode reuse, and
incoming-mode identity across local horizon transitions.

`test_w17_evaluation` checks active material-only scores, classifications and
phase, both colors, cold/warm material caches, symmetry of the covered fixtures,
standard/losers material-cache separation, and board/hash/stack preservation.
King-only search terminals still precede static evaluation and draw clocks.

An address/undefined-behavior sanitizer build can be configured independently:

```bash
cmake -S . -B build/cpp26/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/cpp26/sanitize --parallel
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ASAN_OPTIONS=detect_leaks=1 \
  ctest --test-dir build/cpp26/sanitize --output-on-failure
```

Use the local toolchain prefix and cache environment from chapter 5 when needed.
LeakSanitizer requires an execution environment that permits thread inspection;
the restricted workspace runner may require these checks to run outside its
sandbox. Inspect sanitizer diagnostics and exit codes. Race checking remains a
separate requirement for concurrency changes.

Private measurement records and benchmark artifacts live under `.context/baselines/`.
Record the revision, dirty-tree status, binary hashes, positions, limits, build
configuration, and commands. Complete option/model setup with an `isready` /
`readyok` barrier before starting search CPU measurements; sending `setoption`
alone does not wait for configuration to finish. A short benchmark does not
establish a performance or playing-strength gain.

Independent rule/state checks use `maxima_runner.variants.royal_losers_board`,
a python-chess subclass for [FICS royal losers](https://www.freechess.org/Help/HelpFiles/losers.html).
Hand-authored fixtures verify both colors' compulsory legal captures, pinned
captures, capture/quiet check evasions, legal/illegal en passant, all promotions,
orthodox castling, king-only/checkmate/stalemate wins, terminal precedence,
and ordinary claimable fifty-move/threefold draws. Insufficient orthodox mating
material alone does not adjudicate a losers draw.

`test_board_state` checks incremental hashes against freshly initialized boards
and restores bitboards, matrix, active board/history stacks, counters, castling,
en passant, and search-stack pointers after each legal child and an entire
repetition history. The test-only `tests/board_oracle_probe` target also serves
runner integration checks: fixed fixtures and sorted-move random games with
seeds 17, 2026, and 24301 compare complete move sets, every legal child's six-field
FEN, and repetition equivalence against python-chess for standard and losers.
The bridge does not add engine protocol commands. These sampled checks do not
prove exhaustive chess correctness or validate all search/draw/TT behavior.

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
storage without downloading table files. The complete up-to-five WDL/DTZ set
listed by the mirror on 2026-10-17 contains 290 files, 983,957,920 bytes
(938.38 MiB, about 984 MB), including the smaller prerequisites.
`--directory /path/to/tables` selects another destination; the default
`tablebases/syzygy/` is ignored by Git. To download separately while a game is
running, use a staging directory such as `tablebases/syzygy-5-staging/` and leave
the active engine's directory/settings untouched. Activate the verified set
after the game; changing `SyzygyPath` stops the active search.

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

WDL probing resolves proven outcomes at zero halfmove clock, including full
search windows; widening a window cannot replace a solved outcome with static
evaluation. The depth setting limits probes at the highest available piece count;
smaller covered positions may be probed at leaves. Checked nodes retain normal
mate/evasion search. Root DTZ probing respects the current halfmove clock and
selects moves that make progress; ambiguous rounded results at the fifty-move
boundary fall back to search. Unique reversible game history permits direct DTZ play: minimizing DTZ prevents
cycles before the next capture/pawn move, so previously unique positions cannot
become a threefold repetition. If any position has already repeated in that
history (including the current root), retain normal history-aware search.
Missing DTZ files, ambiguous boundary results, and an excluded DTZ move under
`searchmoves` also retain normal search.
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
- Extend direct root tablebase handling across histories that already contain repetitions.
- Explore selective asynchronous online probes separately from local probing.
- Prepare shared caches and worker state for safe multithreaded search.
- After the final equal-depth checkpoint and baseline commit, select a new
  development focus with measured goals. Candidate directions recorded from the
  user: online play/operator integration; profiling and tuning under real time
  controls; a dedicated bullet setup; a more advanced supported NNUE family;
  and training our own royal-losers evaluator. These are alternatives for the
  next phase, not approved simultaneous implementation tasks. Preserve the
  single-worker NNUE baseline and use held-out strength tests for changes.

No speed or Elo gain should be claimed without the corresponding measurements.

## 9. Opening books and persistent experience

### 9.1 Playing configuration

The reference Polyglot book is read-only. `BookFile` defaults to `book.bin` in
its working directory; online clients should supply an absolute path. `OwnBook`
is now honoured: false disables both reference-book selection and experience.
`UCI_AnalyseMode=true` also disables them. These options affect future searches.

```text
setoption name BookFile value /home/hermen/dev/qm2/book.bin
setoption name ExperienceFile value /home/hermen/dev/qm2/experience/experience.bin
setoption name ExperienceDepth value 16
setoption name OwnBook value true
```

Experience is optional, disabled until a file is configured. The engine loads a
validated immutable snapshot; changing `ExperienceFile` stops the current worker
before publishing a new snapshot. Reapply that option after importing new games.
`<empty>` disables experience. Invalid reloads retain the previous snapshot.
The engine never writes either the reference book or the experience file.

Only generated legal moves can be selected from either source. Experienced
positions with a previous occurrence of the root, imminent fifty-move draws, and covered
Syzygy material fall back to normal search/tablebases. Saved scores are never
used as transposition-table bounds or reported as proven evaluations. Book
moves report zero cp, as before. Infinite/ponder searches still wait for the
appropriate stop/ponderhit before publishing a book move.

### 9.2 Importing online games

Tooling uses the same pinned Python environment as the EPD runner (chapter 6.1).

```bash
./maxima experience:import --pgn games/BugChessFICS.pgn
./maxima experience:import --pgn completed.pgn --player Maxima --player BugChess
./maxima experience:show
./maxima experience:export
```

Defaults are `experience/experience.sqlite` and `experience/experience.bin`,
ignored by Git. `--database` and `--output` select other files. Snapshot export
refuses to overwrite an existing file without an experience snapshot header.
The reference `book.bin` is untouched. Imported games must be standard chess;
invalid games roll back the current import rather than partly changing statistics.

Default player aliases are Maxima and BugChess, matched without case sensitivity.
Only games involving an alias contribute results, but **both sides' moves** are
retained. An opponent's winning line is recorded as successful for that move's
side, rather than penalised because Maxima lost. Own and opponent provenance and
own win/draw/loss totals are stored separately. Scores count draws as half points.
These are observed results, not causal proof that an opening caused the outcome.

The default horizon is the first 32 plies (`--max-ply`). A position/move receives
at most one result observation per game, even if it repeats. Stable game identity
uses starting position, moves, result, and identifying PGN headers; importing the
same game again does not increase its counts. A later import may add stronger
annotations or extend the import horizon. Incomplete games (`*`) may contribute
analysis but do not count as wins/draws/losses. Completed results are treated as
authoritative; corrected game identities are not automatically reconciled.

### 9.3 Confidence and repertoire exploration

The importer keeps the counts as well as the percentages. Selection prefers
Maxima's own results when available. Own results start with 32 neutral prior
games: one win gives an estimated 51.5%, not 100%. Opponent-only observations
use 64 neutral prior games because player strength can differ. With more games,
actual results increasingly outweigh the prior.

Book weights are multiplied by `0.1 + 0.9 * (2 * estimated_score)^4`. This is a
first, unvalidated policy, not an Elo claim. The positive floor preserves trials
of less successful moves. Randomness uses an independently seeded generator per
search and is not reset every time the file opens. New observed moves get a small
trial weight growing with sample count. When sparse experience has no reference
book entry or qualifying analysis, it competes with a fresh search rather than
forcing a previously seen move. This lets both Maxima's and its opponents' new
lines enter the repertoire without treating one game as conclusive.

### 9.4 Remembering serious analysis

Annotated PGN supports standard `[%eval 0.40,18]` (White's score and depth) and
`[%emt 0:01:00]` (elapsed move time). Optional `[%nodes 1200000]` and
`[%pv e4 e5 Nf3]` comments retain effort and a legal SAN or coordinate PV.
A PV candidate is kept separately from the move actually played; unplayed PV
moves do not receive game-result counts. PV scores are attached only to their
first move, not copied into descendant positions. Engine identity comes from
WhiteEngine/BlackEngine headers, falling back to the player name. Unknown clock
annotations are not interpreted as elapsed thinking time.

Analysis retains score perspective, mate/cp type, depth, nodes, time, PV, engine
identity and halfmove clock. Stronger depth/node/time evidence replaces weaker
analysis for the same position/move/engine/clock. Engine versions remain visible
in the database. The compact snapshot exports the strongest record per move.
At a matching halfmove clock, analysis meeting `ExperienceDepth` (default 16)
can qualify an extra book candidate even without result observations. Candidates
must score within 50 cp of the best eligible saved analysis among legal moves;
analysed inferior continuations do not become recommendations merely because
they were examined deeply. Mate scores rank ahead of centipawn wins and behind
centipawn losses as appropriate. It remains
book evidence, not an exact score cache. A deeper recorded move can therefore
be available immediately in a later short game; this reuse policy needs match
validation and does not guarantee that past analysis was correct.

### 9.5 Optional post-game analysis

```bash
./maxima experience:analyze --pgn completed.pgn --movetime 1000 --max-ply 32
./maxima experience:analyze --pgn completed.pgn --movetime 60000 --max-ply 4
```

`--engine` selects an executable; default is the Release Maxima binary. The tool
starts an external UCI engine in an empty directory with book/experience disabled.
It analyses each position in the selected opening horizon and saves the best PV.
If its preferred move differs from the played move, it also searches the played
continuation and reports both scores and an estimated centipawn loss. This may
use two time budgets per position; differing search horizons can produce noisy
comparisons. Reports are candidate improvements, not proven mistakes. Analysis
candidates do not acquire invented wins or losses. Snapshot publication happens
when analysis completes. Result accounting remains a separate import command.

An online client can save annotated PGN at game end, invoke these commands, and
reload ExperienceFile before the next game. Automatic server connection,
background-job scheduling, and live UCI analysis recording are not implemented
here. The existing legacy `learn` self-match tuner is separate and is not used
by this persistent experience layer.

### 9.6 Storage and search ownership

SQLite is the authoritative writable store, used only by external tooling.
An atomic snapshot replace leaves readers with complete data. Snapshot format
v1 is little-endian: `MXEXP001`, an unsigned 64-bit record count, then sorted
80-byte records. Each has a full 64-bit Polyglot key, coordinate move (source six
bits, target six bits, promotion N/B/R/Q encoded 1/2/3/4), total W/D/L, analysis
depth/score/nodes/time/clock/type/engine fingerprint, own W/D/L, and a reserved
zero field. Duplicate or unordered records, bad headers, invalid field ranges,
incorrect lengths, and counts above two million are rejected.

Searches hold shared immutable ownership of the loaded snapshot. Lookups and
weighting occur only at the root, outside the search node hot path. Configuring
a file parses it once; ordinary searches do not reopen or modify it. Statistics
and analysis never change fifty-move/repetition identity or TT score semantics.
Tests cover confidence smoothing, both-side result attribution, duplicate and
repeated-position handling, stronger annotations, unfinished games, atomic
publication, legal selection, rejected reloads, OwnBook, and unchanged book bytes.

## 10. Python runner and project CLI

One repository contains two independently usable components: the C++ engine and
an installable Python runner. The engine owns search, evaluation, time allocation,
UCI handling, book/tablebase probing, and reading immutable experience snapshots.
It never imports, embeds, or invokes Python. The runtime boundary is UCI plus
existing book, tablebase, and experience files.

The runner uses python-chess for boards, legal moves, SAN/UCI notation, EPD, PGN,
Polyglot keys, and UCI process handling. Its `uci` module uses asyncio for engine
sessions, streaming analysis, response deadlines, cancellation, and bounded
shutdown followed by process reaping. Async operations can be awaited by future
server clients; ordinary CLI entry points run their event loop with `asyncio.run`.
Sessions accept ordinary `chess.Board` and `royal_losers_board`. Before search,
they select `Wild=standard` or `Wild=losers` when supported; royal losers requires
an advertised combo option containing `losers`. Unsupported board variants and
Chess960 are rejected before analysis. The losers board retains ordinary UCI
position transport and independent rule/terminal semantics. Offline matches
preserve variants in PGN export/replay.
Losers CLI suites, general PGN imports, and experience remain pending; those
workflows must preserve variant identity when introduced.
Generic UCI orchestration has no dependency on the Maxima source tree.
Maxima's SQLite experience accounting and binary snapshot serialization remain
in the runner's experience module; their formats are unchanged.

The CLI owns repository paths and build helpers. Existing `suite:run`,
`experience:*`, and `tablebases:syzygy:download` commands remain available.
Default engine selection prefers Release, falling back to Debug. Installed
commands discover the checkout from the current directory or
`MAXIMA_PROJECT_DIR`; outside a checkout, generic suite commands accept
`--engine /absolute/path/to/engine` and experience/download data defaults to the
current directory. Benchmark positions ship with the installed package.

```bash
./maxima build                 # configure and compile Release
./maxima build --debug
./maxima test                  # Release engine checks, then runner integration checks
./maxima test --debug
./maxima test --engine-only    # CTest, no python-chess required
./maxima test --runner-only    # runner checks against the selected built engine
./maxima uci                   # replace the CLI process with Maxima2
./maxima uci --engine /path/to/engine
./maxima uci --engine /path/to/engine -- --engine-option
```

`maxima uci` uses the Release engine, falling back to Debug. `--engine PATH`
selects another executable; relative paths resolve against the current directory.
Arguments after `--` pass unchanged to that executable, including `--help`.
This command requires only Python's standard library and uses `os.execv`, so
the engine inherits the process ID, working directory, environment, standard
streams, and normal signal handling. There is no persistent Python proxy and no
CLI banner or styling in protocol output. Launch failures report on stderr and
return nonzero. Use `maxima help uci` or `maxima uci --help` for command help.

Build helpers use the existing CMake presets and automatically use the local
micromamba compiler environment when present; otherwise they use tools on PATH.
Tests require an existing configured build and propagate failed commands' exit
codes. CTest contains engine correctness tests, including raw-UCI lifecycle
checks using the Python standard library. Runner tests are separate:

```bash
.tools/runner/bin/python -m unittest discover -s runner/tests -v
MAXIMA_ENGINE="$PWD/build/cpp26/release/src/maxima2" \
    .tools/runner/bin/python -m unittest discover -s runner/tests -v
```

Without `MAXIMA_ENGINE`, real-engine integration tests are explicitly skipped;
`./maxima test` supplies it and runs both layers. Runner test fixtures may use
raw UCI to assert engine-specific behavior; application code uses python-chess.
With the pinned python-chess version, malformed engine `bestmove` responses
may surface as logged protocol errors followed by the response deadline;
the runner still fails and reaps the process. Async lifecycle checks require
local socket access for asyncio's event-loop wake-up mechanism.

JDrone's Java implementation remains operational in its own existing checkout.
The next migration increment is FICS transport, game/clock orchestration,
automation, and PGN capture built on this async runner. ICC, legacy database
migration, remote control, and notifications are deferred. Learning writes stay
in Python; snapshot reading and move selection stay in Maxima.

### 10.1 Documentation and CLI interface

Update this document in the same change whenever implemented behavior, public
commands, configuration, architecture, or workflows change. Keep examples runnable
and distinguish available functionality from planned features. Private milestone
status, decisions, known bugs, and reproducible measurement records belong in
`.context/plan-maxima.md`; that file is ignored by Git.

The CLI follows the local Nimbly CLI's concise command summaries and section
separators, with a left-aligned ASCII crown above the Maxima name. It provides
the tagline "Chess engine & ops" and
`maxima --help` / `-h` for common
commands, `maxima help` for the full implemented command listing, and
`maxima help COMMAND` or `maxima COMMAND --help` for detailed usage, options,
and defaults. Build/test help includes examples. Current command groups are
engine operation, development, and chess data; online-play commands will be added
when implemented. Existing colon-style commands and the
no-argument native-engine launch are preserved.

Help must work without an engine build, credentials, network access, or optional
runtime dependencies. Invalid commands or arguments return nonzero with useful
guidance. Output must remain readable when redirected; any styling is limited to
terminals and respects `NO_COLOR`: the crown is gold, separators cyan, and titles
bold. Native UCI streams remain free of banners and styling. Detailed examples
for every data command remain a planned documentation improvement.
