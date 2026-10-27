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
11. [Connectors](#11-connectors)

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
- `third_party/fathom/`: pinned MIT-licensed table decoder.
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
worker. Changing one stops and joins the worker and clears the TT;
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
raises alpha to the draw score before the TT probe and cuts off if that reaches
beta; it is not a floor for later fail-soft returns. A singular search's
excluded-move node skips this check.

### 4.2 Root and iterative deepening

Depth one keeps the picker's legal order; each later iteration reorders the root
list by running the picker with the previous best move as hash move. Root LMR
keeps the first three moves at full depth.

`Aspiration` searches from depth two with a window of
`12 + score * score / 30155` around the previous score. A fail low moves beta to
the window midpoint and alpha to `score - delta`; a fail high moves beta to
`score + delta` and repeats one ply shallower per consecutive nondecisive fail
high (reset by a fail low). Delta grows by a third per retry. Reports keep the
iteration depth.

### 4.3 Interior node order

1. Abort, stack ceiling, mate-distance pruning, draw adjudication, tablebase
   probe, upcoming-repetition alpha raise.
2. TT probe. Only non-PV nodes below halfmove clock 90 take score cutoffs, and
   only with a legal or absent TT move: exact scores on either side of the
   window, lower bounds at or above beta (one ply deeper than the node), upper
   bounds at or below alpha. Proven mates need no depth. PV nodes use the entry
   only as a move hint.
3. Static evaluation (section 4.9) and the improving flag: the eval is above
   that of the same side's last evaluated position two plies back, else four
   plies back; with neither, the node counts as improving. Checked nodes are
   never improving.
4. Node pruning, non-PV and outside check only. A TT bound on the far side of
   the eval refines it (exact scores always) for reverse futility.
   - Razoring: `eval < alpha - 300 * depth` runs qsearch on the null window; a
     nondecisive result at or below alpha returns.
   - Reverse futility, depth 1–8:
     `eval - (100 * depth - 100 * improving_without_threat + |correction| / 2) >= beta`
     returns `beta + (eval - beta) / 3`. A threat is an enemy pawn/minor/rook
     attacking a more valuable piece; a legal quiet TT move vetoes the cut.
   - Null move at expected cut nodes with depth above two and
     `eval + 132 * improving >= beta + 8 * depth`, never twice in a row. It is
     skipped when a TT upper bound is below beta or a TT lower bound's move wins
     more than two pawns. The reduced depth is
     `depth - 4 - depth / 3 - min((eval - beta) / 174, 4)`, one more with a
     tactical TT move. From depth 12 a cutoff is confirmed by a normal search at
     the null depth with null moves disabled in its subtree.
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

Node pruning never runs in check, at decisive windows, or at reversible clock 99,
where the next reversible move draws unless it mates. Null moves also need own
non-pawn material. Static cutoffs verify that a legal move exists, so
stalemate stays a draw.

### 4.4 Move loop: pruning and reductions

When a conservative bound proves that the existing late-move-pruning predicate
rejects a quiet even with its maximum history, refutation and check bonuses,
search skips its unused check and history queries. Legality and the considered
move ordinal are still evaluated; pruning decisions and move ordering are unchanged.

All move pruning needs one searched legal alternative and a nondecisive best
score, applies in PV nodes too, and checks legality before recording a skip.
Move counts include every legal move considered at the node, pruned ones too.
The predicted depth is `depth - (estimate + 2 * tt_pv - 0.4)`, where the
estimate is the unclamped LMR value for that move in plies.

- LMP: quiets after `(3 + depth^2) / (2 - improving)` considered moves when the
  predicted depth is below nine.
- History pruning: quiets with learned history below `-2048 * depth` when the
  predicted depth is below seven; killers are exempt.
- Futility: quiets need predicted depth below six and
  `eval + 100 * (1 + d) + history / 64 <= alpha`; nonchecking captures need
  depth below eight and `eval + 200 * (1 + d) + victim + capture_history / 64 <= alpha`.
  Promotions, checks and evasions are exempt.
- SEE: captures below `-100 * depth`, quiets below `-50 * depth^2`.

LMR starts at depth three after the first move (the fourth at the root).
Reductions are computed in 1/1024 plies. With `R(i) = 21.3 * ln(i + ln(i) / 4)`
and `m` the move's count, the base is `(R(depth) * R(m) + 503) / 1024` plies, one
more when not improving and `R(depth) * R(m) > 915`. Adjustments in plies:
+0.96 non-PV, +1.56 expected cut node, +0.98 legal tactical TT move, +1.11 TT-PV
node whose cached score failed low, +0.38 per alpha raise; −1.26 TT-PV, −1 cached
entry at least as deep, −0.76 quiet killer or countermove, −1 capture/promotion,
−1.33 giving check, `−min(|corrected − raw| / 100, 2)` and
`−clamp(history / divisor, −2, 2)` (divisor 4096 for captures, 8192 for quiets).
The applied reduction is clamped to `[0, depth − 2]`, so LMR never extends.

A reduced move that beats alpha is re-searched at full depth on the null window,
one ply deeper when it beat the best score by `32 + 8 * (reduction + 1)` and one
shallower when it did not beat it by `new_depth`; `LMRHistory` then rewards or
penalizes the quiet move's continuation history. Moves after the first use a
null window; PV nodes re-search in the full window when the score beats alpha.

Hindsight: in a non-PV child of a move reduced by at least 1.39 plies, depth
grows by one when the two static evals sum below zero; after at least 2.44
plies it shrinks by one when they sum above 128. There are no check extensions.

### 4.5 Quiescence

Qsearch searches captures and promotions, plus all legal evasions in check.
Outside check, stand pat cuts at beta,
captures need `see >= -60` and the picker stops at the first noisy move below
that. Rejected noisy moves outside check are excluded without full-score SEE
or a second history lookup: their deferred stage is unreachable. Main search
and check evasions retain full SEE/history scoring for deferred captures.
Futility skips a nonchecking, non-recapture move that does not win
material when `eval + 350 <= alpha`, raising the best score to that bound. In
check, after one evasion, evasions that lose material are skipped; quiet
evasions stay searchable. After exhausting tactical candidates without searching
a legal move, a separate legal-move probe detects stalemate. Stand-pat and TT
cutoffs do not prove that a legal move exists. Results are
fail-soft. `QsearchTT` probes and stores qsearch results at depth 0 in the
main table; null-window qsearch nodes below clock 90 take cutoffs from any
bound, and any valid TT move is searched first.

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

Search and qsearch share one table of 32-byte clusters holding three 10-byte
entries: a 16-bit key tag, depth, a 5-bit age with TT-PV flag and bound, a
compact move, the score and the raw static evaluation. The key is the position
hash mixed with a halfmove-clock bucket (clocks 0–13 share one bucket, then
buckets of eight), so scores never cross buckets. Mate and tablebase scores
whose distance exceeds the remaining fifty-move budget are cut back to the
nondecisive band on probe. Score cutoffs stop at clock 90.

Replacement takes the same position or an empty slot, else the entry with the
lowest `depth - 4 * age_distance`. An existing entry of the same position is
overwritten by an exact bound over a non-exact one, or when
`depth + bound + age^2 / 4 + pv` reaches two thirds of its own depth plus
bound; otherwise it keeps its score and takes the newer move and evaluation.
`TTEval` stores the raw network score so other workers and later searches skip
inference; it never authorizes a cutoff. Changes to options, the model or draw
contempt clear the table.

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
packing and byte dot products (`vpmaddubsw`/`vpmaddwd`) have scalar fallbacks.
Dense weights are packed at load time into four-input blocks across outputs,
so AVX2 inference broadcasts each input block and accumulates eight outputs per
vector without horizontal reductions. The loader bounds positive and negative
weight sums before allowing four product pairs to share a widening instruction;
other layers widen each pair immediately and skip zero input blocks. Arbitrary
supported weights and int32 biases retain exact scalar arithmetic and clipping. A per-search
cache keyed by position holds raw network scores; clock-dependent scaling is
applied on every read.
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
cancellation until `ponderhit`, then starts the move clock budget on the worker
without restarting the search. Pondering does not consume the timed allocation.
Explicit zero clocks and `movetime 0` retain a time limit and return a legal
move without requiring `stop`; absent time limits retain untimed search behavior.
`go searchmoves` accepts a nonempty list of legal root moves, including losers'
compulsory-capture rule. It restricts both search and book selection;
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

BMI2 builds use PEXT to index compact rook and bishop attack tables (about
0.82 MiB instead of 2.25 MiB). Other builds retain multiplication-based magic
lookups. Both backends initialize by enumerating blocker subsets directly,
return the same attacks and preserve move order.

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

The bundle includes the NNUE weights, opening book,
290 installed three/four/five-piece Syzygy WDL/DTZ tables (about 984 MB), and GCC runtime libraries.
The bridge translates single Windows drive paths for `EvalFile`, `BookFile`
and `SyzygyPath` to Linux `/mnt/<drive>/...` paths.
`eval.nnue` beside `book.bin` loads automatically; `EvalFile` selects alternative
networks. Use `C:\\dev\\maxima\\syzygy` for `SyzygyPath`, with
`SyzygyProbeLimit=5` and `SyzygyProbeDepth=1`. The complete five-piece set is bundled.
`BookFile` is `C:\dev\maxima\book.bin`. The frozen HCE baseline remains available through its separate launcher. The bundle's README lists the configuration.

Launcher source/build artifacts remain local under ignored `.tools/windows/`.
Copied assets are SHA-256 verified in the bundle's `SHA256.json`. Checks through
the copied Windows executable loaded NNUE/book/Syzygy using Windows
paths, completed a legal depth-three search, and reported a Syzygy root hit.
GUI interaction itself requires the user's Windows desktop.

### 5.1 Optional Release LTO and GCC PGO

The `cpp26_release` preset enables LTO (`-Dmaxima_lto=ON`); CMake checks
compiler/linker IPO support. LTO applies to Release targets and allows
optimization across source files. `./maxima build --pgo` adds GCC PGO to that
Release build: it builds an instrumented engine, trains it with timed searches
of fixed middlegames (`runner/maxima_runner/pgo.py`) and rebuilds with the
profile. A plain `./maxima build` always switches PGO off again, because a
profile of older sources would reject the build. Measured 2026-10-24: LTO gave
about 4-10% and PGO+LTO about 6-19% more nodes per second than plain `-O3`,
with identical searches; use `--pgo` for playing binaries. GCC PGO is
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
royal-losers searches if that variant will use the optimized binary. Keep books
and tablebases disabled when training search/evaluation code, and use
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
and false-negative targets of 0.05 each (`--sprt-alpha` and `--sprt-beta`). Bounds
must be finite with `ELO0 < ELO1`; error targets must be
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

`--sprt` is plain SPRT and has no other stopping rule; choose bounds that fit
the expected difference. For a positive decision that must establish a minimum
gain, use the separate `--min-elo 10` mode. It checks simultaneous 95% bounds
after every complete pair and stops with `minimum_gain_established` or
`below_minimum_gain`. All stopping modes are exclusive.
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
if Release is absent) when a GUI or pipe launches it; started bare in a terminal it
shows the command help instead, and `./maxima uci` runs the engine there. Enable local tables with UCI commands:

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
`third_party/fathom/`. Its README records a local population-count signedness
cast; otherwise keep upstream sources unchanged so updates remain reviewable. Maxima's adapter, configuration, score handling, and search policy
live in `src/tablebase.*` and `src/search.*` and follow project conventions.

## 8. Roadmap

- Extend online variant support on FICS and Lichess: Chess960, antichess/suicide,
  Three-check, and an assessment of atomic chess. Establish each server's exact
  rules, engine/evaluator support, protocol encoding, state/hash invariants and
  lifecycle checks before accepting its games. Keep incompatible rules or extra
  state out of orthodox learning; preserve royal losers as its own ruleset.

- Single-thread speed: TT prefetch and huge pages, TT-only eval caching, a
  shared dense-layer reduction and sort-once move picking gave about 20% more
  nodes per cycle (2026-10-24). Remaining candidates, measured before any
  work: sparse first-layer input, `gives_check`, history lookups. Judge speed
  by nodes per CPU cycle; sample skid and cache-miss counts misled the first
  profiles.
- Bring the D8 and D10 depth-ladder totals within the 1.40× limit (1.64× and
  1.55× at the 2026-10-23 baseline; D12 passes at 1.32×).
- Add incremental CLI documentation lookup (`docs:list`, `docs:search`,
  `docs:section`) using the stable chapters in this file. These commands are
  planned and are not implemented yet.
- Measure tablebase probing cost and strength before adding more probe heuristics.
- Extend direct root tablebase handling across histories that already contain repetitions.
- Explore selective asynchronous online probes separately from local probing.
- Prepare shared caches and worker state for safe multithreaded search.
- Further directions, one at a time with measured goals: profiling and tuning
  under real time controls; a dedicated bullet setup; a more advanced supported
  NNUE family; and training our own royal-losers evaluator. Preserve the
  single-worker NNUE baseline and use held-out strength tests for changes.

Late-move reductions now follow the reference form described in 4.4, and the
online play integration has landed (9.2, 10.2). The 2026-10-23 baseline
(`9370042`) passed SPRT against the previous master (147 wins, 9 draws, no
losses at 5+0.05 s). Its effective branching factor, `sqrt(N(d+2) / N(d))` on
the depth-ladder totals, is 1.85 from D8 to D10 and 1.68 from D10 to D12,
below the required 2.

No speed or Elo gain should be claimed without the corresponding measurements.

## 9. Opening books and persistent experience

### 9.1 Native Polyglot book

The engine reads a Polyglot book without modifying it. `BookFile` defaults to
`book.bin` in its working directory; online clients should supply an absolute
path. `OwnBook=false` and `UCI_AnalyseMode=true` disable book selection for future
searches. `<empty>` clears the book path.

```text
setoption name BookFile value /home/hermen/dev/qm2/book.bin
setoption name OwnBook value true
```

Only generated legal book moves are candidates, with their original Polyglot
weights (duplicate entries add their weights). `searchmoves` restrictions apply.
Repeated roots, imminent fifty-move draws, terminal draws and covered Syzygy
material bypass the book for normal search/tablebase handling. Infinite and
ponder searches wait for the appropriate stop/ponderhit before publishing a
book move. Book results report zero nodes and no evaluation.

Native binary experience loading, `ExperienceFile`, `ExperienceDepth`, and all
`experience:book:*` import/analysis/export/inspection commands have been removed,
along with their exclusive storage and migration code. Existing PGNs, JSON,
binary snapshots and legacy databases remain untouched as historical data.
Statistical online learning uses the separate PGN workflow below.

### 9.2 Online opening learning (MaxDrone)

Status: implemented in `learning.py` and both online clients; thresholds are
first guesses awaiting measurement. Both clients use the same store beneath
`MAXIMA_DATA_DIR/experience` (on the shared PVC), publish after every saved game,
and reload before preparing the next game's opening book. A game already in
progress retains its existing view. Saved analysis metadata is passive; only
statistical evaluation/result evidence affects learned book choices.

FICS royal losers uses a separate store at `experience/losers`, with its own
`games`, `index/shared.json`, locks and `players`. PGNs carry
`Variant "Royal losers"`; readers preserve compulsory captures and royal kings
without modifying python-chess's global variant registry. Stores reject games
of the other variant, snapshots identify their variant, and opening selection
filters variant-tagged records. Standard records from older snapshots default
to standard. Losers uses learned openings only: no standard Polyglot/style/mood
books, standard online tablebases, or standard resignation thresholds. The W17
material evaluator is unchanged; its scores and learning probabilities have not
been calibrated by a new strength test.

Use `./maxima experience:build --variant losers` to rebuild that store, or
`./maxima experience:sync games.pgn --variant losers --player MaxDrone` to import
royal-losers games. Both commands append `losers` to their experience root.
Custom FICS `pgn_dir` places losers PGNs at `<parent>/losers/<directory-name>`
so they never enter the standard PGN directory. Disabling PGN or learning has
the same effect for both variants.

**Storage and concurrency.** Games are first durably saved to
`experience/games/<LearningId>.pgn`. After successful learning, both online
clients invoke `experience:archive` to merge completed games into
`experience/games/<LearningSource>/YYYY-MM.pgn` (UTC game-start month), then
delete the individual pending file. These monthly PGNs are the source of truth;
unfinished games remain individual files. Each includes `LearningSource` (`fics`, `lichess`, or
`local`), `LearningServer`, `LearningColor`, a stable `LearningId`, and the original
metadata, moves, clocks, evaluations and PV annotations. Optional `LearningGroup`
and `LearningResults` tags retain import classification and result-only eligibility.
A pending game is written to a unique temporary file, flushed and fsynced,
then atomically renamed. Monthly appenders share `games/.pgn.lock` across pods.
A per-month byte-offset ID index avoids reparsing old PGNs on routine appends;
an fsynced append journal recovers partial writes before retry or reconstruction.
The pending file is removed only after the monthly PGN and index are durable.
Existing monthly PGNs are indexed on first use. Replays merge stronger annotations
without adding another game; exceptional annotation/result corrections atomically
rewrite the affected monthly file. Repeated publication counts once. A
writer lock covers reading, merging, and publishing `index/shared.json`, which
holds the complete derived game records. Global and human/master/bot evidence is
shared across servers. Personal learning, signature/revenge lines, and player
statistics use `(server, handle)`; readable stats live in
`players/<server>/<handle>.json`. Readers see complete snapshots. The PVC filesystem
must support cross-process advisory locks and atomic rename across the client pods;
this implementation does not provide a network service or configure Kubernetes.

**Imports and rebuilding.** These commands use the shared online store:

```bash
./maxima experience:sync /uploads/games.pgn /data/experience
./maxima experience:sync /laptop/games /data/experience --player Maxima --group bots
./maxima experience:build --experience-dir /data/experience
./maxima experience:archive /data/experience/games/<LearningId>.pgn
```

`experience:archive` accepts multiple individual game paths for migration or retry.
It uses the same command in both online clients, including custom PGN destinations.
Archive failures retain pending files and are reported by the clients. Existing
legacy monthly files are preserved. All pods writing these files must run this
version; shared PVC advisory locks and atomic rename must work across nodes.
This change bounds PGN file growth by month; the existing shared learning JSON
snapshot/player-stat publication and startup reconstruction still scale with total
history and are not a constant-cost, multi-year learning index.

The sync source can be one PGN or a recursively scanned directory; the destination
is the experience root, defaulting to `MAXIMA_DATA_DIR/experience`. Maxima's side
is inferred from its player/engine tags (including local `match:run` EngineA/B
records); `--player` supplies aliases when inference is ambiguous. `--source`
sets missing source metadata and `--group` supplies missing opponent classification.
Existing provenance and identities survive copies and repeated uploads. New local
matches carry unique game IDs. Legacy PGNs without IDs receive deterministic IDs
from source, server, players, timestamps, starting position and moves; distinct
legacy games identical in all those fields cannot be distinguished automatically.
Malformed games, unsupported variants, and conflicting identities fail ingestion.
Conflicting final results require explicit `--replace`; replaying an unfinished or
less-annotated copy cannot erase a final result or stronger recorded analysis.
Unannotated uploaded games of at least ten moves can contribute result-only
evidence; this never invents evaluation or search-quality evidence. Unfinished
uploads without sufficient evaluations teach nothing.

`experience:build` reconstructs learning exclusively from PGNs: `index/` and
`players/` can be deleted and rebuilt completely. Keep **all PGNs**, including
source/evaluation annotations. Startup also rebuilds to recover a durable game
whose snapshot publication was interrupted. The old `games/fics/`,
`lichess/games/`, and `fics/games/` layouts are read and copied into the common
archive without deleting originals. Corrected canonical games take precedence
over their obsolete legacy copies. Stop old client versions during migration;
thereafter both pods must use this version and the same `MAXIMA_DATA_DIR`.
A custom `--pgn-dir` receives the game too; learning still archives a canonical
copy beneath the shared experience root. Idle analysis remains future work;
PGN comments retain `[%nodes]`, `[%nps]` and `[%searchtime]` (seconds of actual
search, separate from server `[%emt]`). Missing legacy effort is unknown. The
versioned derived index rebuilds from archived PGNs without adding duplicate
result observations.

**Learning signal (Maxima's eval).** Maxima's own `[%eval]` is the source of
truth; an eval can miss something, but not for long. Every opening move (both
sides, first 40 plies) gets two values from the mover's point of view:

- *eval*: the mean expected score of our next four searched evals from that
  move on (`1 / (1 + 10^(-cp/400))`: +1.00 ≈ 64%, +3.00 ≈ 85%), so book moves
  are judged by the position they led to, and an opponent's move that left us
  at -1.00 counts as a good move for that side;
- *result*: the game result (1, ½, 0), time forfeits included, since an
  opening that sets the opponent thinking is a real gain.

A move's value is `0.8 × eval + 0.2 × result`; either one alone when the other
is missing (book tails, unfinished games). A game teaches only when it lasted
10 moves and Maxima searched at least four of its moves: early resignations,
aborts, disconnects, and book-only wins carry a result but no judgement. In the result part a draw is
worth `0.5 + 0.45 × DrawContempt/100` (JDrone's draw factor), so draws are
welcome against stronger opponents and near-losses against weaker ones.

**Learned layers.** Global (all games: what suits Maxima), group (engines `(C)`,
masters with a title, other registered humans), and opponent. A move's rate in
each layer is shrunk toward the layer below (global toward 50%) with a prior of
8 games. Evidence weights: same FICS game type (lightning, blitz, standard) 1,
other types ½; unregistered opponents (`Account "unregistered"`) count in the
global layer only, at ½.

**Multi-layer book.** Each game gets its own multi-layer book: book.bin (the
base: its moves are good in principle), the style books `fire` (sharp, e.g.
gambits), `draw` (playing for the draw), and `experiment`, the week's `mood`, and the
learned layers, each with a percentage from the game's profile:

| profile | book | fire | draw | experiment | mood | global | group | opponent | search | revenge |
|---|---|---|---|---|---|---|---|---|---|---|
| equal | 45 | 0 | 0 | 0 | 10 | 15 | 5 | 25 | 5 | 30 |
| much weaker | 20 | 25 | 0 | 0 | 10 | 5 | 5 | 35 | 5 | 40 |
| much stronger | 30 | 0 | 25 | 0 | 0 | 10 | 10 | 25 | 5 | 15 |
| unrated | 30 | 30 | 0 | 10 | 15 | 15 | 0 | 0 | 10 | 50 |

The layers sum to 100. `search` is the chance per game to leave the book once at a random
ply, so the engine can bring in new moves; `revenge` is the least probability of the
opponent's own winning line (below).

Rated games slide from *equal* toward *much weaker* or *much stronger* with
the draw contempt (none within ±0.20, fully at ±1.00); unrated games (guests)
play fire, experiment, and the mood. book.bin is the source of truth and is never
rewritten; other layers only add to it.

**Revenge.** When an opponent (guest or registered) beats Maxima, their moves from the
first 24 plies become our line for their colour: next time Maxima has that colour against
them, each of those moves gets at least the profile's `revenge` probability where it is
legal, labelled `revenge` in the log. A win
with that colour serves the revenge; a newer loss replaces the line.

**Moods.** Like a human who is "in a Dutch mood lately", Maxima plays one theme
repertoire a week: `books/moods/<name>.pgn` (traxler, dutch, dragon, kings_indian,
scandinavian, parham: 1.e4 e5 2.Qh5!?) built into `<data>/moods/<name>.bin`. With
`"mood": "auto"` (default) the moods take turns by ISO week, so every machine agrees
without state; `"mood": "dutch"` pins one and `"off"` disables the layer. Games carry
`[Mood "dutch"]`, and registered accounts announce it in a finger note. A mood
only plays where its book knows the position; elsewhere the other layers decide. Percentages are configurable (`layer_profiles` in the
MaxDrone config). In each position:

1. Book layers give each book's move shares; moves under a tenth of a book's
   heaviest weight are dropped within that book.
2. Each learned layer reweights the books' mix by its sampled rate, with the
   engine's experience factor `0.1 + 0.9 × (2·rate)^4` (×1 at 50%, ≈×2.6 at 65%,
   ≈×0.5 at 40%). Rates are Thompson samples, so untried moves keep a chance
   and `variety` (setting or `--variety`) widens (2) or narrows (0.5) them.
   Moves only experience knows (an opponent's good move, an engine novelty) get
   a base share that grows with their games.
3. A learned layer counts at its percentage × `n / (n + 8)`, n = its games in
   the position; weight a layer cannot use goes proportionally to the others,
   except book.bin's: book.bin is the know-all base, so where it has no move
   its percentage becomes the chance to search instead (unrated: fire 30 against
   search 30), and a style book never decides alone.
   So book.bin and the style books keep their floor, and John's layer counts more as
   games against John accumulate.
4. Proven losers (at least 4 games, rate under 30%) are multiplied by 0.1 in
   every layer, book.bin included.
5. Signature lines, for psychology: when our move beat this registered opponent
   three times in a row, each time with our eval after it at 45% or better, it
   gets 70% of the choice there (`signature` in the PGN comment); the rest keeps
   its proportions, so some variety stays. The streak counts games, not time, so
   the line returns when the opponent comes back months later; a loss or a poor
   eval with it resets the streak.
6. The move is drawn from the result. Out of book a learned move is played only
   with real evidence (two games at 50% or more). Saved analysis alone starts a normal
   search. A deliberate search choice always runs an ordinary engine search.
   Saved analysis metadata remains in historical records but neither selects
   candidates nor shortens searches; opening books keep no analysis cache.
   Search depth, nodes, NPS, duration and PV are still recorded in PGNs.
   Retired `learning_nps` and `learning_margin` settings are rejected as unknown.

The *search* column is the share of games that leave the book once, at a
random ply, so the engine can bring in new moves (a per-move share would
compound over a game).

Style books are Polyglot files beside book.bin (`fire.bin`, `draw.bin`,
`experiment.bin`, not tracked); any Polyglot book works, and `style_books` in
the MaxDrone config points a style at another file, e.g. `{"style_books":
{"fire": "ph-gambitbook.bin"}}` (relative to the project root). A missing file
leaves that style's percentage to the other layers.

No published book aims at safe, drawish positions, so `./maxima
books:draw` builds `draw.bin` from the Lichess masters explorer. Our
moves are scored `draw% - 2 × loss%` for the side to move (at least 50 master
games); moves within 0.05 of the safest stay, weighted 100 down to 10.
Opponent replies played in at least 5% of games are followed, most likely
positions first, to 16 plies and up to 2000 positions per colour. The explorer
needs a Lichess token without scopes, from `LICHESS_TOKEN` or `.env` in the
project root (git-ignored); answers are cached in `experience/explorer/`, and
rate limits wait a minute as Lichess asks. A first build takes a few hours.

`experiment.bin` is for fun in unrated games: gambits and surprise first moves
(1.b4, 1.b3, 1.a3, 1.Nc3; 1...b6, 1...a6, 1...Nc6) that unsettle humans, while the
learned layers measure whether they work. `./maxima init` and `./maxima
books:repertoire` build it from `books/experiment.pgn`, one line per game
tagged `[Repertoire "white"|"black"]` so only our side's moves enter the book.
`--styles fire draw` builds those from their PGNs too, replacing the
downloaded books.

Moves are labelled in the terminal and PGN comment with the label, the move's
probability, and each layer's share of it, e.g. `learned 65%: fire 45%, global
13%, humans 13%, John 29%`; `book` when the books gave most, `learned` when the
learned layers did. These shares are logged for tuning the profiles. The horizon
is 40 plies. `--no-learning` (or `--no-pgn`) leaves the
book to the engine.

The decision is built per game at `Creating:` (opponent, group, game type, and
budget are known then) and must be ready before our first move; if it is late,
the global layer alone is used. Engine-side variants (rating-based draw value,
UCI_Opponent books) are planned. Later: background analysis of positions from games that were
not easy wins, as JDrone's `book_calc`.

## 10. Python runner and project CLI

One repository contains two independently usable components: the C++ engine and
an installable Python runner. The engine owns search, evaluation, time allocation,
UCI handling and native Polyglot book/tablebase probing.
It never imports, embeds, or invokes Python. The runtime boundary is UCI plus
existing book and tablebase files.

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
Losers CLI suites remain pending and must preserve variant identity when introduced.
Generic UCI orchestration has no dependency on the Maxima source tree.
Statistical online learning and its PGN archives belong to the Python runner.

The CLI owns repository paths and build helpers. Existing `suite:run`,
`experience:sync`, `experience:build`, `experience:archive`, and
`tablebases:syzygy:download` commands remain available.
Default engine selection prefers Release, falling back to Debug. Installed
commands discover the checkout from the current directory or
`MAXIMA_PROJECT_DIR`; outside a checkout, generic suite commands accept
`--engine /absolute/path/to/engine` and experience/download data defaults to the
current directory. Benchmark positions ship with the installed package.

`./maxima init` prepares a checkout in one command and skips what is already
ready, so it is safe to rerun and suits CI. In order:

- runner (`.tools/runner` with python-chess, unless the current Python has it)
- network: `networks/nn-62ef826d1a6d.nnue` (the build registers its tests
  against it) and its copy `eval.nnue`, through `tools/download_nnue.py`
- toolchain: GCC 16+, CMake 3.30+ and Ninja on PATH are used as they are;
  otherwise `.tools/cpp26` is created with micromamba, which is itself
  downloaded (pinned 2.9.0, verified by size and SHA-256) on Linux x86-64
- Release and Debug builds (Ninja's no-op check decides "up to date")
- `book.bin` from the pinned polyglot-books v1.0 archive, verified by size and
  SHA-256; a different existing file is kept unless `--force`
- `fire.bin` (Noomen's Daring carried on by `books:fire`) and `draw.bin` from
  Maxima's `books-v1` release; `--draw` rebuilds it from the
  Lichess masters explorer instead (needs a Lichess token, see 9.2)
- Syzygy tables up to `--syzygy N` pieces (default 5, about 940 MiB; 0 skips),
  skipped offline when the manifest lists the complete set

- `experiment.bin` and the mood books, built from `books/*.pgn` when stale

`--check` changes nothing, reports each item, and exits 1 when something is
missing. `--no-build` and `--no-debug` skip builds; `--no-books` skips all
books (about 180 MB, not needed by tests).

For CI, a fresh clone needs only git and Python 3.8+ with `venv`:

```bash
./maxima init --no-debug --no-books --syzygy 0
./maxima test
```

Cache `.tools/`, `build/` and `networks/` between runs; init then finds every
step ready and only Ninja's incremental build does work.

`MAXIMA_DATA_DIR` moves the data out of the checkout, for example onto a
persistent volume: books (`book.bin`, `fire.bin`, `draw.bin`), `eval.nnue`,
`tablebases/` and `experience/` (PGN games, learning index, player stats).
`init`, the download and experience commands and `maxima fics` all use it; the
engine runs in that directory, so its relative `book.bin` and `eval.nnue`
resolve there. MaxDrone sets `SyzygyPath` to `tablebases/syzygy` there unless
configured otherwise. The `.env` token file stays in the checkout.

```bash
./maxima build                 # configure and compile Release (with LTO)
./maxima build --pgo           # Release trained with PGO: fastest, for playing (~4 minutes)
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
FICS play is being migrated onto this async runner (see 10.2); game/clock
orchestration, automation, and PGN capture follow the transport. ICC, legacy
database migration, remote control, and notifications are deferred. Learning
and online book selection stay in Python; native Polyglot probing stays in Maxima.

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
engine operation, development, chess data, and online play (`fics`). Existing
colon-style commands and the
no-argument native-engine launch are preserved.

Help must work without an engine build, credentials, network access, or optional
runtime dependencies. Invalid commands or arguments return nonzero with useful
guidance. Output must remain readable when redirected; any styling is limited to
terminals and respects `NO_COLOR`: the crown is gold, separators cyan, and titles
bold. Native UCI streams remain free of banners and styling. Detailed examples
for every data command remain a planned documentation improvement.

### 10.2 FICS client (MaxDrone)

`maxima fics` connects to freechess.org:5000, logs in, keeps the session
alive, seeks, plays its games with the Release Maxima build, and saves them as
PGN. `--live-feed` and `--pgn-post` (or `live_feed` and `pgn_post` in the config)
attach the connectors of chapter 11.

```bash
./maxima fics                                   # guest login, plays with Maxima
FICS_PASSWORD=... ./maxima fics --handle Maxima # registered account
./maxima fics --debug fics.log                  # wire transcript, password masked
./maxima fics --engine /path/to/engine --option Hash=256
./maxima fics --no-engine                       # connect only
./maxima fics --config fics.json --games 20     # settings file; flags override it
```

Play policy, as JDrone's `ics.seeks`/`autoaccept`/`autorematch`, from a JSON file
(`--config`) or flags:

With no `--config`, the client loads the first `.fics.json` found in the current
directory, `MAXIMA_DATA_DIR` (the data directory), then the project directory.
Only one file is loaded; an explicit `--config FILE` overrides discovery, and
command-line flags override file settings. Without a file, built-in defaults
apply. A malformed or unreadable selected file is an error, not a fallback.
The selected path is printed at startup. To use the mixed guest setup with
plain `./maxima fics`, copy `docs/fics-mixed.json` to `.fics.json`; the local file
is Git-ignored. It sets `handle` to `guest`; all guest seeks are forced unrated,
including any pool entries marked rated. Guest accounts have no finger notes.

```json
{"seeks": ["3 0", "3 1", "5 2"], "seeks_at_once": 2, "accept": true, "rematches": 0,
 "games": 20, "handle": "guest", "engine": null, "options": {"Hash": "256"},
 "owner": ["hof", "hajewiet"], "notes": ["Unrated test games welcome"]}
```

After login and after each game the client posts `seeks_at_once` random seeks
from the pool (`--seek`, `--seeks-at-once`, `--no-seek`); guests always use `u`.
The console prints each selected command, such as `-- posting seek 5 0 u`,
before sending it, so server index confirmations can be read alongside the choices.
After `reseek_minutes` (default 10, 0 off) without a game it draws again, so
the other time controls get their turn and new players see the seeks announced.

Standard chess, royal losers and supported wild variants share one `seeks` pool:

```json
{
  "seeks": ["3 0", "3 0 losers", "5 1 losers", "3 0 w2", "3 0 w3", "3 0 w4", "3 0 w5", "3 0 w8"],
  "seeks_at_once": 3,
  "accept": true,
  "reseek_minutes": 2
}
```

`seeks_at_once` controls the total random sample: use 1, 2 or 3 advertisements,
or 0 for none. A new sample is posted after each game and idle reseek interval;
opponents choose which advertisement to answer. `losers` entries automatically
enable royal-losers challenges and engine capability validation. Use `"losers": true`
or `--losers` to accept them without advertising them. Repeat `--seek "3 0 losers"`
or `--seek "3 0 w3"` on the CLI. Guests always advertise unrated games.
The standard/losers example is checked in as `docs/fics-mixed.json`.

Seek entries may end in a local percentage: `"5 0 50%"`. Entries without a
percentage divide the remaining weight equally. For example,
`"seeks": ["5 0 50%", "3 0 w2", "3 0 w3"]` gives weights 50%, 25%, 25%.
`"5 0 100%"` gives every unspecified entry zero weight. Negative percentages,
totals over 100%, duplicate specifications, and fully specified totals other
than 100% are rejected before connecting or replacing an operator pool. Decimals
are supported. Sampling draws distinct positive-weight entries up to
`seeks_at_once`; percentages never reach FICS. Without percentages, selection
remains uniform. The same syntax works in `--seek` and operator `seek-pool`.
The local `.fics.json` uses 50% for standard 5+0, divides the remaining 50%
equally among its five wild and four losers entries, posts up to three seeks,
and reseeks after five minutes without a game (`reseek_minutes: 5`).

Operators can refresh immediately with `tell <bot> reseek` (a configured owner)
or `tell <bot> <key> reseek`. This withdraws the current ads and draws the pool
again. During a game it reports that seeking resumes after the game, without
interrupting play. Typing `reseek` in the local console also refreshes idle ads.
Older `losers_seeks`, `losers_seeks_at_once`, and their CLI flags remain accepted
for compatibility; a single mixed `seeks` pool is sufficient. `--no-seek`
suppresses all advertisements while retaining challenge support.

On login, enabled losers support sends `set f3 losers`, required for computer
accounts by [FICS losers help](https://www.freechess.org/Help/HelpFiles/losers_chess.html).
Existing account formulas remain in effect: inspect `variables` and remove any
`!losers` restriction if needed. Check `limits` to confirm the account/server
allows the desired number of concurrent ads; `sought all` shows posted seeks.
These commands and the server-dependent ad limit are described in
[FICS seek help](https://www.freechess.org/Help/HelpFiles/seek.html).
The runner uses `Creating:` metadata to select the rules because style12 has no
variant field. In mixed mode, an unidentified resumed board is ignored and
logged rather than searched with guessed rules.

W17 supports FICS **royal losers**: kings remain royal, captures are compulsory,
and king-only, checkmate and stalemate win. It retains material-only evaluation;
protocol/correctness checks do not establish current competitive strength.
Lichess offers [antichess](https://lichess.org/variant/antichess), where kings can
be captured, castling is absent, and promotion to a king is allowed. W17 cannot
play that ruleset. The Lichess client continues to accept standard games only;
enabling antichess requires its own engine rules/search support and a third,
separate experience store.
Challenges are accepted for standard lightning, blitz, or standard chess, plus
royal losers when enabled, with a clock and only when idle (`--no-accept`
declines all); games created with unsupported rules are aborted.
Rematches are off by default; `rematches` (or
`--rematches`) offers `rematch` up to that many games in a row per opponent. At `Creating:` it
fingers the opponent; the first search waits up to two seconds for the reply so
`UCI_Opponent` carries the rating, title, and computer flag. `--games N` quits
after N games; `quit` during a game stops seeking and leaves after the game,
and a second `quit` leaves at once. Unknown settings or wrong types are errors.
An opponent who makes no move for 20% of the starting clock, with a minimum of
`first_move_wait` (default 60 s), triggers one abort request on that turn.
This applies before their first move, and through opponent move 4 when our last
move came from book or scored at or below `DrawContempt` from our side. Unknown
scores outside book do not qualify. The timer measures the current turn, survives
clock-only updates, and is canceled on a reply, takeback, or game end. FICS aborts
without agreement before both players have moved; later requests require the
opponent's agreement (or server permission once they run out of time). Games
aborted before both sides move are not saved.
The log shows each searched move with depth, score, time, and the expected line
(`pv`). `whisper` (or `--whisper`) explains every move to observers (never to the
opponent, unlike kibitz) in plain words:

    1...e5 fire book | 35% pick chance | learned: masters +23 =1 -1
    2. f4 revenge vs John | 40% pick chance | learned: vs John +0 =0 -1
    d19 +2.07 36.6s 7...Bd6 8. c4 O-O 9. cxd5 Bxd5 10. Bxd5 (out of book)
    learned: 24 moves | vs John +2 =0 -1

Searched moves show depth, score (`#5` for mate), `TB` when tablebases were hit, the
time, the speed (`850Knps`, `1.2Mnps`), and the first six plies of the line in numbered SAN, starting with the move played. Book moves name the layer
with the largest share (ties to the more specific layer); `base book` is book.bin.
Both servers show the move's overall `pick chance`, including all active book
and learned layers. This is a selection probability, not a win rate. Observer
whispers and `!book` also name supporting layers contributing at least 5% of the
move's weight, without a second set of percentages.
`learned: all games +23 =1 -1; masters +10 =0 -1; vs John +2 =0 -1` gives actual
unweighted wins, draws and losses for this position and move, from the mover's
side. Each game counts once even if the position repeats; unfinished games add
no result. The group and opponent are overlapping subsets of all games, not
additional games. Opponent history is specific to the current server, and
variants stay separate. Absent results say `learned: no results yet`. These
counts describe evidence; move selection still uses its existing evaluation,
result, game-type weights and sampling rules.

FICS mutes guests, so their whispers fail; MaxDrone notices "You are muted." and stops
whispering. Owners configured in `owner` can steer it by tell without a key.
Alternatively, `MAXDRONE_KEY` authorizes anyone who supplies that key (never in a
file it writes or a log: the key is masked in the terminal and wire logs):
`tell <name> <key> follow` sends the same explanations as tells (`unfollow` stops),
`softquit` stops after the game, `softrestart` stops after the game with exit status 75
so a supervisor loop (`while ...; [ $? -eq 75 ]`) starts it again with the latest code,
`help` lists the commands, and `!abort`, `!resign`,
`!draw` act on the current game. After each saved game, followers also get what
Maxima learned: the opening moves it kept, the score against that opponent, and on a
loss the revenge line it filed. FICS queues guest tells sent too fast, so MaxDrone paces
them itself (one tell every two seconds, joining what waits with ` | `) and drops a
follower who is no longer logged in. Only this fixed list is accepted; a wrong key gets no
answer.

FICS operators listed in `owner` (case-insensitive server-authenticated handles),
or anyone supplying `MAXDRONE_KEY`, can replace the current seek pool:
`tell Maxima seek-pool 3 0, 5 1, 3 0 losers, 5 1 losers, 4 0 losers, 1 0 losers`.
With a key use `tell Maxima <key> seek-pool ...`. The complete pool is validated
before replacement. Idle clients withdraw their advertisements and sample the
new pool immediately; during a game it applies after the game. Random sampling
uses the larger configured concurrent seek count (at least one). The replacement
survives games and connection retries in this process, but is not written to the
configuration file. `owner` handles can also use the existing remote commands.

Supported orthodox-rule wild challenges are `wild/2`, `wild/3`, `wild/4`,
`wild/5`, and `wild/8`; operator pool entries use `w2`, `w3`, `w4`,
`w5`, `w8`. These entries also work directly in JSON `seeks` and
repeated `--seek` flags; `seeks_at_once` samples that combined standard/wild pool.
For example, `"seeks": ["3 0 w2", "3 0 w3", "3 0 w4", "3 0 w5", "3 0 w8"]`
with `"seeks_at_once": 2` posts two random supported wild advertisements. The initial style12 position must pass legal-board validation.
`wild/8a` is unsupported: observed FICS starts place kings on d1/d8 with
castling rights, requiring castling the engine does not implement. Challenges
are declined, seek entries are rejected, and created games are aborted.
On 2026-10-26, direct FICS guest examinations confirmed that `wild/8` starts
with kings on e1/e8 and pawns on ranks 4/5. Both colours castled on both wings
with ordinary king/rook destinations. The same server placed `wild/8a` kings
on d1/d8, contradicting the help text describing normal piece placement.
Historical wild/8a PGN labels remain readable; this does not imply playing support.
Standard chess and supported numbered wild variants use one orthodox learning
store (`experience`) and the same books, on both FICS and Lichess. Position keys
retain side to move, castling rights and effective en-passant rights; moves are
checked for legality before use. PGNs retain their variant and starting FEN;
FICS also keeps the per-variant live PGN directories. A shared rebuild imports
older `experience/wild/<style>/games` archives without deleting them, preserving
identities and avoiding duplicate learning. Rebuild/sync accept numbered-wild
`--variant` names as aliases for the shared orthodox store. Royal losers keeps
its separate `experience/losers` store. Chess960 remains unsupported and declined
by both online clients because its castling rules and encoding need explicit
support. Wild 0/1 and Fischer Random require different castling support
and remain declined, as do suicide, atomic, crazyhouse, and bughouse.

FICS computer accounts must keep the owner, machine, and engine in their finger
notes (`help computers`), and may only play the engine, time controls, and
variants the computer committee approved. On a registered login the client
writes note 1 (`<engine name and version> on MaxDrone`), note 2 (`owner`, a handle
or a list), note 3 (CPU model with the engine's `Threads` and `Hash`, and the
OS), then up to seven `notes`. Guest accounts may test engines freely and get no
notes.

The multi-layer book (9.2) chooses opening moves; `layer_profiles` in the
config overrides its percentages per profile, e.g. `{"layer_profiles": {"weaker":
{"fire": 35, "book": 10}}}` (layers `book`, `fire`, `draw`, `experiment`,
`global`, `group`, `opponent`, `search`; profiles `equal`, `weaker`, `stronger`,
`unrated`).

Draw contempt follows the rating gap: at `Creating:` MaxDrone sets the engine's
`DrawContempt` (cp, our view) to the default -10 plus a quarter of the opponent's
rating minus ours, clamped to `contempt_window` (default ±100 cp = ±1 pawn; 0
disables). Equal ratings keep -10; 200 points stronger gives +40 (a draw is
welcome); 400 or more weaker gives -100 (avoid draws). Without both ratings
(guests, unrated) the engine default is sent, as python-chess would otherwise
keep the previous game's value.

Every game with at least one move is atomically saved to a pending
`experience/games/<LearningId>.pgn`, then completed games are moved by
`experience:archive` into `<LearningSource>/YYYY-MM.pgn` (`pgn_dir` or `--pgn-dir` overrides the extra
PGN destination; `--no-pgn` disables saving and learning). See 9.2 for shared
publication, imports and rebuilding. Tags: `Event` (`FICS unrated blitz game`),
`Site`, `Date` (local) and `UTCDate`/`UTCTime` at game start, `White`/`Black`, `WhiteElo`/
`BlackElo` when rated, `TimeControl` (`180+0`), `Termination` (`normal`,
`time forfeit`, `abandoned`, `adjudication`), our side's `Type "program"` and
`Engine` (full engine id), and the opponent's `Title` and `Type` (`program` for a
(C) account, `human` otherwise) when finger answered, and `WhiteAccount`/
`BlackAccount "unregistered"` for guests and other `(U)` logins. Every move has `[%clk]`
(mover's clock after the move) and `[%emt]` (time FICS charged); our searched
moves add `[%eval score,depth]`, `[%pv ...]` (SAN, up to 12 plies), and
`[%nodes]`, `[%nps]` and `[%searchtime]` (actual engine search seconds);
moves from the multi-layer book carry its label and layer shares
(9.2); the server's reason ends the game.
Shared statistical learning reads the evaluation annotations. A takeback rewinds the move list;
a board that does not follow (missed update) restarts it with a `FEN` tag. A
game cut off by a lost connection has no result line and is not saved. Unregistered
names (guests, and `(U)` logins such as `zahrani`) are not stable identities, so
per-opponent learning should skip `Account "unregistered"` opponents; their games
are kept as a record.

The engine runs in the project root, so its relative `eval.nnue` and `book.bin`
defaults resolve. Login sets `iset ms 1`, so style12 clocks are milliseconds.
Each style12 board of our own game (relation 1 or -1) is checked and applied to a
python-chess board holding the whole game, so the engine receives
`position startpos moves …` and can see repetitions. A board that does not
follow from the previous one (takeback, missed update, resumed game) restarts
the history from the server position; invalid boards are ignored. On our move
the engine searches with both clocks and the increment; a new game number gives
a new python-chess game token, which sends `ucinewgame`. Moves go out in
coordinate form, promotions as `e7e8=Q`. Searches run as tasks, so server
lines, pings, and commands keep flowing; a new board or a game end cancels a
stale search. A search that misses our clock plus five seconds, or an engine
that fails, is restarted and the board requested again with `refresh`; an
`Illegal move` reply also triggers one `refresh`. The terminal shows each move
with its number and the mover's clock: the opponent's moves, and ours with
depth, score, time and PV, or the book layers behind a book move. Style 12
board lines are left out of the terminal; `--debug` transcripts keep them.

Lines typed at the terminal go to the server verbatim, except the console's own
`help`, `quit`, `softquit`, `softrestart` and `reseek`; `help <topic>` still reaches the
server. When the bot plays, `quit` stops after the game and a second `quit`
leaves at once; without a player it logs off.
Handles are validated before connecting (3–17 letters, or `guest`); the
password is read only from `FICS_PASSWORD`. Login applies style 12, seek/shout
silence, autoflag, noescape, width 240, and nowrap.

Outgoing lines use timeseal v1 so the server charges thinking time, not network
lag. FICS decodes timeseal input only after the client's `TIMESTAMP|…|` hello;
without it, the encoded login name is rejected. Each line carries milliseconds
since connecting; the server's `[G]` pings (sent during games) are answered with
the encoded `\x02` `9` reply. `--no-timeseal` sends plain lines.

Every wait has a deadline. After five minutes without output from the server,
or without input from us (`--probe-interval`), the client sends `date`; no reply
within a minute counts as a dead connection. Probing on our silence also keeps
FICS from logging us off after an hour without input. Lost connections reconnect after
5, 10, 30, then every 60 seconds. Wrong passwords, unregistered names, and
rejected handles stop with an error instead of retrying. JDrone could hang on
waits without deadlines (engine readiness, a half-open socket); the client
avoids both.

FICS and Lichess engine moves enable UCI pondering automatically. A legal predicted
reply starts `go ponder` during the opponent turn; matching replies use
`ponderhit`, otherwise the old search is stopped and a new timed search begins.
Book/tablebase choices and game ends stop background pondering. Game tokens,
position history and option changes prevent reuse across incompatible searches.
The ordinary allocation and casual thinking ceiling apply after the actual reply.
A completed ponder search retains its evaluation and PV for an instant hit;
missed predictions discard those reports. Resignation still requires a score
and depth from the same report.

Both online clients share conservative automatic resignation (`resignation.py`),
in rated and unrated games. After move 20, the last three completed own-turn
searches must score at most −800 cp at depth 16 or greater. Four qualifying
searches must decline by at least 100 cp overall, with no consecutive improvement
greater than 50 cp. The last three actual opponent replies must match the replies
in our preceding PVs. Book choices, missing or stale scores and provisional UCI
bounds break evaluation evidence. Score and depth must come from the same UCI
report. Known tablebase draws or wins prevent resignation.

Eligible opponents are confirmed bots, opponents with unavailable ratings, and
humans rated at least our rating minus 200. Unknown bot status receives human
clock protection. The opponent must retain the speed reserve (15 seconds for
bullet/lightning, 30 for blitz, 60 for other/unknown speeds), plus
`20 × max(0, median_reply_seconds − increment_seconds)`, using the last three
measured opponent reply times. Missing clocks or timings prevent resignation.
Against humans, we also continue when our clock is at least twice theirs and
theirs is below twice the required reserve. Lichess derives reply times from
the clock at the start of their turn; batched moves or changed increments cannot
supply a timing sample. FICS uses the server's elapsed move time.

Only explicitly unrated/casual games gain a thinking ceiling: two qualifying
evaluations at most −300 cp enable a 30-second ceiling on subsequent searches.
Ordinary engine clock allocation still applies; the runner sends UCI `stop` at
the ceiling and collects `bestmove`. Rated games retain ordinary allocation,
and missing/provisional ratings do not classify a game as unrated. Takebacks,
resets, reconnects and new games clear policy evidence. The clients check the
current game and position before one resignation request and await the normal
server game-end event for PGN saving. Resignation logs include scores, prediction
matches, ratings and the complete reserve calculation.

Both JSON configuration files accept the same optional object (shown with defaults):

```json
{"resignation": {"enabled": true, "loss_cp": -800, "confirmations": 3,
                 "min_depth": 16, "rating_margin": 200, "unrated_ceiling": 30}}
```

Unknown settings, wrong types and invalid ranges fail before connection.
`enabled` controls resignation; the casual thinking ceiling remains independent.
Loss thresholds are negative integer cp; confirmation count and depth are positive
integers, the rating margin is nonnegative, and the ceiling is positive finite
seconds. Shared online learning and PGN ingestion are described in 9.2.

Both clients handle `SIGTERM` and `SIGINT` for Kubernetes/container shutdown.
The first signal stops seeking, accepting and creating challenges immediately;
idle or startup/reconnect sessions close without waiting for a game. An active
game can finish during `shutdown_grace` seconds (default 15). A confirmed finish
uses the ordinary PGN and shared-learning path. At the deadline, or on a second
signal, the client cancels its searches, saves the confirmed moves as an unfinished
PGN, and closes the engine/connection. Engine cleanup reaps the subprocess,
killing it if its bounded UCI quit fails. Shutdown exits with status 0; genuine
startup/runtime failures retain their error status. Existing console softquit and
Lichess SIGUSR1 behavior remain available.

`--shutdown-grace SECONDS` overrides JSON `"shutdown_grace": 15`; zero means no
game-drain wait. Nonfinite, negative, boolean and null settings fail validation
before connecting. A longer drain needs a correspondingly longer Kubernetes
`terminationGracePeriodSeconds`: allow at least `shutdown_grace + 15` seconds
for cleanup and pending HTTP calls (default 30). Use an exec-form container
entrypoint, e.g. `exec maxima fics ...` or `exec maxima lichess ...`, so signals
reach the Python client. Kubernetes can still forcibly terminate after its own
deadline; finishing a long game is not guaranteed by the default drain period.
A `preStop` hook is not required for this signal handling.

Interrupted PGNs go to `games/interrupted/` (or beneath a custom `--pgn-dir`),
with `Result "*"`, `Termination "unterminated"`, and `LearningIncomplete "true"`.
Their IDs are separate from a later completed/resumed game's identity;
`LearningParentId` retains the complete game's base identity. These diagnostic
snapshots never count as wins/losses or teach the online book, including during
PGN-only rebuilds. They preserve only server-confirmed moves and existing search
annotations; no move or result is invented. `--no-pgn` still disables saving.
A filesystem failure is logged and does not prevent engine/connection cleanup.

Deploy one active client per server account, including during upgrades. Health
checks and account-lock automation are outside this shutdown increment.

Searched-move descriptions on both servers start with `pondered` only when
the protocol actually sent `ponderhit` for that play request. Per-search
`_ponder_hit` metadata survives immediate completion without fresh info and the
next background ponder, and resets for every request and engine restart.
Prediction misses, game changes and ordinary searches keep their usual wording.

### 10.3 Lichess client (MaximaEngine)

`maxima lichess` plays on Lichess through the Bot API as MaximaEngine: it
accepts challenges, plays them with the Release Maxima build, saves them as
PGN and learns from them. The move choice is the same as on FICS
(`runner/maxima_runner/move_choice.py`): the multi-layer book and experience
(9.2), then the engine while the online tablebase is asked (11.4), with draw
contempt from the rating gap.

```bash
./maxima lichess                                # bullet, blitz and rapid challenges
./maxima lichess --speed blitz --games 20 --live-feed 8765
./maxima lichess --config lichess.json          # settings file; flags override it
```

Setup, once: create an account that has never played a game, create a token for
it with only the scope "Play games with the bot API" (`bot:play`), and upgrade
it (irreversible):

```bash
curl -d '' -H "Authorization: Bearer $LICHESS_BOT_TOKEN" https://lichess.org/api/bot/account/upgrade
```

The client reads `LICHESS_BOT_TOKEN` and `LICHESS_OWNERS` (Lichess users who may
send commands, comma-separated) from the environment or from `.env` in the
checkout (git-ignored; the environment wins). It refuses to start unless the
token belongs to a BOT account.

Bots cannot seek, so MaximaEngine plays challenges, its own and others'.
After `challenge_idle` minutes without a game (default 30, `--challenge-idle`;
0 or `--no-challenge` turns it off) it challenges one online bot to a rated
game: a speed by share (bullet 30, blitz 50, rapid 20, among the configured
speeds), a random clock of that speed (1+0, 1+1, 2+1; 3+0, 3+2, 5+0, 5+3;
10+0, 10+5, 15+10), and a bot within `challenge_window` rating points (default
300) at that speed, never the last opponent. Lichess is asked only then, at most
once a minute; an unanswered challenge is cancelled after a minute, and a bot
that declined or did not answer is left alone for that speed for a day. Owners
switch it with `!challenge` and `!nochallenge` in a game's chat. Five seconds
after a game against a bot it offers that bot one rematch with the same clock and rating mode,
never a second in a row, and not while challenges are switched off.

It accepts standard
chess with a clock at the configured speeds (`speeds`, `--speed`; default
bullet, blitz and rapid; Lichess offers bots no UltraBullet), rated or casual,
one game at a time, and declines the rest with Lichess's own reasons (`later`
while busy or stopping, `tooFast`, `tooSlow`, `timeControl`, `standard` for
variants). Takeback requests are declined; draw offers are accepted only in dead
draws or when a draw is worth at least our score (below). When the opponent leaves and Lichess allows it, the win is claimed.

The player room opens with "Hi, I'm Maxima running on MaxDrone. Good luck!",
the spectator room with "Hi! I'm Maxima, running on MaxDrone. Type !help for
available commands." A known opponent's record follows on the same chat line,
for example "vs Quirky2000: +1 =2 -3." from Maxima's side; it is omitted for a
first game or without learning. There is no second greeting line. Lichess silently
drops repeated messages, so the rooms use different wording. At the end the player room gets a thank-you for the game and the
spectator room "Thanks for watching!". Chat lines carry no links, which Lichess
blanks. MaximaEngine posts nothing else unasked. Anyone may type `!help`, `!name` (engine,
authors; no links, which Lichess blanks in chat), `!eval` (our last move with
depth, score and expected line), `!stats` (our record against the opponent and
on Lichess) or `!book` (our last book move, e.g. "5...Nc6 base book | 55% pick
chance | learned: masters +23 =1 -1", or where we left the book). Book answers
put the move and chance first, with supporting layers and learned results on
separate chat lines. The last three commands are answered in the spectator room only, never to the opponent. Answers to
non-owners come at most every 5 seconds per game, so chat spam cannot push the
bot into Lichess's rate limit, which would also hold up its moves.

Owners steer the bot from the game chat; the spectator room keeps it out of the
opponent's sight. Lichess authenticates chat usernames, so no key is needed:
`!softquit` (stop after this game), `!softrestart` (exit status 75 after this
game, for a supervisor loop as on FICS), `!resign`, `!abort`, `!draw` (offer or
accept). These commands from anyone else are logged and ignored. The same
commands work on the console without the `!`, and each one answers there; `help`
lists them. `quit` (or `kill -USR1 <pid>`) stops after the game; a second `quit`
leaves at once. Every `maxima` command shows the Maxima header in a terminal.
The console logs each move with its number and the mover's clock: the
opponent's moves, and the bot's with eval, depth, time and PV, or the book
source and its percentage for a book move.
Opening stalls use the same move 1–4 and book/score rule as FICS, waiting at least
60 seconds or 20% of the initial clock. One abort request is sent per eligible
turn; the Lichess server decides whether the game can still be aborted. A refused
request leaves play running normally.

Lichess shares the PGN archive and global/group learning with FICS (9.2);
individual opponents and rating statistics remain server-specific. The learner maps Lichess speeds to its game types (bullet to lightning,
rapid and classical to standard); the PGN keeps the real speed in `Event`
(`Lichess rated blitz game`) and links the game in `Site`. Settings files take
`engine`, `options`, `speeds`, `accept`, `games`, `pgn_dir`, `learning`,
`variety`, `contempt_window`, `layer_profiles`, `style_books`, `mood`,
`live_feed`, `pgn_post`, `online_tablebase`, `resignation` and `shutdown_grace`, as on FICS.

Streams are read line by line; Lichess sends a keepalive every 7 seconds, so 30
seconds of silence counts as a dead connection and the stream reconnects (the
event stream with backoff up to a minute; a game stream gives up after eight
failures and records the game as unfinished). After HTTP 429 every request
waits a full minute, as Lichess asks. Only one event stream per token can be
open, so run one client per bot account. A game's end arrives on both streams;
it is saved, reported and posted once.

Draw offers, on both servers, are judged on our next move with the fresh choice
for the current position, so an offer made right after a blunder is not
accepted on an old evaluation. The draw is accepted when no side has mating
material, when the online tablebase says draw for us (including cursed win and
blessed loss, which the fifty-move rule makes draws), when the search reached
the tablebases and scores exactly 0.00 with seven pieces or fewer, or when our
score is no better than the engine's own draw value, the game's `DrawContempt`
(10.2; -10 cp without ratings). So against a stronger opponent, where a draw is
worth a little more, a repetition the engine sees is accepted; while we are
better than a draw, never (`move_choice.accept_draw`). Otherwise the move
declines it. The move is sent
anyway, so an offer withdrawn meanwhile does not leave our clock running. FICS
offers are read from `<name> offers you a draw.`, Lichess offers from the game
state.

## 11. Connectors

A game source plays games somewhere: FICS (10.2) and Lichess (10.3). It reports
what happens as neutral game events; connectors consume them without knowing the
source. Both connectors below are off by default and use only the Python
standard library (`runner/maxima_runner/connectors.py`).

### 11.1 Game events

Every event is one JSON object with `"v": 1` (bumped only for incompatible
changes; new fields may appear at any time, so ignore unknown ones), `"type"`
and `"source"` (`"fics"` or `"lichess"`). `game` is the FICS game number or the
Lichess game id (a string such as `"pG3WSP96"`); colors are
`"white"`/`"black"`; clocks are milliseconds; FENs are full six-field FENs.

| type | when | fields |
|---|---|---|
| `game_start` | our game begins | `game`, `white`, `black`, `white_elo`, `black_elo` (null when unknown), `initial_seconds`, `increment_seconds`, `rated`, `variant` (FICS type or Lichess speed, e.g. `blitz`), `our_color`, `fen`, `clock_ms` |
| `move` | after each move, ours or theirs | `game`, `ply` (after the move), `san`, `uci`, `fen` (after), `by` (`"us"`/`"them"`), `clock_ms`, `search` |
| `position` | takeback or resync | `game`, `ply`, `fen`, `reason` (`"takeback"`/`"reset"`), `clock_ms`; drop moves after `ply` |
| `game_end` | game over | `game`, `result` (`1-0`, `0-1`, `1/2-1/2`, `*`), `reason`, `headers`, `pgn` (null when aborted before both sides moved) |

`search` is null for the opponent's moves. For our book moves it is
`{"book": "<reason>"}`, for online tablebase moves `{"tablebase": {...}}` (11.4); for searched moves it is `{"depth", "seldepth", "nodes",
"nps", "time_ms", "score", "pv_san"}` with `score` either `{"cp": n}` or
`{"mate": n}` from White's point of view, and `pv_san` the expected line in SAN
starting with the move played.

```json
{"v": 1, "type": "move", "source": "fics", "game": 17, "ply": 1, "san": "e4", "uci": "e2e4",
 "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "by": "us",
 "clock_ms": {"white": 179500, "black": 180000},
 "search": {"depth": 18, "seldepth": 24, "nodes": 2100000, "nps": 850000, "time_ms": 2470,
            "score": {"cp": 25}, "pv_san": ["e4", "e5", "Nf3"]}}
```

### 11.2 WebSocket live feed

```bash
./maxima fics --live-feed 8765              # ws://127.0.0.1:8765/
./maxima fics --live-feed 0.0.0.0:8765      # reachable from other machines
```

Any path is accepted. Each event is one WebSocket text message. On connect a
viewer first gets `{"v": 1, "type": "hello", "source", "server", "handle",
"engine", "game"}`, where `game` is null between games or
`{"start": <game_start>, "moves": [<move>, ...]}` for the game in progress, so a
late viewer can draw the board at once. Viewers do not send anything; pings are
answered. A viewer that falls 256 messages behind is disconnected. There is no
authentication: keep the default `127.0.0.1` and publish it through a reverse
proxy when it must be public.

A minimal browser viewer:

```html
<pre id="log"></pre>
<script>
const feed = new WebSocket("ws://127.0.0.1:8765/");
feed.onmessage = (message) => {
  const event = JSON.parse(message.data);
  const text = event.type === "move"
    ? `${event.ply}. ${event.san} (${event.by}) ${event.search?.score ? JSON.stringify(event.search.score) : ""}`
    : `${event.type} ${event.game ?? ""}`;
  document.getElementById("log").textContent += text + "\n";
};
</script>
```

The FEN in every `move` and `game_start` is enough for a board library such as
chessboard.js; `clock_ms` drives the clocks.

### 11.3 PGN post

```bash
MAXDRONE_POST_TOKEN=... ./maxima fics --pgn-post https://example.org/games
```

After each finished game with moves, MaxDrone sends `POST <url>` with
`Content-Type: application/json` and, when `MAXDRONE_POST_TOKEN` is set,
`Authorization: Bearer <token>` (the token comes only from the environment and
is never logged). The body is the `game_end` event without its type:

```json
{"v": 1, "source": "fics", "game": 17, "result": "1-0", "reason": "guestNinja resigns",
 "headers": {"Event": "FICS unrated blitz game", "White": "MaxDrone", "...": "..."},
 "pgn": "[Event \"FICS unrated blitz game\"]\n...\n1. e4 {[%clk 0:02:59.5]} ... 1-0"}
```

Any 2xx status counts as delivered. The request times out after 10 seconds;
failures are logged and not retried, since the game is already saved locally
(10.2). A receiving server can be this small:

```python
from http.server import BaseHTTPRequestHandler, HTTPServer
import json

class games(BaseHTTPRequestHandler):
    def do_POST(self):
        game = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        print(game["game"], game["result"], len(game["pgn"]))  # store it here
        self.send_response(204)
        self.end_headers()

HTTPServer(("0.0.0.0", 9000), games).serve_forever()
```

### 11.4 Online tablebase

With seven or fewer pieces and no castling rights, and more pieces than the
engine's local Syzygy set covers (the largest complete set in `SyzygyPath`, e.g.
5 after `maxima init`; there the engine answers from its own tables), MaxDrone asks
`https://tablebase.lichess.ovh/standard?fen=<FEN>` while the engine already
searches, and plays the first move of the answer (the 7-man Syzygy tables,
DTZ-optimal so the fifty-move rule is respected) when it arrives before the
engine's move; the search is then stopped. A lookup takes about 0.75 s and gives
up after 1 s, so it never costs clock time: in a hurry the engine simply moves
first. Unknown positions leave the move to the engine; an error (offline, rate
limited, timeout) also pauses lookups for a minute. The move is logged, whispered and sent to the live feed as
`"search": {"tablebase": {"category", "dtz", "dtm"}}` (category `win`, `draw`,
`loss`, `cursed-win`, `blessed-loss`, ...), and the PGN comment reads e.g.
`tablebase: win DTM 27`. It is on by default; `--no-online-tablebase` (or
`"online_tablebase": false`) leaves endgames to the engine and its local tables
(7). The module is `runner/maxima_runner/online_tablebase.py`; it needs no
account or token.
