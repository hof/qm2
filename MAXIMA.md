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
9. [Opening books and persistent experience](#9-opening-books-and-persistent-experience)
10. [Python runner and project CLI](#10-python-runner-and-project-cli)

Keep chapter numbers stable; append new chapters rather than renumbering existing
ones. Use descriptive headings and keep each chapter understandable independently.
Until a dedicated documentation reader exists, list chapters with
`rg '^## ' MAXIMA.md` and locate topics with `rg -n '<term>' MAXIMA.md`.

## 1. Engine and goals

Maxima2 is a C++26 UCI chess engine with bitboard move generation, classical
search and evaluation, and experimental losers-chess support.

### Goals

- Improve playing strength, correctness, performance, and reliability.
- Make clean, understandable code a first-class project goal. Preserve clear
  control flow, explicit state ownership, and coherent interfaces as the engine
  gains capabilities.
- Preserve the engine's focus on speed: efficient chess algorithms, high search
  throughput, and compilation tuned for the target CPU.
- Explore a learned evaluator, such as NNUE, with correctness tests and measured
  playing-strength gains guiding integration into search.
- Later, investigate play that exploits weaknesses in other engines' selective
  search, including null-move assumptions in zugzwang and forced-move positions.
  Use targeted positions and paired games against multiple engine opponents to
  establish gains. This is deferred research after the basic NNUE/search work.
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

- `src/`: engine, UCI handling, board state, move generation, search, evaluation,
  hash tables, time management, and Polyglot opening-book support.
- `src/w17/`: losers-chess search and evaluation.
- `tests/`: tests for bit operations, move generation, search, static exchange
  evaluation, Polyglot keys, hash tables, evaluation, board flipping, and mates.
- `gen/`: KPK bitbase generator.

CMake generates `generated/maxima/version.h` inside each build directory from
`src/version.h.in`. Do not edit generated headers. The generated include directory
propagates to engine-library consumers; any obsolete source-tree `src/version.h`
is ignored and unused.

- `src/tablebase.*`: Maxima's Syzygy adapter and probing policies.
- `third_party/fathom/`: pinned, unmodified MIT-licensed table decoder.
- `runner/`: independently packaged Python CLI, UCI orchestration, suites,
  tablebase downloads, experience tooling, and their tests.
- `tests/data/syzygy/`: small offline tablebase test fixtures.
- `tablebases/syzygy/`: downloaded user tables, ignored by Git.

## 4. Search correctness and caches

Search owns its configuration and pre-root repetition history. Real game history
requires three occurrences; cycles strictly inside search use one earlier match
as a search heuristic. Repetition identity excludes en passant squares when no
legal capture is available. Irreversible moves and null moves bound history scans.
Checkmate takes precedence over a fifty-move draw. Standard quiescence detects
stalemate at every tactical depth, including before a stand-pat cutoff; absence
of captures or promotions alone does not imply stalemate. Standard interior
search also verifies legal play before static beta or null-move pruning can
run, so stalemate remains an exact draw with production pruning enabled. The
probe uses a king escape first and fresh move generation when needed, without
changing the active move picker.

Standard search now uses a simple shared pruning baseline for HCE and NNUE,
both expressed on the 100-cp pawn scale. Reverse futility is limited to depths
one through three: require `eval - 100 * depth >= beta` and return `eval` on
success. Null move requires `eval >= beta + 50`, a zero-window search and a
fixed extra reduction of three (`depth - 1 - 3`). These round margins replace
the earlier 50-cp-per-ply reverse-futility margin and zero null-move cushion.
They have no improving/history/evaluator-specific adjustments; the constants
are starting values, not claimed tuned optima. Existing legal-terminal,
non-PV, check, non-pawn-material, clock and decisive-score guards remain active.
Evaluation functions and other pruning features are unchanged. Earlier 112/40
experiments below remain historical controls, not measured results for this
new combined 100/50 policy. Confirm strength at equal nodes/time before tuning.

Forward futility now uses a conservative frontier-only baseline. At a non-PV
node with exactly one ply remaining, an ordinary quiet move may be skipped
when `eval + 300 <= alpha`, after at least one legal move has been searched
and its score is outside the decisive-loss range. Never prune captures,
promotions, checks, castling or advanced passed-pawn moves with this rule.
Checked nodes, the existing material-defined endgame regime and decisive
alpha/beta windows are excluded. It replaces `40 * depth` through depth eight
and its capture/promotion gain filter. The round three-pawn allowance is a
conservative starting value, not a tuned NNUE optimum. Static evaluation does
not provide a proven upper bound on positional improvement; this remains an
empirically tested selective heuristic.

Quiescence delta pruning uses the simple captured-piece gain plus a 200-cp
allowance: skip a nonchecking capture when `eval + captured_value + 200 <= alpha`.
Exclude checked nodes, promotions, advanced pawn captures, existing endgame
flags and decisive alpha/beta windows. Require the opponent's non-pawn material
after the capture to exceed 1300 cp, protecting low-material transitions. The
200-cp allowance and post-capture material threshold are provisional choices,
requiring validation in Maxima. Use the existing capture values
(100/325/325/500/925), not inferred NNUE piece prices. Checks,
promotions and advanced pawn moves are protected separately because their gains
can exceed a fixed positional allowance. This remains selective pruning, not a
proven NNUE bound. Historical delta controls below have SEE disabled.

Quiescence SEE pruning now skips only captures with `see(move) < 0`, after the
same tactical, endgame, post-capture material and decisive-window exclusions.
Equal exchanges remain searchable regardless of alpha. This replaces the former
`eval + 50 + see(move) <= alpha` rule; delta runs first and SEE is tested only
for captures that survive it. SEE models material exchanges on one square,
not positional compensation, so even a negative SEE is not a proof that a
sacrifice is unsound. The exchange estimator checks recapturers against current
temporary occupancy, excluding captures that expose their king and king captures
onto attacked squares. It handles changing pins, en passant and promotion gains
without modifying board or search state. Equal-value attackers are ordered by
piece type and source square; recapturing pawns promote to queens. This remains
a least-valuable-attacker material model, with either side allowed to decline;
it does not search alternative move choices or positional compensation. These
conservative exclusions require their own strength tests.

The overloaded SEE interface supports both full scores and threshold tests:
`see(move)` returns the material score, while `see(move, threshold)` returns
whether that score is at least the threshold. Both share one exchange routine
and its legality checks. The threshold path uses material bounds to stop when
the answer is determined, including possible recapture promotion gains; it
does not build or fold a complete gain list. Threshold arithmetic uses a wider
integer to handle extreme inputs safely. Qsearch, check extensions and boolean
gain tests use this path; ordering that needs the numeric score retains the
full-score call. This is an implementation optimization, not a pruning-policy
change. Certificate tests compare both interfaces at equality boundaries,
extreme integer thresholds and a broad range of ordinary thresholds.

Standard null-move pruning excludes consecutive artificial passes and runs
endgame verification with null pruning disabled throughout its subtree. The
setting and board/history state are restored before handling cancellation;
an interrupted verification cannot authorize a cutoff. Node pruning excludes
mate/tablebase score windows, and artificial null lines cannot supply reserved
mate/tablebase scores as cutoffs. Pawn-only positions retain the non-pawn-material
exclusion. These safeguards do not make selective search an exhaustive oracle.

Static beta and null-move node pruning also stop at reversible clock 99: the
next legal reversible move draws unless it checkmates, while static evaluation
or an artificial pass can incorrectly claim a winning cutoff. Earlier node
pruning restricts its compatible TT clock range through 98; parent entries
inherit that restriction. Legal captures and pawn moves remain searchable.

At the search-stack ceiling, standard PVS and quiescence resolve checkmate,
stalemate, and draws before static evaluation. Royal losers preserves king-only
and draw precedence and resolves checkmate/stalemate wins at its ceiling.
Playable positions, including checks with legal evasions, retain static
evaluation when no further child stack is available. Cancellation retains its
existing precedence; a ceiling evaluation is not a completed deeper search.

Late-move reduction uses a conservative one-ply baseline. Standard PVS searches
the first three legal moves at full depth and reduces later ordinary quiet
moves only with at least three plies remaining. Check evasions, checking moves,
captures, promotions, castling and advanced passed-pawn pushes are protected.
The root driver does not reduce moves. A reduced trial that beats alpha is
verified at full depth with the original alpha/beta window before accepting
an improvement or cutoff; cancellation restores the parent state. Reductions
at depths one/two and the former depth-scaled late-move reduction are removed.
This simple policy applies to both HCE and NNUE when `LMR=true`; it remains a
heuristic rather than a proof that reduced fail-low moves cannot improve.

Standard quiescence retains castling at entry and below entry, alongside captures
and promotions; entry also searches quiet checks, and checked nodes search all
legal evasions. Ordinary quiet checks at entry require `see(move, 0)`; at
reversible clock 99 they remain searchable to preserve fifty-move draws.
Checked nodes retain all legal evasions, including losing counterchecks.
Capture delta/SEE preserves castling; castles have zero material SEE and their
positional effect is evaluated after the move.
This is an existing bounded quiet extension, not a certified strength gain.

An exact TT return can shorten the searched PV because an entry stores a score
and move rather than a complete continuation. Search clears the stale PV before
reuse. UCI text may append TT move hints to a short searched prefix; those hints
are not a completed deeper search or a guaranteed line of the cached score.
Ponder selection uses the searched prefix and can therefore be absent.

Per-root ordering/complexity counters use signed 64-bit values to preserve their
initial SEE bias, add unsigned visited work with saturation, and compare without
subtraction. They are ordering estimates; the separate unsigned visited counter
continues to drive UCI nodes and node budgets.

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
Both centipawn and mate scores retain lower/upper-bound qualifiers. NPS uses
reported nodes per second with the reported monotonic elapsed milliseconds; a
zero-millisecond sample uses a one-millisecond denominator. Rates retain unsigned
64-bit range and saturate if that range is exceeded. Reported nodes count visited
search calls, including quiescence, using the same counter as `go nodes`;
pruned moves are excluded from nodes and NPS. Cancellation suppresses subsequent
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

Accepted changes to `KingAttackShelter` or `KingAttackPieces` stop and join the
current search, then clear the transposition table before subsequent searches.
Unchanged or rejected values preserve cached scores. Draw contempt remains part
of the TT score context; pawn and material cache entries do not depend on these
king-attack weights and remain reusable.
These are manual UCI tuning weights: 256 preserves the normal contribution,
0 disables it, and 512 doubles it. Search captures the configured values;
the engine does not adjust them automatically during play.

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

The transposition table uses 24-byte entries: a full 64-bit position signature,
a 64-bit context fingerprint, and packed move, score, depth, age, and halfmove
clock-range metadata. Move hints can survive a context mismatch, but score
cutoffs require compatible context and clock ranges. Royal-losers score context
also includes the incoming mate-search mode (automatic, White, or Black), whose
horizon rules differ. A node keeps that identity when it changes mode or reduces
depth locally; incompatible modes retain move hints but cannot reuse scores.
Material and pawn caches and the transposition table still need synchronization
before parallel search.

KPK is an embedded win/draw bitbase. Proven draws stop search with the configured
draw score; winning positions retain search because KPK has no distance-to-zero
information. Tests independently verify all legal normalized KPK states.

### Standard NNUE bootstrap

NNUE development lives on `feat/nnue-bootstrap`. The target is standalone NNUE
evaluation: no blending with HCE and no automatic classical-score fallbacks in
NNUE mode. HCE remains separately selectable and the standard chess default
until the strength gate passes; royal-losers remains material-only.
The initial target is pretrained CPU inference, without training or GPU hardware.

Candidate assessment: the current local Stockfish architecture combines HalfKA,
threat and piece-pair features and has substantial position/accumulator coupling.
Other engines' networks require their matching feature sets and inference cores;
`.nnue` does not identify one universal format. The first supported family is
legacy HalfKP, 41024 inputs, two 256-wide accumulators, 512->32->32->1 integer
inference. It has an independently pinned CFish-derived probe for exact reference
checks and a compact board adapter. This is a correctness/bootstrap choice, not
proof of the strongest evaluator or a commitment to this family indefinitely.
These weights were deployed in Stockfish 13's hybrid evaluator. Exact agreement
with its raw neural core does not validate using that core alone as a strong
standalone evaluator. Assess replacement weights with their matching standalone
inference implementation; importing Stockfish's hybrid policy is outside this
target.

Official CC0 bootstrap weights: `nn-62ef826d1a6d.nnue`, full SHA-256
`62ef826d1a6d11b9e814188025aa02a60815c037292e0ef9bbb9bf4f724e5e63`.
Run `python3 tools/download_nnue.py` to fetch and verify them into ignored
`networks/`. The downloader does not change engine defaults. Weight provenance:
https://github.com/official-stockfish/networks. The GPL reference probe revision
and source ownership are recorded in `third_party/nnue_reference/README.md`.

Evaluation contract: standard scores are relative to the side to move, in the
Maxima centipawns, clamped below the tablebase score range. The pinned SF13
core returns internal units: convert with `raw * 100 / 208`, matching its UCI
conversion (`PawnValueEg=208`). Raw/full/incremental inference is checked before
this conversion. Stockfish's separate material, tempo and rule50 scaling is not
imported; any further calibration needs separate strength evidence.
Future Maxima-native networks and offline converted exports should be trained
or calibrated to the 100-cp pawn convention in the model artifact itself. Do
not rely solely on an adapter to conceal a foreign score convention such as
SF13's 208-unit pawn scale. Keep the format and inference clean, minimal and
optimized. Internal weight/activation quantization may use powers of two for
efficient arithmetic; quantization precision is separate from chess score
units. Document the file's output calibration and quantization. Any departure
from the preferred score convention needs a demonstrated performance or
precision reason. Offline conversion of pretrained quantized weights must
check output accuracy and strength rather than assume rescaling is lossless.
The pinned upstream SF13 file is a legacy bootstrap/reference artifact; this
preference governs future model/export work, not an unvalidated rewrite of it.

Search retains checks, terminal/draw/tablebase decisions and material/pawn
pruning metadata. HCE tempo, positional terms and endgame score adjustments
must not be added to NNUE scores. Search captures an immutable model snapshot;
mutable accumulators belong to that search. Evaluator/model changes must prevent
reuse of incompatible TT scores. Invalid loads must retain the previous model.
Correctness gates include full/reference/incremental equivalence, special moves,
null moves, restoration and cancellation. Measure CPU throughput and paired
playing strength before changing the default. Parallel-cache work is separate.

The engine now supports `EvalFile` and `UseNNUE` (default false). Load the model
first, then enable NNUE:

```text
setoption name EvalFile value /absolute/path/to/nn-62ef826d1a6d.nnue
setoption name UseNNUE value true
```

Missing/unsupported files report a load failure and retain the previous model.
Enabling without a loaded model is rejected. `EvalFile value <empty>` unloads
and selects HCE; `UseNNUE value false` retains the loaded model while selecting
HCE. Changes stop/join active search and clear incompatible TT entries. Search
score context also fingerprints the model and adapter version, so separately
retained search snapshots cannot reuse another evaluator's score bounds. The
fingerprint is a cache identity, not a cryptographic integrity check; the model
downloader verifies the full pinned SHA-256. Royal-losers ignores standard NNUE.

`src/nnue.cpp` owns the isolated HalfKP loader/inference implementation. Weights
are immutable, accumulators are allocated per search/ply, and updates are lazy.
A changed king refreshes its perspective; captures, promotions, castling and
en passant update the affected non-king features. Null moves exchange output
perspectives without changing features. Unmake restores the parent accumulator.
The loader checks exact size, version and architecture hashes before decoding;
wide affine sums and score clamps keep external weights outside reserved scores.
Native AVX2 feature accumulation and dense inference have scalar fallbacks.
There is no network I/O or Python dependency in the engine or its build.

`test_nnue` runs offline using synthetic weights for load validation, score bounds,
metadata, model lifetime, evaluator/TT isolation and HCE/losers preservation.
Each evaluated standard-chess search position owns snapshots of its pawn and
material metadata. Descendant searches can replace the shared cache slots without
changing metadata belonging to a cached parent evaluation. Regression checks
force replacement after both cache misses and hits in HCE and NNUE modes.
To add real-weight independent-probe and NNUE lifecycle checks, configure either
preset with `-Dmaxima_nnue_test_network=/absolute/path/to/nn-62ef826d1a6d.nnue`.
Those checks compare both accumulators and integer scores across special moves,
seeded legal games, skipped parent evaluation, null moves, cancellation and restart.
Both accumulator arrays are also checked lane by lane against independently
refreshed reference accumulators. Storage-boundary null fixtures exercise the
last allocated ply and restoration. Synthetic constant-output weights check
that material imbalance and move clocks do not trigger classical-score blending
or fallback.
The lifecycle suite also runs with NNUE enabled and tests reload/disable/unload.
The pinned reference is compiled only for these optional tests. Configure with
`-Dmaxima_nnue_test_network=` to return to the model-independent suite.

For paired A/B games, select `--model-a EvalFile=/absolute/path/to/network.nnue`
and `--option-a UseNNUE=true`; engine B can use the same executable with HCE.
The runner applies model options before evaluator-enabling flags, after validating
both complete configurations, and records the model checksum in the match report.

Bootstrap validation on the local Intel i5-10210U: Debug and Release each
passed their 34-check suite; after the explicit SF13-to-centipawn conversion,
all three NNUE checks passed again in both builds. The reference check covers
3169 position comparisons, with bit-identical raw full/incremental/probe scores
and accumulator values. The separately compiled scalar fallback also passed the
reference and loader/contract checks. ASan/UBSan with leak detection passed all
three corrected NNUE checks, including the 31-scenario lifecycle suite (outside
the restricted sandbox so LeakSanitizer can inspect threads). The runner passed
88/88 with real-engine integration enabled.

Seven alternating Release samples of 20000 warmed evaluations measured a median
0.083828 s with the initial scalar dense path versus 0.031709 s with AVX2 dense
inference (2.64x). A separate five-repeat, three-position, 50000-node search
benchmark measured median 1.471M NPS for HCE and 0.431M for NNUE (29.3%). These
are local workload measurements, not general scaling or playing-strength results.
An eight-game, four-pair 2+0.02s correctness smoke after unit conversion scored
1W/5D/2L versus HCE, with 803 independently verified legal plies and no errors,
unfinished games or forfeits. Both smoke runs used already reserved openings and
are diagnostic only; the earlier raw-unit run is not a strength baseline. Neither
run establishes an Elo gain or justifies changing the default. Further work is
pretrained network quality, model/search score calibration and credible paired
strength measurement. HCE tuning and parallel cache refactoring are not required
first. Reproducible scripts, logs, identities and PGNs are private under
`.context/baselines/2026-10-13/nnue-bootstrap/`; reserve these fixtures from training.

Expanded standalone checks passed in Debug, Release and ASan/UBSan with leak
detection: 3426 raw/full/incremental/reference score comparisons, including
independent reference accumulator lanes and the last allocated ply. The suite
and shared model-option checks passed 19/19. Cold-process HCE/NNUE diagnostics
with the same Release binary, Hash64, no book or tablebases, and alternating
evaluator order measured the following solved-target counts:

| Suite | HCE, 200k nodes | NNUE, 200k nodes | HCE, 1s | NNUE, 1s |
| --- | ---: | ---: | ---: | ---: |
| Bratko–Kopec (22) | 15 | 15 | 17 | 18 |
| modern_2026 (22) | 20 | 20 | 22 | 20 |
| corrected WAC (300) | 278 | 263 | 288 | 277 |

All 1376 searches and 17567 PV plies passed the independent report audit,
including inputs, legal moves, evaluator settings and executable/model hashes.
Corrected WAC came from Arasan revision
`207a88358c414c15eec7933774d25ffe24fd4598`, corpus SHA-256
`9bfb7b42cf5bbc7f3d60ff767ca3be460e0ae4d9110ef9a71ddf0bd7071d4d46`.
These are tactical target diagnostics, not a playing-strength result or proof
that alternative moves lose. Light tooling ran during the timed comparisons;
this was not an isolated-machine speed benchmark. The bootstrap has no measured
Elo gain. Keep the paired-game strength gate open. Reports, provenance, scripts,
and a preliminary standalone model assessment live privately in
`.context/baselines/2026-10-13/nnue-sanity/`; reserve these fixtures from training.

A subsequent fixed 200-game, 100-pair match at 2+0.02s and Hash64 used fresh
opening endpoints excluded from all earlier reserved match corpora. Standalone
NNUE scored 37W/103D/60L versus HCE (44.25%, approximately -40 logistic Elo as a
point estimate). The conservative 95% opening-pair Hoeffding score interval was
30.67%–57.83%, so this run does not establish a precise rating or exclude equality.
All 24996 PGN plies, paired openings/colors, terminal outcomes, settings and model/
executable identities passed independent audit; no errors, unfinished games or
forfeits. This short-clock sample shows no strength gain. The inference core was
also compared directly with original Stockfish 13 revision
`3597f1942ec6f2cfbd50b905683739b0900ff5dd`: all 2018 raw full evaluations matched
exactly, including sampled match positions and quiet material-deletion probes.
Only one diagnostic output line was added to its trace; its inference was unchanged.

In the quiet starting-position probes, removing White's queen or c1 bishop
changed NNUE by approximately 540 cp in either case; HCE changed by 932 cp and
280 cp respectively. Original SF13 raw inference reproduces the NNUE responses.
These probes include positional effects, not isolated piece values, and do not
prove a calibration bug or explain the tactical deficit. Maxima's beta/futility
and capture pruning still use fixed margins and classical exchange values;
isolate evaluator/pruning interactions before assigning a cause. WAC at one
second had median 1.31M visited nodes for HCE versus 475k for NNUE, and median
depth 16 versus 14; at 200k nodes both median depths were 12. Inference speed
therefore contributes to the timed deficit but cannot by itself explain the
fixed-node result. Match, raw reference, probe and audit artifacts live privately
in `.context/baselines/2026-10-13/nnue-games/`, reserved from future training.

Live search diagnostics are available in a separate build configured with
`-Dmaxima_nnue_search_audit=ON` and a nonempty `maxima_nnue_test_network`.
The option defaults off and adds no counters or verification to ordinary builds.
The diagnostic evaluator compares live and fresh accumulators lane by lane,
raw output, cached centipawn scores, material/pawn metadata keys and board state;
the first mismatch reports the root, position, move path, model and differing
lanes. Tests deliberately corrupt a lane and a cached score to verify detection.
Search-owned counters distinguish incremental updates, king refreshes and
refreshes caused by an unevaluated immediate parent.

The audit-only `nnue_search_probe` accepts
`MODEL POSITIONS.tsv hce|nnue off|verify|full|replay|fixed|quiet POLICY NODES DEPTH [CUTOFFS.jsonl]`.
Each TSV row is `id<TAB>FEN`; every root starts with cleared caches. Policy bits
enable beta pruning (1), forward futility (2), null move (4), LMR (8), quiescence
delta (16) and quiescence SEE (32). Normal policy is 63; conservative is 0.
`full` forces fresh inference; `replay` performs one full-window PVS at the
requested depth. Optional beta-cutoff recording is capped at 128 events per
root and restricted to irreversible nodes, allowing FEN replay without relevant
earlier repetition history. Policy controls are diagnostic probe arguments.
Audit builds also expose `NNUEAuditHorizon` with `normal` (default), `fixed` and
`quiet` values. `quiet` works with either HCE or NNUE; `fixed` is active only with
NNUE selected. Active contracts disable heuristic pruning, reductions,
extensions and TT score cutoffs. `fixed` evaluates neural leaves
at exactly the requested ply; `quiet` adds captures, promotions and legal check
evasions, without quiet checks or castling. Probe modes of these names require
NNUE and policy zero. The UCI option is absent from ordinary builds; changing
it stops search and clears TT entries. These are comparison contracts, not
strength configurations or changes to production defaults.
`NNUEAuditNullMove` (default false, audit builds only) re-enables the existing
null-move algorithm in the `quiet` contract while retaining the other
restrictions. Changing it stops search and clears TT entries. Search-owned
attempt/cutoff counters are reported as `info string audit_null ...` in that
contract and in diagnostic probe output. The regression checks active null
cutoffs, live inference equivalence and node-budget cancellation/restoration.
`NNUEAuditBetaPruning` (default false, audit builds only) similarly enables
Maxima's existing reverse-futility rule in `quiet`, independently of null move.
Worker-owned candidate/cutoff counters appear in `info string audit_beta ...`
and probe output. Candidates reflect each engine's own eligibility guards and
must not be treated as identical populations across engines.

`NNUEAuditFutility` (default false, audit builds only) independently enables
the same production frontier-futility rule in the `quiet` contract. Changing
it stops search and clears TT entries. Worker-owned quiet-move candidate/prune
counters are reported as `info string audit_ffp ...` and in probe output.
Tests cover the 299/300-cp boundary, protected tactical moves, first legal move,
PV/deeper-node exclusions and NNUE cancellation/restoration.

`NNUEAuditDelta` (default false, audit builds only) enables only delta pruning
in the `quiet` contract; SEE stays off. Changing it stops search and clears TT.
Worker-owned candidate/prune counters are reported as `info string audit_delta`
and in the diagnostic probe. Tests exercise the 200-cp boundary with HCE/NNUE
and both colors, en passant protection, low-material transitions, live neural
parity and node-budget cancellation/restoration.

`NNUEAuditSEE` (default false, audit builds only) independently enables the
production negative-SEE rule in `quiet`. Changing it stops search and clears TT.
Worker-owned candidate/prune counters appear as `info string audit_see` and in
the diagnostic probe. Candidates are captures surviving delta and all common
guards; the count is not the number of all captures. Tests cover losing and
equal exchanges, checking sacrifices, low-material exclusions, HCE/NNUE parity
and cancellation/restoration.

`NNUEAuditQuietChecks` and `NNUEAuditCastling` (default false, audit builds
only) independently add ordinary quiet checks and all legal castles at the
first quiescence ply in `quiet` mode. Quiet checks use the production SEE and
clock guards; checked nodes always retain every legal evasion. These controls
do not add quiet moves below entry. Changing either stops search and clears TT.
Worker-owned `audit_checks` counters distinguish candidates, SEE rejections
and searched checks; `audit_castles` counts searched entry castles. Tests cover
a hanging queen check, safe checks, losing countercheck evasions, checking and
nonchecking castles, below-entry exclusions and cancellation/restoration with
both evaluators. Ordinary search retains its existing below-entry castling.

Audit builds report `info string audit_work` in `quiet` mode: effective
positive-depth PVS visits, quiescence visits, root-driver calls, made moves,
null moves and full-window re-searches (with root and fail-high subsets).
The depth-zero handoff counts once in quiescence; root-driver calls are
reported separately. Null searches and re-searches count as visited work;
rejected move candidates do not count as visited child positions. Fixed neural
leaves use the quiescence/leaf counter. Work counters are worker-owned and
restart with the node counter when resetting the self-play stack. Ordinary
UCI node counting and search decisions are unchanged by this instrumentation.
The standard-search visit count reconciles with `pvs_nodes + q_nodes`.

`NNUEAuditCheckExtensions` (default false, audit builds only) adds a controlled
check-extension rule in `quiet` mode: one extra main-search ply for a checking
move with nonnegative SEE, including root moves. There is no per-line extension
cap; the existing draw, repetition, cancellation and stack-ceiling handling
remain active. Child stack frames track accumulated extensions, including
through null moves; siblings and re-searches retain the proper parent count.
Quiescence does not add these extensions but retains captures and compulsory
evasions. The `audit_extensions` report distinguishes candidates, SEE rejections
and applied extensions, plus peak extensions per line and main/q
ply. Changing the flag stops search and clears TT. Tests cover both colors and
evaluators, safe/losing checks, extension beyond the former three-ply budget,
inheritance and cancellation.
This controlled rule does not change ordinary search's existing extensions.
Internal iterative deepening stays disabled in `fixed`. In `quiet`, the
independent `NNUEAuditIID` flag defaults false, so extensions cannot silently
activate it. `audit_iid` reports calls; `audit_iid_work` adds eligible candidates,
legal ordering hints, cancellations, helper visits and remaining/probe depths.
A depth-four/no-TT-move regression verifies exclusion when the flag is off.
Each same-position IID helper entry is included in visited work: with other
same-position helpers disabled, visits equal moves + nulls + re-searches + IID
calls.

`NNUEAuditHashMoves` (default false, audit builds only) enables legal TT move
hints in nonroot positive-depth search in `quiet` mode, independently of cached
score usability. Cached search scores never cut off these controlled searches
or replace their static evaluation. Fixed neural leaves ignore the flag, and
quiescence retains its existing no-hash-move policy. Changing it stops search
and clears TT. `audit_hash` reports ordering probes, accepted hints, hints
selected first, rejected hints and TT score cutoffs; the last must remain zero.
Root drivers retain their existing PV and ordering behavior. Matched controls
must explicitly enable main-search hash moves in both engines.

`NNUEAuditLMR` (default false, audit builds only) enables this same conservative
LMR rule in `quiet` mode; other controlled features remain independently
selectable. It is ignored in `fixed` mode. Changing it stops search and clears
TT. `audit_lmr` reports eligible candidates, reduced trials, full-depth
verification attempts, refutations and verified cutoffs, plus minimum/maximum
remaining depth and minimum reduced child depth. Candidates are counted even
with LMR disabled; reduction/verification counts then remain zero. At nominal
depth three no eligible nonroot nonchecking node retains three plies. Tests
verify that boundary and activation at depth four with both evaluators/colors,
certified tactical exclusions, full-depth re-searches and cancellation.

The agreed comparison order is to finish the feature-by-feature depth-three
iteration, then advance to depth four, five, six and onward one step at a time.
At each depth, repeat the established configuration before enabling another
feature. Check actual activation counters and existing rules crossing depth
thresholds; isolate any new gap before increasing depth again. Record
actual feature activity; deeper-only features remain unexercised until their
activation depth. Diagnose a new gap by disabling features selectively at the
same horizon. Fixed nodes can then compare work efficiency; normal builds,
managed clocks and timed games remain the eventual strength gate. Avoid using
expensive live-inference audit builds for timed performance conclusions.

Across 344 tactical roots at 50000 nodes and 33 WAC evaluator disagreements at
200000 nodes, 17576241 live evaluations, including cached returns, passed fresh
verification. Normal, verified and forced-fresh searches agreed exactly on
move, score, PV, completed depth and nodes. On the 344-root run, 32.4% of computed
perspective accumulators required a full refresh; 22.5% were caused by an
unevaluated immediate parent. Walking back to an older ready accumulator is a
performance lead, not an implemented or measured improvement.

On the selected 33 disagreements at 200000 nodes, disabling both beta pruning
and forward futility changed solved targets from 9 to 19 for NNUE and from 24
to 27 for HCE. Fixed-depth ablations also exposed the considerable node cost of
less selective search. Independent conservative depth-one replay found six
false fail-highs among 264 recorded NNUE beta cutoffs. This identifies damaging
heuristic bounds in tested positions, not an inference arithmetic bug or a
proven playing-strength fix; production pruning remains unchanged. Original
SF13's hybrid evaluator selected classical scores on those six diagnostic
positions. That observation alone does not establish that its network requires
a hybrid host.

The diagnostic Release suite passed 35/35, the ordinary Release suite passed
34/34, and the audit, reference and contract checks passed ASan/UBSan with leak
detection. Artifacts and ablation/replay scripts live privately in
`.context/baselines/2026-10-13/nnue-search-audit/`; reserve them from training.

A diagnostic original-SF13 host control returned only its raw neural core when
`Use NNUE=true`, bypassing all classical switches and neural material/tempo/
rule50 adjustments; `false` retained its classical evaluator. Same-binary,
200-game/100-pair play at 2+0.02s and Hash64 scored 76W/102D/22L (63.5%) for
neural-only. Its conservative 95% pair score interval was 49.92%–77.08%, narrowly
including equality. Independent audit verified 34108 legal plies, paired
openings, identities, options and all board-terminal outcomes; six clock
forfeits remained included, with no engine errors or unfinished games. The
openings were reused reserved diagnostic endpoints. This is evidence against
assuming the raw net is useful only in a hybrid, not a clean Elo measurement,
proof of a gain in Maxima or a reason to import classical switching. Source
patch, protocol, binaries, PGNs and audit are private under
`.context/baselines/2026-10-13/nnue-sf13-control/`, reserved from training.

At the user's request, matching nonselective depth-three controls were added
to Maxima's native PVS and a separate original-SF13 build. Both use the pinned
raw network and equivalent centipawn units; heuristic pruning, reductions,
extensions and TT score cutoffs are disabled. The exact-ply neural-leaf control
matched all 344 tactical root scores and 342 moves. The common quiescence
contract (captures, all promotions, check evasions; no quiet checks or castling)
also matched all 344 scores and 340 moves. Forced-move searches verified every
different move as an equal-score alternative in the common score units.

A fixed 100-game/50-pair exact-ply depth-three match then scored 28W/52D/20L
(54%) for Maxima against SF13. All 10960 legal PGN plies and 9360 searched moves,
depth reports, paired openings, outcomes and binary identities passed audit;
no unfinished games or forfeits. Openings are reused reserved diagnostics, and
this exact horizon is not normal depth-three play with quiescence or a rating
estimate. These controls show no unexplained numerical/shallow-PVS deficit in
the tested cases. They do not identify the complete cause of ordinary-search
strength differences. Add selective features back in controlled experiments;
the complexity of another engine's pruning rules alone is not an explanation.
Scripts, source patches, binaries, move records and independent audits are
private under `.context/baselines/2026-10-13/nnue-depth-control/`, reserved from
training. Production scoring and search policy remain unchanged.
The updated audit Release suite passed 35/35 and ordinary Release passed 34/34.
ASan/UBSan with leak detection passed the audit test, including an exhaustive
one-ply neural-leaf comparison and matched-horizon initialization/restoration.

The next controlled step enabled that common unpruned quiescence contract in
both engines, retaining depth three, the same 50 opening pairs, model and Maxima
binary, and all other diagnostic restrictions. Maxima scored 24W/54D/22L (51%)
over 100 games, versus 54% in the exact-ply control. The independent audit passed
10940 legal PGN plies and 9340 searched moves, with all reported depths three
and no unfinished games or forfeits. The conservative 95% opening-pair score
interval is 31.79%–70.21%; the small relative-score change does not establish a
strength difference or measure the gain from quiescence itself. This matched
quiescence step shows no large observed host deficit. Native quiet-check and
castling choices, quiescence pruning and other selective features still need
separate experiments. Scripts, fixed protocol, PGNs and audit are stored beside
the prior controls as `games_quiet.py`, `quiet-games.json` and
`quiet-games-audit.json`. Production behavior remains unchanged.

An HCE sanity check used the same matched-quiescence search, depth three, 50
opening pairs and SF13 NNUE-only opponent. The audit-only `quiet` horizon was
corrected to remain active with HCE; previously its activation required a
loaded NNUE snapshot. HCE and NNUE were then run from the same updated binary.
HCE scored 7W/58D/35L (36%); NNUE repeated 24W/54D/22L (51%), reproducing all
previous NNUE game records and move traces exactly. Both 100-game runs passed
independent audits: HCE 10842 legal PGN plies/9242 searched moves, NNUE
10940/9340, every reported depth three, no unfinished games or forfeits.
This common-opponent diagnostic supports continuing with the current network;
it is not a direct HCE-versus-NNUE match or proof of a normal-search Elo gain.
The HCE initialization/restoration regression, standard and NNUE lifecycle
checks, and ASan/UBSan with leak detection passed. Reports and scripts are
stored as `quiet-hce-games.*`, `quiet-nnue-recheck-games.*` and
`hce-comparison-summary.json` beside the other reserved controls.

NNUE development resumed with a null-move-only comparison at depth three and
the same common quiescence/opening pairs. Four fixed 100-game runs used one
Maxima binary and the pinned network:

| Null pruning enabled | Maxima W/D/L | Maxima score |
| --- | ---: | ---: |
| Neither engine | 24/54/22 | 51% |
| Maxima only | 21/54/25 | 48% |
| SF13 only | 25/54/21 | 52% |
| Both engines | 20/58/22 | 49% |

The separate SF13 null control retained its null reduction/guards but evaluated
null positions directly with NNUE, removing its previous-static-score plus
two-tempos shortcut and TT score substitution into evaluation. This preserves
the pure neural leaf contract; it is not an unmodified SF13 search. All other
heuristic pruning, reductions, extensions and TT score cutoffs remained off.
The new baseline reproduced prior qsearch game records and core move traces.
Counters verified actual cutoffs: Maxima 211910 alone/213341 with both enabled;
SF13 172570 alone/161232 with both. Independent audits passed all 400 games,
41895 legal PGN plies and 35495 searched moves, every reported depth three,
no unfinished games or forfeits. Regression/lifecycle checks passed 3/3 and
ASan/UBSan with leak detection passed the updated audit test.

These small, reserved-opening diagnostics show no large observed deficit from
adding null move at depth three. The small relative-score changes do not prove
a strength loss, and the depth does not exercise deeper null verification.
Production policy remains unchanged. Scripts, source patches, frozen binaries,
declared protocols, move-level counters, PGNs and audits are private under
`.context/baselines/2026-10-13/nnue-null-control/`, reserved from training.

The next isolated step tested reverse futility, with null move off throughout.
Four fixed 100-game/50-pair runs retained depth three, common quiescence, the
pinned network and the same reserved openings. Each engine retained its own
reverse-futility rule: Maxima `eval - 50 * depth`, SF13's improving-dependent
`234 * (depth - improving)` margin in SF units. Both binaries supported runtime
toggles, so every configuration used the same pair of frozen executables.

| Reverse futility enabled | Maxima W/D/L | Maxima score |
| --- | ---: | ---: |
| Neither engine | 24/54/22 | 51% |
| Maxima only | 15/55/30 | 42.5% |
| SF13 only | 24/61/15 | 54.5% |
| Both engines | 22/55/23 | 49.5% |

The baseline reproduced previous qsearch/null-off game records and core move
traces exactly. Counters confirmed actual reverse-futility cutoffs: Maxima
233902 alone/223009 with both enabled, SF13 283495 alone/282773 with both.
Null attempts were zero in every run. Independent audits passed all 400 games,
41500 legal PGN plies and 35100 searched moves, every reported depth three,
no unfinished games or forfeits. The isolated-control, cancellation/restoration
and lifecycle checks passed 3/3; ASan/UBSan with leak detection passed the updated
audit regression. Production scoring and pruning remain unchanged.

Maxima's one-sided point-score drop is larger than in the null-move experiment
and supports investigating its margin, alongside the earlier bad-cutoff
replays. SF13's rule also had a lower point score when enabled alone. These
small diagnostics do not prove normal-search Elo losses: at fixed depth, saved
work cannot buy deeper search, so pruning's accuracy cost receives no depth
benefit. Test any proposed change at equal nodes and time before deployment.
The SF control retains direct neural evaluation and disables TT score
substitution; it is not unmodified SF13. Source patches, scripts, declared
protocols, frozen binaries, counters, PGNs and audits are private under
`.context/baselines/2026-10-13/nnue-beta-control/`, reserved from training.


The combined null-move plus reverse-futility check enabled both features in
both engines, retaining the same depth-three/common-qsearch contract, pinned
network, 50 paired openings and frozen Maxima binary. The SF13 control was
rebuilt from the preceding beta-control source with only its null-move gate
restored. Maxima scored **24 wins, 47 draws, 29 losses (47.5%)** in 100 games.
For comparison, neither feature scored 51%, null move alone in both engines
49%, and reverse futility alone in both 49.5%. No large relative gap appears
in this combined depth-three sample; this does not establish normal-search
strength or guarantee equal results at deeper horizons.

Counters confirmed both features were active: null attempts/cutoffs were
117808/66378 for Maxima and 37182/24902 for SF13; reverse-futility
candidates/cutoffs were 338454/220646 and 530356/283176 respectively. The
independent audit checked every searched move against the full PGN history,
legal moves/PVs, paired openings, terminal results, reported depth three and
frozen model/binary identities. All 100 games passed, with no unfinished games
or forfeits. Other selective features remained disabled; this is the combined
two-feature control, not full production search. Production code and defaults
are unchanged. Source reconstruction, the added SF patch, declared protocol,
frozen SF binary, PGNs, counters and audit are private under
`.context/baselines/2026-10-13/nnue-combined-control/`, reserved from training.


A review before adding further features compared Maxima's null-move and
reverse-futility implementations with the pinned SF13 source. The basic null
window and pass/undo guards are equivalent; Maxima's total reduction of four
matches SF13's base reduction at depths one through three. Both reach common
qsearch there, and neither deep verification policy is exercised. SF13 adds
static-evaluation, improving, prior history-score and TT-PV eligibility guards,
uses dynamic reduction at larger depths and has a different verification
policy. Maxima retains its own legal-terminal, clock and decisive-score guards.
For reverse futility, SF13 returns `eval`, uses the improving-dependent margin
`112.5 * (depth - improving)` in Maxima cp units and allows depths through eight;
Maxima returns `eval - margin`, uses `50 * depth` and allows depths through three.
SF13's depth-one improving margin is zero: it is not always more conservative.

Four exploratory 100-game/50-pair controls changed only Maxima, with both
features enabled in both engines and all other restrictions unchanged:

| Maxima policy | W/D/L | Score | Median searched nodes per move |
| --- | ---: | ---: | ---: |
| Original | 24/47/29 | 47.5% | 1369 |
| Reverse futility returns eval | 24/47/29 | 47.5% | 1369 |
| Reverse-futility margin 112 * depth | 25/51/24 | 50.5% | 1514 |
| Null eligibility requires eval >= beta + 40 cp | 23/54/23 | 50% | 1428 |

The wider margin changes both eligibility and the returned `eval - margin`;
the null cushion is a simple diagnostic, not an exact SF13 guard port. The
original reproduced every previous combined-control game record and move row.
The return-only variant also reproduced all move rows, scores, PVs, nodes and
counters exactly. A static threshold screen of the earlier 264 irreversible
cutoff records retained 162 at the wider margin and rejected five of six false
fail-highs. This screen reuses conservative replay labels, not new searches
under the common-qsearch horizon.

All 400 games passed independent audits: 40216 legal PGN plies, 33816 searched
moves, depth three, no unfinished games or forfeits. Each temporary build passed
live NNUE/cancellation/restoration tests plus explicit witnesses of its return,
margin and null-eligibility behavior; standard/NNUE lifecycle and audit checks
passed 3/3. Changes were confined to temporary source snapshots and aligned
audit mode. No tracked native source or production defaults changed. The two
point improvements remain uncertain: exploratory paired-bootstrap 95% change
intervals were -2.5 to +8.5 percentage points for the margin and -3 to +8.5 for
the null guard. The additional median node cost was about 10.6% and 4.3%; equal
node/time tests and fresh openings are required before selecting a production
change. Do not tune toward an exact 50% score. Review protocols, source patches,
policy witnesses, frozen binaries, PGNs, counters and audits are private under
`.context/baselines/2026-10-13/nnue-pruning-review/`, reserved from training.


The requested 100-game repeat with the implemented round 100/50 policy scored
**22 wins, 60 draws, 18 losses (52%)** against the same frozen SF13 combined
control. It retained both null move and reverse futility in both engines,
the pinned network, depth three/common qsearch and exactly the same 50 paired
openings. The preceding aggressive combined baseline scored 47.5%. The new
4.5-percentage-point improvement is an exploratory point result; its paired
bootstrap interval includes zero, and these reused diagnostic openings do not
establish normal-search Elo.

All 100 games passed independent audits: 10561 legal PGN plies, 8961 searched
moves, every reported depth three, no unfinished games or forfeits. Model and
opening hashes, seeded selection, comparison settings and the exact SF binary
identity were checked against the preceding control. Null attempts/cutoffs
were 91287/63906 for Maxima and 37984/25982 for SF13; reverse-futility
candidates/cutoffs were 349219/149530 and 553594/285132. Maxima's median
searched nodes rose from 1369 to 1518, about 10.9%, at the fixed horizon.
Live NNUE evaluation checks stayed active throughout. The new baseline remains
close to even in this matched control; equal nodes/time and deeper horizons
remain separate checks. Declared protocol, frozen Maxima binary, scripts,
PGNs, counters, audit and comparison are private under
`.context/baselines/2026-10-13/nnue-rounded-control/`, reserved from training.


The frontier-futility depth-three comparison ran four fixed 100-game/50-pair
profiles with null move and reverse futility retained in both engines. Each
profile used the same frozen binary pair, pinned NNUE and reserved openings.
The SF control used the same simple 300-cp frontier rule (624 internal units),
matching tactical and material-endgame exclusions; its native history/LMR/SEE/
move-count/parent-futility block remained disabled. This measures a common
basic futility concept, not SF13's tuned production parent-futility policy.

| Forward futility enabled | Maxima W/D/L | Maxima score | Median nodes Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 22/60/18 | 52% | 1518 / 1474.5 |
| Maxima only | 22/60/18 | 52% | 1411 / 1474.5 |
| SF13 control only | 22/60/18 | 52% | 1521 / 1384 |
| Both engines | 22/60/18 | 52% | 1414 / 1384 |

Every individual game result matched the baseline across all profiles. The
baseline reproduced the preceding rounded-control game records and core move
rows exactly; enabling Maxima alone also preserved every move, root score and
PV while reducing work. Counters confirmed actual futility prunes: Maxima
599942 alone/599722 with both enabled, SF 508650 alone/508650 with both.
Disabled sides recorded zero candidates/prunes. Independent audits passed
all 400 games, 42220 legal PGN plies and 35820 searched moves, every reported
depth three, with no unfinished games or forfeits. Full release-audit checks
passed 35/35, and ASan/UBSan with leak detection passed the updated selective
and NNUE search tests. The ordinary release binary was rebuilt and exercised
with both HCE and NNUE.

This simple frontier rule introduced no observed depth-three deficit, with
roughly 7% fewer median Maxima nodes when enabled. It is not a timed-strength
claim or a proof of NNUE pruning soundness. Finish the remaining depth-three
features before advancing to depth four and five. Source patches, declared
protocols, frozen binaries, PGNs, per-feature counters, audits and comparison
scripts are private under `.context/baselines/2026-10-13/nnue-ffp-control/`,
reserved from training.


The isolated 200-cp delta step retained null move, reverse futility and simple
frontier futility in both engines, with SEE entirely disabled. Four fixed
100-game/50-pair depth-three profiles used the same frozen binary pair, pinned
NNUE and reserved openings. The SF control used the same plain captured-piece
rule and post-capture material guard in normalized SF units; its native qsearch
move-count, SEE and history pruning remained disabled.

| Delta enabled | Maxima W/D/L | Maxima score | Median nodes Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 22/60/18 | 52% | 1414 / 1384 |
| Maxima only | 23/58/19 | 52% | 1324 / 1294.5 |
| SF13 control only | 27/55/18 | 54.5% | 1387 / 1286 |
| Both engines | 23/55/22 | 50.5% | 1308 / 1305 |

The baseline exactly reproduced the preceding both-FFP game records and core
move traces. Enabled delta prunes were Maxima 1561383 alone/1707858 with both,
SF 7360991 alone/7503561 with both; disabled sides recorded zero candidates and
prunes. Different candidate populations and move ordering prevent interpreting
these totals as comparative pruning aggressiveness. All 400 games passed
independent audits: 41929 legal PGN plies, 35529 searched moves, depth three,
no unfinished games or forfeits; model/opening identities, seeded selection,
settings and frozen executable identities matched across profiles.

All 35 release checks passed across the full run and isolated rerun. The full
run initially missed one timing-sensitive lifecycle assertion (1.057 seconds
against a one-second bound) while compiling and running sanitizers concurrently;
the isolated lifecycle rerun passed. ASan/UBSan with leak detection passed
NNUE/horizon tests and the additional en-passant/endgame-transition witnesses.
Actual-search boundary tests cover HCE/NNUE and both colors without changing
qsearch's nonvirtual hot-path dispatch. The ordinary release engine was rebuilt
and exercised with both evaluators. The both-enabled score remains near even
in this small fixed-depth sample, not evidence of a timed Elo gain/loss. Keep
200 cp as the documented starting margin; review SEE independently next, then
finish the remaining depth-three features before depth four/five. Source patches,
scripts, protocols, frozen binaries, PGNs, counters and audits are private under
`.context/baselines/2026-10-13/nnue-delta-control/`, reserved from training.

The isolated quiescence SEE step retained null move, reverse futility, frontier
futility and delta in both engines. Four fixed 100-game/50-pair depth-three
profiles tested the guarded negative-SEE rule with the same frozen binary pair,
pinned NNUE and reserved openings. Both used the same capture values and outer
eligibility guards; their existing exchange estimators and move ordering remained
distinct. Other selective features stayed disabled.

| SEE enabled | Maxima W/D/L | Maxima score | Median nodes Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 23/55/22 | 50.5% | 1308 / 1305 |
| Maxima only | 24/55/21 | 51.5% | 1103 / 1257 |
| SF13 control only | 24/64/12 | 56% | 1227 / 903.5 |
| Both engines | 19/63/18 | 50.5% | 1058 / 898 |

The baseline exactly reproduced the preceding both-delta game records and
existing core move-row fields. Enabled SEE prunes were Maxima 927999 alone/
888652 with both, SF 1156317 alone/1243655 with both; disabled sides recorded
zero candidates/prunes. All 400 games passed independent audits: 42305 legal
PGN plies, 35905 searched moves, every reported depth three, no unfinished games
or forfeits. Frozen identities, model, settings and seeded openings matched.
Full release-audit checks passed 35/35; targeted ASan/UBSan with leak detection
passed 3/3. The ordinary release engine was rebuilt and exercised with HCE/NNUE.

The both-enabled result shows no large relative deficit in this reused sample;
lower node counts do not establish a timed Elo gain. Maxima's existing SEE
can count pinned recapturers: a legal witness labels a free pawn capture as a
225-cp loss because the defending pawn cannot legally move. This witness
motivated the subsequent legality correction below. Complete
the remaining depth-three feature checks before advancing to depth four/five.
Protocols, frozen binaries, PGNs, counters, audits and the witness are private
under `.context/baselines/2026-10-13/nnue-see-control/`, reserved from training.

The SEE legality correction replaces the approximate gain/pawn shortcuts and
pseudo-legal exchange sequence with recaptures checked against each updated
occupancy. The pinned-pawn witness now scores +100 cp and survives qsearch
SEE pruning. No evaluation weights, pruning margins or eligibility guards
changed; root ordering and check extensions can change because they use SEE.

An independent legal-move/material reference supplies 1851 checked-in cases,
including 36 targeted cases covering both colors, dynamic pins, captures along
pin lines, king safety, en passant and promotions. Exhaustive same-square
material minimax agreed on every case in this sample; that does not prove the
least-valuable model universally exact. Native tests verify unchanged board,
hash, clock and stack state, and live HCE/NNUE tests cover qsearch and cancellation.
Full release-audit checks passed 35/35. Targeted ASan/UBSan/leak checks passed
after correcting a historical SEE fixture that lacked a black king. Another
historical exchange label was independently corrected from -600 to -500 cp.
Regenerate certificates with `tests/data/see/generate_legality.py`; `--check`
verifies them. Python-chess is needed only for regeneration.

In an isolated small timing sample, five alternating repeats of 3702000 SEE
calls had median times 0.1912 seconds before and 0.2739 after. Ordinary search
over eight paired opening positions, 100000 nodes each and three balanced
repeats, had median elapsed times 82.43/87.12 ms with HCE and 251.39/252.80 ms
with NNUE (before/after). These include changed search trees and do not establish
universal throughput costs or timed strength.

A fixed 100-game/50-pair depth-three repeat against the same both-SEE control
scored **19 wins, 64 draws, 17 losses (51%)**, versus 50.5% before. Model,
opponent, settings and reused paired openings matched. Independent audits
checked 10961 legal PGN plies and 9361 searched moves, every reported depth
three, no unfinished games or forfeits. This is a correctness fix with no large
observed depth-three deficit; the half-point change does not establish an Elo
gain. Artifacts are private under
`.context/baselines/2026-10-14/see-legality/`, reserved from training.

The shared SEE threshold overload preserves the numeric exchange model and
search policy. All 1851 independent scores and 236928 threshold comparisons
passed, including equality, integer extremes, promotions and king safety;
castling has an additional zero-score boundary check. Full release-audit checks
passed 35/35, and targeted ASan/UBSan/leak checks passed 3/3.

Five alternating repeats of 3702000 predicate calls had median times 0.3187
seconds for full-score comparison and 0.1966 seconds for the threshold overload,
about 38% less time. Ordinary fixed-node comparisons over eight opening
positions, three balanced repeats and both evaluators matched all 48 paired
searches exactly in moves, scores, PVs, reported/selective depths and nodes.
Median paired elapsed ratios (after/before) were 1.0025 with HCE and 0.9859
with NNUE; timings varied across repeats, so no reliable whole-engine speed or
Elo gain is claimed. Protocols, frozen binaries, raw timings and equivalence
checks are private under `.context/baselines/2026-10-14/see-threshold/`.

The first-quiescence-ply quiet-check comparison retained null move, reverse
futility, frontier futility, delta and capture SEE in both engines. Four fixed
100-game/50-pair depth-three profiles used the same frozen binary pair, pinned
network and reserved openings. Ordinary quiet checks require nonnegative SEE,
except at reversible clock 99; checked nodes retain all legal evasions.
Castling remained disabled in this step. Other selective features stayed off.

| Quiet checks enabled | Maxima W/D/L | Maxima score | Median nodes Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 19/64/17 | 51% | 1061 / 899 |
| Maxima only | 31/64/5 | 63% | 1149 / 837.5 |
| SF13 control only | 14/65/21 | 46.5% | 1114 / 1027 |
| Both engines | 19/75/6 | 56.5% | 1131 / 899.5 |

The baseline reproduced all preceding legality-repeat game records and 9361
move rows exactly. Both-enabled Maxima median nodes increased about 6.6%; this
is a shallow fixed-depth diagnostic, not a timed Elo or throughput claim.
Independent audits and separate castling comparisons are recorded with source
patches, declared protocols, frozen binaries and per-move counters privately
under `.context/baselines/2026-10-14/nnue-quiet-control/`.

The subsequent castling step retained quiet checks in both engines and
independently added all legal castles at quiescence entry, including nonchecking
castles. It reused the both-checks run as its baseline and the same frozen
binaries, network and 50 paired openings for three additional 100-game runs.

| Entry castling enabled | Maxima W/D/L | Maxima score | Median nodes Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 19/75/6 | 56.5% | 1131 / 899.5 |
| Maxima only | 21/75/4 | 58.5% | 1190.5 / 909 |
| SF13 control only | 15/79/6 | 54.5% | 1130 / 890 |
| Both engines | 17/76/7 | 55% | 1117 / 838 |

A separate paired-root diagnostic searched the same 50 opening FENs with both
engines at depth three. Maxima's median within-position node ratio was 1.0119
when adding checks and 1.0191 when adding castles; the largest sampled ratios
were 1.8133 and 2.8152. Total reported nodes were 108040/89476 in the baseline,
112268/94855 with checks and 112284/103231 with checks and castles (Maxima/SF).
Match medians also depend on the positions reached. Maxima counts search calls;
SF13 counts made moves, with different null/re-search accounting. Cross-engine
node ratios therefore do not directly measure relative efficiency. Neither
these fixed-depth results nor the small point-score changes establish timed Elo.

Independent audits passed all 700 games, 84444 legal PGN plies and 73244
searched moves, with legal PVs, reported depth three and no unfinished games
or forfeits. Counters verified disabled features stayed inactive and enabled
ones actually searched checks/castles. Full Release-audit checks passed 35/35;
focused ASan/UBSan/leak checks passed 3/3. The ordinary Release engine was
rebuilt and exercised with HCE and NNUE. The independent clock-99 horizon and
castling certificates retained their original expectations: an initial SEE
filter lost drawing checks at that boundary, motivating the clock safeguard.
All protocols, source patches, audits and paired-root data are preserved with
the quiet-control artifacts above, reserved from training.

The node-accounting follow-up searched the same 50 opening FENs at depth three
under eight declared profiles, totaling 800 searches. In the baseline profiles,
all 300 prior moves, PVs, depths and reported node counts were reproduced
exactly. Both engines were instrumented for the same effective PVS/quiescence
visits, excluding the root driver. Each controlled search reconciled
`visits = made_moves + null_moves + full_window_researches`. Maxima's existing
reported count equals those visits; the SF13 reported count equals made moves.
These equations are for the present controls: other same-position helper
searches, when restored, require their own accounting.

| Settings in both engines | Visits Maxima/SF, summed over 50 FENs | Median per-FEN Maxima/SF visit ratio |
| --- | ---: | ---: |
| Prior baseline, no optional quiets | 108040 / 90524 | 1.3141 |
| Add quiet checks | 112268 / 95903 | 1.3008 |
| Add entry castles | 112284 / 104210 | 1.2683 |
| Checks/castles, null move off | 120441 / 114636 | 1.2070 |
| Checks/castles, reverse futility off | 118516 / 141046 | 0.9781 |
| Checks/castles, null/RFP/frontier futility off | 139208 / 159266 | 1.0134 |
| Checks/castles, delta/capture SEE off | 175373 / 355062 | 1.0399 |
| Checks/castles, all five tested pruning rules off | 193832 / 467195 | 0.7788 |

The quiet-check SEE filter stays active in these last profiles. Normalizing
accounting changes the both-checks/castles median ratio from 1.2902 to 1.2683,
so it explains only a small part of that difference. Turning reverse futility
off changes the relative visited work substantially; this supports differing
pruning as a cause rather than assuming uniformly worse move ordering.
Actual RFP cutoffs in the both-enabled sample were 1119/3637. Re-searches were
654/433, with zero measured fail-high PV re-searches in these shallow controls.
Quiescence represented about 93% of visits. Aggregate ratios differ from median
per-position ratios because some positions dominate the totals. These are
reserved, fixed-depth diagnostics, not normalized CPU cost or timed Elo.
Protocols, frozen binaries, source patches, per-root counters and independent
audits are private under `.context/baselines/2026-10-14/node-accounting/`.

The independent accounting audit passed all 800 searches and their counter
equations. Focused NNUE audit and standard/neural lifecycle tests passed 3/3;
the additional stack-reset accounting regression also passed. Ordinary node
counting and playing behavior remain unchanged. ASan/UBSan with leak detection
passed the final accounting and played-move stack-reset regression.

The bounded check-extension step ran four fixed 100-game/50-pair depth-three
profiles with all preceding pruning, quiet-check and entry-castling controls
enabled. Both frozen binaries used one-ply nonnegative-SEE check extensions,
including root checks, with a budget of the nominal iteration depth per line.
Other extension/reduction/helper features and TT score cutoffs stayed disabled.

| Check extensions enabled | Maxima W/D/L | Maxima score | Median search visits Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 17/76/7 | 55% | 1117 / 854 |
| Maxima only | 35/55/10 | 62.5% | 1425 / 965 |
| SF13 control only | 19/61/20 | 49.5% | 1152 / 1080 |
| Both engines | 27/57/16 | 55.5% | 1469 / 1094 |

Visits use the same definition in both engines, including null searches and
re-searches. Native SF reported-node medians were 838, 946, 1066 and 1075 in
the corresponding profiles; Maxima's reported count already equals visits.
The both-enabled match medians rose about 31.5%/28.1%. They include different
positions reached during play and do not establish same-position work growth
or timed strength. On the same 50 opening FENs, total visits changed from
112284/104210 to 120037/105801, about 6.9%/1.5%. The control did not exhibit
an unbounded main-search tree; it remains a provisional extension baseline.

Both engines reached the three-extension line budget in the games. Applied
extensions were 154991/79376, with 484/332 further checking candidates denied
an extension by the exhausted budget. Those moves remain searchable at the
ordinary remaining depth. Maximum positive-depth main-search ply was five
(quiescence entry can be at ply six); maximum quiescence ply was 23/39, also
observed in the extensions-off baseline. Compulsory check evasions remain
searchable independently of the extension budget.

The initial instrumented trial detected extra IID searches after a root check
made depth-three interior nodes reachable. IID had been dormant in the earlier
unextended depth-three controls. That trial was stopped and preserved; the
final four profiles explicitly exclude IID and verify its counter is zero.
The final baseline reproduced all prior castling-control game records and core
move rows exactly. Independent audits passed all 400 games, 48440 legal PGN
plies and 42040 searches, including legal PVs, depth three, counter equations,
budget bounds and no unfinished games or forfeits. The additional 400 paired
root searches also passed independent checks. Full Release checks passed 35/35
and focused ASan/UBSan/leak checks passed 3/3. Protocols, frozen binaries,
source patches, interrupted-trial evidence and all reports are private under
`.context/baselines/2026-10-14/check-extensions/`, reserved from training.

The follow-up removed the per-line check-extension budget in both controls,
retaining the same one-ply nonnegative-SEE rule and all other settings. All four
100-game profiles reproduced the capped run's game records, searched positions,
chosen moves and scores; a few PVs and work counters changed. Scores and median
visits therefore remain exactly as in the table above. With extensions enabled
in both engines, total visits changed from 10791888/13403611 to
10792120/13403800, only +0.0021%/+0.0014%. Maximum accumulated extensions were
five in Maxima and six in the SF13-only profile; positive-depth main-search ply
reached seven/eight respectively. Quiescence peaks remained 23/39. Existing
repetition, draw, cancellation and stack-ceiling handling remains in force.

The same 400 opening-root searches reproduced every capped trace. A separate
sample of 50 positions selected because the earlier games had exhausted the
cap compared each engine's capped and uncapped searches with identical empty
prior histories. Total visits increased 1.03% for Maxima and 0.35% for SF13,
with no chosen-move changes. Median per-position ratios were 1.0, but the
largest increases were 2.08 and 1.52 times: individual checking sequences can
still grow substantially. These targeted diagnostics show little aggregate
benefit from the cap at depth three, not a guarantee at higher depths or a
timed strength measurement. The uncapped rule is the current controlled
baseline; ordinary search behavior is unchanged.

Independent audits passed all 400 games, 48440 legal PGN plies, 42040 game
searches and 600 additional root searches, including legal PVs, counter
equations, depth, identities and zero IID activity. No unfinished games or
forfeits occurred. Full Release checks passed 35/35 and focused ASan/UBSan/leak
checks passed 3/3. Protocols, binaries, source patches and comparison reports
are private under `.context/baselines/2026-10-14/uncapped-extensions/`, reserved
from training.

The depth-four baseline retained every established control and enabled no
additional feature. LMR, IID, other extensions/helpers and TT score cutoffs
remained off. A depth-threshold review found that the opponent's native
aspiration driver starts at iteration four and reduces retry depth after
fail-highs, while Maxima starts aspiration at iteration six. The early trial
was stopped and preserved. The final comparison uses full root windows in
both engines and the nominal iteration depth on every root attempt. Native
move ordering and existing depth-dependent history/context updates remain.

| Controlled horizon | Maxima W/D/L | Maxima score | Median search visits Maxima/SF |
| --- | ---: | ---: | ---: |
| Depth three, preceding uncapped baseline | 27/57/16 | 55.5% | 1469 / 1094 |
| Depth four, no additional features | 25/69/6 | 59.5% | 4644 / 3167 |

The depth-four match used the same 50 paired opening endpoints, model, Maxima
binary and preceding feature settings. Native SF reported-node median was
3122.5; the visit figure includes null searches and re-searches in both engines.
Total game visits were 32876565/27351502, but positions reached in the games
differ from the depth-three run. On the same 50 opening positions, total visits
rose from 120037/105801 to 442334/303072, about 3.69/2.86 times. The median
paired Maxima/SF visit ratio rose from 1.338 to 1.485. Maxima retained a positive
observed match score while searching more work; neither this score nor the
change from depth three establishes timed Elo or a statistically proven gain.

Both engines reached six accumulated check extensions and main-search ply
nine in the games; quiescence peaks were 27/39. IID activity was zero, and the
visit equations reconciled without unexplained helper searches. Every retained
pruning/check/castling rule was exercised. Independent audits passed all 100
games, 11232 legal PGN plies, 9632 game searches and 200 additional opening-root
searches. All 100 depth-three opening traces exactly reproduced the preceding
baseline after disabling the opponent's aspiration driver. No unfinished games,
forfeits or engine errors occurred. Production engine code was unchanged.
Protocols, binaries, source patch, threshold review, interrupted trial and
reports are private under `.context/baselines/2026-10-14/depth-four-control/`,
reserved from training.

The next depth-four comparisons enable one feature at a time with simple,
conservative settings, recording actual activation, work savings, re-searches
and match outcomes before adding another feature. LMR and root aspiration are
separate steps. IID can also be measured at fixed depth: its shallow ordering
search costs nodes but may save more through earlier cutoffs. It was excluded
to isolate features, not because fixed-depth experiments make it invalid.
Distinguish an ordering search from internal depth reduction when comparing
host mechanisms.

A depth-four fixed-position ablation investigated the additional work before
attributing it to move ordering. Seven profiles searched the same 50 opening
FENs in both engines, retaining uncapped check extensions, entry quiet checks
and castles, with LMR/IID/aspiration still off. Disabling reverse futility alone
changed the median paired Maxima/SF visit ratio from 1.485 to 1.008; total
visits changed from 442334/303072 to 616825/715404. With all three main-search
prunes off (reverse futility, null move and frontier futility), the median
ratio was 0.997. With all five tested prunes off, including capture delta and
capture SEE, it was 0.750. Quiet-check SEE filtering and check-extension
eligibility remained active in these profiles.

This identifies reverse-futility selectivity and its interactions as the main
measured explanation of the typical work gap in this sample, not proof of a
blanket move-ordering deficit. In particular, the opponent's improving
adjustment can make its depth-one reverse-futility margin zero, whereas
Maxima retains a 100-cp allowance. Other margins and guards differ too;
conservative behavior is contextual. Disabling a feature changes subsequent
ordering, windows and pruning opportunities, so these effects are not additive
percentages or a playing-strength recommendation. Independent audits passed
all 700 searches, including exact reproduction of the 100 baseline opening
traces, identities, legal moves/PVs, depth, disabled-feature counters and visit
equations. No engine source or search defaults changed. Scripts and reports
are private under `.context/baselines/2026-10-14/depth-four-pruning/`, reserved
from training.

Keep the current reverse-futility baseline unchanged while completing the
stepwise search comparisons. More aggressive reverse futility is a deferred
strength experiment: compare candidate margins and guards against this
baseline, then validate playing strength with managed-time games. Fewer nodes
alone are not sufficient evidence to adopt a change.

The conservative LMR step ran four 100-game/50-pair depth-four profiles using
the same reserved opening endpoints, raw network and preceding feature flags.
Both controls use the declared nonroot one-ply quiet reduction after the first
three searched moves, with remaining depth at least three and full-depth
verification above alpha. Native host PVS verification-window handling remains;
the opponent's adaptive post-LMR history feedback is excluded. Reverse futility
is unchanged, and IID, aspiration, other helpers and TT score cutoffs stay off.

| LMR enabled | Maxima W/D/L | Maxima score | Median search visits Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 25/69/6 | 59.5% | 4644 / 3167 |
| Maxima only | 24/60/16 | 54% | 4035 / 3388 |
| SF13 control only | 34/60/6 | 64% | 5062 / 2945 |
| Both engines | 31/57/12 | 59.5% | 4080 / 2769 |

Native SF reported-node medians were 3122.5, 3338, 2909 and 2724 respectively;
visits use the common accounting definition including null searches and
re-searches. Both-enabled median visits fell about 12.1%/12.6%; total game
visits changed from 32876565/27351502 to 30565000/25717318. Game positions differ,
so these are not same-position savings. On the same 50 opening FENs, total
visits changed from 442334/303072 to 367160/232788, about 17.0%/23.2% fewer.
The observed both-enabled score held up, while each one-sided profile scored
less for the engine using LMR than its LMR-off baseline. This illustrates the
fixed-depth work/coverage tradeoff, not a managed-time strength verdict.

In the both-enabled games, Maxima/SF applied 282730/257714 reductions and
started 15453/2827 full-depth verifications. Of those, 10540/1627 were refuted
and 1559/485 produced verified beta cutoffs. Every reduction was at remaining
depth three with a one-ply trial child. Maximum main-search ply was eight/nine
and maximum quiescence ply was 27/42. An additional 100 depth-three opening
searches with LMR enabled reproduced all prior traces and made no reductions.
The LMR-off depth-four profile reproduced every previous game record and core
move trace exactly. Independent audits passed all 400 games, 44321 legal PGN
plies, 37921 game searches and 500 additional opening searches, with legal PVs,
identities, depth, counter equations and LMR guards checked. No unfinished
games, forfeits or engine errors occurred in the accepted runs.

An independent move-trace check in the opponent control verified 45 reduced
ordinary quiet moves and 61 protected advanced passed-pawn pushes across both
colors. A preliminary guard-order error in that experimental control was
corrected before accepting results; affected trials were stopped and preserved.
Earlier runs exceeding the runner's 30-second response deadline were also
excluded. The final fixed-depth protocol uses a 120-second response deadline.
A slow-position replay reproduced the same 1278837 nodes and move with the
previous control, the updated control with LMR off, and LMR on; LMR had no
eligible candidates there, and almost all work was in quiescence. Such outliers
explain the longer response allowance without changing the search horizon.

Ordinary Release checks passed 34/34. All 35 audit checks passed across the
suite and an isolated NNUE lifecycle timing recheck. ASan/UBSan with leak
detection passed selectivity/search-contract checks and both lifecycle suites,
with an isolated ponder-deadline timing recheck; strict wall-clock assertions
initially failed under concurrent work, and sandbox restrictions required
running LeakSanitizer outside it. Original failures and recheck logs remain
preserved. Protocols, frozen binaries, source patches, interrupted trials,
move traces and independent audits are private under
`.context/baselines/2026-10-14/lmr-control/`, reserved from training. Timed games
with ordinary optimized builds remain the strength gate.

Root aspiration uses a 50 cp half-window around the previous completed
iteration from depth four. Only the failed bound widens, with a doubling
increment; every retry retains the requested depth. Decisive scores use a full
window. `Aspiration=false` disables this driver independently of LMR. Root
fail-low work searches every alternative before retrying; an interrupted
fail-low reports a legal provisional move without an unsupported score.
The controlled quiet-horizon option `NNUEAuditAspiration` enables the same
policy independently, and its counters record attempts, failures, completed
windows, cancellation and requested depths. Fixed-horizon controls keep it off.

The aspiration step retained LMR and all preceding controlled features in
both engines, with reverse futility unchanged and IID/TT score cutoffs off.
Four depth-four 100-game/50-pair profiles used the same reserved openings:

| Aspiration enabled | Maxima W/D/L | Maxima score | Median search visits Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 31/57/12 | 59.5% | 4080 / 2769 |
| Maxima only | 34/55/11 | 61.5% | 3804.5 / 2737 |
| SF13 control only | 29/58/13 | 58% | 4137 / 2965 |
| Both engines | 28/58/14 | 57% | 4062 / 3028.5 |

Both-enabled total visits were 27634159/24356932, versus 30565000/25717318
with aspiration off. These game totals include retries and different reached
positions. On the same 50 opening FENs, visits changed from 367160/232788 to
355075/232234, about 3.3%/0.2% fewer, with no chosen-move changes. The measured
work savings are modest; the 57% both-enabled score remains above even, versus
59.5% with aspiration off, without establishing managed-time strength.

Both-enabled Maxima/SF made 1684/1814 retries across 4517/4528 narrow-window
iterations. Counters verified every retry stayed at requested depth four.
Maximum main-search ply was 10/9 and maximum quiescence ply was 27/42.
Independent audits passed all 400 games, 44710 legal PGN plies and 38310 game
searches. The aspiration-off run reproduced all 100 previous game records and
core move traces exactly. Another 500 opening searches passed: 100 depth-four
traces with aspiration off matched the preceding baseline, and 100 depth-three
traces with it enabled matched the prior control with zero aspiration counters. Independent
window traces verified the declared bounds and retry depths in 50 opening
searches. No unfinished games, forfeits or engine errors occurred.

All 35 ordinary Release checks passed across the suite and an isolated NNUE
lifecycle recheck; an initial ordinary-time-budget assertion missed its
one second deadline by 57 ms. The full audit Release suite passed 36/36. Focused
ASan/UBSan/leak checks passed, including interrupted root fail-low restoration.
Failure and recheck logs, frozen binaries, protocols, source patches, move
traces and independent audits are private under
`.context/baselines/2026-10-14/aspiration-control/`, reserved from training.
Managed-time matches with ordinary optimized builds remain the strength gate.

IID uses one ordering helper at nonroot PV nodes with no hash move and at
least four plies remaining. It searches two plies shallower, without nested IID,
and accepts a legal helper move only when the helper score exceeds alpha.
The main search always retains its original depth; the helper score, including
a mate score, cannot replace it. The search frame is restored before continuing,
and cancellation returns immediately. `IID=false` disables it independently.
The threshold keeps one-ply ordering probes out of shallow search. At nominal
depth four it is eligible mainly after a root check extension preserves four
plies; ordinary interior nodes become eligible at depth five.

The following IID comparison and earlier quiet-horizon cross-engine match
tables used different main-search hash-move policies: Maxima excluded TT lookup,
while the opponent retained hash-move ordering. They are historical diagnostics,
superseded for comparisons claiming aligned hash-move settings. A new depth-four
baseline and IID matrix with hash-move ordering enabled in both engines follow
below. Neural-core equality checks remain unaffected.

The IID step kept aspiration, LMR and all preceding controlled features on
in both engines, with reverse futility unchanged. Four depth-four 100-game/
50-pair profiles used the same reserved opening endpoints:

| IID enabled | Maxima W/D/L | Maxima score | Median search visits Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 28/58/14 | 57% | 4062 / 3028.5 |
| Maxima only | 29/57/14 | 57.5% | 4076 / 3054 |
| SF13 control only | 28/58/14 | 57% | 4062 / 3028.5 |
| Both engines | 29/57/14 | 57.5% | 4076 / 3054 |

One draw became a Maxima win when its IID was enabled. Both-enabled total
visits were 28142027/24843078, versus 27634159/24356932 with IID off; game
positions and lengths differ. Median visits increased only about 0.3%/0.8%,
so this step did not produce a large tree expansion. On the same 50 opening
FENs, visits changed from 355075/232234 to 353042/232234, about 0.6%/0% fewer,
with no chosen-move changes. These shallow results do not establish timed Elo.

In the both-enabled games, Maxima made 886 helpers, installed 768 legal hints
and visited 94540 positions inside helpers, already included in its normal
visit count. The opponent made 14 helpers with 14 visits and installed no hint.
Existing host move ordering is retained: the quiet Maxima control excludes
TT lookup, whereas the opponent retains hash-move ordering. The same IID
eligibility rule therefore does not imply equal helper activity. Every helper
used remaining depth four and probe depth two. The opening sample triggered
two Maxima helpers and none in the opponent. A separate cold-table diagnostic
searched those 50 openings without the first three root iterations and
exercised four opponent helpers with independently legal hints; that diagnostic
binary and its results are excluded from all games and paired-root comparisons.

Independent audits passed all 400 games, 43630 legal PGN plies and 37230 game
searches, with legal PVs, requested depths, identities, feature guards and
work equations checked. The IID-off profile reproduced the preceding 100
aspiration-baseline game records and core move traces exactly. Another 500
opening searches passed, including 100 exact depth-four baseline traces and
100 unchanged depth-three traces with IID enabled but inactive. No unfinished
games, forfeits or engine errors occurred. Ordinary Release checks passed
36/36, audit Release 37/37, and focused ASan/UBSan/leak checks passed 2/2.
Protocols, frozen binaries, source patches, traces and independent audits are
private under `.context/baselines/2026-10-14/iid-control/`, reserved from
training. These results retain the unequal hash policy described above.

The replacement depth-four comparison enables legal main-search hash moves in
both engines, while keeping TT score cutoffs and cached search-score
substitution off in both. Qsearch hash moves remain off. All matches use the
same pure NNUE model, reserved 50 paired openings and preceding search features;
only IID varies. Native TT replacement and root ordering remain unchanged.

| IID enabled | Maxima W/D/L | Maxima score | Median search visits Maxima/SF |
| --- | ---: | ---: | ---: |
| Neither engine | 26/61/13 | 56.5% | 3627 / 3026 |
| Maxima only | 26/61/13 | 56.5% | 3627 / 3026 |
| SF13 control only | 26/61/13 | 56.5% | 3627 / 3026 |
| Both engines | 26/61/13 | 56.5% | 3627 / 3026 |

All four profiles have identical game records and every searched move, score
and PV. Both-enabled total visits are 28159143/29032517. Maxima installs
1357526 legal hash hints and searches 1308660 first; the opponent installs
996663 and searches 906605 first. Neither records a TT score cutoff. IID
makes only 26/13 probes, each visiting one position and producing no hint,
with remaining depth four and probe depth two. It has no playing effect in
this sample; broader eligibility starts at depth five. The previous 886-to-14
probe disparity was caused by unequal hash-ordering settings.

On the same 50 opening FENs, enabling Maxima hash ordering reduces its visits
from 353042 to 315689 (10.6%); the opponent remains at 232234. One Maxima
chosen move changes. All four aligned IID profiles have identical opening
traces and no IID calls. Median paired Maxima/SF visit ratio is 1.405; game
medians describe different positions and must not be used as a same-position
work ratio. The aligned control is the baseline for subsequent depth steps.

Independent audits passed all 400 games, 47012 legal PGN plies and 40612 game
searches, with identities, legal PVs, requested depths, hash/IID guards and work
equations checked. Another 700 opening searches passed, including 100 exact
legacy depth-four traces and 100 unchanged aligned depth-three traces with
IID enabled but inactive. No unfinished games, forfeits or engine errors
occurred. Ordinary Release passed 36/36. Audit Release passed 37/38 initially;
one lifecycle timing check exceeded its one-second budget by 54 ms, then the
entire lifecycle suite passed in isolation. All 38 checks passed across those
runs. Focused ASan/UBSan/leak checks passed, including live hash-ordering and
cancellation coverage. HCE is covered only by shared-search correctness tests;
all playing comparisons remain pure NNUE. Protocols, frozen binaries, source
patches, logs and independent audits are private under
`.context/baselines/2026-10-14/hash-aligned-iid/`, reserved from training.
The subsequent depth-five baseline retains every established flag, the same
frozen engines, pure NNUE model and 50 opening pairs. Only requested depth
changes. Maxima scores 33 wins, 63 draws and 4 losses (64.5%), compared with
26/61/13 (56.5%) at depth four. Both engines deepen, so this is a diagnostic
comparison rather than an isolated feature gain or Elo result. Independent
audits pass all 100 games, 12925 legal plies and 11325 searches at completed
depth five; no unfinished games, forfeits or engine errors occur.

On the same 50 opening FENs, total search visits rise from 315689/232234 at
depth four to 809319/617296 at depth five (Maxima/SF). Each engine changes
19 chosen moves. The median paired Maxima/SF visit ratio changes from 1.405
to 1.365. Another 200 searches pass independent audits, including exact
reproduction of all 100 preceding depth-four traces. Neither engine calls
IID on these opening roots, despite broader depth eligibility. During the
games IID makes 96/551 calls, supplies 15/404 legal hints and visits
772/62869 positions in its helpers; remaining depths range from four to five.
Equal feature settings do not imply equal TT hits, move ordering or trees.

Game search visits total 78278264/82249420, with medians 8365/7331.5 over
different game positions. LMR operates at remaining depths three and four,
with 3176286/2935502 reductions and 33520/21690 full-depth re-searches.
Aspiration has 3720/4339 failed attempts across depths four and five, all
retried without cancellation. Decisive previous scores can bypass an
aspiration window; auditors allow that full-window fallback while requiring
completed depth five. Peak main-search plies are 13/12 and quiescence plies
28/42. Legal hash hints remain enabled, and TT score cutoffs remain disabled.

Maxima's existing endgame null-move verification requires more than five
plies remaining and searches the original position at `depth - 6`, with null
moves disabled in that subtree. Current one-ply check extensions preserve
remaining depth, so this verifier remains inactive at nominal depth five.
The matched feature switches do not imply identical pruning policies. The
private SF control retains reverse futility through remaining depth eight,
with a margin of `112.5 * (depth - improving)` cp; Maxima uses `100 * depth`
cp only through remaining depth three. Ordinary non-PV nodes with four plies
remaining become reachable at nominal depth five, allowing an additional SF
pruning level. The control also retains its native null-move guards and
adaptive reduction. ProbCut, native internal reductions, adaptive LMR,
main-search history/SEE pruning and singular extensions are explicitly disabled.
A source and runtime investigation of these differences is separate from the
unchanged baseline. A review of basic verified-null alternatives is also
separate. Frozen protocols, traces and successful audits are private under
`.context/baselines/2026-10-15/depth-five-baseline/`, reserved from training.
A subsequent fresh-position depth-five run exposed a material-cache collision:
a descendant replaced the cache entry still referenced by a parent with a
cached evaluation. The NNUE value and accumulators were correct, but retained
material metadata belonged to another position. That entire partial match was
invalidated. The repair stores completed pawn/material metadata in each search
position, keeping the live pawn entry available until shelter evaluation has
finished; both cache hits and misses publish an owned snapshot. Search rules,
options and network weights are unchanged.

The repaired engine reproduces all 43 Maxima searches preceding the abort,
then completes the formerly failing root at depth five. More importantly,
all 5673 recorded Maxima searches across the original 100 depth-five games
reproduce exactly in move, score, PV and work counters; SF is unchanged.
The original 64.5% result is therefore retained for that sample. This review
covers depth five only; earlier depth results have not been revalidated for
this defect. Final ordinary Release passes 36/36, Audit Release 38/38, and
focused ASan/UBSan/leak checks 3/3. All printed HCE components match the original
binary over 100 evaluations of the 50 new FENs. Forced cache replacement tests
cover HCE/NNUE and cache misses/hits. A short sequential 30-pair 50k-node check
per evaluator measures median repaired/original time per node of 0.900 for HCE
and 0.949 for NNUE; this is a local speed check, not playing-strength evidence.
Repair provenance, failed attempts and checks are retained privately under
`.context/baselines/2026-10-15/depth-five-metadata-fix/`.

The repaired depth-five pair then completes one fixed fresh sample of 100
games from 50 positions, each with engine colors reversed: 25 middlegames and
25 endings (10 rook, 10 minor and 5 queen endings). Starts come from the CC0
official Stockfish development books pinned at
`65815ccdbc7727cd4f6aee252ba8f67fb740e92f`. Selection seed 20261016, structural
phase filters, exclusion of 40105 earlier played position keys and an independent
SF19 cold 50k-node finite absolute-score screen of at most 150 cp were frozen
before games. The repaired run reuses exactly the first attempt's starts and
order; no input is replaced based on its outcome. Both engines request depth
five with the original model, options and SF binary. Maxima contains only the
metadata ownership repair.

| Depth-five sample | Maxima W/D/L | Score | Maxima search visits | SF search visits |
| --- | --- | --- | --- | --- |
| Original opening sample | 33/63/4 | 64.5% | 78278264 | 82249420 |
| Fresh sample, all 100 | 15/75/10 | 52.5% | 57063295 | 53290471 |
| Fresh middlegames, 50 | 8/34/8 | 50.0% | 41047739 | 40104350 |
| Fresh endings, 50 | 7/41/2 | 55.0% | 16015556 | 13186121 |

Visits sum positive-depth and quiescence entries in both engines. Independent
audits pass all 100 games, 9270 legal plies and 9270 completed depth-five searches,
including input/source/binary/model/options identities, legal PVs, terminals,
pairing and work equations. No crashes, unfinished games or forfeits occur.
No SF same-position null-verification entries occur in this sample. Castling
search counts are zero because every selected start has no castling rights.
The original failed attempt and a superseded repair attempt are excluded entirely.
The fresh sample does not reproduce the 60%+ score. Its deliberately different
phase mix tests generalization, rather than an identical-distribution opening
repeat; it neither proves the original score was luck nor establishes Elo.
No result-dependent extension or new feature experiment is added. Protocols,
source archives, frozen inputs/binaries and audits are private under
`.context/baselines/2026-10-15/depth-five-fresh-sample-fixed/`, reserved from training.
The subsequent depth-six checkpoint retains the exact repaired Maxima and SF
binaries, pure NNUE model, options and the fresh 50 FENs in the same order.
Only both requested depths change from five to six. ProbCut and other deferred
heuristics remain disabled. Null move remains Maxima's fixed three-ply reduction
plus the pass, with its established 50 cp cushion and safety guards; SF retains
its native adaptive null policy. At an ordinary remaining-depth-five node,
Maxima's null trial now searches one positive-depth ply. Maxima's endgame null
verification still cannot activate at nominal six under the existing root and
check-extension rules. This is a sequential checkpoint, with no feature change.

| Same starting sample | Depth | Maxima W/D/L | Score | Maxima search visits | SF search visits |
| --- | --- | --- | --- | --- | --- |
| All 100 | 5 | 15/75/10 | 52.5% | 57063295 | 53290471 |
| All 100 | 6 | 24/73/3 | 60.5% | 218237855 | 152606533 |
| Middlegames, 50 | 5 | 8/34/8 | 50.0% | 41047739 | 40104350 |
| Middlegames, 50 | 6 | 13/35/2 | 61.0% | 135963286 | 90340539 |
| Endings, 50 | 5 | 7/41/2 | 55.0% | 16015556 | 13186121 |
| Endings, 50 | 6 | 11/38/1 | 60.0% | 82274569 | 62265994 |

The automatic game/audit pipeline passes all 100 games, 9057 legal plies and
9057 completed depth-six searches, including exact baseline identities/options,
legal PVs and work equations. No errors, unfinished games or forfeits occur.
LMR operates at remaining depths three through five; IID calls total 157/1770
and helper visits 2258/198145 at parent depths four through six (Maxima/SF).
Aspiration has 3466/5051 failed attempts at depths four through six, with no
cancellation. No same-position null-verification accounting entries occur.
Maxima uses 43.0% more total visits than SF in this sample. Aggregate game work
covers diverging positions, so it does not isolate an ordering/pruning cause.
This fixed-depth result is not an Elo estimate. Engine source is unchanged;
prior native validation applies to the exact frozen binaries. Protocols,
automation, frozen inputs/binaries and audits are private under
`.context/baselines/2026-10-15/depth-six-baseline/`, reserved from training.
Managed-time games remain the strength gate.

## 5. Build and debugging

The default build uses C++26 and requires a compiler that supports that mode
(tested with GCC 16), CMake 3.30 or newer, and Ninja. Engine lifecycle tests need
Python 3.7+ with its standard library. Configure with `-DBUILD_TESTING=OFF`
to build the engine without Python or test targets. The separately packaged
runner needs Python 3.8+; see chapters 6.1 and 10.
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
70 installed three/four-piece Syzygy WDL/DTZ tables, and GCC runtime libraries.
The bridge translates single Windows drive paths for `EvalFile`, `BookFile`,
`ExperienceFile` and `SyzygyPath` to Linux `/mnt/<drive>/...` paths. In the GUI,
set `EvalFile` to `C:\dev\maxima\networks\nn-62ef826d1a6d.nnue` before
enabling `UseNNUE`; use `C:\dev\maxima\syzygy` for `SyzygyPath`, with
`SyzygyProbeLimit=5` and `SyzygyProbeDepth=1`. No five-piece tables are bundled.
`BookFile` is `C:\dev\maxima\book.bin` and `ExperienceFile` is
`C:\dev\maxima\experience\experience.bin`. HCE remains separately
available by turning `UseNNUE` off. The bundle's README lists the configuration.

Launcher source/build artifacts remain local under ignored `.tools/windows/`.
Copied assets are SHA-256 verified in the bundle's `SHA256.json`. Checks through
the copied Windows executable loaded NNUE/book/experience/Syzygy using Windows
paths, completed a legal depth-three search, and reported a Syzygy root hit.
GUI interaction itself requires the user's Windows desktop.

The bundled engine was byte-identical to the ordinary Release executable:
`-O3 -DNDEBUG -march=native`, search auditing off, with AVX2 NNUE instructions
verified in the object file. A short diagnostic on one fixed middlegame,
300,000 nodes and two trials per configuration, measured median reported NPS
of 1.15M HCE / 0.391M NNUE with a Linux temporary working directory, versus
1.05M / 0.388M with a Windows-mounted working directory. This is a small
throughput diagnostic, not a playing-strength result or a general speed ratio.
UCI logging currently opens and flushes a file for each line, so filesystem
and protocol latency still matter near clock exhaustion; the current clock
manager reserves only 50 ms. No timing or logging behavior was changed here.

GNU Chess 6.3.0 is also bundled locally at `C:\dev\gnuchess`; add
`C:\dev\gnuchess\gnuchess.exe` as a second UCI engine. It uses the same
WSL `Ubuntu` launcher approach and includes its Linux engine, small opening
book, GPL license and launcher source. The copied executable completed a legal
depth-three UCI search. No measured Elo is claimed for this particular build.
For comparisons, use equal time/hash settings and disable `OwnBook` on both
engines unless supplying a controlled opening book.

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
  --model EvalFile=networks/nn-62ef826d1a6d.nnue --option UseNNUE=true
```

Repeatable `--option NAME=VALUE` and `--model OPTION=PATH` select the evaluator
or search settings. Models are loaded before enabling evaluator options, hashed
in the report, and checked for changes before and after each search. Requested
options must be advertised by the engine. Use identical budgets and settings
for HCE/NNUE comparisons; `--option UseNNUE=false` explicitly selects HCE.

The default is `modern_2026`: 22 positions sampled from the 2026 Tata Steel
Masters broadcasts, one second per position and 64 MiB hash. Stockfish 19
labels were checked with two cold, single-thread MultiPV searches at two million
and eight million nodes. Targets must remain stable with at least a 50 cp margin
over the runner-up at both budgets and a final completed depth of at least 18.
The packaged manifest records sources, hashes, options, depths, scores and legal
PVs; source PGNs and offline provenance checks accompany it. Rebuild the corpus
with `runner/tools/build_modern_suite.py`; see the bundled suite README for the
exact command and limitations. JSON run reports include corpus and executable
SHA-256 hashes; a changed corpus or executable rejects the run report.

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
threefold/fifty-move claims are adjudicated by the independent board. There is
no evaluation-based adjudication. `--max-plies` (default 600 searched plies,
excluding the opening) leaves a nonterminal game unfinished, with result `*`.
`--timeout` bounds each protocol response (default 30 seconds); increase it for
long clocks. Protocol/illegal-move failures preserve the current game's partial
PGN and error record, stop the match, and return nonzero. Completed game records
are flushed as they finish; abrupt interruption may lose the active game.
Output paths must be new and distinct from each other and inputs.

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

Reports record hypotheses/error targets, model/provenance, five paired counts,
LLR, thresholds and decision. SPRT reports omit fixed-sample confidence intervals;
those are not valid after optional stopping. This mode is mutually exclusive
with Bonferroni checkpoints. Omitting both retains fixed-sample operation for
baselines and smoke checks. SPRT remains available for small-gain hypothesis comparisons;
existing historical match decisions retain their predeclared methods.
The same SPRT mode works with `--variant losers`: our independent royal-losers
board remains responsible for compulsory captures and variant terminal rules.
The official fast-chess v1.8.2-alpha executable supports standard/Chess960 only;
passing `Wild=losers` to an engine would not change its arbiter's rules. The
statistics port itself is independent of chess rules, so both variants can use
our existing match pipeline.
The isolated [fast-chess port](runner/maxima_runner/third_party/fastchess/README.md)
uses the upstream constrained multinomial likelihood, with a more precise
bisection solver and independent pinned Fishtest reference fixtures. Runtime
needs only the existing runner dependency. MIT attribution/license is bundled.
See [Fishtest mathematics](https://github.com/official-stockfish/fishtest/wiki/Fishtest-mathematics)
for the paired generalized SPRT model.

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

### 6.2 Bug audit and sanitizer checks

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
`testEvaluation` additionally checks the material lookup at sixteen pawns and
above its supported external range; lifecycle tests reject those FENs before
evaluating and searching the preserved position.
`test_search_tt_context` checks each king-attack weight independently and together
in both directions, all TT bound types, unchanged/rejected-setting reuse, and
draw-contempt context changes. Lifecycle tests compare completed warm and cold
UCI search scores after king-attack option changes and verify that changing either
weight safely stops an active search before another search begins.
`test_null_search` instruments artificial passes and endgame verification,
including consecutive-null exclusion, verification subtree exclusion, failed
verification, cancellation, reserved scores, pawn-endgame bounds, and state
restoration. Reserved-score tests inject search results through the virtual
PVS boundary and require no tablebase files.

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

`test_search_selectivity` observes production move-futility and LMR decisions
through synthetic child contracts at the virtual PVS boundary. Both null and
full PV windows cover reduced improvements, full-depth re-search, cancellation
and restart. Parent internal iterative deepening is isolated by the test
contract; real-search coverage remains separate. Independent
python-chess certificates supply both colors' legal moves, checks, captures,
promotions, castling and advanced passed pawns. It checks futility margins,
depth/check/endgame/mate exclusions, first-move retention and gain-based tactical
pruning; protected tactical improvements must still reach a cutoff. LMR checks
early and late move depths, dangerous-move exclusions, full-depth verification
of a refuted reduced improvement, cancellation in either pass and restart/state
restoration. These isolated guard probes disable node pruning and TT; they do
not certify selective-search equality, evaluation or real quiescence. Verify
data with `python tests/data/search/generate_selectivity.py --check`; native
tests need no python-chess installation.

`test_nnue_hash_ordering` seeds poisoned exact scores with legal ordering
hints for both colors and evaluators. It verifies that deep, shallow, context-
and clock-mismatched entries can order a move while their scores never cut off
the controlled search, and that legal hash moves suppress IID. It also covers
disabled ordering, invalid hints and fixed-mode exclusion. Real HCE and NNUE
searches cover warmed ordering hints, unchanged root evaluation and restoration
after node-budget cancellation.

`test_search_iid` controls only the shallow helper and child scores while
running the production IID preparation and main PVS path. It checks both colors,
remaining/probe depth, first-move ordering, hash/disabled/non-PV exclusions,
suppression of nested IID, full-depth continuation after a shallow mate score,
cancellation, absent hints and frame restoration after a helper exception.
Live audit tests cover HCE and NNUE, depth-three inactivity, depth-four helper
work accounting and cancellation inside a real helper.

`test_search_aspiration` exercises the production root driver with controlled
child scores for both colors. It checks fail-high, fail-low and alternating
failures, unchanged nonfailed bounds, full-depth retries, decisive-score
fallback, cancellation and board/hash/stack restoration. A first-move fail-low
must still search an improving alternative. Interrupted fail-low work must omit
an unproven root score. Live audit tests cover HCE and NNUE, depth-three
inactivity and depth-four activation with LMR enabled.

`test_search_horizon` exercises real quiescence at depths 0/-1/-2 and PVS at
depths 0/1 on independently certified capture, recapture, promotion, en passant,
check-evasion and mate trees, including reversible clock 99. Its exhaustive
reference uses certified legal/tactical move sets and terminal scores, Maxima's
static stand-pat labels and its existing SEE-based check extensions. It has no
alpha-beta pruning, TT or move ordering; it does not independently validate
evaluation or SEE. The sparse fixtures assert the endgame exclusion from
quiescence capture pruning. Full/null windows, selective options enabled and
disabled, cold/warm/disabled TT, incompatible deep entries at quiescence entry,
real repetition histories, node-budget restart and state restoration are
checked. This is bounded search-integration coverage, not a general proof of
selective search. Verify certificates with
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

Royal-losers playing evaluation deliberately remains material-only. The old
positional gate required material flag 16, which its classifier never produces.
The first restoration experiment added the existing pawn/king tables to ordinary
classes 3, 12 and 15, preserving specialized near-win material scores. Against
frozen ordinary Release `81f13b0`, 32 color-swapped opening pairs at 10+0.1 seconds
scored 27 wins, 14 draws and 23 losses (53.125%; conservative 95% opening-pair
interval 29.12–77.13%). This small screen did not establish a gain, so the
candidate was reverted under its predeclared rule. It does not prove the tables
are harmful. NNUE promotion requires separate correctness and match evidence.

The unused W17 positional tables and helper have been removed. Its future
positional evaluator will be a separately trained royal-losers NNUE. Standard
chess retains its existing classical evaluation as the default and comparison
baseline until standard NNUE passes correctness and measured strength gates.

`test_w17_evaluation` checks active material-only scores, classifications and
phase, both colors, cold/warm material caches, symmetry of the covered fixtures,
standard/losers material-cache separation, and board/hash/stack preservation.
King-only search terminals still precede static evaluation and draw clocks.

An address/undefined-behavior audit build can be configured independently:

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

Private audit records and benchmark artifacts live under `.context/baselines/`.
Record the revision, dirty-tree status, binary hashes, positions, limits, build
configuration, and commands. A short benchmark does not establish a performance
or playing-strength gain. The 2026-10-10 audit adds zero-time, root-restriction,
book-shortcut, tiny-node, and losers terminal/draw regression cases.

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
storage without downloading table files. `--directory /path/to/tables` selects
another destination; the default `tablebases/syzygy/` is ignored by Git.
Downloads check the mirror's published SHA-256 hashes, replace files atomically,
and verify/reuse completed files when rerun. The download command is part of
the Python 3.8+ runner and uses only the standard library; see chapter 6.1.
Running the compiled engine requires no Python.

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
