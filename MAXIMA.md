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

Maxima2 is a C++26 UCI chess engine with bitboard move generation, alpha-beta
search, NNUE evaluation, and experimental royal-losers support.

### Goals

- Improve playing strength, correctness, performance, and reliability.
- Make clean, understandable code a first-class project goal. Preserve clear
  control flow, explicit state ownership, and coherent interfaces as the engine
  gains capabilities.
- Preserve the engine's focus on speed: efficient chess algorithms, high search
  throughput, and compilation tuned for the target CPU.
- Improve NNUE evaluation with correctness tests and measured playing-strength
  gains guiding model and inference changes.
- Standard chess uses NNUE exclusively after passing the equal-time HCE
  comparison gate. The classical evaluator and its caches are removed; retain
  required search metadata, pruning guards, exact draws and tablebase integration.
  Keep a frozen HCE executable for historical comparisons.
  The agreed strength gate is NNUE at least as strong as pure HCE, rather than
  a required positive Elo margin. Use the runner's `--min-elo 0` simultaneous
  confidence rule: pass only when the lower score bound exceeds 50%; a cap
  without a crossing is inconclusive. Exact equality cannot be established by
  a finite match. After a pass, remove HCE in one coherent cleanup, preserving
  independently required search metadata and exact endgame/draw handling;
  incremental HCE removal stages are not requested.
  Defer dual-network routing (cheap material-based selection or a small-network
  screening pass followed by a larger network) and related inference experiments.
  Accept some neural evaluation weaknesses in unusual endgame imbalances;
  tablebases help only for covered material sets within the available piece limit.
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

Search owns its configuration and pre-root repetition history. At the actual
root, repetition requires three occurrences. Below the root, the first earlier
match in either game or search history scores as a draw heuristic. Repetition identity excludes en passant squares when no
legal capture is available. Irreversible moves and null moves bound history scans.
Checkmate takes precedence over a fifty-move draw. Standard quiescence detects
stalemate at every tactical depth, including before a stand-pat cutoff; absence
of captures or promotions alone does not imply stalemate. Standard interior
search also verifies legal play before static beta or null-move pruning can
run, so stalemate remains an exact draw with production pruning enabled. The
probe uses a king escape first and fresh move generation when needed, without
changing the active move picker.

`CheckEvasions` is a default-on standard-search checkbox. At checked picker
entry, compute a destination mask for non-king moves: capture the checker or
block its ray in single check, and generate only king moves in double check.
The existing staged capture, promotion and quiet generators apply this mask
before emitting moves; cached TT, killer and countermove hints use the same
restriction. King moves still require search-owned final legality. En passant
remains conservative in single check because its captured pawn is off the
destination square; final legality handles pins and discovered attacks. Castling
is excluded from checked lists. The mask belongs to the search frame and is
restored with same-position verification state.

Each worker snapshots the option. Changing it stops the worker and clears both
TT modes. With it off, the picker retains unrestricted pseudo-legal generation
and search still rejects illegal moves. Ordinary move generation and terminal
probes start unrestricted; royal losers keeps its existing path. Audit builds
report `audit_evasions nodes` and `double_check` picker-entry counts. These are
candidate restrictions, not selective score cutoffs. Picker stage identifiers
consistently use lowercase `snake_case`; "noisy" means capture or promotion,
and quiet checks remain quiet moves under that classification.

Check-evasion validation passed 42/42 Debug and 42/42 Release checks, including
both lifecycle suites, plus six focused ASan/UBSan checks (leak detection off)
and six audit checks. Direct legal-set comparisons covered 2,566 positions;
the independent rules/state oracle covered 4,372 positions and 95,734 legal
children across both variants. All 288 depth-eight probes had legal results;
96 disabled-control signatures matched the parent exactly in nodes, scores,
best moves and full PVs, with LMR on/off and cold/retained history.

A separate same-binary depth-four perft comparison preserved 4,315,290 leaves
and reduced emitted candidates from 4,503,566 to 4,463,759. It includes legality
and make/unmake work, rather than isolating raw generator throughput. Enabled
search nodes changed by +1.67%/-0.74% with cold history and -0.61%/+0.62% with
retained history (LMR on/off). Short CPU samples varied substantially; these
measurements establish neither playing strength nor a general speed result.

`UpcomingRepetition` is a default-on standard-search checkbox. At descendants,
when `alpha < draw_score()`, search checks whether one legal reversible non-pawn
move reaches an earlier position. An immutable two-choice hash table maps key
differences to possible moves; full legality and the resulting repetition key
confirm each candidate. Scans respect available game/search history, the reversible
clock and null-move boundaries. The existing first-match descendant draw policy
applies to both pre-root and in-search matches; the actual root still uses its
normal move search and three-occurrence adjudication.

A confirmed move establishes a draw lower bound before TT lookup and selective
search: raise alpha, cut off if it reaches beta, otherwise continue for a better
score. Preserve that floor through fail-soft returns and reject TT scores below it. A selected draw can
supply a one-move PV; a higher cached score or stand pat cannot use that drawing
move as its PV. Propagate the minimum clock required by the historical match.
Early proof cutoffs do not train ordinary history or store ordinary TT results.
Verification helpers and fixed/aligned audit horizons disable this mechanism.
Ordinary repetition, fifty-move and terminal adjudication remain mandatory with
the checkbox off and at every alpha. Royal losers retains its existing behavior.
Each worker snapshots the option; changing it stops the worker and clears both
TT modes. Audit builds expose `audit_repetition` attempts, hits and cutoffs, with
separate quiescence hit/cutoff counts.

Upcoming-repetition validation passed 41/41 Debug and 41/41 Release checks,
six focused ASan/UBSan checks (leak detection disabled), and six audit checks.
Tests cover all 3,668 lookup edges in both directions and agreement with an
exhaustive legal-move repetition oracle over 320 positions. A quiet-escape
qsearch control changes from -233 cp with detection off to draw with it on.
All 288 depth-eight comparison searches completed with legal PVs; 96 paired
disabled-control signatures matched the parent exactly, with LMR on/off and
cold/retained history. Enabled detection changes nodes in different directions
across these controls; variable CPU samples establish no speed or strength gain.

Standard PVS carries an explicit expected-cut-node flag separately from the
window and returned TT bound. PV children and open-window re-searches are not
expected cut nodes; non-PV children alternate expectations, while reduced scout
searches predict a cutoff. Null children alternate and same-position verification
retains its caller's expectation. Royal losers ignores this standard-only context.

Each standard search frame also tracks whether its static evaluation improved
since the nearest valid same-side position two plies earlier, falling back to
four plies when needed. Equality, missing evaluations and checked positions do
not count as improvement. Artificial null passes block comparisons across their
boundary. Tracking reuses cached static evaluations without extra evaluation
calls; it never uses TT search scores. Child entry and root reset clear stale
trends, and same-position helpers restore their caller's state. Adaptive LMR
uses the improving flag; other pruning policies retain their own guards.

`LMP` and `HistoryPruning` are independent default-on standard-search controls.
At non-PV descendants outside check, a completed legal alternative permits
pruning ordinary quiet moves. LMP's searched-move budget is
`(3 + depth * depth) / (2 - improving)` and applies when the predicted
LMR-adjusted depth is below nine. History pruning applies below seven and
requires learned quiet history strictly below `-2048 * depth`; equality and
neutral history are retained, and killers are protected from history pruning.
The predicted depth is `depth - reduction`, using the same current depth,
move count, history and node context as the subsequent reduced search. With
LMR disabled, this is the unreduced depth. These constants are provisional.

Each move is considered separately: later quiet checks remain available and
picker bonuses never substitute for learned history. Captures, promotions,
castles, advanced passed pawns and singular hints are protected. Require
nonpawn material and a nondecisive window/best alternative; skip IID, verification,
excluded searches, artificial null subtrees, repetition floors and clock 99+.
Confirm legality before a prune and restrict its TT clock domain to at most 98.
Pruned candidates neither advance searched-move counts nor train history.
The first legal move and all legal terminal adjudication retain their contracts.
Both switches are worker snapshots; policy changes stop the worker and clear
both TT modes. Royal losers and artificial audit horizons disable both.
Audit builds expose `audit_quiet_pruning` candidate counts and legal prune counts;
overlapping prunes are attributed once to LMP. Generator batching is unchanged.

Standard LMR varies the reduction with depth, searched legal move count,
learned history and node context. `ContextLMR` defaults true under the `LMR`
and `AdaptiveLMR` masters. At depth three and above, preserve the first interior
move and first two root moves. All move types may receive reduced trials;
checks, evasions, captures/promotions, castling and advanced passers are no
longer blanket exclusions. Singular hints remain protected. Verification/IID
helpers retain their previous eligibility and excluded-move probes never reduce.

The immutable base table is `floor(1 + ln(depth) * ln(searched_moves + 1) / 3)`,
with indices saturated at 63. Start with one extra ply for an expected cut node
and one when not improving; subtract one for a current PV node and
`clamp(learned_score / divisor, -2, 2)`. The divisor is 4096 for tactical moves
and 8192 for quiets, whose learned score combines several components. Read it
in the parent position; material, SEE and picker bonuses are excluded.
Contextual policy adds another cut-node ply, one for a legal tactical TT hint,
and `min(alpha_raises / 2, 2)`. Subtract one each for persistent/current PV
provenance, giving check, evading check, a tactical move, a quiet killer or
countermove, and an advanced quiet passer. Subtract up to two more plies for
correction uncertainty, `min(abs(corrected_eval - raw_eval) / 100, 2)`.
Compute correction and validate a tactical hint once per node, outside the move
loop. Root metadata/correction uses one cached evaluation per iteration when
eligible and outside check. The final reduction stays in `[0, depth - 2]`;
check/singular extensions are applied separately. No LMR-created extensions
or score-dependent shallower verification are used. These round constants are
provisional policy choices, not measured playing-strength gains.

Persistent PV provenance occupies existing TT-entry padding: entries remain
40 bytes. Preserve it when retaining a deeper bound; replace it with a replaced
score, and clear it on eviction. Move-only stores and raw-evaluation attachment
cannot rewrite it. Only a main-search bound with matching position, repetition
context and clock domain supplies this heuristic, even when too shallow to
supply a score cutoff. A qsearch or no-bound entry supplies none. Provenance
never implies an exact score and does not change TT cutoff conditions.

`ContextLMR=false` restores the preceding adaptive interior policy exactly:
first three moves, non-dangerous quiets only, no extra context modifiers and
no root reductions. That same eligibility applies with `AdaptiveLMR=false`,
which restores fixed-one reductions. Both retain the positive-child-depth clamp.

Every reduced result above alpha is re-searched at the original full child
depth and original window, including a reduced result already above beta.
Both passes contribute reversible-clock constraints; cancellation restores the
parent and cannot authorize a cutoff or feedback. A completed verification may
teach continuation history at one, two and four plies: penalize a refuted result
at or below alpha and reward a result at or above beta. An in-window result has
no additional feedback. Use the existing bounded gravity with bonus
`+/-16 * min(depth, 8)^2`; ordinary cutoff learning still applies independently.
This feedback changes neither main nor pawn history. It excludes IID,
verification/excluded searches, null subtrees, repetition floors and cancelled
searches. Each continuation component respects its own enabled policy and root/
null context boundaries.

`AdaptiveLMR`, `ContextLMR` and `LMRHistory` default true and are independent
worker snapshots. Changing any of them clears both TT modes. `AdaptiveLMR=false`
restores fixed-one interior reductions and disables contextual/root LMR;
`LMRHistory=false` retains ordinary cutoff-only history updates. Disabling
`AdaptiveLMR` and `LMRHistory` restores the fixed-one policy, including retained
histories. Additional verification feedback remains interior-only; root ordering
keeps its existing PV/subtree-effort policy.
`LMR=false` disables reductions and their feedback regardless of the new flags.
Royal-losers retains its current policy. Fixed/aligned diagnostic horizons keep
the existing fixed-one `NNUEAuditLMR` behavior and disable the new feedback.
See [CPW Late Move Reductions](https://chessprogramming.org/Late_Move_Reductions)
and [History Heuristic](https://chessprogramming.org/History_Heuristic) for
mechanisms and limitations; reductions remain selective search, not score bounds.

`BroaderFutility` is a default-on, standard-search policy switch under the
existing `BetaPruning` and `FutilityPruning` controls. Workers snapshot it;
changing it stops search and clears both TT modes. False preserves the old
reverse-futility depth1..3 `eval-100*depth>=beta`/return-eval policy and quiet
frontier `eval+300<=alpha` rule, including its material-defined endgame exclusion.
Royal-losers and fixed/aligned diagnostic horizons keep their existing policy.

Broader reverse futility covers interior non-PV depths1..8. Its margin is
`max(25,100*depth-100*improving_without_lower_value_threat+abs(correction)/2)`;
correction here is the difference between corrected and raw clock-scaled static
evaluation. The threat test uses opponent pawn/minor/rook pseudo-attacks on more
valuable own pieces, using the same threat model as quiet ordering. A validated
legal quiet TT hint vetoes this static cutoff. Return `beta+(eval-beta)/3`,
without substituting a TT score for static evaluation. Checked nodes, decisive
scores, missing own nonpawn material, IID, verification/excluded searches,
repetition contexts and clock99 are excluded. Verify legal play before cutting;
stalemate is a draw. Pruned bounds propagate a maximum compatible clock of98.
Current PV exclusion does not represent historical TT-PV status.

Broader move futility uses the existing estimated reduced parent depth. Ordinary
quiet moves require reduced depth<6 and
`eval+100*(1+reduced_depth)+learned_history/64<=alpha`. Nonchecking captures
require reduced depth<8 and
`eval+200*(1+reduced_depth)+victim_value+capture_history/64<=alpha`;
en passant includes its off-target victim. Require a searched legal alternative
with a nondecisive-loss score, ordinary non-PV interior search, own nonpawn
material and nondecisive alpha/beta. Protect promotions, checks/evasions,
castles, advanced passed-pawn quiet moves, IID/verification/excluded/repetition
contexts and clock99. Check each candidate's legality; preserve later tactical quiets
rather than disabling a picker stage. Retain the actual searched best score.
These provisional margins are selective heuristics, not proven bounds on neural
evaluation or measured strength gains. See
[CPW futility](https://chessprogramming.org/Futility_Pruning) and
[reverse futility](https://chessprogramming.org/Reverse_Futility_Pruning).

Outside check, quiescence searches captures and queen promotions with
`see(move) >= -60` and stops at the first noisy move below that bound; quiet
moves are not generated. A valid TT hint is still searched first. Futility skips
a nonchecking, non-recapture tactical move when `eval + 350 <= alpha` and the
move does not win material (`!see(move, 1)`); the best score rises to that
futility value. In check, the first evasion is always searched; after it only
noisy moves passing the quiescence SEE bound remain.

The exchange estimator checks recapturers against current temporary occupancy,
excluding captures that expose their king and king captures onto attacked
squares. It handles changing pins, en passant and promotion gains without
modifying board or search state. Equal-value attackers are ordered by
piece type and source square; recapturing pawns promote to queens. This remains
a least-valuable-attacker material model, with either side allowed to decline;
it does not search alternative move choices or positional compensation.

The overloaded SEE interface supports both full scores and threshold tests:
`see(move)` returns the material score, while `see(move, threshold)` returns
whether that score is at least the threshold. Both share one exchange routine
and its legality checks. The threshold path uses material bounds to stop when
the answer is determined, including possible recapture promotion gains; it
does not build or fold a complete gain list. Threshold arithmetic uses a wider
integer to handle extreme inputs safely. Qsearch and boolean
gain tests use this path; ordering that needs the numeric score retains the
full-score call. This is an implementation optimization, not a pruning-policy
change. Certificate tests compare both interfaces at equality boundaries,
extreme integer thresholds and a broad range of ordinary thresholds.

Standard null-move pruning excludes consecutive artificial passes and runs
endgame and deep-node verification with null pruning disabled throughout its subtree. The
setting and board/history state are restored before handling cancellation;
an interrupted verification cannot authorize a cutoff. Node pruning excludes
mate/tablebase score windows, and artificial null lines cannot supply reserved
mate/tablebase scores as cutoffs. Pawn-only positions retain the non-pawn-material
exclusion. These safeguards do not make selective search an exhaustive oracle.

`Razoring` is an independent standard UCI checkbox, enabled by default and
snapshotted by the worker. At non-root, non-PV nodes with 1–3 plies remaining,
a static score strictly below `alpha - 300 * depth` triggers entry qsearch with
the same null window. Only a completed, nondecisive result at or below alpha
returns a selective fail-low. A fail-high or decisive result resumes full-depth
search. The 300-cp-per-ply margin is provisional and untuned. Checks, endgames,
pawn-only sides, IID/verification/excluded searches, repetition floors,
mate/tablebase score windows and reversible clocks at least 99 are protected.
The caller frame is restored after qsearch, including on cancellation; clock
constraints merge back. Accepted results restrict compatible parent TT clocks
through 98. This return stores no main-depth TT bound; qsearch retains its own
mode-gated cache policy. Disabling razoring skips verification entirely. Fixed
and aligned diagnostic horizons disable it; royal losers retains its policy.
This heuristic can miss quiet tactical resources; correctness checks do not
establish playing strength. Changing the option stops the worker and clears TT.

Same-position IID and verification helpers restore the complete caller frame,
including its picker, PV, evaluation, repetition state, killers and local move
exclusion. Clock restrictions merge back conservatively; work and cancellation
remain visible. Verification also restores its caller's null-pruning, IID and
verification-mode settings on return, cancellation or an exception at the helper
boundary. Null verification disables artificial passes and IID throughout its
subtree. Verification subtrees neither probe/store ordinary main/q TT entries
nor train ordinary history, countermoves or killers.

Move-excluded verification filters one compact move only at its current node;
real and null child entry clear local exclusions. Its root skips whole-position
tablebase results and static/null/quiet-futility pruning and LMR. If no legal
alternative remains, it returns the search alpha as a fail-low result, without
inventing mate or stalemate. Actual draw adjudication retains precedence. The
helper supports the singular-extension policy described below; ProbCut remains
pending. Audit builds expose
`audit_verification` calls, excluded calls, cancelled calls and helper visits.

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

Late-move reduction follows the adaptive policy described above. The fixed-one
baseline remains available with `AdaptiveLMR=false`; disable `LMRHistory` too
for the previous cutoff-only learning policy. Both retain the same tactical
protections and full-depth verification. A reduced fail-low remains a heuristic
rather than a proof that the move cannot improve.

Standard quiescence searches captures and promotions at every depth, plus all
legal evasions when in check. Optional quiet checking moves and castling are
excluded, including TT hints. The picker defers quiet generation entirely at
quiescence nodes unless check evasions require it. Main search retains its full
legal move domain.

Main-search TT lookup accepts compatible exact scores in both PV and non-PV
windows. Non-PV lower bounds require `value >= beta`; upper bounds require
`value <= alpha`. All score reuse requires sufficient stored depth, matching
board and search context, and a clock inside the propagated validity interval.
Reused clock constraints enter the current stack and propagate on undo, while
mate/tablebase scores are decoded for the probing ply. Draw adjudication and
tablebase probing precede TT lookup. Incompatible scores may still supply a
matching-board move hint. Exact reuse at non-PV nodes avoids redundant search;
its integration alone is not a measured playing-strength gain.

TT storage keeps matching-board move hints when a new result has no move.
A supplied move can update the hint without replacing a protected score; score
context, clock range and mate-distance encoding remain attached to that score.
Move-only writes cannot overwrite scored entries or evict scored collisions.
Within the same context, clock range and age, a deeper entry survives a shallower
non-exact write. Exact results, new ages, changed score domains and equal/deeper
searches may replace the score. This is a provisional cache-retention policy.
Collisions use empty slots first, then move-only entries, then older and
shallower scored entries. Packed ages wrap modulo 64. These policies share the
existing storage between standard chess and royal losers without changing the
score-compatibility rules.

Quiescence TT entries retain separate entry and deeper tactical mode markers;
both now search captures, promotions and all legal check evasions. The mode
uses the two spare packed bits;
the raw-evaluation fields described below bring entries to 40 bytes. Scores are
reused only within the same mode: main
results do not substitute for qsearch bounds, and neither qsearch mode satisfies
a main-search probe or the other qsearch mode. Move hints cross modes after
validation and filtering to the requested move domain. Valid current main entries retain priority over
q writes in the same score domain; incompatible or aged domains may refresh.

`QsearchTT` is a UCI checkbox, enabled by default and snapshotted by each standard
worker. Disabled means no qsearch bound probes, move hints or bound stores;
raw evaluation reuse is controlled independently by `TTEval`. Changing it
stops the worker and clears cached results. Fixed and aligned diagnostic horizons
disable it to retain their independent search contracts. Royal losers retains
its existing search behavior. Reuse follows cancellation, ceiling, draw and
tablebase handling and propagates clock constraints. Completed beta cutoffs
store lower bounds, other nonterminal completions store upper bounds, and
certified mate/stalemate results may be exact. Interrupted searches store no
unfinished result. Qsearch clears stale PV/best-move state before probing.
Standard qsearch returns fail-soft scores: a completed fail-low retains its best
searched score or optimistic pruning bound below alpha, including for empty
playable tactical lists. Keep alpha separate for move-selection windows and
preserve any established repetition draw floor. This provides useful upper TT
bounds and the score magnitude needed by adaptive tactical verification.

Search-policy options carry their TT invalidation rule beside their declaration.
Changing `NullMove`, `AdaptiveNullMove`, `NullVerify`, `BetaPruning`, `FutilityPruning`, `LMR`,
`AdaptiveLMR`, `LMRHistory`, `Aspiration`, `IID`, `IIR`, `MainSEE`, `Razoring`, `SingularExtensions`, `ProbCut`, `TTHistory`, `TTEval`, `QsearchTT`, draw contempt,
variant or tablebase probing policy stops and joins the worker before clearing both main and quiescence TT
results and move hints. History controls and audit search policies follow the
same rule. Invalid values and unchanged numeric/checkbox policies preserve the
cache. Successful network loads and tablebase-path changes clear it separately.
Clock overhead and other non-search-policy settings retain cached results.

Standard search exposes independent `MainHistory`, `ContinuationHistory`,
`LongContinuationHistory`, `PawnHistory`, `CaptureHistory`, `Countermoves` and
`Killers` checkboxes, all
defaulting to true.
Each worker snapshots them before search. A disabled history table contributes
zero to ordering and receives neither rewards nor penalties. Disabled
countermoves and killers skip hint selection and learning; their moves remain
available through ordinary generation. Standard quiet/capture history and countermoves
remain worker-owned and persist across moves, including dormant entries while
a component is disabled. Re-enabling can reuse those entries; use `ucinewgame`
for a fresh-history comparison. Killers retain their existing search lifetime.
Royal losers keeps its legacy ordering and learning behavior.

Baseline-control validation passed 40/40 Debug and 40/40 Release checks,
five focused ASan/UBSan checks (leak detection disabled), and six audit checks.
All 32 history-control combinations passed controlled scoring/learning tests
and 128 legal depth-six searches. Frozen-parent comparisons matched all 72
paired depth-eight signatures, including nodes, scores, best moves and full PVs,
with both cold and retained history. Short CPU samples had higher candidate
medians (6.25% cold, 9.82% retained), mixed paired directions and substantial
variation; they establish neither a speed benefit nor a reliable isolated cost.
Playing-strength acceptance remains outstanding.

Standard quiet ordering adds parent-position threat context after the earlier
TT, good noisy, killer and countermove stages. A minor uses enemy pawn attacks;
a rook also includes enemy minor attacks; a queen also includes enemy rook
attacks. Escaping earns 4000/8000/12000 ordering units respectively; entering
loses the same amount, and remaining threatened cancels. Pawns and kings have
no such adjustment. These are pseudo-attacks with current occupancy, including
pinned attackers; they guide ordering and do not certify destination safety.
The unions are local to the deferred quiet batch, with no persistent board cache.

Quiet checks, including discovered checks, earn 10000 ordering units only when
`see(move, 0)` passes. Castling retains its learned score without either context
adjustment because it moves two pieces. These provisional constants are not
tuned strength claims. Context scores affect the existing positive-quiet stage
and selection within quiets; they do not change good-noisy SEE classification,
move generation, final legality, or pruning. Available
qsearch quiet checks/evasions use the same scoring without expanding their domain.
`move_history_score` remains learned history only, so contextual bonuses do not
leak into history-based search decisions or get learned as rewards.

`ThreatOrdering` and `CheckOrdering` are independent UCI checkboxes, default true.
Workers snapshot them; changed settings stop/join search and clear incompatible
TT results. Disabling both restores the prior learned-only quiet scoring.
They allocate no tables and leave learned-history persistence/reset unchanged.
Royal losers retains its own picker and both worker flags are false.

An exact TT return can shorten the searched PV because an entry stores a score
and move rather than a complete continuation. Search clears the stale PV before
reuse. UCI text may append TT move hints to a short searched prefix; those hints
are not a completed deeper search or a guaranteed line of the cached score.
Ponder selection uses the searched prefix and can therefore be absent.

Standard root search orders moves like an interior node: D1 keeps the move
picker's legal order, and each later
iteration reorders the root list by running the picker with the previous best
move as hash move. There is no second root sort and no initial SEE/danger bonus.
Royal losers retains its variant ordering. Maxima has no check extensions: the
general interior check extension and the audit `NNUEAuditCheckExtensions` rule
were removed (2026-10-22). Singular allocation is the only extension. Per-root ordering/complexity counters
use signed 64-bit values, add unsigned visited work with saturation, and compare
without subtraction. They are ordering estimates; the separate unsigned visited counter
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

The transposition table uses 40-byte entries: a full 64-bit position signature,
a 64-bit search-context fingerprint, packed move/score/depth/age/clock/mode
metadata, a full 64-bit evaluation-model fingerprint, a signed 16-bit raw
centipawn evaluation, a PV-provenance flag in existing padding, and alignment
padding. Move hints can survive a context mismatch, but score
cutoffs require compatible context and clock ranges. Standard chess includes
the ten-halfmove damping bucket in that context, so different counters within
a bucket can reuse scores, while other buckets retain only move hints. This
keeps repetition identity unchanged. It is an approximation: descendant bucket
boundaries can differ with the initial clock, even when the initial bucket
matches. Existing propagated terminal-clock ranges remain enforced.
Royal-losers score context
also includes the incoming mate-search mode (automatic, White, or Black), whose
horizon rules differ. A node keeps that identity when it changes mode or reduces
depth locally; incompatible modes retain move hints but cannot reuse scores.
Material and pawn caches and the transposition table still need synchronization
before parallel search.

`TTEval` is a default-on standard-search checkbox. After a miss in the existing
worker-local neural cache, reuse a raw TT evaluation only when the full position
key and immutable model fingerprint match. Cache centipawns before rule-50 damping
and correction history; every read uses the current clock and current correction.
Evaluation reuse is independent of score depth, bound, mode and repetition context.
A raw value never authorizes a search cutoff or supplies a move/PV. The existing
frame cache still holds the uncorrected, clock-damped score and rebuilds metadata.
Skipped inference leaves accumulators lazy under the existing ancestor contract.

Evaluation writes preserve matching search bounds and move hints. On a miss they
use a no-bound entry and cannot evict a scored collision; replacement by another
position invalidates the raw evaluation. Full model identity also protects retained
worker snapshots across model changes. At fixed Hash size, 40-byte entries hold
about 40% fewer positions than the prior 24-byte layout. Saved inference must be
measured against that capacity and lookup cost. The worker-local cache remains.

Each worker snapshots the switch. Changes stop/join the worker and clear both TT
modes. Disabling the TT also disables raw reads/writes; clear and resize discard
raw evaluations. Royal losers and fixed/aligned diagnostic horizons disable this
feature. Raw position/model evaluation may be reused in null, IID and verification
helpers without publishing their search bounds. Audit verification compares the
production raw cache with fresh independent inference; `audit_tt_eval` reports
TT probes/hits and production neural evaluations after local-cache misses. Full audit
inference retains its independent path.

KPK is an embedded win/draw bitbase. Proven draws stop search with the configured
draw score; winning positions retain search because KPK has no distance-to-zero
information. Tests independently verify all legal normalized KPK states.

### Standard NNUE bootstrap

Standard chess uses standalone NNUE evaluation, without classical blending or
fallback. Royal-losers retains its separate material-only evaluator. The HCE
strength gate passed on 2026-10-18; the comparison executable is frozen.
Standard static evaluation applies a separate progress policy to NNUE: ten-halfmove buckets 0–9, 10–19, …, 90–99 retain 100%, 90%, …, 10%
of the score. At 100 it reaches zero; actual draw detection and checkmate
precedence remain search rules. The uncorrected frame value is scaled once before
caching; correction history applies the same decay to its separate adjustment
on each read. Raw neural output and accumulators remain unchanged.

The initial target is pretrained CPU inference, without training or GPU hardware.

Bootstrap network: until Maxima trains its own network, it borrows the
Stockfish 13 network. The format is legacy HalfKP: 41024 inputs, two 256-wide
accumulators and 512->32->32->1 integer inference. This is a correctness and
bootstrap choice, not a commitment to this family.

CC0 weights: `nn-62ef826d1a6d.nnue`, full SHA-256
`62ef826d1a6d11b9e814188025aa02a60815c037292e0ef9bbb9bf4f724e5e63`, from
https://github.com/official-stockfish/networks.
Run `python3 tools/download_nnue.py` to fetch and verify them into ignored
`networks/`. Copy or link the downloaded file to `eval.nnue` beside `book.bin` for automatic
loading, or select it explicitly with `EvalFile`. The GPL reference probe used for
exact inference checks is recorded in `third_party/nnue_reference/README.md`.

Evaluation contract: standard scores are relative to the side to move, in
Maxima centipawns, clamped below the tablebase score range. The borrowed network
returns internal units at 208 per pawn: convert with `raw * 100 / 208`.
Raw/full/incremental inference is checked before this conversion. No separate
material, tempo or rule50 scaling is imported; any further calibration needs
separate strength evidence. Maxima's own networks should be trained or
calibrated to the 100-cp pawn convention in the model artifact itself. Keep the
format and inference clean, minimal and optimized. Internal quantization may use
powers of two; quantization precision is separate from chess score units.
Document each file's output calibration and quantization.

Search retains checks, terminal/draw/tablebase decisions and material/pawn
pruning metadata. HCE tempo, positional terms and endgame score adjustments
must not be added to NNUE scores. Search captures an immutable model snapshot;
mutable accumulators belong to that search. Evaluator/model changes must prevent
reuse of incompatible TT scores. Invalid loads must retain the previous model.
Correctness gates include full/reference/incremental equivalence, special moves,
null moves, restoration and cancellation. Measure CPU throughput and paired
playing strength before changing the model. Parallel-cache work is separate.

The default model is `eval.nnue`, beside `book.bin`. Initial `isready` loads it
before the chess clock starts. If a client skips readiness, search initialization
still requires a valid model. `MAXIMA_EVAL_FILE` overrides automatic discovery;
otherwise lookup checks the working directory, the executable directory, and its
parent (for the bundled `engine/maxima2` layout). An existing invalid file fails
explicitly. Missing models reject standard searches; there is no HCE fallback.
To select another compatible model:

```text
setoption name EvalFile value /absolute/path/to/network.nnue
isready
```

`UseNNUE`, `KingAttackShelter` and `KingAttackPieces` are removed. Empty or
unsupported `EvalFile` loads report a failure and retain the previous model.
Successful model changes stop/join active search and clear incompatible TT
entries. Search score context fingerprints the model and adapter version, so
retained snapshots cannot reuse another model's score bounds. The fingerprint
is a cache identity, not a cryptographic integrity check; the downloader verifies
the full pinned SHA-256. Royal-losers requires no standard NNUE model.

`src/nnue.cpp` owns the isolated HalfKP loader/inference implementation. Weights
are immutable, accumulators are allocated per search/ply, and updates are lazy.
A changed king refreshes its perspective; captures, promotions, castling and
en passant update the affected non-king features. Null moves exchange output
perspectives without changing features. Unmake restores the parent accumulator.
The loader checks exact size, version and architecture hashes before decoding;
wide affine sums and score clamps keep external weights outside reserved scores.
Native AVX2 feature accumulation and dense inference have scalar fallbacks.
Dense activations are unsigned bytes in their original perspective/lane order.
AVX2 processes 32 inputs with `vpmaddubsw`, immediately widening pair sums with
`vpmaddwd` before int32 accumulation. Clipping to [0,127] bounds every pair to
[-32512,32258], so the saturating instruction is exact even for extreme int8
weights. A 512-product dot fits int32; bias addition remains int64, followed by
the original arithmetic shift, clipping and output scaling. The network file,
loaded weight layout and UCI interfaces are unchanged.
There is no network I/O or Python dependency in the engine or its build.

`test_nnue` runs offline using synthetic weights for load validation, score bounds,
metadata, model lifetime, TT isolation and royal-losers preservation. Standard
search nodes own only the passed-pawn mask and the endgame pruning guard.
Directional bitboard fills reproduce the original passer mask, including its
exclusion of rear doubled pawns; the guard retains the pawn-count/mating-power
predicate. The classical material/pawn caches, their incremental keys, tapered
scores, PSTs, pawn weaknesses, king shelter and positional endgame adjustments
are removed. TT/repetition keys, exact KPK draws, Syzygy decisions and pruning
policies remain. Royal-losers computes its existing material formula directly.

Descendants cannot change a parent's owned values. Tests compare the required
metadata with classical evaluation and an independent square scan, cover legal
special-move children, null/undo, up to 24 games of 100 legal plies plus undo,
and 20,000 synthetic pawn/material configurations, and verify that NNUE metadata
preparation leaves classical cache entries intact. No additional cache or
search-stack fields were introduced. Pruning, reductions and extensions remain
unchanged.
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

The 2026-10-16 byte-dot optimization retained exact raw scores and every
accumulator lane against the independent reference. New 32/512-input tests cover
extreme signed weights, unsigned activation limits, unrestricted int32 biases,
rounding/clipping boundaries and scalar/SIMD agreement; a separate build with
AVX2 disabled also passed. The fused copy/update accumulator experiment was
rejected: its median position throughput changed by -0.10%, 11/22 positions
regressed, and depth-ten timings were slower, within roughly 10.7% median
within-position timing variation. Production retains the existing lazy updates.

Performance used the existing 22-position `modern_2026` corpus with its recorded
move clocks, 500,000 visited nodes per position, three alternating frozen
baseline/candidate trials, Hash 64, one worker, pinned network, and books,
experience, pondering and tablebases disabled. Every final score, bound, PV,
bestmove, depth and visited-node result matched. Median position throughput
was 452,033 -> 566,334 NPS; median per-position gain was 26.21% (range
+6.18% to +42.17%, no position-median regressions). Aggregate nodes divided by
total wall time improved 448,446 -> 543,890 NPS (+21.28%). Median within-position
max/min variation was 5.66% / 7.92%, with worst outliers 52.29% / 90.98%; the
large overall gain exceeds typical noise, but individual three-trial estimates
remain imprecise. Three alternating completed-depth-ten trials took median
1.5113 -> 1.2524 seconds at start and 7.0848 -> 5.2239 at Kiwipete, with identical
search work/results. This is local i5-10210U/WSL2 speed evidence, not an Elo gain.
A subsequent four-million-node CPU-clock profile still attributed approximately
55% to NNUE evaluate (including inlined dense/update work) and 12.8% to refresh;
raw sample addresses were mapped to frozen ELF symbol intervals. Baseline,
candidates, exact settings, logs, per-position variations and rejected fused
results are under `.context/baselines/2026-10-16/nnue-byte-dot/`.
Full Debug and Release suites passed 36/36 each, including standard and neural
search lifecycles. Focused ASan/UBSan checks passed 4/4 (NNUE, independent
reference, leaf search and null search; leak detection disabled in this sandbox).
The existing Windows bundle was refreshed with this byte-identical measured
Release binary; all 94 manifest hashes verified. Its Windows launcher loaded
NNUE with a Windows model path, completed depth three, hit a Syzygy root and
exited cleanly. Restart existing GUI engine processes to load the new binary.

The subsequent 2026-10-16 CPU-cost diagnosis verified that canonical Release and
Windows bundle still share SHA `88ba9f671db821edf71efd172099ffa786261e7fdb54068b3e884accc16b360a`,
with `-O3 -DNDEBUG -march=native`. CPU-clock samples land on actual AVX2 byte-dot
instructions; a no-AVX2 NNUE-object microbenchmark is about 2.4x slower for warm
inference. However, the then-current activation-clipping loop processed one value at
a time. A five-million-node Kiwipete profile attributed approximately 28.17% of
whole-process sampled CPU time to this loop, 21.73% to dense layers/output,
12.66% to full accumulator refresh and 3.89% to incremental updates. Vector
register instructions in the clipping loop handle individual scalar values;
their presence alone does not demonstrate efficient vector packing.

A local packing-only prototype clips/packs 32 lanes at a time with AVX2 while
preserving all clipping, perspective/lane ordering and network arithmetic.
Three rotated five-million-node trials per configuration measured median
ordinary Release / same-source LTO / packing candidate throughput of
526,929 / 542,727 / 764,922 NPS at start and
531,073 / 533,104 / 723,892 at Kiwipete. Packing improved 45.17% / 36.31%; LTO
changes of 3.00% / 0.38% were within local variation. All final search results
and pruned totals matched. Independent reference comparisons passed 3426 cases;
exhaustive int16 clipping/lane-order checks and focused ASan/UBSan passed.
This identifies a specific generated-code bottleneck, not globally inactive
SIMD. At that diagnostic stage, the packing candidate had not undergone full
22-position or complete engine-suite acceptance and had not replaced production
or the Windows bundle. It was subsequently validated and installed as recorded
below. PGO was not tested in this diagnosis.

The user subsequently authorized promotion of vector activation packing as the
new baseline. Production `nnue::detail::clip_activations` clips and packs 32 lanes
per AVX2 vector, restores lane order across 128-bit halves, preserves perspective
order and retains the scalar fallback. Permanent tests exhaustively cover every
signed int16 value, asymmetric lanes, both sides to move and scalar/SIMD equality.
Release36/36, Debug36/36 and focused ASan/UBSan4/4 passed, including independent
reference/lane checks and standard/neural search lifecycles. Separate no-AVX2
compilation of the production helper tests also passed.

The accepted three-alternating-trial 22-position/500k-visited-node corpus run
measured median per-position throughput 571,096 -> 845,561 NPS and median gain
48.36% (range +43.08% to +59.09%, no median regressions). Aggregate visits divided
by wall seconds measured 561,149 -> 832,790 NPS (+48.41%). Median within-position
max/min trial variation was 5.91% / 6.63%, worst 26.73% / 35.57%. All final
scores/bounds/PVs/bestmoves/depths/visits and pruning totals matched. Three
completed-depth-ten trials took median start 1.18985 -> 0.81518 seconds and
Kiwipete 4.69869 -> 3.37439 seconds, with identical search work and results.
Settings remained Hash64, one worker, pinned NNUE, contempt zero,
books/experience/ponder/tablebases off; EPD clocks were preserved. No other
task-owned CPU-heavy work ran during timing. These local speed results are not
Elo evidence; do not multiply summaries from different benchmark runs.

Canonical Release, measured candidate and installed Windows engine share SHA256
`448cee5704bcac95b35d5a3980ed4cd60e9e4231e8da8ba64451d32b0f5ceb22`.
All94 bundle manifest hashes verified, and its Windows launcher passed NNUE
Windows-path load, legal depth-three search, direct Syzygy root and clean exit.
Restart existing GUI engine processes to load this new baseline. Prior bundle
files were backed up. Full evidence:
`.context/baselines/2026-10-16/nnue-packed-base/`.

Follow-up diagnostic counters on this baseline confirmed that leaves dominate:
quiescence visits were 70.92% at start / 86.50% at Kiwipete; quiescence plus the
last three main-search plies covered 98.62% / 99.27% of five million visited
nodes. Non-check qsearch entry generated 13,045,793 / 2,291,221 quiet moves;
12,832,864 / 2,202,498 legal non-checking quiet moves were subsequently discarded,
approximately 99% of legal ordinary quiet returns. Those q0 buckets incurred
199,792,445 / 42,428,362 move-selection comparisons across all stages. Deeper
checked quiescence nodes at Kiwipete additionally generated 5,157,213 quiet moves
and incurred 79,904,697 comparisons; quiet evasions must remain searchable.
The exact wall-time gain from specialized checking/evasion generation is not yet
measured. Any candidate must retain check discoveries, ordering, castling,
promotions, evasions, rule50 and stalemate handling; disabling all quiets would
change search behavior and can misclassify stalemate.

The next quiet-leaf experiment retained one legal quiet witness when no generated
ordinary quiet could give check, then skipped the remaining selection work.
Checks/evasions and actual search results were preserved, but it still generated,
scored and scanned all quiet moves. Three alternating 22-position trials showed
median gain5.27% amid median max/min variation28.55%/27.84%; a longer five-pair
CPU2-pinned five-million-node follow-up found only1.90% /0.81% process-CPU gains
at start/Kiwipete, with mixed paired signs and larger variation. It was rejected
and the original move picker restored. The narrower pruned-move counts in its
logs reflect skipped unused quiet enumeration, not changed visited-node totals.
Candidate and rejected evidence:
`.context/baselines/2026-10-16/quiet-leaf/`.

A subsequent accepted optimization adds a private cache of undamped neural
static scores to each search. It lazily allocates65,536 entries (1MiB here),
stores the full board/side key and Maxima-centipawn neural score before rule50
damping, and clears on every search initialization. Its model identity is the
search's immutable snapshot. Material/pawn metadata and current clock damping
still run; terminal/draw checks remain before evaluation. HCE/royal-losers do
not allocate this cache. This does not add searched-score cutoffs to qsearch.
Permanent tests revisit the board through a legal knight cycle across clock8->12,
verify cached100cp becomes90cp with current damping exactly once, verify metadata
and skipped inference, and verify cache reset on reinit. Existing model lifetime,
load/unload, score-bound and independent accumulator/reference checks pass.
Release36/36, Debug36/36, focused ASan/UBSan4/4 and both lifecycle suites passed.

Three alternating CPU2-pinned 22-position/500k-visited-node trials used the same
NNUE, Hash64, contempt zero and disabled books/experience/ponder/tablebases, with
EPD clocks preserved and no task-owned concurrent timing load. Final scores,
bounds, PVs, bestmoves, depths, visits and pruning totals all matched. Aggregate
wall throughput improved843,697->906,380 NPS (+7.43%); aggregate process-CPU
throughput863,396->927,900 (+7.47%). Median per-position gains were5.45% wall /
5.06% CPU; median trial max/min wall variation5.46%/3.54%. Four position medians
regressed: corpus4 -3.90%,7 -1.27%,8 -4.40%,14 -3.76%. Keep these limitations
visible rather than claiming universal improvement. Exploratory position-block
bootstrap of paired CPU log ratios suggested geometric gain8.43% with an interval
[4.39%,13.26%]; it was not a predeclared statistical guarantee or strength test.

Three pinned five-million-node pairs measured start essentially flat (+1.22%
CPU), Kiwipete +21.09% CPU (all paired gains19.3–22.8%). Completed depth10
measured start .79270->.81826s with noise, Kiwipete3.25968->2.66846s (+22.16%
throughput, trial ranges1.45%/.71%). Counter-only runs avoided830,219/4,492,190
neural requests at start (18.48%) and1,719,794/4,371,554 at Kiwipete (39.34%).
Score hits can leave accumulators unready: start missing-parent refreshes rose
1,043,574->1,410,370, helping explain its small gain. Compatible-ancestor lazy
updates or cheap accumulator maintenance remain further measured candidates.

The accepted static-score cache is now installed in canonical Release and the
Windows bundle, SHA256
`22d100141fcc61ae01274aeca020c127d519679bc7e53cd1b3db869b98aca422`.
All94 manifest hashes verified; Windows launcher NNUE load, depth-three legal
move, Syzygy root and clean exit passed. Restart GUI engine processes. Full
protocol, per-position results, regression ranges, source identities, diagnostics
and prior bundle backups:
`.context/baselines/2026-10-16/neural-static-cache/`.

The next accepted optimization reuses a ready ancestor accumulator when neural
score-cache hits left the immediate parent unready. Each perspective walks the
active prepared states backward, stopping at a changed king anchor or when the
feature-operation cost reaches a fresh rebuild. It copies the compatible ready
base once and applies intervening feature changes in order. Ancestors remain
read-only; immediate-parent and scalar paths, clipping, arithmetic, model and
search policies are preserved.

The final Release binary passed three alternating CPU2-pinned trials of the same
22-position corpus at500,000 visited nodes per position, Hash64 and one worker,
with books/experience/ponder/tablebases disabled and recorded clocks preserved.
Aggregate wall throughput improved934,839->992,654 NPS (+6.18%); process-CPU
throughput938,708->996,145 (+6.12%). Median per-position wall gain was6.44%,
with no position-median regressions in this final run. Median within-position
max/min variation was1.54%/2.38%. An earlier prototype run gained5.92% overall
but had one position regression (corpus21 -2.46%); three-trial estimates retain
noise. All final scores, bounds, PVs, bestmoves, depths, visits and search-result
pruning/provenance totals matched exactly.

Three alternating five-million-node pairs on the final binary measured median
start796,455->883,009 NPS (+10.87%) and Kiwipete941,584->984,445 (+4.55%).
Process-CPU gains were10.15%/5.39%, with maximum trial ranges below0.8%.
Separate completed-depth-ten medians were start .81159->.71255 seconds and
Kiwipete2.70321->2.55589 seconds. These are local speed measurements, not Elo.

Full Release36/36 and Debug36/36 suites passed. Independent reference tests
passed2642 score/accumulator comparisons, including skipped evaluations through
captures, underpromotion, en passant, castling, king changes, null moves and undo;
a forced no-AVX2 NNUE object passed the same checks. Production-path focused
ASan/UBSan4/4 passed, including both lifecycle suites (sandbox leak detection off).
Audit-mode native NNUE/reference/search-audit tests and search lifecycle passed;
an audit neural-lifecycle run passed30/31 before correcting its timer fixture,
and that isolated corrected case then passed. The fixture now gives a750ms
hard budget under its one-second assertion; its previous1050ms budget could
legitimately exceed that assertion. The cache fixture also explicitly disables
audit verification when checking that a cache hit skips inference. Neither
fixture correction changes production time management or evaluation.

Canonical Release, frozen final candidate and installed Windows engine now share
SHA256 `66675658291b42748ad05cd589ddb863f5c42f7dabb31d08e4bfacaa3e178643`.
All94 bundle hashes verified after backup and installation. Windows launcher
NNUE load, depth-three legal search, Syzygy root and clean-exit checks passed.
Restart existing GUI engine processes to load it. Protocol, per-position results,
source identities, validation logs and bundle backups are under
`.context/baselines/2026-10-16/nnue-ancestor/`.

The next accepted leaf-cost optimization shortens quiet-move selection while
preserving the complete returned sequence. In the final quiet phase of standard
chess, the previous maximum score bounds the remaining frozen scores. Scanning
can stop at its first occurrence, preserving the original first-maximum and
swap-removal tie order; if absent, a complete scan finds the next maximum.
The existing returned-move slot stores this bound, initialized to INT_MAX for
the first scan. No new search state, layout, allocation, generator, evaluation,
legality or search-policy change is included. Other variants retain the original
selection. Direct-only quiet-check generation remains unimplemented here:
filtering unused moves can change the order of equally scored checks.

Independent original-picker shadow checks compared every quiet return and the
remaining list. Ordered SHA256 node traces matched3,715,559 root/PVS/quiescence
entries per side over31fixtures, including all22corpus positions and special
moves, checks/evasions, discovered checks, stalemate and clock99. Each entry
records board key, window, depth, ply, fifty counter and node kind. Permanent
checks compare the original max/swap algorithm across768combinations of history
ties, hash/killer hints, positions and depths. Full Release36/36, Debug36/36 and
focused ASan/UBSan4/4 passed, including both lifecycle suites; sandbox leak
checking was disabled. No sanitizer diagnostics appeared in captured test logs.

The final canonical Release passed three alternating CPU2-pinned runs on the
same22-position modern_2026 corpus at500,000visits each, one worker, Hash64,
unchanged NNUE, contempt0 and books/experience/ponder/tablebases disabled.
Recorded EPD clocks were retained. All final scores, bounds, PVs, bestmoves,
depths, visits and pruning/provenance totals matched. Aggregate wall throughput
improved978,996->1,021,839NPS (+4.38%); process-CPU gain4.38%. Median position
gain was5.00%, with no position-median regressions in this run. Median trial
max/min variation was2.13%/1.57%. The private candidate independently measured
+4.83% aggregate with no median regressions and1.79%/1.75% typical variation.

Longer timings were less stable. The first three five-million-node pairs showed
only1.83%/1.00% wall gains at start/Kiwipete, amid CPU trial ranges8.7–19.1%.
One predeclared five-pair retry measured median871,082->899,906NPS at start
(+3.31%) and978,124->1,012,242 at Kiwipete (+3.49%). Nine of ten paired CPU
comparisons favored the candidate, but one start pair regressed8.74%; within-side
CPU ranges still reached19.7%. No samples were removed or further retries added.
Retain this optimization on the two repeated corpus gains exceeding typical
corpus variation; precise long-run per-position gains remain uncertain.
Completed-depth-ten final medians were .86933->.71817s at start and
2.70787->2.67240 at Kiwipete, but trial ranges14–42% prevent interpreting these
as reliable depth-time gains. These are speed measurements, not Elo evidence.

An earlier maximum/count-field prototype was rejected after matched-flags pilot
CPU changes -1.81%/-1.50%. Initial private-build timings inherited extra conda
flags and were invalidated before acceptance; all retained candidates use the
canonical empty extra C/CXX flags. Original failed/confounded attempts remain
archived. Canonical Release, frozen final and installed Windows bundle now share
SHA256 `dd022848a5c613ccf0b117afcaa34dc618398bd6e75809b5f6d2d2eeac11b6b7`.
All94 bundle hashes verified after backup/update. Windows launcher NNUE load,
legal depth3, Syzygy root and clean exit checks passed. Restart GUI engine
processes to load it. Full protocol, raw logs, profiles, test evidence, source
backups and identities: `.context/baselines/2026-10-17/quiet-order/`.

For paired A/B games, select `--model-a EvalFile=/absolute/path/to/network.nnue`
and `--option-a UseNNUE=true`; engine B can use the same executable with HCE.
The runner applies model options before evaluator-enabling flags, after validating
both complete configurations, and records the model checksum in the match report.

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

The fresh current-build NNUE–HCE confirmation completed and passed its audit:
200 games / 100 fresh color-swapped opening pairs, 5+0.05 seconds, Hash64,
one worker, identical ordinary Release binary and production search defaults.
Only the evaluator differed; books, pondering, contempt, experience and tables
were disabled. NNUE scored 73 wins, 89 draws and 38 losses (58.75%). Its
conservative 95% opening-pair interval is 45.17–72.33%, including 50%; this
encouraging result does not establish that NNUE is decisively stronger or
justify deleting HCE. NNUE visited 544,642,560 nodes in 1327.916 charged seconds;
HCE visited 1,770,504,141 in 1317.340 seconds. There were zero errors, unfinished
games or clock forfeits. The audit checked identities, all 25,277 legal plies,
score evidence, node counts and paired statistics. Of 200 games, 105 used the
predeclared 1000 cp / six consecutive plies / move-20 win adjudication, 89 ended
by threefold repetition and six by checkmate. Adjudicated wins cannot establish
conversion skill. HCE remains available and defaults are unchanged.
Private artifacts: `.context/gui-tests/2026-10-15/` and
`.context/baselines/2026-10-15/nnue-hce-current/`.

A new controlled NNUE-versus-HCE match was prepared on 2026-10-17 using the
shared period-reserve Release binary (`2e17702095a9828bb936dbd3d26d6a2c6590270c20477582e31d1259357ba5ad`).
The predeclared cap is 1000 fresh color-swapped opening pairs at 10+0.1 seconds,
Hash64, one worker, Move Overhead100, contempt zero, no books/experience/ponder,
and identical complete five-piece tables. Only NNUE loads the pinned network.
`--min-elo 0` requires a simultaneous 95% lower score bound above 50% before
acceptance; a cap without a crossing is inconclusive. Score adjudication needs
both engines to agree for six plies at 800 cp or more from move20. Such wins
do not establish conversion skill. Failures, time forfeits, illegal moves or
unfinished games invalidate acceptance, rather than counting as strength evidence.
The protocol, frozen runner/dependency/runtime files, openings, games and an
automatic independent legal/statistical audit live under
`.context/baselines/2026-10-17/nnue-hce-strength/`.
`pipeline-status.json`, `results.json` and the eventual `audit.json` are the
authoritative progress/result records. Preparing or starting this match is not
evidence that NNUE has passed the gate. Keep HCE until it passes its audit.
This match subsequently passed: it stopped at the first boundary crossing after
33 pairs / 66 games, NNUE 56 wins, seven draws and three losses (90.15%). The
simultaneous 95% lower score bound was 50.082%, above the predeclared 50% target.
Independent audit passed all 8776 legal plies, configurations, identities,
clock/node records, adjudication evidence and every prior stopping checkpoint.
There were zero errors or time forfeits. Of 66 games, 59 ended by score
adjudication, two by the fifty-move rule, four by repetition and one by stalemate.
The automatic audit initially used the wrong PGN filename; changing only
`games.pgn` to the actual `results.pgn` allowed the unchanged audit criteria to
run successfully. Original logs/source and an explicit correction receipt are
preserved. The protocol and scored games were unchanged. This passes the
requested strength gate under these settings and authorizes the planned HCE
cleanup; it does not establish a general Elo rating or conversion strength.
The subsequent HCE cleanup is described in the current NNUE architecture above;
older experiment sections below retain their historical evaluator settings.
At the user's request, a separate three-game 10+0.1 depth diagnostic then used
the same frozen engines and settings on three unplayed reserved openings,
alternating colors. NNUE won all three by the same score adjudication rule;
these games do not extend the completed strength test. Across 217 searched
moves per evaluator, mean completed depth was NNUE 9.14 and HCE 10.17 plies,
with medians nine and ten. Through move40 the means were 8.91 and 9.39.
All normal search options were explicitly enabled and both engines reported
millions of pruned moves; individual feature invocation counters were not
collected. The depth averages exclude book/direct-tablebase/terminal returns
and unfinished zero-depth iterations (none occurred in this sample). HCE's
late-game depths included 74- and 32-ply iterations with relatively few nodes,
so maximum depth is not representative. Independent replay verified all 482
legal plies, serialized depth comments, sampling coverage and configurations.
Artifacts: `.context/baselines/2026-10-18/depth-three/`.

The separate preliminary screen against the archived pre-counter-correction
HCE executable completed: current NNUE scored 9 wins, five draws and six losses
(57.5%) over 20 games / ten fresh color-swapped opening pairs, 5+0.05 seconds,
Hash64 and each version's native search defaults. Audit passed all 2681 legal
plies, provenance, configuration, clock/node records and adjudication evidence;
zero errors, unfinished games or clock forfeits. Fifteen games used the agreed
1000 cp / six plies / move-20 win adjudication, five ended by repetition.
This shows competitiveness in the small screen, not a reliable strength/Elo
advantage: the conservative paired 95% interval is 14.55–100%, including 50%.
Current NNUE visited 59,770,256 nodes in 137.088 charged seconds; archived HCE
reported 769,514,426 nodes in 135.196 seconds INCLUDING skipped moves. These
node totals cannot be compared as equal counters. The matchup compares complete
historical/current versions and does not isolate an evaluator or pruning gain.
No extension beyond 20 games was started. Frozen protocol, games and audits:
`.context/baselines/2026-10-15/nnue-versus-archived-screen/RESULT.md`.

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
futility (16). Normal policy is 31; conservative is 0.
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

`NNUEAuditDelta` (default false, audit builds only) enables the production
quiescence futility rule in the `quiet` contract. Changing it stops search and
clears TT. Worker-owned prune counts are reported as `info string audit_delta`
and in the diagnostic probe. Tests exercise the 350-cp boundary and node-budget
cancellation/restoration.

Audit quiescence uses the same captures/promotions/evasions move domain as
production. The obsolete quiet-check and castling audit options are removed.

Audit builds report `info string audit_work` in `quiet` mode: effective
positive-depth PVS visits, quiescence visits, root-driver calls, made moves,
null moves and full-window re-searches (with root and fail-high subsets).
The depth-zero handoff counts once in quiescence; root-driver calls are
reported separately. Null searches and re-searches count as visited work;
rejected move candidates do not count as visited child positions. Fixed neural
leaves use the quiescence/leaf counter. Work counters are worker-owned and
restart with the node counter when resetting the self-play stack. Ordinary
UCI node counting and search decisions are unchanged by this instrumentation.
The standard-search node count equals `move_visits`.

Internal iterative deepening stays disabled in `fixed`. In `quiet`, the
independent `NNUEAuditIID` flag defaults false. `audit_iid` reports calls; `audit_iid_work` adds eligible candidates,
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

When `IIR=false`, IID uses one ordering helper at nonroot PV nodes with no hash move and at
least four plies remaining. It searches two plies shallower, without nested IID,
and accepts a legal helper move only when the helper score exceeds alpha.
The main search always retains its original depth; the helper score, including
a mate score, cannot replace it. The search frame is restored before continuing,
and cancellation returns immediately. `IID=false` disables it independently.

Standard search defaults `IIR=true`. At PV or expected cut nodes with remaining
depth at least six and no legal TT move, internal iterative reduction subtracts
one ply from the whole node. Root searches, checks, same-position IID helpers,
verification subtrees, excluded moves and reversible clocks at least 99 are
protected. Normal TT, draw, terminal/tablebase and static/null decisions precede
this reduction. The TT lookup requests the original depth; subsequent child
searches, LMR, history bonuses and TT stores use the effective searched depth.
A legal shallow TT hint suppresses IIR; an invalid or self-check hint does not.
IIR is selective and can miss deeper tactics; node savings do not prove strength.

`IIR=true` takes precedence over `IID` at every node, including nodes below the
IIR depth threshold. Both true therefore selects IIR alone. `IIR=false` restores
the existing IID policy; both false disables both mechanisms. Each worker
snapshots the option, and changing it clears incompatible TT entries. Royal-losers
and fixed/aligned audit horizons retain their previous IID policy with IIR off.
The depth threshold and reduction are provisional, awaiting integrated strength
validation. See [CPW Internal Iterative Reductions](https://chessprogramming.org/Internal_Iterative_Reductions).
Audit counters record reductions by
PV/cut context, depth range, reduced-node cutoffs, terminals and cancellation.
The threshold keeps one-ply ordering probes out of shallow search.

Standard search defaults `MainSEE=true`. At non-PV nodes with one through six
remaining effective plies, SEE pruning can skip a move after at least one legal
alternative has completed search. A capture must meet `-100 * depth`; a quiet
must meet `-50 * depth * depth`, in the 100-cp pawn scale. Equality passes.
A node that began PV stays protected even if its alpha rises to one point below
beta. These simple provisional margins use full effective depth independently
of LMR and history. SEE models exchanges on the destination square, so this heuristic can
miss tactical sacrifices and still needs integrated playing-strength validation.

Root nodes, checks and checking moves, promotions, castles, advanced passed
pawns, pawn-only sides, endgame metadata, IID helpers, verification subtrees,
excluded moves, decisive windows and clocks at least 99 are protected. The
searched best value must exceed the negative mate/tablebase band. A failed SEE
also requires final legality before recording a skip. Skips do not enter the
searched-move history list. At least one searched legal alternative preserves
terminal handling and a real best move; completed results retain ordinary PVS
bound handling. Legal skips cap the propagated TT clock interval at 98.

Workers snapshot `MainSEE`; disabling it skips both tests and pruning, and
changing it stops the worker and clears incompatible TT entries. Royal losers
and fixed/aligned audit horizons keep it disabled. Existing qsearch SEE retains
its own policy. Audit counters distinguish pseudo-legal candidates from legal
capture/quiet skips and record their depth range. See
[CPW Static Exchange Evaluation](https://chessprogramming.org/Static_Exchange_Evaluation).

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

The separate development launcher `C:\dev\uci-engines\maxima-new.exe` (also
available at `C:\dev\maxima\maxima-new.exe`) runs `engine/maxima-new` from the
same bundle. The 2026-10-21 package was refreshed to engine revision `7fee1d1`,
including ProbCut, TT history feedback, depth/context-aware quiet pruning, raw
static-evaluation TT caching, richer correction history and adaptive null move.
It shares the existing model and chess data; the original standard and frozen HCE
launchers retain their existing backends. See `README-new.txt` in the bundle.
The remaining baseline features and playing-strength acceptance are still pending.

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
A later Cute Chess `40/300` game (2026-10-16) flagged Maxima before White's
40th move. Its complete GUI handshake subsequently showed `UseNNUE=false`:
that game used HCE despite being named “Maxima 2 NNUE”. Loading EvalFile alone
does not select neural evaluation. The earlier automated NNUE matches explicitly
enabled NNUE and are unaffected.
 The GUI correctly supplied `movestogo 1`, `wtime 13683`; it sent
`stop` at 13483 ms of reported search, followed by Maxima's `bestmove f2e4`.
Across the preceding four moves the GUI charged 146–362 ms beyond reported
search time, exceeding the 50 ms reserve. This is evidence of inadequate
end-to-end margin; the individual latency source is not yet isolated and no
timing fix has been applied. The candidate Ne4 was not played, and the next
control had not been reached. Preserved evidence:
`.context/gui-tests/2026-10-16/time-forfeit/`.

A second Cute Chess game flagged before White80: the supplied clock was3804ms,
and final engine elapsed3781ms left only23ms before delivery; the preceding
two moves had89–90ms of GUI-charged time beyond engine elapsed. Its endgame
was within installed coverage from White66 (KBPvK), then KPK from White72.
The root shortcut's reversible-history exclusion caused normal searches after
otherwise instant capture-reset probes. This is a concrete integration limitation;
removing that exclusion requires repetition-aware move selection. Preserved log
and clock analysis: `.context/gui-tests/2026-10-16/second-time-forfeit/`.

The subsequent reliability repair accepts unique reversible histories for direct
DTZ play, resolves zero-clock WDL outcomes consistently across search windows,
and counts command handling/setup in the clock budget. Protocol logging is now
buffered after stdout delivery and deadline polling tightens near expiry; the
50 ms reserve is unchanged. All 15 covered White roots in the second game,
in both HCE/NNUE modes, now use direct tablebase moves (30/30; previously4/30).
White71 selects Kf3 rather than offering the bishop, and White80 selects g7.
A DTZ continuation from actual White66, using both sides and checking each
position independently, ended in checkmate after19plies without losing the
bishop. This is one continuation, not a fastest-mate or longest-defence claim.

Release36/36, focused ASan/UBSan3/3, and concurrent-output ThreadSanitizer1/1
passed. Three forced300ms searches on the final position with tablebases disabled
finished at312.36–314.60ms through the old Windows launcher versus300.61–300.69ms
after repair. The real final position with tablebases enabled returned g7 in
2.87ms through the updated launcher. The earlier89–90ms excess was not reproduced;
a full GUI game remains necessary. The Windows bundle was updated and all94
manifest entries verified. Evidence and frozen before/after binaries:
`.context/gui-tests/2026-10-16/reliability-fixes/`.

The next full Cute Chess NNUE game at40/300 ended with72.Qa5# (1–0), with
all White moves49–72 selected directly from tablebases. For moves49–71,
successive GUI clock readings minus engine elapsed times give0–2ms excess,
median1ms. The last move has no following clock reading. Longer searches
show negative differences between the two clocks, so this log does not
establish exact interface latency throughout the game. The clock reserve is
now configurable through `Move Overhead`, default50ms, range0–5000ms.
Evidence: `.context/gui-tests/2026-10-16/move-overhead/`.

A subsequent starting-position `go depth 14` test, one fresh process per mode
with the same Hash64/options, completed in 1.810 seconds for archived HCE,
11.330 seconds for current HCE, and 25.970 seconds for current NNUE. Current
completed-depth metadata confirmed depth14 with no interruption; the old
unlimited-depth14 command finished normally. Current HCE visited 13,943,212
nodes; NNUE visited 10,297,207. Old output reported 10,892,953 including skipped
moves, so it is not directly comparable to those visited totals. The 6.26x
HCE time-to-depth increase is real on this workload and independent of NNUE or
node reporting. Historical/current search selectivity and trees differ; one
position/trial at equal nominal depth does not establish equal strength or a
general regression. The policy causing the difference has not been isolated.
Full logs/results: `.context/gui-tests/2026-10-15/post-reboot/depth14-results.json`.

Controlled post-reboot follow-up (2026-10-15) supersedes the inference in the
user-observation note below that low NNUE rates necessarily came from system
sluggishness. Three fresh-process trials per mode/position, rotated order,
Hash64 and one million visited-node budget, produced these medians:

| Position | Evaluator/version | Reported nodes | Reported NPS | Wall seconds |
|---|---|---:|---:|---:|
| Start | Archived HCE | 5,940,819 | 7,325,300 | 0.812 |
| Start | Current HCE | 1,000,000 | 1,272,264 | 0.787 |
| Start | Current NNUE | 1,000,000 | 461,893 | 2.166 |
| Kiwipete | Archived HCE | 4,641,576 | 4,506,384 | 1.032 |
| Kiwipete | Current HCE | 1,000,000 | 1,302,083 | 0.769 |
| Kiwipete | Current NNUE | 1,000,000 | 452,284 | 2.212 |

NNUE was explicitly loaded and enabled; HCE explicitly disabled NNUE. All 18
searches exited successfully. Books, pondering, contempt and tables were off;
working directories were empty Linux temporary directories. Old totals include
skipped moves; its stopping budget counts visits. Policies/search trees differ,
so this is not equal-depth or strength evidence. Load averages were low and the
old 7–8M starting-position rate reproduced. Nevertheless, current NNUE measured
0.45–0.46M on these workloads, rather than the user's observed 1.3M. Their exact
position/settings were not captured, so that discrepancy remains unresolved.
Do not assume which evaluator they used, or interpret these two positions as a
universal/optimal speed baseline. Protocol, hashes, logs and results:
`.context/gui-tests/2026-10-15/post-reboot/`.

Post-reboot correction (2026-10-15): the user observed the archived engine
return to 7–8M reported NPS and current NNUE reach approximately 1.3M visited
NPS. Before reboot, even the archived engine had fallen to 1.82M and typing was
sluggish. The exact resource culprit was not identified. Earlier 0.4–0.5M NNUE
measurements and the profile below came from that suspect pre-reboot environment;
they must not be treated as healthy-machine absolute rates or evaluator speed
ratios. Post-reboot figures are user observations, without a captured matching
position/settings protocol. For future performance claims, check system health
and repeat matched workloads. The different old/new node conventions still
prevent directly comparing 7–8M with 1.3M.

Historical HCE figures around 7.6–7.8M NPS used the older reporting convention
that added pruned moves to visited nodes, corrected in `05a3c4d`. Replaying the
archived pre-correction binary and current HCE with a 500,000 visited-node
budget, Hash16, three alternating trials per position, reproduced old reported
NPS of 7.80M at the start and 5.06M in the middlegame. Its reported node totals
were 3,074,301 and 2,383,642, including pruned moves. Median wall times were
395/366 ms (old/current) at the start and 472/359 ms in the middlegame; current
reported totals were 500,000 and NPS 1.37M/1.40M. The lower displayed NPS does
not demonstrate a matching HCE throughput regression. Different search trees
and depths prevent interpreting these fixed-visit timings as equal strength
or equal time-to-depth. NNUE's separately measured inference/search cost remains.
The old skip count included quiet moves discarded in quiescence; it was not a
measurement of the fraction of a hypothetical full tree pruned away.

A subsequent single-position CPU profile used four million visited nodes per
evaluator on ordinary Release: HCE took 3.209 engine seconds (1.246M NPS),
NNUE 9.006 seconds (0.444M NPS). Approximately 64% of NNUE user-space self
samples mapped to neural evaluation, including the AVX2 affine loops, and 10%
to accumulator refresh. Sampling offsets were mapped to the executable's text
symbols; percentages are approximate, without callstack attribution. These are
different search trees, not equal-depth or strength results. They locate
neural work in that pre-reboot run; fresh measurements under healthy machine
conditions are needed before drawing performance conclusions. Private profile data and symbol mapping:
`.context/gui-tests/2026-10-15/cpu-profile-functions.json`.

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

Measured compiler experiment, 2026-10-17: all four configurations used the
same frozen source, GCC 16, ordinary Release flags, and unchanged NNUE network.
Each passed the full Release suite (36/36), including independent NNUE checks
and search lifecycle tests. Each PGO configuration trained from fresh, separate
profiles on 104 searches: 64 NNUE, 16 HCE, 16 royal-losers, and eight four-piece
tablebase probes. Training covered eight game stages, both sides to move, and
72 distinct roots; measurement roots were held out.

The primary measurement used the existing 22-position NNUE corpus, 500,000
visited nodes per position, three rotated rounds, one worker and Hash 64.
Books, experience, pondering and tablebases were disabled for these searches.
Results below compare wall throughput with ordinary Release; positive means
faster. Trial variation is the median across positions of the three-run
maximum/minimum timing spread, expressed as a percentage.

| Build | Aggregate gain | Median position gain | Trial variation | Positions with slower medians |
| --- | ---: | ---: | ---: | ---: |
| Release | baseline | baseline | 24.22% | — |
| LTO | +4.53% | +3.75% | 19.87% | 7/22 |
| PGO | +1.07% | +3.95% | 24.26% | 7/22 |
| PGO+LTO | -2.65% | +6.25% | 43.19% | 7/22 |

A bounded confirmation on another pinned CPU repeated the complete NNUE corpus
for Release, LTO and PGO+LTO. Aggregate gains were +1.46% and +1.94%, median
position gains +2.94% and +3.40%, with trial variation 26.07%/34.05%/27.40%.
Eight LTO and seven PGO+LTO position medians regressed. CPU-time measurements
closely tracked wall measurements. No samples were discarded.

Longer start-position/Kiwipete searches used five million visits, three rounds.
Initial aggregate gains of +12.67%/+11.30%/+17.95% for LTO/PGO/PGO+LTO did not
repeat: confirmation gave -0.56% for LTO and +2.19% for PGO+LTO, with both
candidates slower at each position's median. Completed-depth-10 confirmation
looked better (+4.72%/+5.04% aggregate), but the conflicting longer searches and
large variation prevent accepting those numbers as a general speedup.

Across 792 initial and 234 confirmation searches, all compared builds/repeats
matched bestmove, scores, PVs, depth, visited nodes, pruning/result provenance,
and every reported unbounded scored iteration. Bestmoves and reported PVs were
independently replayed for legality. Initial coverage also included HCE,
royal-losers and tablebase roots; tiny root-probe latencies do not establish a
decoder speed improvement. No new Debug/sanitizer run was needed for this
Release-flags-only experiment; the preceding source checks remain separate.

Disassembly confirms AVX2 byte dot products in every configuration. Compiler
reports show cross-module inlining with LTO and specialized copies/cold code
partitions with PGO. PGO+LTO includes inlined SEE calls in search routines;
these observations do not isolate a cause of speed improvement. Reporting-only
relinks were byte-identical to the measured binaries. No source-level hints or
manual inlining were introduced from these observations.

Decision: no stable overall NNUE gain exceeded the observed noise. Keep LTO and
PGO opt-in and retain the installed ordinary Release, SHA-256
`dd022848a5c613ccf0b117afcaa34dc618398bd6e75809b5f6d2d2eeac11b6b7`.
The Windows bundle remains unchanged; all 94 manifest hashes were verified.
No playing-strength claim follows. Private commands, raw per-position results,
regressions, profiles, compiler reports and hashes are retained under
`.context/baselines/2026-10-17/compiler-nnue/`.

Source efficiency review, 2026-10-17 (analysis, no implementation or speed
claim): prioritize separating NNUE search metadata from classical scoring.
`evaluate()` still calls material/pawn evaluation before NNUE: hash hits avoid
full calculation, but misses calculate unused pawn weakness, king shelter and
other classical scores. Normal search consumes the endgame flag and passed-pawn
mask; a compact NNUE path must reproduce those exactly and retain owned parent
metadata. HCE cache entries must remain complete. A passer-only cache can avoid
invalidations from king/castling changes in the current classical pawn key.
Other concrete candidates are exposing tiny move setters/copies for inlining
(real generator calls remain in ordinary Release), lazily caching qsearch's
repeated opponent-material count, a legal-move existence generator that stops
at its first witness, precomputed immutable TT-context terms, and blocking a few
NNUE output rows together to reuse activation loads. No speedup is established.

The local i5 reports 32 KiB L1 data/instruction caches per core, 256 KiB L2 per
core and 6 MiB shared L3. Compiler-derived search frames are 8,112 bytes and
accumulators 1,088 bytes; unused portions are not automatically fetched. Evaluate
cache lines actually accessed, instruction footprint, register spills and branch
behavior before adopting layout/inlining changes. Allocated hash/model sizes
alone do not prove cache misses. The private review, assembly evidence, layout
probe and hardware report are in
`.context/baselines/2026-10-17/code-efficiency-review/`.

Current NNUE optimization scope (user decision, 2026-10-17): remove unused
classical evaluation calculations, including pawn-weakness and king-shelter
scoring, from the NNUE path. Preserve useful pruning, reductions, extensions,
legal-terminal handling and their exact required metadata. This is a performance
cleanup, not authorization to remove search guards or change the searched tree.
Prioritize compact NNUE metadata preparation, then measure the other source
candidates separately with CPU/cache effects and exact-result checks. Keep the
classical evaluator available through the existing interface.

NNUE metadata implementation accepted and installed, 2026-10-17. Ordinary
GCC 16 Release, LTO/PGO off, unchanged model and search policy. Release and Debug
each passed 36/36 tests. Focused ASan/UBSan checks passed 5/5 with leak detection
disabled, including both lifecycle suites and the independent NNUE score/lane
reference. Ordered traces matched 3,715,559 entries per binary across 31 fixtures.
All 528 performance comparison searches matched bestmove, scores, PVs, visited
nodes, depth, pruning/result provenance and reported unbounded scored iterations;
bestmoves and PVs were independently replayed for legality.

The initial run used 22 NNUE positions, 500,000 visits, three alternating pairs,
one worker, Hash 64, and books/experience/ponder/tablebases off. A single bounded
confirmation used the same positions at two million visits and reversed the
starting order, giving three baseline-first and three candidate-first rounds
over both passes. No samples were discarded. Initial coverage also included
16 HCE, 16 royal-losers and eight four-piece tablebase roots, three pairs each.

| NNUE corpus | Aggregate visited NPS, before → after | Aggregate gain | Median position gain | Median three-trial spread, before / after |
| --- | ---: | ---: | ---: | ---: |
| 500k visits | 950,354 → 988,332 | +4.00% | +4.86% | 10.66% / 18.24% |
| 2M confirmation | 883,646 → 957,505 | +8.36% | +5.06% | 37.51% / 26.40% |

Median complete-corpus throughput across the three rounds was 960,218 → 982,854
NPS initially and 885,847 → 943,554 NPS in confirmation. CPU aggregate gains
were +4.01%/+8.38%. Paired round gains were +4.35/+2.36/+5.23% initially and
+6.51/+3.68/+14.23% in confirmation. Both passes had 20/22 faster position medians
and 54/66 positive paired CPU comparisons; paired CPU median gains were
+5.63%/+4.80%. An exploratory joint position/trial bootstrap for confirmation
had a positive interval (+2.37% to +17.08%); initial data alone did not exclude
zero (-1.86% to +9.09%). This supports retention on repeated paired results,
while substantial host timing variability prevents a precise universal gain.
Report roughly 5% typical position improvement, not a guaranteed 8% upgrade.

Retain individual regressions: initial modern-12 -9.74%, modern-18 -22.31%;
confirmation modern-13 -13.38%, modern-3 -1.10%. No position regressed in both
passes. The larger regressions coincided with very wide timing spreads (up to
85%); their isolated magnitudes remain uncertain. All raw results are retained.
HCE/royal timing changes were small relative to variation; no fallback speed
claim. Tablebase-root latencies do not establish decoder throughput.

Five-million-visit start/Kiwipete searches, three alternating pairs, gained
+4.79% aggregate. Position medians were -2.01% for start and +5.19% for Kiwipete;
trial spreads were 10.88/20.95% and 1.53/14.92%, respectively. Completed-depth-10
median times were 0.8693 → 0.6560 s for start and 2.9095 → 2.6203 s for Kiwipete,
with wide trial spreads (start 43.99/19.51%, Kiwipete 17.43/17.06%). These do not
establish precise per-position or depth-time speedups. No playing-strength gain
was measured.

Installed SHA-256:
`5616d5986ac21679b0b8e8313d23e8bfbff29a11c84a9db13950ef19aa63a300`.
The previous installed baseline was
`dd022848a5c613ccf0b117afcaa34dc618398bd6e75809b5f6d2d2eeac11b6b7`.
The Windows bundle was backed up and refreshed; all 94 manifest hashes passed.
The existing Windows launcher loaded the unchanged model through its Windows
path, completed a legal depth-three search, hit root Syzygy and quit successfully.
Restart any existing GUI engine process to load the new executable. Commands,
focused source patch, validation, original fixture failure/correction, all
per-position results, hashes and backups are under
`.context/baselines/2026-10-17/nnue-metadata/`.

### NNUE-only cleanup — 2026-10-18

After the completed strength gate, standard HCE and its caches are removed in
one cleanup. The network, raw inference, score damping, pruning policies and
TT/repetition identities remain unchanged. The frozen HCE comparison binary is
preserved separately. The automatic model is `eval.nnue`, beside `book.bin`;
`EvalFile` selects alternatives. Initial readiness loads the model outside the
chess clock. Missing models reject standard search; invalid replacement loads
retain the previous network. Royal-losers retains its material-only formula.

Removed material/pawn tables free 72 MiB: at Hash64 after model readiness,
baseline/candidate RSS was 166064/92360 KiB (one process each). Board frames
shrink from 40 to 24 bytes and search frames from 8112 to 7848 bytes. Search
metadata consists only of the passed-pawn mask and endgame pruning guard.

Release and Debug checks pass 36/36 each, after replacing obsolete HCE score
thresholds and color-flip symmetry assertions with neural reference/restoration
checks. Exact KPK draws and promotion certificates remain. The horizon test
separates an existing approximate reverse-futility cutoff from its exhaustive
oracle; frozen/current NNUE search agrees on that fixture. Focused audit checks
pass 7/7, ASan/UBSan checks 6/6 (leak detection disabled), and runner checks
87 pass with nine skips. No new race-detector run. An auxiliary trace comparison
matches all 3,715,559 ordered root/PVS/q entries over 31 fixtures against the
earlier validated NNUE trace executable, whose search source matches the frozen
pre-cleanup source; node budgets omit clock allocation. The current frozen binary
is independently compared below.

Performance protocol: ordinary Release, pinned CPU2, one worker, Hash64, pinned
unchanged model, books/experience/ponder/tablebases off except explicit tablebase
roots. Three alternating baseline/candidate rounds, 22 modern positions at
500,000 visited nodes each, 16 royal-losers positions at 100,000 nodes and eight
five-piece root probes. Start/Kiwipete also use 5M nodes and completed depth10.
All score/PV/nodes/completed-iteration/provenance signatures match exactly and
all reported PVs replay legally. No builds/tests ran alongside the benchmark.

| Workload | Baseline median round NPS | Candidate median round NPS | Aggregate time gain | Round NPS range, baseline/candidate |
| --- | ---: | ---: | ---: | ---: |
| 22 NNUE positions | 1,086,329 | 1,095,742 | +0.75% | 5.13% / 3.59% |
| 16 royal-losers positions | 4,068,370 | 4,091,977 | -0.17% | 2.84% / 1.19% |
| Start/Kiwipete, 5M nodes | 1,062,764 | 1,018,795 | +0.12% | 13.82% / 1.91% |

Repeat range is `100 * (max/min - 1)` across three runs. Aggregate gain compares
summed times; medians can differ when a repeat is noisy. Positive gain means
less elapsed time for the same work. These measurements
support the cleanup and memory reduction; small timing differences within the
observed variation do not establish an additional NPS improvement. No extra
inference optimization is promoted from these measurements. Per-position medians
and full min/max repeat ranges follow; negative gains identify regressions.

| NNUE position | Baseline/candidate median seconds | Gain | Repeat range, baseline/candidate |
| --- | ---: | ---: | ---: |
| modern-1 | 0.4725 / 0.4720 | +0.11% | 5.65% / 1.16% |
| modern-2 | 0.5098 / 0.4970 | +2.57% | 6.63% / 3.53% |
| modern-3 | 0.4848 / 0.4794 | +1.13% | 3.01% / 2.93% |
| modern-4 | 0.4616 / 0.4630 | -0.31% | 7.86% / 8.03% |
| modern-5 | 0.5034 / 0.4886 | +3.02% | 11.25% / 5.17% |
| modern-6 | 0.4696 / 0.4664 | +0.68% | 11.64% / 10.31% |
| modern-7 | 0.5102 / 0.4975 | +2.55% | 0.93% / 10.31% |
| modern-8 | 0.5409 / 0.5461 | -0.96% | 8.93% / 8.06% |
| modern-9 | 0.4866 / 0.4746 | +2.54% | 3.88% / 17.50% |
| modern-10 | 0.4380 / 0.4338 | +0.97% | 2.16% / 4.92% |
| modern-11 | 0.4423 / 0.4385 | +0.86% | 13.94% / 4.30% |
| modern-12 | 0.5089 / 0.4939 | +3.03% | 1.88% / 4.34% |
| modern-13 | 0.4934 / 0.4833 | +2.09% | 6.50% / 6.88% |
| modern-14 | 0.4707 / 0.4709 | -0.05% | 6.52% / 6.21% |
| modern-15 | 0.3516 / 0.3577 | -1.71% | 4.48% / 16.93% |
| modern-16 | 0.4567 / 0.4630 | -1.36% | 5.29% / 2.69% |
| modern-17 | 0.4735 / 0.4721 | +0.30% | 7.18% / 4.77% |
| modern-18 | 0.4438 / 0.4284 | +3.60% | 1.85% / 9.19% |
| modern-19 | 0.4742 / 0.5028 | -5.69% | 8.31% / 5.73% |
| modern-20 | 0.3980 / 0.3859 | +3.12% | 11.12% / 6.44% |
| modern-21 | 0.3509 / 0.3371 | +4.11% | 5.39% / 17.94% |
| modern-22 | 0.3551 / 0.3531 | +0.58% | 29.24% / 5.21% |

| Completed depth10 | Baseline median seconds | Candidate median seconds | Gain |
| --- | ---: | ---: | ---: |
| kiwipete-long | 2.2956 | 2.2657 | +1.32% |
| start-long | 0.6055 | 0.6069 | -0.23% |

corpus-losers negative median cases: losers-late_middle-8 -1.58%, losers-minor_endgame-8 -0.96%, losers-minor_endgame-9 -3.71%, losers-pawn_endgame-8 -4.75%, losers-promotion_endgame-9 -2.31%, losers-queen_endgame-8 -0.08%, losers-rook_endgame-8 -2.36%.

long-nnue negative median cases: kiwipete-long -1.58%, start-long -3.36%.

depth-nnue negative median cases: start-long -0.23%.

Tablebase root correctness matches; zero searched nodes make root-probe NPS
inappropriate as a throughput metric. Full results, per-position variation,
CPU times, logs, source snapshots and initial/final test reports are retained at
`.context/baselines/2026-10-18/hce-removal/`. Benchmark executable SHA-256:
baseline `2e17702095a9828bb936dbd3d26d6a2c6590270c20477582e31d1259357ba5ad`,
candidate `f1734529fd7561b772d926bba13f9285479e950e5400cf08f7284c5c5feab889`. Model SHA-256:
`62ef826d1a6d11b9e814188025aa02a60815c037292e0ef9bbb9bf4f724e5e63`.

The Windows bundle uses the candidate as `engine/maxima2`, the frozen baseline
as `engine/maxima-hce2`, separate NNUE/HCE launchers, and `eval.nnue` at bundle
root. Refresh saved NNUE options and restart engine processes. The frozen HCE
entry retains its legacy `UseNNUE=false` setting. The bundle manifest and both
launchers passed independent smoke checks: legal depth-three searches, automatic
NNUE loading only for the current engine, five-piece Syzygy root probes and clean
exit. All 318 bundle hashes verify. The GUI desktop itself was not driven.

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

The user amended termination rules during that rematch: the first 20 games
used natural outcomes; the next 10 also allowed 800 cp agreement over six
consecutive exact-score plies from both engines after full move 20; the final
34 additionally used an external four-piece Syzygy referee. All completed
games and the original opening order were preserved. Final terminations were
19 checkmates, 36 score adjudications, four tablebase adjudications (three
Maxima wins and one draw), and five other natural draws. The private referee
is an isolated match adapter, not a new public runner option: it checks actual
coverage, treats cursed wins/blessed losses as draws, and requires conservative
DTZ/clock/history checks for decisive nonzero-clock positions. Missing tables
and uncertain boundaries continue play. Ten focused referee cases and a
runner integration check passed; every actual tablebase decision was independently
reprobed. The three policies and their separate results are retained in the
local match artifacts; do not compare their different opening subsets as an
adjudication experiment. Reserve the opening corpus and games from NNUE training.

The follow-up Vienna trace qualified that LMR lead. A private instrumented
binary reproduced the installed engine's move, score, PV, depth, visited/pruned
counts and iterations exactly. After12...bxa6 13.Qxa6+ Nb7, White14.Nd4 is
searched 38th and reduced from child depth4 to3. Its trial equals alpha and
does not trigger a re-search; removing that single reduction still does not
recover a queen defense. Deeper reductions also matter. All13,709recorded
reductions satisfy the existing re-search predicate, with18unreduced retries
when the trial improves alpha; no omitted successful re-search was found.
Separate forced-root depth7 probes give bxa6 andQe6 identical Maxima scores:
-47cp with LMR, or-82cp without. Disabling reductions only in the final iteration
still choosesbxa6; changing only the earlier iteration5Qe6 subtree can select
Qe6 at the unchanged-47cp score. Root ordering by accumulated work and a shallow
tie therefore contribute to the apparent LMR-off recovery. Six trace graphs,
1,641sampled board/history paths,35single-edge interventions and eight forced-root
probes were audited. No safe general correction emerged; keep this as a
diagnostic, without opening-specific guards or a global LMR disable. Production
source, the installed binary and Windows bundle remain unchanged.

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
static stand-pat labels and its existing SEE-based check extensions. It has no
alpha-beta pruning, TT or move ordering; it does not independently validate
evaluation or SEE. The sparse fixtures assert the endgame exclusion from
quiescence capture pruning. Full/null windows, selective options enabled and
disabled, cold/warm/disabled TT, incompatible deep entries at quiescence entry,
real repetition histories, node-budget restart and state restoration are
checked. With selective search enabled, a one-node reverse-futility cutoff is
checked against its documented static margin rather than the unpruned oracle;
the neural evaluator triggers this existing approximate policy on a fixture
where HCE did not. This is bounded search-integration coverage, not a general
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
configuration, and commands. Complete option/model setup with an `isready` /
`readyok` barrier before starting search CPU measurements; sending `setoption`
alone does not wait for configuration to finish. A short benchmark does not
establish a performance or playing-strength gain. The 2026-10-10 audit adds
zero-time, root-restriction, book-shortcut, tiny-node, and losers terminal/draw
regression cases.

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
The 2026-10-17 five-piece download is verified and staged in that directory:
220 files fetched, 70 smaller files reused, all 290 checked against published
SHA-256 values and three independent five-piece WDL/DTZ probes passed. The
tables were left inactive during the game. After the user supplied its completed
189-move PGN, all 220 additional files were installed in the Windows bundle at
the existing path, with changed metadata backed up and all 314 manifest entries
verified. The engine/model binaries are unchanged. Its Windows launcher detects
five-piece coverage, loads NNUE, completes a legal depth-three search and directly
returns the exact draw for the game's first five-piece position (zero searched
nodes, one tablebase hit). Restart the GUI engine process before the next game.
Download, activation and verification receipts are in
`.context/baselines/2026-10-17/syzygy-five-download/`.

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

#### First isolated timed-strength candidate: frontier quiet-move pruning

A private candidate in `.context/baselines/2026-10-18/lmp-frontier/` adds
one rule only. At remaining depth exactly one, non-PV nodes outside check
may skip a later non-dangerous quiet move after four legal moves have actually
been searched. Both window endpoints must be below the decisive-score range,
the best searched score must be above the negative decisive threshold, and
existing endgame metadata must be false. Existing dangerous-move classification
protects captures, checks, promotions, castling and advanced passed pawns.
Existing score-based futility, SEE, LMR, extensions and evaluation are unchanged.
The four-move threshold was fixed before collecting candidate results.

The experiment starts with correctness and node diagnostics: candidate OFF must
exactly reproduce frozen production scores, moves, PVs and node counts across
the 50 diagnostic roots at D1–D7. Separate enabled-rule guard checks verify its
move protections and state restoration. These reused roots are diagnostic data,
not a fresh strength sample. Disabled mode uses the private startup environment
`MAXIMA_TEST_NO_FRONTIER_LMP=1`; no public UCI option is added.

Node savings alone do not establish faster execution or stronger play. A timed
protocol must be frozen separately before any paired candidate-versus-baseline
games. Production and the Windows bundle remain unchanged during this screen.

The first node screen completed 1,050 searches. Candidate OFF exactly reproduced
all 350 frozen comparisons (50 roots at each D1–D7). An independent audit checked
legal PVs, completed requested depths, input identities and that `src/search.cpp`
is the only changed engine source. Candidate ON results:

| Depth | Frozen nodes | Candidate nodes | Change | Changed best moves / 50 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 2,656 | 2,656 | +0.00% | 0 |
| 2 | 16,653 | 18,077 | +8.55% | 0 |
| 3 | 75,753 | 49,694 | -34.40% | 13 |
| 4 | 227,866 | 216,961 | -4.79% | 9 |
| 5 | 587,707 | 430,249 | -26.79% | 15 |
| 6 | 1,727,978 | 1,397,165 | -19.14% | 13 |
| 7 | 3,996,518 | 3,211,491 | -19.64% | 15 |

At D7, 39 roots used fewer nodes, nine used more and two were unchanged. The
median per-root candidate/baseline node ratio was 0.8165; the worst increase was
1.7945. This is selective search with changed results, not a tree-preserving
implementation speedup. The D2 increase and per-position regressions are retained.

Release and Debug each passed 33 existing enabled-candidate checks, including
search and NNUE lifecycle checks. Three existing tests (`test_search_history`,
`test_search_tree`, `test_search_selectivity`) intentionally require isolated or
exhaustive child contracts; each passed with the new pruning rule disabled.
Their initial enabled-rule failures are preserved in the private logs and are
not counted as enabled passes. A separate enabled-rule test passed 60 probes,
skipping 167 eligible quiet moves while covering all five protected categories,
PV/depth/window/best-score guards and board/hash/search-state restoration in
both builds. Its first Debug invocation lacked the network environment property;
regenerating the private build configuration fixed the harness, without changing
the engine or rule. That failed invocation is also retained.

The next screen is predeclared in `lmp-frontier/timed-screen/protocol.json`:
20 games / ten fresh paired eight-move openings, 5+0.1, Hash 64, one worker,
100 ms overhead each, identical network and full five-piece tablebases. All
other search settings remain native; books, experience and pondering are off.
The fixed sample runs to completion regardless of interim score. It is a screen,
not a sufficient strength-confirmation or deployment gate. Both engines retain
authoritative completed-depth telemetry, with direct tablebase returns separated.

The fixed timed screen completed all 20 games: **5 wins, 12 draws, 3 losses
(55%)** for the candidate. All ten opening pairs, 2,712 legal plies (including
320 prescribed opening plies), input identities, configurations and outcomes
passed audit. There were no engine failures, illegal moves, time forfeits or
unfinished games. Terminations were ten threefold repetitions, eight agreement
win adjudications, one fifty-move draw and one insufficient-material draw.
The conservative opening-pair Hoeffding 95% score interval is 12.05–97.95%;
this small screen does **not** establish an improvement or non-inferiority.

Both engines had median completed depth 8 in positions with at least ten pieces.
Raw mean depths include cheap ceiling-depth iterations: one candidate sample and
five baseline samples reported depth 128 using only 128–6,287 nodes. These are
retained in the raw telemetry and explicitly separated in `paired-depths.json`.
For positive completed depths below that ceiling in positions with at least ten
pieces, means were 7.9115 (836 candidate searches) and 7.8523 (833 baseline
searches). Those samples come from different game positions and do not establish
a depth advantage. At the ten matched starting roots, mean completed depths were
7.4 and 7.2 respectively, with identical initial clocks but independently chosen
per-move thinking times; that small sample also proves no depth advantage.

Reported node totals were 145,666,883 candidate and 157,691,725 baseline across
their different game trajectories. Neither these totals nor the fixed-depth node
savings establish an NPS improvement. Protocol and analysis-script identities
were rechecked after the match. Installed, frozen and public Release binaries
still share the original `f7a5d64d…` identity. No candidate was promoted or shipped.
The next decision requires a separately predeclared larger paired confirmation
of this exact rule; do not combine another search change with it or tune the
threshold using these screening outcomes.

The user selected a practical improvement gate rather than merely preserving
strength: independent paired SPRT **H0 = 0, H1 = +5 logistic Elo**, nominal
alpha = beta = 0.05, at **10+0.1**, capped at 1,000 fresh opening pairs / 2,000
games. This is recorded before play in `lmp-frontier/confirmation/protocol.json`.
A positive model-based SPRT decision supports improvement against zero; it does
not certify a minimum +5 Elo gain. Equal or inconclusive results do not qualify
for adoption. A stricter +5-versus-+10 comparison was discussed but never run.
The original pilot is not pooled into confirmation. The exact candidate binary,
four-searched-move threshold, baseline, network, resources, adjudication and all
other search rules remain unchanged. Confirming games run with twice the pilot's
initial clock; the increment remains 0.1 seconds. No new engine build is involved.

The fresh sample excludes all earlier reserved opening/corpus/position endpoints,
including the pilot. Protocol, analysis scripts, binaries, network, copied runner
and shared tablebase identities are pinned. Automatic final audits check legal
replay, matching configurations, adjudications, whole-pair statistical stopping
and that no earlier pair already crossed a boundary. Source changes or time
forfeits prevent promotion pending investigation. SPRT tests passed six checks
with one optional integration check skipped; no statistics implementation changed.

For scheduling only, the pilot's pentanomial counts `[1, 1, 4, 3, 1]` produce an
LLR of 0.042461 after ten pairs for 0/+5. Scaling those same proportions to the
upper boundary 2.944439 suggests about 694 pairs / 1,388 games. Pilot search wall
time averaged 17.94 seconds per game at 5+0.1. Allowing roughly ten additional
seconds per game for the longer initial clocks suggests approximately 10–12 hours.
This is a fragile planning extrapolation from twenty games and assumes the same
55% score and paired outcome pattern; it is not a confidence interval, stopping
rule or promised completion time. The test may stop sooner or reach the cap
inconclusively. Confirmation was started with the frozen 0/+5 protocol; production
remains unchanged pending completion and review.

#### Pragmatic feature-screening workflow and runtime limit

The user stopped the long 10+0.1 confirmation before a decision. Its original
protocol, raw outputs and incomplete report are preserved in `lmp-frontier/confirmation/`
and the verified archive `lmp-frontier/archives/confirmation-10plus01-stopped-2026-10-18.tar.gz`.
The archive manifest verifies 45 members. There were 67 recorded games: 16 wins,
35 draws and 16 losses for the candidate, including 33 complete opening pairs
and one unpaired game. The run is marked `stopped_by_user`, not completed or
accepted. The unrecorded interrupted game remains unscored. These data are not
pooled into the new screen, and no hypothesis is accepted from this interruption.

For future feature work, start with lean, pragmatic sanity screens to identify
likely useful changes before spending hours on a small-Elo statistical gate.
Keep one change per candidate, paired colours, frozen inputs and settings, and
report all outcomes, actual depths and runtime. A screening score is a direction
for further work, not proof of a small gain. Estimate the time before starting
an expensive run. **Eight hours is the user's maximum runtime for a long test**;
long confirmation runs are intended for overnight use, not the default first step.

The replacement screen is fixed at **100 games / 50 unplayed paired openings,
5+0.1**, unchanged candidate and baseline binaries, shared network and five-piece
tablebases, one worker, Hash 64 and 100 ms overhead each. Books, experience and
pondering remain off. Its protocol and scripts were frozen before play in
`lmp-frontier/sanity-5plus01/`. Starts are sampled without replacement from the
unused independent reserve, excluding all 34 opening endpoints with recorded
long-run games. The original pilot is also disjoint. There is no score-dependent
stopping; explicit user interruption or a hard eight-hour total wall limit
leaves an incomplete screen. The driver terminates its active process group on
interruption or deadline, preserving already recorded results. The expected
runtime from the pilot is roughly 30–45 minutes, not a promised deadline.

D7 is useful coverage for this frontier pruning and many established search
mechanisms, but nominal depth is not evidence that every feature fired. Earlier
D4–D7 diagnostics, for example, recorded zero actual IID calls in this corpus.
The short screen records authoritative completed depths for both engines and
reports the fraction reaching D7+ in positions with at least ten pieces, keeping
direct depth-zero tablebase returns and cheap depth-128 ceiling samples separate.
Source-level and guard-test evidence establish that the candidate rule can run;
reaching D7 alone does not establish exhaustive search-feature coverage.
Production remains frozen while this pragmatic screen runs.

The replacement 5+0.1 sanity screen completed all 100 games / 50 pairs and passed
its game/configuration/identity/adjudication audits: **28 wins, 48 draws, 24 losses
(52%)** for the candidate, with no errors, time forfeits or unfinished games.
There were 13,674 legal plies including 1,600 prescribed opening plies. The
conservative fixed-sample opening-pair Hoeffding 95% score interval is
32.79–71.21%; the small positive raw score is not an established playing gain.

For positions with at least ten pieces and positive completed depth below the
128 ceiling, candidate and baseline both had median depth **8**. Candidate
reached D7+ on 3,841/4,180 searches (91.89%); baseline on 3,830/4,178 (91.67%).
Means were 8.4294 and 8.4064 respectively, across different game trajectories.
At the 50 identical starting roots, mean completed depths were 7.70 and 7.72
(medians 8 each), with equal initial clocks but engine-selected per-move thinking
times. There is no demonstrated depth advantage. Direct depth-zero returns and
ceiling-depth samples are kept separate. The descriptive ceiling-node range in
the inherited `coverage.json` limits sentence was pilot-specific; the corrected
range for this screen is recorded in `coverage-note.json` without rewriting any
frozen script or raw game data.

This passes the pragmatic reliability/depth sanity screen, but neither proves
non-inferiority nor a small strength gain. Retain the candidate privately as
unconfirmed; do not promote it based on 52% or node savings alone. Production
remains unchanged. The long confirmation stays stopped and archived, and no
further match was automatically started.

#### Current two-stage screening protocol

| Stage | Pure SPRT (logistic Elo) | Alpha / beta | Clock | Hash per engine |
| --- | --- | --- | --- | --- |
| 1: screen candidates | 0 / +10 | 0.10 / 0.10 | 5+0.05 | 32 MB |
| 2: later verification | 0 / +5 | 0.05 / 0.05 | 10+0.1 | 64 MB for now |

Both stages use one search worker per engine. Future parallel verification means
independent simultaneous games; this is not implemented by the current serial
runner and does not imply engine search threading. Stage 2 must not start
automatically. The user's planning ranges of 150-400 and 1,500-3,000 games are
not guaranteed stopping counts.

Stage 1 uses pure SPRT only: no hybrid/custom confidence-bound stop and no
100-game stopping condition. Whole-pair likelihood boundary crossing stops play;
the eight-hour hard wall limit or finite opening-input exhaustion without a
decision means inconclusive. H0 rejects the candidate for this screening round,
with code/results archived, not proof of regression. H1 shortlists a candidate,
not a certified minimum +10 gain or automatic production adoption.

The user requested retrying ORIGINAL depth-1 quiet pruning first. The initial
UHO/pure-SPRT retry used the old Hash64 setting, then was stopped for the user's
Hash32 preference after 22 games: 3 wins, 5 draws, 14 losses, LLR -0.321116
against boundaries about +/-2.197225, no decision. All results and protocol are
retained in lmp-frontier/stage1-sprt/ and its verified archive
archives/stage1-sprt-hash64-stopped-2026-10-18.tar.gz. No data were pooled or
discarded based on score. The Hash32 restart in stage1-sprt-hash32/ uses the
remaining 9,989 unplayed endpoints from the UHO reserve, excluding all eleven
previously played endpoints, and the exact original LMP candidate binary.
All other resources, model, native search settings and baseline remain pinned.

The interrupted UHO retry used UHO_Lichess_4852_v1.epd, not a verified book named
UHO_LMR_2022. The selected book and its provenance were checked directly.
Official download SHA384 matches books.json SRI; provenance is retained in
rfp-improving/book-provenance.json. This 2,632,036-position FEN book supplies
original clocks and either side to move, without reconstructed pre-FEN history
or a quiet-only filter. Analysis and audits handle generic FEN roots accordingly.

The new improving-RFP candidate remains PARKED, with no match games. Its only
change is remaining-depth-1 margin100cp->0 for improving cached same-side eval,
comparing two plies earlier, falling back four plies, unavailable history=true,
equality=false. All existing guards and other depths remain unchanged.
Disabled mode exactly matched 350 frozen D1-D7 comparisons; independent audit
passed all 1,050 completed legal searches. Enabled nodes at D1-D7:
2,656 / 12,244 / 62,562 / 146,610 / 410,970 / 1,187,815 / 2,652,462.
D7 saved 33.63% nodes with eight changed best moves and 25 changed scores.
These are not strength/NPS claims. Its new guard test passed 1,874 probes.
Debug/Release each passed 36/37 initial checks, with the policy-aware horizon
test then updated privately for the explicit zero-margin rule and exact-history
isolation from approximate RFP; corrected horizon checks passed both builds.
Initial failures remain in logs. No public source/test files or production
binaries changed. This RFP rule is NOT included in the active original-LMP test.

#### Current opening policy: 8moves_v3

The user explicitly stopped and rejected UHO_Lichess for the present testing
phase and selected 8moves_v3. Use 8moves_v3 for subsequent smoke/verification
work unless the user requests a change. UHO is deferred until the user revisits
it after establishing higher engine strength; do not infer a rating threshold
from self-play screening or treat 3200 as an experimentally established cutoff.

The Hash32 UHO retry was stopped without a statistical decision after 29 recorded
games (13 wins, 9 draws, 7 losses; 14 complete pairs plus one unpaired game).
All raw data, frozen inputs and status are preserved in stage1-sprt-hash32/
and archives/stage1-sprt-uho-hash32-stopped-2026-10-18.tar.gz. No UHO results are
pooled into the replacement 8moves test.

The active original-LMP retry is now lmp-frontier/stage1-sprt-8moves/. Settings
remain pure SPRT 0/+10 with alpha=beta0.10, 5+0.05, Hash32MB per engine, one
search worker, shared model/five-piece tablebases, books/experience/pondering off,
100ms Move Overhead each. No custom confidence-bound stop or100-game cap is
enabled. SPRT decides after complete pairs; eight-hour wall timeout or natural
input exhaustion without crossing means inconclusive. Stage2 remains deferred.

The original8moves_v3 archive SHA256 remains
7e1e9dd118b4bb97d8a8b5b8a790c86e21f8509d59a27d2883767d94477be02e.
A fresh10,000-pair input reserve excludes previous reserved opening/corpus roots.
An independent preflight audit checked all10,000 lines for16 legal prescribed
plies, unique nonterminal endpoints and no repeated positions using effective
en-passant repetition keys. No custom quietness/evaluation filter was added.
Opening histories are preserved in PGN/UCI searches; depth analysis accounts for
the16 prescribed plies. Inputs/scripts were frozen before any replacement games.
Production remains unchanged. No UHO run or improving-RFP match remains active.

The 8moves_v3 fixed-four LMP SPRT is now retired at the user's request. It
stopped earlier on a technical response timeout, with 159 finished games
(candidate 42 wins, 49 draws, 68 losses) and one unfinished game. The 79
complete pairs scored 41.772%; LLR was -1.281849 against boundaries
-2.197225/+2.197225. Neither boundary was reached: this is an unfavorable
interrupted screen, not an SPRT rejection. All original failed outputs are
preserved, with a verified archive; no continuation games were launched.
The user authorized a separate diagnostic of skipped quiet moves, including
whether they would cause a frontier beta cutoff and whether existing futility
pruning would independently skip them. No new match or production change is
authorized by that diagnostic.

#### Skipped quiet-move diagnostic after retiring the LMP screen

A separate private instrumented copy in `lmp-order-audit/` tested every LMP
skip on the same fifty diagnostic roots at D5, including all iterative-deepening
passes. Settings were cold TT/history, Hash64, shared NNUE, tablebases/books/
experience off. These are diagnostic searches, not the timed match's Hash32,
tablebase-enabled, warm game states. Each skipped move was probed through the
original depth-zero PVS/quiescence path in a separate owned worker, preserving
board-stack history, repetition boundaries, killers/history, draw perspective
and NNUE state. No probe reached its 200,000-node cap.

All 100 comparisons (50 without probes, 50 with probes) exactly reproduced the
frozen candidate's scores, best moves, full PVs and node counts. Independent
audit passed all 132,977 legal nonchecking quiet-move records and input hashes.
The probes found 8,282 cutoff-producing alternatives. Existing score-based
forward futility independently skips 1,744 of the logged moves, including 37 of
those alternatives. Excluding these redundancies leaves 8,245 missed local
cutoffs among 131,233 additional LMP skips (6.283%). They occur at 1,644 of
4,693 node visits with LMP skips (35.031%), across 47 of the fifty roots. Several
alternatives at one node are not independent losses or additive errors.

Of all logged skips, 129,747 (97.571%) had zero history scores. At 88.678% of
skips, fewer than four noncapturing/nonpromoting/noncastling/nonchecking moves
had actually been searched: tactical moves also consume the four-move budget.
There were 7,465 additional missed cutoffs in that group. Even after four such
quiet moves, the probes found another 779 additional missed cutoffs (and one
after five). Thus counting tactical moves is not the whole explanation.

Maxima clears history on every new search worker. This diagnostic supports investigating ordering and context-sensitive
pruning; it does not establish that continuation history fixes the misses or
that these shallow local misses caused the interrupted SPRT's unfavorable score.
Local frontier bounds are not objective best moves or deeper tactical truth.

The initial check run retained five failures: three isolated search-contract
tests require LMP disabled (all three then passed, matching the original
candidate's documented isolation); two lifecycle pause tests were disturbed by
the diagnostic CPU throttle's SIGCONT (both full suites passed when rerun on a
single low-priority CPU without external pause signals). The other 32 checks
passed initially. A separate shadow-versus-direct-search test also passed; its
root and non-root/repetition/null-history probes are recorded privately. No
production source, frozen candidate, installed engine or match results changed.
The user retired the SPRT; no new match was started.

#### Quiet-count and history-guard LMP follow-up diagnostic

The user authorized a separate private candidate requiring four completed quiet
move searches before frontier LMP, and protecting positive-history moves from
LMP. Here quiet means noncapturing, nonpromoting, noncastling and nonchecking;
advanced passed-pawn quiet moves count when searched but remain protected from
pruning. The counter advances after a completed search, never for an LMP- or
forward-futility-pruned move. Other safety guards and existing score-based
forward futility remain unchanged. The original rule deliberately counted all
searched moves; this is a policy experiment rather than a miscount correction.
Positive history protection applies to LMP specifically.

On the same fifty reused cold D5 roots and original diagnostic settings, the
independent audit passed 115,478 skipped-move probes with no aborts. It found
6,471 local cutoff-producing alternatives, or 5.604%, versus the original
8,245/131,233 additional skips (6.283%). No revised skips overlapped existing
forward futility, occurred before four completed quiet searches, or had positive
history. Misses occurred at 1,391/4,355 node visits with skips (31.940%), across
47/50 roots. Multiple alternatives at a node are not independent errors.

The two new guards therefore hold, but the conditional local miss fraction
fell only 0.679 percentage points, not below 1%. Revised searches visit different
nodes: these fractions are not a matched counterfactual on one unchanged tree.
Ordinary search nodes rose from 430,249 to 447,748 across the fifty roots
(+4.067%); the unpruned control used 587,707. Seven best moves and eleven scores
changed relative to the retired candidate. These work counts and shallow
selective bounds do not establish tactical truth, NPS or playing strength.
Zero history also does not establish random ordering: hash, killer and generated
move order still matter. This result does not attribute the interrupted match's
unfavorable score to a specific mechanism or establish a 58-Elo strength loss.

All fifty LMP-disabled signatures matched the frozen production control. All
fifty revised plain-versus-shadow signatures matched exactly in score, best
move, full PV and nodes, and all PVs replayed legally. The independent audit
reconstructs previously searched quiet moves from the searched-move list,
separately from the new loop counter. The policy guard test passed 120 cases
covering zero/positive history and safety exclusions, and the unchanged shadow
versus direct-search harness passed 724 root/history/null probes. Artifacts are
kept separately in the private `lmp-quiet-four-audit/` experiment; original audit
and match artifacts, production source and production binaries are preserved.
No strength match, installation or continuation of the retired SPRT is part of
this task. Diagnostic resource limits remain one low-priority CPU at 50% duty,
with lifecycle checks exempt from external pause signals.

Required algorithm reading revisited:
[CPW Futility Pruning](https://chessprogramming.org/Futility_Pruning), including
move-count pruning. Source comparisons are retained in private development
records; they do not establish a gain or safe miss-rate threshold in Maxima.

The revised private Release candidate passed all 37 existing CTest checks
(35 non-lifecycle plus both full lifecycle suites), with the three previously
documented unrelated search-contract fixtures isolating LMP. The shadow harness
also passed all 724 probes. No new Debug, sanitizer, race or timed-strength run
was performed; no shared-state or ownership changes were introduced. Original
and production identities were rechecked after completion.

Quiet-rank analysis of the revised audit found a broad distribution, not a
pile-up just beyond the gate. Ranks exclude captures/checks/promotions/castles
and include searched protected advanced-pawn quiets. For every record, full
encounter rank equals searched moves plus earlier logged skips plus one,
verifying no unlogged pruning gaps before reconstructing quiet occurrence rank.
The 6,471 cutoff alternatives break down as quiet ranks5-7:700(10.817%),
8-10:731(11.297%),11-15:1282(19.811%),16-20:1083(16.736%),
21-30:1698(26.240%),31+:977(15.098%). Median missed rank18;90th percentile33;
maximum51. Per-skipped-move cutoff fractions stay roughly5.2-6.1% across these
rank buckets, with no sharp decline after the first few skipped quiets.
The first missed cutoff per affected node has median rank11;470/1391 affected
node visits(33.789%) have their first miss at5-7, and724/1391(52.049%) first
miss at11+. Several alternative cutoff moves at one node are not independent
losses. This describes the recorded tree, not the effect of changing the gate;
raising the threshold would require a new experiment. The rank analysis preserves the original log and does not run new searches.

#### History lifetime review and isolated normalization candidate

The history review separates persistence, bounded signed reward/penalty updates
and explicit new-game resets from the choice of continuation context. Source
comparisons and pinned revisions are retained privately.

Maxima production instead starts history empty at every new worker/go and uses
positive rewards plus a penalty shift by the total previous searched-move
count. It retains history within iterative deepening during one go. A first
reward31 can be erased by a five-bit shift; lifetime, update scale and context
matter more than the raw magnitude. No measured causal share of the97.57% skipped-move
sample or Elo loss follows from this source comparison.

The user prioritized normalizing history and SPRT-testing it before LMP. A
private standard-only candidate preserves the existing piece/destination key,
copies history only after joining the old worker, and clears it on newgame.
Its signed gravity update uses limit16384 and a provisional bonus
min(32*clamp(depth,1,8)^2,2048), with equal negative updates for earlier searched
quiets. It protects the winning key from penalties when two moves alias that
entry. Royal-losers evaluation/history behavior and all other search policies
remain unchanged. LMP and continuation history are absent from both match
engines. This is a heuristic candidate, not a proven strength fix.

Release37/37 checks passed, including both lifecycle suites and60 independent
controlled parent-history cases. The existing shift-specific regression selects
the retained legacy-update control; the added test verifies the signed policy.
UCI persistence/new-game reset checks and50 exact frozen-baseline coldD5 controls
passed. No new Debug/sanitizer/race run was performed. The next authorized
strength screen uses fresh8moves_v3, pure paired0/+10SPRTalpha=beta.10,
5+.05seconds,Hash32,shared model/five-manTB and an8h hard wall limit. No automatic
installation, stage2 or LMP retest follows. Production source/binaries remain
unchanged while this candidate is evaluated.

The history-only stage-one screen has now started after all validation gates.
Preflight passed and the driver reached timed games. The fresh reserve contains
10,000 independently checked legal16-ply8moves_v3 histories (160,000 opening
plies), excluding90,822 previously reserved position keys. Seed2026101901;
input identities/scripts/options were frozen before launch. The eight-hour
budget includes preflight. Both engines run on one low-priority CPU without
external pause throttling during timed play. A symmetric180s technical UCI
response deadline does not relax chess-clock forfeits. There is no result yet;
completion requires the subsequent independent PGN/options/hash/all-prefix-SPRT
and telemetry audits. No new strength claim or production adoption follows
merely from launching this screen.

### Structural move-picker refactor (2026-10-19)

See [move-picking contracts](docs/move-picking.md). Standard-chess picker output
is now pseudo-legal; PVS and qsearch own make/test/unmake legality. Root/book
consumers explicitly filter legality, and terminal decisions count legal play.
Castling retains origin/transit safety checks in search. No speed or strength
gain is established by this architectural change. The history-only SPRT was
cancelled by the user without a statistical verdict; its candidate is unpromoted.

The structural experiment now also separates positive quiets from deferred
negative-SEE noisy moves, adds countermoves and worker-owned signed main,
one-ply continuation and pawn-structure history, and transfers history between
standard searches until newgame. Cached pawn hints receive structural geometry
checks. A qsearch empty-tactical fail-low inconsistency found by the legality
ablation has a regression; before/after legality now produces identical paired
trees. These are local development changes, not an installed-engine promotion.

Final validation: 37/37 Debug, 37/37 Release, 3/3 ASan/UBSan (leak detection
disabled), independent oracle 4,348 positions/95,618 legal children, UCI
cold/newgame exactness and 72 paired D7 signatures matching nodes/scores/best/PV.
A 12-root cold D7 diagnostic with three alternating trials measured +4.1%
nodes versus the frozen original; positive quiets before bad noisy moves used
+1.1% nodes versus late quiets with the same history implementation. Its original
+22.8% median CPU estimate included pending option/model configuration because
the timing wrapper lacked a readiness barrier. That estimate does not isolate
search cost and must be remeasured before attributing a CPU regression. Node
counts and paired tree-equivalence results remain valid. Duty-throttled wall
times are not ordinary NPS measurements. No strength gain is established. No new
match or installation was performed for this experiment.

### Capture ordering history (2026-10-20)

`CaptureHistory` defaults true for standard chess. A worker-owned bounded table
learns which captures/promotions cause completed main-search beta cutoffs;
previously searched noisy alternatives receive penalties even when a quiet move
wins. Context includes moving piece/color, target and victim type, with explicit
en passant and promotion handling. History changes ordering within the existing
SEE stages; it does not change legality or the SEE threshold. Quiescence reads
history without training it. Verification/IID probes, aborted nodes and unsearched
cutoffs do not train capture history.

Normal searches retain capture and quiet history by transferring ownership after
joining the previous worker; `ucinewgame` resets both. Disabling `CaptureHistory`
skips scoring and learning and restores the previous noisy ordering. Changing
the option stops active search and clears main/qsearch TT entries. Royal-losers
keeps its existing policy. Constants remain provisional; playing-strength
acceptance and parameter tuning are pending. See [move-picking.md](docs/move-picking.md).

Capture-history validation passed 43/43 Debug and 43/43 Release checks, eight
focused ASan/UBSan checks (leak detection disabled), eight audit checks, and the
independent oracle's 4,372 positions/95,734 legal children. Audit builds count
lookups/nonzero hits, rewards/penalties, noisy cutoffs and searched cutoff ranks.
A real-search D6 audit recorded 274,620 lookups, 141,158 nonzero hits, 8,866
rewards and 1,253 penalties; all capture-history counters were zero when off.

A fixed-depth diagnostic used 12 reserved D8 roots, three alternating cold
trials and one retained-history trial per profile, with LMR on/off, a frozen
parent, and capture history on/off. Retained trials warmed each root at D6 then
searched its fixed legal two-ply continuation at D8. All 288 searches completed
legally; all 96 disabled-control signatures matched the parent exactly in
nodes, score, best move and PV. Cold enabled nodes increased 13.92% with LMR
and 1.20% without it; retained nodes increased 6.06% and 0.95%, respectively.
Initial median cold CPU increased 58.89%/1.18%; retained CPU increased
1.59%/16.19% in its single trial. A bounded three-pair LMR-on confirmation
reproduced all 72 per-variant signatures and measured median CPU 2.65 seconds
off versus 3.28 seconds on (+23.77%). The original results remain recorded.
Timing used a readiness barrier and one low-priority CPU without pause
throttling. These short, variable samples show no efficiency benefit; they do
not establish playing strength or isolate the cause of the LMR-dependent tree
changes. Baseline acceptance remains pending. No parameter tuning, timed match
or Windows installation accompanied this increment.

### Longer continuation ordering history (2026-10-20)

Standard quiet ordering now includes move pairs from two and four plies back.
The original `ContinuationHistory` option still controls the immediate preceding
move; the independent `LongContinuationHistory` checkbox controls both scoring
and learning for the longer contexts. Both default to true. Each worker snapshots
these settings; a policy change stops and joins the worker and clears main and
quiescence TT data. Dormant history remains available when re-enabled, and
`ucinewgame` clears it together with the other retained history.

The existing colored-piece/destination pair table serves all three distances,
without allocating another table. Immediate pairs use opposing colors; two- and
four-ply pairs use the same color. Both longer distances contribute equally and
receive the existing bounded gravity bonus `32 * clamp(depth, 1, 8)^2` and matching
penalties for previously searched quiet alternatives. These weights are provisional.
Failed alternatives sharing the winner's piece/destination do not penalize its
slot. One accessor supplies the same contexts to scoring and learning. It reads
only actual search-stack moves, stops at the root and never crosses a null pass.
Captures, promotions and castling may supply prior context using the recorded
original mover; current quiet eligibility remains unchanged.

Longer contexts learn only at completed ordinary PVS quiet beta cutoffs.
IID and verification helpers, cancelled nodes and unsearched TT/static/null
cutoffs do not train them; quiescence can read quiet history without learning it.
Existing immediate-history behavior and royal-losers search remain unchanged.
Audit builds count lookups, nonzero scores, rewards and penalties separately for
two/four plies, plus participating quiet cutoff ranks and first-move cutoffs.
Implementation and correctness validation remain separate from efficiency and
playing-strength acceptance of the combined baseline.

Validation passed 43/43 Debug and 43/43 Release checks, including both lifecycle
suites, eight focused ASan/UBSan checks (leak detection disabled), eight audit
checks, and the independent oracle's 4,372 positions/95,734 legal children.
Native tests cover root/null boundaries, independent one-ply/longer switches,
worker snapshots, cutoff rewards/penalties, protected winner aliases, special
prior moves, helper isolation, retained ownership and fresh-history clearing.
A real-search audit across 12 depth-six roots recorded 793,447 two-ply lookups
and 672,886 four-ply lookups, with nonzero scores and both rewards and penalties;
all longer-history counters were zero when disabled.

The frozen-parent comparison completed 432 legal depth-eight searches: three
cold trials and one retained-history trial across nine profiles. Retained trials
warm each root at depth six before searching a fixed legal two-ply continuation.
All 144 disabled comparisons matched parent nodes, scores, best moves and full
PVs exactly, including LMR on/off and capture-history-off controls. With LMR on,
the capture/longer-history interaction produced these total nodes over 12 roots:

| Capture history | Longer continuation | Cold nodes | Retained nodes |
|---|---|---:|---:|
| Off | Off | 2,095,218 | 1,480,071 |
| On | Off | 2,386,835 | 1,569,792 |
| Off | On | 2,267,725 | 1,546,675 |
| On | On | 1,983,516 | 1,600,988 |

Relative to capture history alone, longer continuation changed cold nodes by
-16.90% with LMR and -1.57% without it; retained nodes changed by +1.99% and
-3.32%. Corresponding process-CPU changes were -11.78%/+6.29% for cold medians
and +3.03%/-1.84% in the single retained trial. Cold CPU ranges overlapped and
varied substantially. Both histories together searched fewer cold nodes than
neither, while retained searches grew. This small corpus demonstrates interactions
and mixed costs; it establishes neither a general speed gain nor playing strength.
The combined baseline still requires integration and strength acceptance.

### Shared move-history access (2026-10-20)

Standard search exposes `move_history_score(move)` as the common learned-history
query for ordering and search policies. Query it in the parent position before
making the move. Captures and all promotions use capture history; ordinary
quiets and castling use main, pawn and enabled one/two/four-ply continuation
history. En passant uses its recorded victim, and a non-capturing promotion uses
the empty-victim capture-history slot. Disabled components contribute zero.
Root/null boundaries, worker snapshots, ownership and learning rules are unchanged.

The query ignores `move.score`: the picker adds material and SEE stage offsets
separately. TT, killer and countermove candidates can therefore be queried without
first passing through batch scoring. Reads do not train history or change board
state; audit builds retain their existing lookup/hit counters. The current fixed
LMR and pruning policies do not yet consume learned scores. Their later changes
will use this interface with separately validated eligibility and scaling.
This consolidation adds no option or search heuristic and keeps the existing
component controls and royal-losers ordering.

Validation passed 43/43 Debug and 43/43 Release checks, both lifecycle suites,
eight focused ASan/UBSan checks (leak detection disabled), eight audit checks,
and the independent oracle's 4,372 positions/95,734 legal children. A native
regression covers all 32 history-policy combinations, special-move dispatch,
independence from picker scores, and unchanged tables/board state. All 144 paired
cold/retained comparisons at depth eight matched parent nodes, scores, best moves
and full PVs exactly, with LMR on/off and learned histories off. Default cold CPU
median changed +0.36%; LMR-off cold changed +10.25%, then +4.26% in one bounded
confirmation with 36 further exact pairs and overlapping CPU ranges. Retained
CPU changed +2.21%/-1.23% with LMR on/off. These short, variable samples establish
neither a general speed gain nor playing strength; combined baseline acceptance
remains pending.

### Threat and checking-move ordering validation (2026-10-20)

Validation passed 44/44 Debug and 44/44 Release checks, including both lifecycle
suites; nine focused ASan/UBSan checks (leak detection disabled), nine audit
checks, and an independent oracle covering 4,372 positions/95,734 legal children.
The new scoring regression compares batch scoring against a per-square attack
oracle for all four policies and preserves candidate sets, board and history.
The existing learned-only tie/order regression explicitly disables both new
context switches.

A 12-root D8 matrix completed 480 legal searches: three deterministic cold
trials and one retained-history trial per profile, with parent/off/threat/check/
both and LMR on/off. All 96 disabled signatures matched the parent exactly.
Retained trials warm each root at D6 then search a fixed legal two-ply continuation.
Compared with the disabled candidate, both features reduced cold nodes by
9.20%/1.04% with LMR on/off; retained nodes changed by -5.14%/+1.92%. Cold
median CPU changed by -7.42%/+1.52%; retained CPU by -2.83%/+2.77%.
Compared directly with the frozen parent, both-feature CPU changed by
-5.20%/+4.12% cold and +2.49%/+3.21% retained. Disabled structural CPU
cost was +2.40%/+2.56% cold; cold timing ranges overlap the parent. Timing
used a readiness barrier and one low-priority CPU without pause throttling.
These short, mixed samples establish no general speed or strength gain.

A real-search D6 audit over 12 roots recorded 1,093,687 scored quiet candidates,
21,292 net threat escapes, 138,182 net threat entries, and 3,114 SEE-passing
checks out of 8,758 checking candidates with both features enabled. Ordinary
quiet cutoffs numbered 12,616, with rank sum 18,329 and 10,518 first-move
cutoffs; helper searches are excluded. Disabled context counters were zero.
Constants remain provisional; no parameter tuning, timed match or installation
accompanied this increment. Integrated baseline and strength acceptance remain
pending.

### Evaluation correction history (2026-10-20)

`CorrectionHistory` is a default-on standard-chess checkbox. A worker-owned
448 KiB set of tables indexed by side to move and pawn, piece-group and move-pair
features learns a bounded estimate of static-evaluation error. It adjusts main-search and quiescence static
scores; it changes neither legal moves nor draw, mate or tablebase adjudication.
The independent switch skips both correction lookups and learning when disabled.
Changing it stops the worker and clears incompatible main and quiescence TT
results. Royal losers retains its existing evaluator and allocates no table.

Each table has 16,384 signed entries per side, bounded at 16,384 units with
128 units per centipawn. Updates use history gravity with a bonus equal to
`error * clamp(depth, 1, 16) / 8`, capped at one quarter of the table bound.
These are provisional constants awaiting integrated validation and tuning.
Feature-hash collisions intentionally share a heuristic estimate, not an exact
score. Piece-group and continuation domains are described below.

Raw NNUE cache entries stay unadjusted. Search-frame `eval_result` retains its
existing uncorrected, reversible-clock-damped value, metadata and improving
comparison contract. Every evaluation read applies the current table correction,
including reads of an already evaluated frame. The correction receives the same
clock decay and the result stays below the decisive tablebase score band. This
keeps raw inference checks independent of learning and avoids stale or repeatedly
applied corrections. Fixed and aligned audit horizons bypass correction and
learning. Normal audit modes still verify raw incremental/full evaluation parity.

Only completed ordinary main-search move results can train the table. Require
a position where the side to move is not in check, a quiet best move, valid
nondecisive evaluation/result,
and a reversible clock below 90. Captures and promotions cannot train; castling
can. A lower bound trains upward only when it exceeds the current corrected
static evaluation; an upper bound trains downward only when it falls below it.
Exact results can train in either direction. Descendants may update the same
bucket, so compare against a fresh correction of the parent's cached raw value.
Terminal, repetition-floor, TT, static-pruning, null-cutoff, quiescence and ceiling
returns do not train. Aborted nodes, IID and verification helpers, excluded-move
searches and null-move subtrees cannot train either. Updates precede killer
mutation, preserving the best-move classification when picker storage aliases.

History ownership transfers after joining the old worker. Correction transfers
only when both workers share the same immutable network snapshot. A successful
model replacement starts correction cold; a failed replacement retains the
previous model. `ucinewgame` discards the worker and its learned state. Existing
searched TT scores remain reusable while online learning evolves, as with move
ordering histories; correction updates do not clear the table or cache adjusted
static scores. This is selective-search reuse, not a claim of history-independent
minimax results.

Audit builds expose `audit_correction` lookup, nonzero-hit and changed-update
counts, with lower/upper/exact update counts. Native regressions cover table
arithmetic and aliases, raw-cache stability, fresh corrected reads, clock and
score limits, update eligibility and direction, controlled PVS outcomes and
restoration, nonlearning returns, worker controls and model ownership.

Validation passed 45/45 Debug and 45/45 Release checks, including both lifecycle
suites, 11 focused ASan/UBSan checks with leak detection disabled, and 11 focused
audit checks. The independent board oracle covered 4,372 positions and 95,734
legal children across both variants. The existing synthetic extreme-output NNUE
regression now verifies the corrected decisive-band limit alongside the original
disabled/raw limit and model/context identity.

All 288 depth-eight diagnostic searches completed with legal PVs; 216 cold
searches repeated deterministically and 96 disabled-control signatures matched
the frozen parent exactly. The matrix used 12 reserved roots, parent/off/on
controls with LMR on/off, three cold trials and one retained-history trial after
a depth-six warmup and fixed legal two-ply continuation. Configuration completed
before CPU timing; all work ran sequentially on one low-priority CPU.

Enabling correction increased cold nodes by 18.25%/6.14% and median CPU by
16.73%/5.74% with LMR on/off. Retained nodes changed by +7.38%/-0.79%, with CPU
+7.69%/+7.95%. CPU samples were short and retained timing used one trial. These
results show added cost on this sample; efficiency and playing-strength
acceptance remain pending. A real depth-six audit recorded 231,628 lookups,
11,203 nonzero hits and 2,297 changed updates: 1,693 lower, 457 upper and 147
exact. All correction counters were zero with the feature disabled.

### Adaptive reductions and verification feedback (2026-10-20)

`AdaptiveLMR` replaces fixed-one interior reductions with the depth/move-count,
learned-history and node-context policy documented in chapter 4. `LMRHistory`
adds directional continuation feedback after completed full-depth verification.
Both default true, have independent worker flags and clear incompatible TT
entries on change. The old policy is available with both false; `LMR=false`
disables both consumers. Tactical protections, full-depth verification, clock
propagation and cancellation/restoration retain their contracts. Constants are
provisional; integrated playing-strength acceptance remains pending.

Validation passed 46/46 Debug checks (191.05 s), 46/46 Release (109.29 s),
13 focused ASan/UBSan checks (57.22 s, leak detection disabled), 15 audit checks
(22.44 s), and the independent oracle covering 4,372 positions/95,734 legal
children. The first Release run passed 44/46: both lifecycle suites encountered
five-second startup/model-loading/shutdown timeouts. An unchanged full repeat
passed, with no timeout or assertion changes; original logs are retained.
The native reduction test includes 108 controlled PVS verification/cancellation
cases and 48 feedback-eligibility cases, plus directional history, component,
null-boundary and worker-option checks. The certified selectivity suite now
exercises both reduction policies, including tactical exclusions and restarts.

A 12-root matrix compared the frozen parent and the same binary with both off,
adaptive only, feedback only and both enabled. Master-LMR-off controls were
also included. Three alternating cold D8 trials and one retained-history trial
per profile completed 384 legal searches. Retained trials warm each fresh root
at D6, play the same fixed legal two-ply continuation and search D8. All 288 cold
searches were deterministic; 144 disabled-parent signatures matched nodes,
scores, best moves and full PVs exactly. Measurements used one low-priority CPU,
a readiness barrier before process-CPU timing, and no pause throttle.

Relative to the same-binary disabled control, the combined policy changed cold
nodes by -48.35% and median CPU by -43.75%; retained nodes changed by -46.33%
and CPU by -48.43%. Adaptive-only nodes changed by -44.91%/-45.12% cold/retained;
feedback-only changed by -3.46%/+1.68%. Their cold CPU medians changed by
-37.85%/+1.39%, and retained CPU by -46.64%/-0.45%. This interaction supports
measuring the combination rather than judging feedback solely in isolation.
Compared with the complete frozen parent, combined CPU changed by -47.57%
cold and -46.51% retained. With LMR disabled all trees matched exactly, while
CPU varied: both-on versus both-off differed by +9.15% cold/+6.58% retained.
The short, tick-quantized samples and single retained trial limit timing claims;
smaller trees do not establish playing strength or generator throughput.

The real D6 activation audit recorded 7,424 combined reduced trials, 2,514
reducing more than one ply, 110 full-depth verifications, 55 refutations and
11 verified cutoffs. Eligible ordinary searches taught 11 continuation rewards
and 36 penalties. Learned-score reads had 5,937 nonzero hits out of 7,940;
six reached the positive modifier threshold and none the negative threshold.
Native seeded-history contracts cover both signs. Disabled consumers had zero
activity. Audit counters distinguish accepted legal trials from parent history
reads that can include rejected pseudo-legal attempts. TT history feedback,
remaining baseline mechanisms and final integration/strength validation remain
pending.

### Internal iterative reduction baseline increment — 2026-10-20

The standard default now selects IIR in place of IID, with the explicit option
matrix documented above. Correctness gates passed:47/47 Debug (161.91s),47/47
Release (95.62s),15 selected ASan/UBSan checks (51.51s, leak detection off),17
audit checks (21.54s), and the independent board oracle (4,372 positions/95,734
legal children,30.993s). Native coverage includes864 controlled IIR/IID cases
across both colors, searched-depth TT storage, upper/exact/lower bounds,
cancellation and restoration. Changed C++ files passed the brace scan.

Twelve reserved roots produced288 legal completed D8 searches:three alternating
cold trials and one retained-history trial per policy. Retained searches warm
each root at D6, advance a fixed legal two-ply continuation, then search D8.
The frozen parent has IID on/off controls; the candidate covers neither,
IID-only, IIR-only and both.96 disabled-parent signatures matched nodes, scores,
best moves and full PVs exactly;48 IIR-only/both signatures also matched exactly.
One low-priority CPU and a readiness barrier preceded process-CPU sampling.

| Policy | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---:|---:|---:|---:|
| Parent, IID on | 1099908 | 1.64 | 875285 | 1.17 |
| Parent, IID off | 1099908 | 1.63 | 876623 | 1.22 |
| Neither | 1099908 | 1.64 | 876623 | 1.25 |
| IID only | 1099908 | 1.62 | 875285 | 1.23 |
| IIR only | 1099216 | 1.59 | 858115 | 1.23 |
| Both (default IIR precedence) | 1099216 | 1.78 | 858115 | 1.17 |

The default reduced nodes by0.063% cold and1.962% retained versus the frozen
parent. Default cold CPU samples were1.65/1.90/1.78s versus parent1.61/1.66/1.64s;
the median rose8.537%, while retained CPU matched. IIR-only has identical trees
to both-on but different short CPU samples (1.59/1.89/1.56s), illustrating timing
variability. These tick-quantized samples and one retained trial do not establish
a speed improvement or playing-strength gain. No generator-throughput or perft
performance claim is made. Integrated strength acceptance remains pending.

Direct production D7 probes recorded35 reductions (11PV/24cut),12 reduced-node
cutoffs and no IID calls with IIR enabled; both-on matched IIR-only. IID-only
recorded13 helper calls/112,131 visits; disabled IIR counters were zero. Native
tests additionally cover cancellation and terminals absent from this sample.
Initial test-fixture clock/cancellation assumptions and a test-expansion compile
error were corrected before full validation; runtime policy was unchanged.
No timed match, tuning or installation was performed in this increment.

### Main-search SEE pruning validation — 2026-10-20

Standard search now defaults `MainSEE=true`, with an independent worker snapshot
and policy-change TT clearing. The conservative policy and protections are
specified in chapter 4. Implementation and correctness validation are complete;
integrated playing-strength acceptance remains pending.

All 48 Debug tests passed in 196.35s and all 48 Release tests in 106.19s,
including both lifecycle suites. Sixteen selected ASan/UBSan tests passed in
60.61s with leak detection disabled; eighteen audit tests passed in 22.64s.
The independent oracle checked 4,372 positions and 95,734 legal children in
32.012s. Native tests include 120 controlled PVS cases, threshold/guard checks,
first-move and terminal safety, legality, clock-safe TT reuse, cancellation,
restoration and worker/variant/audit isolation. Review caught a PV node becoming
eligible after alpha rose to beta-1; a new regression failed before the explicit
original-PV guard and passed afterward. The final full gates include that fix.
Initial fixture mistakes and the superseded first Debug run remain recorded.

A frozen parent and same-binary SEE off/on controls were compared with LMR both
on and off: 12 reserved roots, three alternating cold depth-eight trials, plus
one retained trial per profile after a depth-six warm search and fixed legal
two-ply continuation. All 288 measured searches completed legally; all 216 cold
searches repeated deterministically. Ninety-six disabled-parent signatures
matched nodes, scores, best moves and complete PVs exactly. Measurements used
one low-priority CPU, no pause throttle, and a UCI readiness barrier before
process CPU sampling.

| LMR | SEE | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Off | 1099216 | 1.83 | 858115 | 1.32 |
| On | On | 1008871 | 1.61 | 785676 | 1.26 |
| Off | Off | 6255546 | 8.78 | 3996389 | 5.41 |
| Off | On | 5117158 | 8.87 | 3623621 | 5.81 |

Relative to the same-binary off control, enabled SEE with LMR changed cold/
retained nodes by -8.219%/-8.442% and CPU by -12.022%/-4.545%. Without LMR,
nodes changed by -18.198%/-9.328%, while CPU rose 1.025%/7.394%. Short,
tick-quantized CPU samples and one retained trial limit interpretation: even
identical disabled-parent trees had materially different timings. These results
do not establish general speed, generation throughput or playing-strength gains.
No additional tuning, timed match or installation was performed.

Direct production depth-six audit searches across the same roots recorded
21,066 legal SEE skips with LMR (2,602 captures and 18,464 quiets), from 92,815
pseudo-legal candidates. Without LMR, 114,537 legal skips were recorded. Actual
skip depths ranged from one to four in both samples; native tests cover the
eligibility boundaries through six. Disabled counters were all zero. Evidence,
source/binary/model/corpus hashes and initial logs are retained privately under
`.context/baselines/2026-10-20/main-see/`. Next are razoring, singular extensions
and ProbCut, with TT feedback and final integration/strength acceptance pending.

### Razoring with qsearch verification — 2026-10-20

Standard search now defaults `Razoring=true`, with worker-owned policy and TT
invalidation. The shallow eligibility and provisional margin are documented in
chapter 4. Implementation and correctness checks are complete; integrated
playing-strength acceptance remains pending.

Final checks passed: 49/49 Debug (179.26s), 49/49 Release (105.39s), 17 selected
ASan/UBSan tests (68.00s, leak detection disabled), 19 audit tests (30.38s), and
the independent oracle (4,372 positions/95,734 legal children, 38.465s). Native
coverage includes 96 controlled PVS cases, strict margin/depth boundaries,
qsearch fail-low/fail-high and cache modes, decisive-score rejection, complete
frame/history restoration, clock propagation, cancellation after legal captures,
terminal/draw precedence and worker/variant/audit isolation. UCI tests cover
policy-cache invalidation and changes during active search. The changed C++
files passed the brace scan. Initial test-fixture issues were corrected before
these final gates: isolate the SEE-only mock children from independent razoring,
and use a naturally eligible tactical position instead of overriding metadata.
The earlier logs remain available; assertions and deadlines were retained.

The frozen parent and same-binary razoring off/on were compared with main-search
SEE on/off: 12 reserved roots, three alternating cold depth-eight trials and one
retained-history trial per profile after depth-six warming and a fixed legal
two-ply continuation. All 288 measured searches completed legally; all 216 cold
searches repeated deterministically. All 96 disabled-parent signatures matched
nodes, scores, best moves and complete PVs exactly. One low-priority CPU and a
readiness barrier preceded process-CPU timing.

| Main SEE | Razoring | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Off | 1008871 | 1.72 | 785676 | 1.32 |
| On | On | 978174 | 1.93 | 729100 | 1.10 |
| Off | Off | 1099216 | 1.68 | 858115 | 1.42 |
| Off | On | 1195190 | 1.83 | 852893 | 1.41 |

With main SEE enabled, razoring changed cold/retained nodes by -3.043%/-7.201%
and CPU by +12.209%/-16.667%. Without main SEE, nodes changed by +8.731%/-0.609%
and CPU by +8.929%/-0.704%. Cold enabled CPU samples were 1.56/1.93/2.08s versus
1.72/1.63/2.28s disabled with SEE; identical parent/disabled trees also varied.
The short, tick-quantized CPU samples and single retained trial establish no
general speed or playing-strength gain. These interactions remain part of final
baseline acceptance. No generator-throughput claim, tuning, timed match or
installation was made.

Direct production depth-six probes with SEE recorded 471 attempts, 418 verified
fail-lows, 53 rejections and 958 qsearch visits, across remaining depths 1–3.
Without SEE: 478 attempts, 417 fail-lows, 61 rejections and 1,078 visits. Disabled
counters were zero. These non-iterative probes increased total nodes with SEE
(249,375 to 266,579), whereas the iterative depth-eight matrix reduced them;
search context affects the result. Native tests cover cancellation separately.
Evidence and source/binary/model/corpus identities are retained privately under
`.context/baselines/2026-10-20/razoring/`. Next are singular extensions and
ProbCut; TT feedback and final integration/strength acceptance remain pending.

### Singular extensions

`SingularExtensions=true` with `SingularPolicy=false` allocates one extra ply to a legal TT move when a
reduced search of all alternatives fails below a lower threshold. It uses only
compatible main-search lower or exact scores searched to at least `depth-3`;
ordinary TT cutoffs still require the full requested depth. An ordering hint
alone cannot nominate an extension. The provisional threshold is `tt_score-depth`
in the engine's pawn-100 scale, and the excluded search has depth `(depth-1)/2`
and window `[threshold-1, threshold]`. These values await integrated tuning.

Eligibility requires a non-root, non-check node at depth seven or greater,
a nondecisive window, reversible clock below 99, and stack room. The current
ply must be below twice the attempted iteration depth; direct PVS calls without
an iteration use remaining depth as the limit. IID, verification subtrees,
local exclusions and an upcoming-repetition draw floor disable the test.
Candidate TT clock limits and all verification limits propagate to the caller;
attempted verification restricts compatible clocks through 98.

The existing excluded-move helper restores the complete caller frame, including
on cancellation or an exception. It cannot probe/store ordinary main/q TT
entries or train ordinary history. No legal alternative is a fail-low, rather
than a synthetic mate or stalemate. A completed fail-low extends only the
nominated move. A rejected verification continues ordinary search; cancellation
cannot authorize an extension or an unfinished main TT result. Check and singular
extensions share at most one extra ply; the singular move is not LMR-reduced.
The original node depth remains the depth of its stored TT result.

Each worker snapshots the independent checkbox; changing it stops the worker
and clears incompatible TT results. With it off, the additional probe and helper
are skipped. Fixed/aligned audit horizons disable it, and royal losers keeps
its existing behavior. Audit builds report `audit_singular` attempts, extensions,
rejections, cancellations, helper visits, check overlaps and attempted depths.
The original increment adds neither multiple/negative extensions nor multicut pruning;
the later singular-policy completion below supplies them.
Correctness and search-cost measurements do not establish playing strength.

Singular-extension validation (2026-10-20) passed all 50 Debug tests (183.65s),
50 Release tests (106.08s), 18 selected ASan/UBSan tests (76.43s, leak detection
disabled), 20 audit tests (26.17s), and the independent oracle over 4,372 positions
and 95,734 legal children (33.275s). Native tests cover both colors, compatible
TT bound/depth/context/mode/clock guards, illegal hints, strict verification
thresholds, helper cancellation/exception restoration, child depth and TT bounds,
check overlap, repetition floors, a real only-legal-move position, real cancelled
searches and history/TT isolation. UCI tests cover advertisement, invalidation,
worker snapshots and changed-option lifecycle. Frozen source hashes remained
unchanged through validation; seven changed C++ files passed the brace scan.

The search comparison used 12 reserved positions, frozen parent and same-binary
off/on controls, with IIR independently enabled/disabled. Three alternating cold
D8 trials and one retained-history trial (D6 warmup, fixed legal two-ply continuation,
D8) produced 288 legal completed searches. Cold signatures were deterministic;
96 parent/disabled signatures matched exactly in nodes, score, best move and PV.
One low-priority CPU, a UCI readiness barrier and process CPU timing were used.

| IIR | Singular extensions | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Off | 978,174 | 1.57 | 729,100 | 1.27 |
| On | On | 1,015,349 | 1.73 | 759,724 | 1.25 |
| Off | Off | 978,574 | 1.65 | 727,921 | 1.09 |
| Off | On | 1,015,843 | 1.65 | 760,023 | 1.18 |

With IIR, cold/retained nodes increased 3.80%/4.20%, with CPU changes of
+10.19%/-1.57%. Without IIR, nodes increased 3.81%/4.41%, with CPU changes of
0.00%/+8.26%. Short, variable, tick-quantized samples and one retained trial
limit timing conclusions. This extra search allocation establishes neither
playing-strength improvement nor regression; no timed match or tuning was run.

A separate real D6 TT warmup followed by direct production D8 searches recorded
25 attempts, 15 extensions, 10 rejections and 8,138 helper visits with IIR;
without IIR, 28 attempts, 10 extensions, 18 rejections and 5,964 visits. All
attempts were at depth seven, one overlapped a check extension, and disabled
counts were zero. Native probes cover cancellation separately. Full evidence
is under `.context/baselines/2026-10-20/singular/`. ProbCut, TT feedback and
integrated baseline acceptance remain pending.

### Fail-soft quiescence and tactical ProbCut

`ProbCut` is a default-on standard-search checkbox. At a nonroot, non-PV,
nonchecked node with at least five remaining plies, try legal captures and
promotions that pass a static-exchange threshold. Reuse the ordinary tactical
generators and learned capture ordering in a separate list, prioritizing the TT
hint. No quiet or castling batch is generated for this probe. En passant and
underpromotions retain their normal legality and SEE rules.

The raised beta is `beta + 250 - 50 * improving`. A compatible main TT upper or
exact bound below it at depth `depth - 3` suppresses the attempt; a low lower
bound cannot refute a higher score. A preliminary child qsearch must reach the
raised beta before main verification. Qsearch returns fail-soft scores
throughout standard search, tracking its best score separately from alpha so a
completed fail-low retains useful information. Delta-pruned captures contribute
the optimistic material bound used by that test; repetition draw floors remain
in force. Empty tactical lists and illegal-only candidates return the static
score when legal quiet play exists, while mate/stalemate precedence is preserved.
Base child depth is
`clamp(depth - 3 - (eval - beta) / 300, 0, depth - 1)`. A large qsearch result
can reduce this by up to three more plies, increasing the verification threshold
by 200 per extra reduction. If reduced main verification fails, retry at the
base depth and original raised beta. Zero child depth uses the completed qsearch
screen alone. These pawn-100 constants are provisional, not fitted confidence
bounds or a playing-strength claim. See [ProbCut](https://chessprogramming.org/ProbCut)
for the statistical motivation and its empirical limitations.

A successful ordinary score stores a lower bound at the actual verified child
depth plus one, then removes the raised margin from the score returned to the
caller. It never stores the requested full-depth bound. Decisive scores fall
through to ordinary search. Preserve draw/tablebase/full-depth-TT precedence,
original PV status, upcoming-repetition floors, clock-99 boundaries, stack room,
IID and verification exclusions. Making a real legal tactical move prevents a
stalemate cutoff. The complete caller frame survives rejection, cancellation
or an exception; made moves are undone before frame restoration. Captures and
pawn moves reset the clock; successful selective results restrict compatible
parent clocks through 98. The parent cutoff does not train ordinary histories;
normal main-search descendants retain their existing learning rules.

Workers snapshot the switch; a changed policy stops the worker and clears both
TT modes. Royal losers and fixed/aligned diagnostic horizons disable ProbCut.
Audit builds report attempts, legal SEE-qualified candidates, qsearch passes,
main verifications, adaptive reductions/retries, q-only screens, cutoffs,
cancellations, visits, TT vetoes and attempted depths in `audit_probcut`. Search-cost and correctness validation
remain separate from paired playing-strength acceptance.

Fail-soft/ProbCut validation (2026-10-21) passed 51 Debug checks (183.21s), 51 Release checks (102.51s),
19 selected ASan/UBSan checks (74.44s, leak detection
disabled), and 21 audit checks (26.04s). Both lifecycle
suites passed in both ordinary builds. The independent oracle covered 4,372
positions and 95,734 legal children. Native tests cover both colors, cold
fail-soft qsearch and real adaptive activation without q-TT seeds, screen/main
thresholds, adaptive retries and q-only cases, compatible cache bounds/depths/
contexts/clocks/modes, cancellation/exception restoration, en passant, promotions,
pinned captures and worker/policy isolation. Frozen code/test hashes stayed
unchanged through validation; all eight changed C++ files passed the brace scan.

The controlled comparison used 12 reserved roots and three configurations:
the frozen parent with clamped qsearch, global fail-soft with ProbCut disabled,
and global fail-soft with ProbCut enabled. Each also ran with null move enabled
and disabled. Three alternating cold D8 trials plus one retained-history trial
(D6 warmup, fixed legal two-ply continuation, D8) produced 288 legal completed
searches. Cold signatures repeated deterministically. Parent/foundation equality
was not expected because the global qsearch contract intentionally changed.
One low-priority CPU, UCI readiness barriers and process CPU timing were used.

| Null move | Qsearch / ProbCut | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Clamped / absent | 1,015,349 | 1.540 | 759,724 | 1.090 |
| On | Fail-soft / off | 1,087,558 | 1.750 | 773,775 | 1.110 |
| On | Fail-soft / on | 1,104,878 | 1.760 | 784,709 | 1.160 |
| Off | Clamped / absent | 1,296,343 | 1.970 | 993,644 | 1.510 |
| Off | Fail-soft / off | 1,388,570 | 2.130 | 880,353 | 1.280 |
| Off | Fail-soft / on | 1,274,734 | 2.000 | 850,172 | 1.260 |

ProbCut relative to the same-binary fail-soft control:
With null move on, cold/retained nodes changed +1.59%/+1.41%,
and CPU changed +0.57%/+4.50%.
With null move off, cold/retained nodes changed -8.20%/-3.43%,
and CPU changed -6.10%/-1.56%.

Global fail-soft relative to the clamped parent:
With null move on, cold/retained nodes changed +7.11%/+1.85%,
and CPU changed +13.64%/+1.83%.
With null move off, cold/retained nodes changed +7.11%/-11.40%,
and CPU changed +8.12%/-15.23%.

The cold foundation cost with null move was concentrated in one changed
search tree: reserved root 7 added 129,787 nodes, exceeding the net increase
of 72,209 over all 12 roots; its score and PV also changed. Other positions
both gained and lost work. This accounts for the aggregate tree-cost increase
without identifying an isolated per-node throughput or playing-strength effect.

Short variable CPU samples, clock-tick precision and one retained trial limit
timing conclusions. These tree/cost results establish no playing-strength gain
or regression. No timed match, parameter tuning or installation was run.

Direct production D8 probes after real D6 warmup confirmed ProbCut activation:
With null move disabled, 655 attempts tried 214 legal SEE candidates,
with 167 cutoffs, 93 main verifications, 55 adaptive reductions
and 76 q-only screens; attempts spanned depths 5–7.
With null move enabled, 482 attempts tried 129 legal SEE candidates,
with 98 cutoffs, 70 main verifications, 34 adaptive reductions
and 28 q-only screens; attempts spanned depths 5–7.
Disabled counters were zero. Native regressions cover adaptive retry and
cancellation independently. Evidence is under
`.context/baselines/2026-10-20/probcut/`. TT history feedback is next;
the remaining search-baseline groups and integrated strength acceptance remain
unfinished. This increment does not complete the baseline.

### TT cutoff history feedback

`TTHistory` is a default-on standard-search control, snapshotted by each worker.
Changing it stops the worker and clears main and quiescence TT entries. Royal
losers and artificial audit horizons disable this feedback.

An accepted non-PV main-search TT exact/lower fail-high can teach move ordering:
reward its legal quiet move in enabled main, pawn and immediate/2/4-ply
continuation history. If the preceding move was an early quiet move (legal
main-search ordinal 1–3), penalize that predecessor in its own enabled
continuation contexts. The ordinal includes the currently searched legal move;
illegal or pruned candidates do not consume it. Contexts stop at the root or a
null move. Castling retains the existing separate ordering policy.

Validate TT hints both pseudo-legally and legally before learning: a retained
hint is not itself a score certificate. The existing depth, context, clock,
bound and main/qsearch mode checks still own score reuse. Feedback changes no
TT cutoff conditions. Hint-only hits, fail-lows, PV reuse, repetition floors,
decisive scores, clock 99+, actual root reuse, aborted searches, IID and
verification/exclusion helpers, and artificial null subtrees do not train.

Use the existing bounded gravity update with provisional bonus
`32 * clamp(depth, 1, 8)^2`. Reward only the cached quiet move; no alternatives
were searched here, so none receive current-node penalties. Predecessor penalties
update continuation history only. Capture history, killers, countermoves and
ordinary-cutoff counters are not updated by TT feedback. Audit builds expose
`audit_tt_history rewards` (eligible quiet hints) and `penalties` (updated
predecessor continuation entries). These are different units.

Validation passed 52/52 Debug (182.70s), 52/52 Release (101.03s), including
both lifecycle suites in each, seven focused ASan/UBSan checks (16.79s,
leak detection off), nine audit checks (6.79s), and the independent board
oracle's 4,372 positions / 95,734 legal children (32.348s).

The frozen parent and same-binary disabled control matched all 96 cold/retained
search signatures exactly: nodes, depth, score, best move and full PV. All 288
D8 results were legal and complete; 216 cold searches repeated deterministically.
Twelve reserved roots used three alternating cold trials and one retained trial
with D6 warmup and a fixed legal two-ply continuation, Hash64, one low-priority
CPU, pinned model, readiness barriers, and books/experience/tablebases off.

| AdaptiveLMR | History | Nodes, enabled vs disabled | Process CPU |
|---|---|---:|---:|
| On | Cold | -13.167% | -12.651% |
| On | Retained | +5.454% | +17.117% |
| Off | Cold | -5.991% | -6.751% |
| Off | Retained | +1.911% | +1.818% |

Cold CPU uses the median total of three trials. Retained results have one trial;
short CPU samples and clock-tick resolution limit timing conclusions. Existing
retained rows locate the default-policy net increase of 42,799 nodes primarily
at reserved root 7 (+43,009), with gains and losses elsewhere. Its score changed
while best move/PV stayed the same. Total retained CPU rose 0.19s; identical-work
parent/disabled timings differed by 0.07s. This accounts for tree cost without
isolating per-node overhead or establishing move quality. No general speed,
generator-throughput or playing-strength gain is established.

Production D8 probes after real D6 warmup recorded 4,765 eligible quiet rewards
and 2,697 predecessor continuation penalties with adaptive LMR enabled; with it
disabled, 3,984 and 2,289. Disabled feedback counters were zero. Evidence is under
`.context/baselines/2026-10-21/tt-history/`. TT feedback completes the first
search-baseline implementation group. Depth/context-aware LMP, history pruning,
raw static-evaluation TT caching, richer correction and selective-policy
completion remain. Integrated strength acceptance is pending.

### Depth/context-aware quiet pruning (2026-10-21)

Independent `LMP` and `HistoryPruning` controls add the depth/context policy
described in chapter 4. Generator batching and low-level optimizations are
preserved. The retired fixed-four pruning experiment remains historical.
Validation passed 53/53 Debug checks (181.30 s), 53/53 Release (106.69 s),
11 focused ASan/UBSan checks (47.28 s, leak detection disabled), 13 instrumented
checks (11.52 s), and the independent oracle's 4,372 positions/95,734 legal
children. Native checks cover 9,648 depth/count/history thresholds, 192 controlled
PVS cases, helper and tactical guards, clock/cache bounds, cancellation and
restoration. Preserve original PV status after alpha rises: a temporarily narrow
window cannot authorize quiet pruning. Older artificial minimax/history probes
explicitly disable the new policies to retain their independent contracts.

The frozen parent/off/LMP/history/both matrix, with adaptive LMR on/off, completed
480 legal D8 searches on 12 reserved roots. Three alternating cold trials repeated
exactly; all 96 cold/retained disabled-parent signatures matched nodes, depth,
score, best move and full PV. Retained trials warm D6 and play a fixed legal
two-ply continuation before D8. One low-priority CPU and a readiness barrier
separate search CPU from configuration; measurements use no pause throttling.

With adaptive LMR enabled, combined pruning changed cold/retained nodes by
-49.31%/-56.78% and measured CPU by -46.99%/-39.34%. LMP alone changed nodes
by -51.45%/-57.26%; history alone by +0.46%/-4.99%. With adaptive LMR disabled,
combined nodes changed by -62.98%/-61.29% and CPU by -56.19%/-56.52%.
These results establish neither playing strength nor generator throughput.

Production D8 after real D6 warmup counted 208,798 LMP prunes and three
additional history prunes with both policies and adaptive LMR enabled. History
alone counted 10,663 prunes; LMP shadows most qualifying moves in the combination.
Disabled mechanisms counted zero. The combined cold tree exceeds LMP alone by
20,571 nodes, mostly two roots; retained adds 4,020, mostly one changed-score
root. This interaction does not establish standalone history-pruning benefit or
move quality. CPU samples are short and tick-quantized: identical cold parent/off
trees used median 1.97/1.66 s; retained LMP/combined used 0.55/0.74 s. Three cold
trials and one retained trial limit broader timing claims. Private artifacts are
under `baselines/2026-10-21/quiet-pruning/`. Raw static-evaluation TT caching,
richer correction and the selective-policy group remain before baseline acceptance.

At the end of Phase 1, before Phase 2 and before any strength tests, the
integration review must include fixed-depth, single-worker comparisons against
the four pinned reference implementations plus the historical control using
a compatible evaluator. Normalize completed-depth and node/work definitions, freeze settings/
models, and investigate large per-position gaps with ordering, reductions,
pruning, extensions, cache and quiescence evidence. Feature-name coverage does
not establish equal trees; native modern evaluations also differ. This gate
precedes parallel search, time-management and modern-network work. Smaller
trees alone do not establish correctness or playing strength.

### Raw static-evaluation TT caching (2026-10-21)

`TTEval` adds model-tagged raw neural scores to the shared table while retaining
worker-local and frame caches. Apply current rule-50 damping and correction on
reuse; raw entries supply no search bound. The full model fingerprint permits
reuse across score contexts and retained immutable snapshots. Entries grow from
24 to 40 bytes, holding about 40% fewer positions at the same Hash setting.

Validation passed 54/54 Debug checks (190.14 s), 54/54 Release (106.03 s), both
lifecycle suites in each, 16 focused ASan/UBSan checks (64.78 s, leak detection
disabled), 18 instrumented checks (10.54 s), and the independent oracle's 4,372
positions/95,734 legal children. An older reinitialization test explicitly disables
TT evaluation to isolate its worker-local invalidation contract; shared TT reuse
can legitimately leave the new accumulator lazy. Audit verification compares
production raw cache values with fresh full inference.

The frozen parent/off/on matrix with correction history on/off completed 288
legal D8 searches: three alternating cold trials and one retained trial over 12
reserved roots. Retained searches warm D6 and play a fixed legal two-ply
continuation before D8. All 96 parent-enabled and 96 parent-disabled signatures
matched nodes, completed depth, score, best move and full PV; cold trials repeated
exactly. One low-priority CPU and a readiness barrier exclude configuration cost.

With correction enabled, cold median parent/off/on CPU totals were 0.89/0.87/0.89 s;
the single retained trial used 0.63/0.71/0.69 s. Thus enabled versus parent was
unchanged cold and +9.52% retained (+0.06 s). Without correction, enabled versus
parent CPU changed by +4.11%/+1.72%. These short, tick-quantized samples establish
no general speed benefit. The disabled layout also costs more in the retained
sample; existing rows concentrate most of that difference in two roots. Identical
cold parent totals varied from 0.88 to 1.23 s. The data do not isolate layout cost
from timing variability; no additional engine runs or causal per-node claim.

Separate real-D6-warm/new-worker D8 probes with correction enabled preserved
238,883 nodes while avoiding 9,758 of 177,367 production neural evaluations
(5.50%). Without correction, 10,773 of 188,992 were avoided (5.70%); disabled
TT probes/hits were zero. This demonstrates reuse after local-cache misses,
including across workers, without claiming a global CPU or playing-strength gain.
Private evidence is under `baselines/2026-10-21/raw-eval-tt/`. Richer correction
history remains next; baseline integration and strength acceptance are pending.

### Piece-group and continuation correction (2026-10-21)

`PieceCorrection` and `ContinuationCorrection` are default-on standard-search
controls under the existing `CorrectionHistory` master switch. Piece correction
adds independent tables for white and black nonpawn placement, minor pieces and
major pieces. Nonpawn tables include kings; the minor group contains knights,
bishops and kings, and the major group contains rooks, queens and kings. These
hashes update through normal piece mutations, including captures, promotions,
en passant and castling. Board snapshots preserve them over null moves and undo;
board flipping rebuilds the feature hashes. Each board frame grows by 32 bytes.

Continuation correction uses the last move paired with the move two or four
plies back. Its directional hash combines colored landing pieces and destination
squares, using the promoted piece after a promotion. Both contexts stop at the
search root or an intervening null move. Separate distance tables prevent a
coincident bucket from receiving two updates for the same observation. These
contexts are independent of move-ordering continuation-history controls.

All seven tables have 16,384 signed entries per side, consuming 448 KiB per
standard worker (previously 64 KiB). Each enabled table learns the same fresh
aggregate evaluation error using the existing bounded gravity update. Sum the
integer entries before dividing by 128; the four-ply continuation contributes
half its stored value, and other domains have unit weight. These provisional
scales require integrated strength validation. Shared feature buckets hold
heuristic estimates and confer no search-bound validity.

The existing quiet-result, bound-direction, check, decisive-score, clock,
helper, exclusion, cancellation and null-subtree learning guards apply to every
domain. Raw neural/frame/TT evaluations stay uncorrected; every read applies the
current aggregate with rule-50 damping and the decisive-band clamp. Workers
snapshot the controls; changing either stops the worker and clears both TT modes.
All tables transfer together only for the same immutable model snapshot. Royal
losers allocates no correction tables. Artificial fixed/aligned audit horizons
bypass correction and learning. Audit output adds changed-entry counts for the
four piece domains and each continuation distance; these count table entries,
whereas the existing update counter counts learning nodes.

Validation passed 54/54 Debug checks (207.17 s), 54/54 Release (108.97 s),
both lifecycle suites in each, 18 focused ASan/UBSan checks (65.61 s, leak
detection disabled), 20 instrumented checks (11.48 s), and the independent
oracle's 4,372 positions / 95,734 legal children (34.462 s). Native regressions
cover incremental feature keys against fresh board calculations, both colors,
special moves, null/undo/flip, promoted continuation pieces, root/null context
boundaries, aggregate limits, fresh-error learning, worker/model ownership and
raw TT/local/frame-cache independence over rule-50 boundaries. Two older
post-search raw-inference assertions required explicit separation from learned
correction; their raw full/incremental/reference scores still agreed. The
instrumented-only fixture fix reused unaffected ordinary/sanitizer evidence.
All 14 changed source/test hashes remained frozen through their relevant gates;
13 changed C++ files passed the brace scan.

The parent/disabled/pieces/continuation/combined matrix, with raw TT evaluation
enabled and disabled, completed 480 legal D8 searches over 12 reserved roots.
Three alternating cold trials repeated deterministically. All 96 parent/disabled
signatures matched nodes, depth, score, best move and full PV exactly; all 240
paired raw-TT on/off signatures also matched. Retained trials warm D6 and play
a fixed legal two-ply continuation before D8. One low-priority CPU and UCI
readiness barriers isolate search CPU from configuration, with no pause throttle.

| Raw TT evaluation | Correction policy | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Parent | 486,342 | 0.89 | 357,670 | 0.64 |
| On | Disabled additions | 486,342 | 0.91 | 357,670 | 0.65 |
| On | Pieces | 399,847 | 0.77 | 365,201 | 0.61 |
| On | Continuation | 442,380 | 0.84 | 354,230 | 0.61 |
| On | Combined | 407,264 | 0.78 | 416,075 | 0.73 |
| Off | Parent | 486,342 | 0.88 | 357,670 | 0.62 |
| Off | Disabled additions | 486,342 | 0.88 | 357,670 | 0.61 |
| Off | Pieces | 399,847 | 0.76 | 365,201 | 0.61 |
| Off | Continuation | 442,380 | 0.79 | 354,230 | 0.62 |
| Off | Combined | 407,264 | 0.76 | 416,075 | 0.74 |

Combined correction versus the same-binary disabled control changed cold/retained
nodes by -16.26%/+16.33%. With raw TT evaluation enabled, CPU changed by
-14.29%/+12.31%; without it, -13.64%/+21.31%. Piece correction alone changed
nodes by -17.78%/+2.11%, continuation alone by -9.04%/-0.96%. These interactions
establish neither the strength of either component nor their combination.

Existing retained rows locate the combined net increase of 58,405 nodes at
reserved root 5 (+85,090), with savings elsewhere. Its score changes from +18
to +3 and best move from f8e8 to d7d5. Its CPU increase of 0.15 s exceeds the
net 0.08 s with raw TT evaluation enabled. Combined versus pieces alone adds
50,874 retained nodes, including 62,835 at that root. This locates changed tree
allocation, without proving move quality or an isolated per-node cost. CPU
samples are short and tick-quantized, with one retained trial; no general speed
or playing-strength claim follows. No timed match, tuning or installation ran.

Direct production D8 probes after real D6 warmup and same-model history transfer
recorded 38,580 changed piece entries, 9,584 immediate continuation entries and
8,944 four-ply entries with both additions enabled, across 9,645 learning nodes.
Disabled components and the master-off control recorded zero corresponding
updates. Private evidence is under `baselines/2026-10-21/richer-correction/`.
This completes the second search-baseline implementation group. Selective-policy
completion and final baseline integration remain before Phase 2 or strength tests.

### Adaptive null-move pruning (2026-10-21)

`AdaptiveNullMove` is a default-on standard-search checkbox under `NullMove`.
At an interior expected non-PV cutoff node with depth at least three, require
corrected static evaluation at least beta and
`eval + 132 * improving >= beta + 8 * depth`. A real previous move is required;
root, IID, verification, excluded-move and repetition-floor contexts do not
attempt the adaptive policy. Check, pawn-only material, decisive windows,
clock 99 and legal terminal guards remain. Reverse futility still runs first.

The total reduction, including the artificial pass, is
`4 + depth / 3 + clamp((eval - beta) / 174, 0, 4) + tactical_hint`.
Search the opponent at `max(0, depth - reduction)` with window
`[-beta, -beta + 1]` and opposite cut-node context. A capture or promotion TT
hint contributes one extra ply only after pseudo-legal and legal validation;
quiet, malformed and pinned hints contribute none. This uses no TT score as a
static evaluation and changes no move generation. The constants are provisional.

With `NullVerify`, ordinary null fail-highs require same-position verification
at depth 12 or more, including middlegames. Existing endgame confirmation still
starts at depth six. Verification depth is at least one and at least the null
child depth; endgames also retain their previous `depth - 6` floor. Thus even a
null probe reduced to qsearch receives real-move confirmation when verification
is required. Reverse futility is disabled in adaptive null-verification subtrees
to prevent the same static premise from confirming itself. Failed confirmation falls through to legal search at the original
depth. Scoped verification disables null pruning throughout the helper subtree,
isolates normal TT bounds and history learning, restores the caller frame and
merges clock constraints. Cancellation authorizes no cutoff. Artificial decisive
scores remain inadmissible; these safeguards are heuristic, not zugzwang proofs.

Each worker snapshots the switch. Changing it stops/joins the worker and clears
both TT modes. Disabling it restores the prior fixed reduction, 50-cp cushion
and endgame-only verification, while `NullMove=false` disables both policies and
`NullVerify=false` disables confirmation. Royal losers and fixed/aligned audit
horizons retain their existing policies. Audit counters distinguish adaptive
attempts, reductions, null-probe work, confirmations, rejections, cancellation,
verification work/depth and attempt-depth range.

Validation covered all 54 Debug checks: 53 passed in the full run (286.06 s),
and the sole corrected boundary fixture passed its focused rerun (7.10 s), with
engine source unchanged. All 54 Release checks passed (132.79 s), including
both lifecycle suites in each configuration. The 22 focused ASan/UBSan checks
passed (128.58 s, leak detection disabled), as did 24 instrumented checks
(24.02 s) and the independent oracle's 4,372 positions / 95,734 legal children
(42.944 s). The boundary fixture now exercises legacy null at parent depth two
and adaptive null at parent depth four, retaining identical losing-bound and
clock-range rejection assertions; its old depth-one child cannot activate the
new policy. Native coverage includes reduction bounds and surplus monotonicity,
legal/pinned/malformed capture and promotion hints, improving/depth thresholds,
ordinary/reserved fail-highs, confirmation/refutation, cancellation, real-move
verification, state restoration, worker snapshots and variant/audit isolation.
All ten final source/test hashes stayed frozen; nine C++ files passed the brace scan.

The parent/disabled/adaptive matrix with `NullVerify` enabled and disabled
completed 288 legal D8 searches over 12 reserved roots: 216 deterministic cold
searches across three alternating trials and 72 retained searches after D6
warmup and a fixed legal two-ply continuation. All 96 disabled/parent node,
depth, score, best-move and full-PV signatures matched exactly. One low-priority
CPU and readiness barriers isolated configuration from measured search CPU.

| Null verification | Policy | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Parent | 407,264 | 0.94 | 416,075 | 0.81 |
| On | Adaptive disabled | 407,264 | 0.94 | 416,075 | 1.38 |
| On | Adaptive enabled | 395,469 | 0.94 | 268,508 | 0.69 |
| Off | Parent | 407,239 | 0.84 | 416,050 | 0.94 |
| Off | Adaptive disabled | 407,239 | 0.95 | 416,050 | 0.81 |
| Off | Adaptive enabled | 395,393 | 0.92 | 268,474 | 0.66 |

With verification enabled, adaptive versus disabled changed cold/retained nodes
by -2.90%/-35.47% and CPU by 0%/-50.00%. Without verification, nodes changed
by -2.91%/-35.47% and CPU by -3.16%/-18.52%. These timings do not establish a
precise CPU gain: the default retained parent and disabled controls have matching
search signatures but differ by 0.57 s (0.81 versus 1.38 s). Enabled is 0.69 s,
or -14.81% against the parent. This control spread occurs across several roots;
short samples, 100-Hz CPU ticks and one retained trial do not isolate its cause.
No general speed or per-node throughput claim follows.

Existing rows locate the default cold increase at reserved root 7: +44,471
nodes (+108.61%), with best move f7f5 (+51) changing to b7b6 (+56). Root 5
adds 15,183 nodes; root 4 saves 28,796 and root 8 saves 15,196. Retained root 5
saves 85,771 of the net 147,567 nodes, changing d7d5 (+3) to d7d6 (-5).
Root 8 saves 21,324; root 7 adds 6,946. These are changes in search allocation,
not evidence of better moves. No position-specific guards or tuning were added.

Production D8 probes after real D6 warmup and same-model history transfer
recorded 2,179 adaptive attempts, 1,475 cutoffs, 7,651 effective reduction plies
and 6,914 null-probe visits, at remaining depths three through seven. Three
endgame confirmations visited nine nodes. A separate real depth-12 expected-cut
probe exercised general verification: one null visit and two confirmation visits
at verification depth one. Disabled controls recorded zero corresponding activity;
native probes cover rejected confirmations and cancellation. The general depth-12
branch is not claimed active in the D8 matrix. Evidence is under the private
`baselines/2026-10-21/adaptive-null/` directory. Playing strength remains unmeasured;
broader futility and the other selective policies remain before baseline completion.
No timed match, parameter tuning or installation ran.

### Broader futility validation (2026-10-21)

The default policy described in chapter 4 is implemented and correctness-validated;
playing strength remains unmeasured. Debug covered all 55 checks: the initial
full run passed 54/55 in 221.82 s, then the corrected legacy leaf fixture and
expanded futility test passed 2/2 in 15.19 s. Release passed 55/55 in 110.87 s,
including both lifecycle suites. Selected ASan/UBSan passed 25/25 in 438.24 s
(leak detection disabled). Instrumented coverage is 27 checks: initial 26/27
in 51.28 s, then the corrected audit-only cutoff-recorder test 1/1 in 1.07 s.
The independent board oracle passed 4,372 positions and 95,734 legal children
in 35.035 s. Runtime stayed frozen through validation. The fixture changes retain
legacy direct-root return-eval assertions and separately test the new interior
damped cutoff, legal king/rook certificates and recorded FEN/window/evaluation.
GCC 16 reports array-bound diagnostics in virtual-search test harnesses, including
the new harness; the same pattern exists in five parent harness files. Sanitized
checks passed without diagnostics; no compiler warning suppression was added.

The comparison completed 432 legal D8 searches over 12 reserved roots: 324 cold
searches across three alternating trials and 108 retained searches after D6
warmup and a fixed legal two-ply continuation. All 96 disabled/parent node,
depth, score, best-move and full-PV signatures matched exactly. Cold search
signatures repeat deterministically. Settings include one low-priority worker,
Hash 64, the same pinned model, contempt 0 and book/experience/tablebases off.
Readiness barriers finish option/model configuration before measured search CPU.

| MainSEE | Policy | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| On | Parent | 395,469 | 0.84 | 268,508 | 0.58 |
| On | Broader disabled | 395,469 | 1.00 | 268,508 | 0.52 |
| On | Broader enabled | 322,804 | 0.65 | 282,311 | 0.54 |
| Off | Parent | 368,009 | 0.76 | 298,667 | 0.57 |
| Off | Broader disabled | 368,009 | 0.78 | 298,667 | 0.69 |
| Off | Broader enabled | 372,621 | 1.57 | 310,867 | 0.89 |

With MainSEE enabled, broader versus disabled changes cold/retained nodes
by -18.37%/+5.14%; without MainSEE, by +1.25%/+4.08%. Neither/reverse-only/
move-only/both controls with MainSEE on use cold nodes 510,144/354,219/458,987/
322,804 and retained nodes 422,474/282,069/370,499/282,311. These controls disable
both futility master rules for neither, then enable each broader rule separately;
they demonstrate interaction, not additive gains over the legacy policy.

CPU timing is variable: default parent cold trials are 1.70/0.83/0.84 s,
disabled 2.05/0.81/1.00 s and enabled 2.04/0.65/0.65 s despite deterministic trees.
The default retained controls differ 0.58/0.52 s, while enabled is 0.54 s.
Short samples, 100-Hz CPU ticks, trial drift and one retained trial preclude
precise/general CPU or per-node-throughput claims. Median component timings
must not be treated as additive mechanism efficiency. No cause of timing drift
is established, and smaller trees alone do not establish stronger play.

Existing rows explain the default retained increase: root 4 adds 21,885 nodes
(c6b4, -35 -> c8d7, -41), root 2 adds 14,247 (same b1c1, +8 -> +6), offset by
root 7 saving 13,095 and root 0 saving 10,749; the net increase is 13,803. Cold
root 10 adds 7,681 (b2b1q, +51 -> b2b1r, +49) and root 0 adds 5,504; root 7 saves
25,062, root 5 saves 18,998 and root 3 saves 12,349, with net savings 72,665.
These legal score/PV changes are search-allocation observations, not move-quality
labels. No position-specific guard, extra diagnostic search or tuning was added.

Direct production D8 probes after D6 warmup and same-model history transfer
recorded 16,922 reverse-futility cutoffs, including 204 beyond depth 3, and 21,424
move-futility prunes, including 100 captures and 12,281 at reduced depth > 1.
These subsets overlap. Corresponding master-disabled counters were zero;
legacy controls had no new deep/capture activity. All 13 final runtime/test/build
hashes match the recorded validation sources, with fixture-only corrections
tracked separately; 11 C++ files passed the brace scan. Evidence is under private
`baselines/2026-10-21/broader-futility/`. Remaining Phase 1 work is LMR context/
eligibility and multiple/negative singular extensions with multicut, followed by
the consolidated integration/reference gate. No timed match, tuning or install ran.

### Contextual reduction eligibility and confidence (2026-10-21)

`ContextLMR` completes the broader LMR policy described in chapter 4: early
interior and root trials, tactical/evasion/check eligibility, learned per-type
history, refutations, PV provenance, legal tactical TT hints, correction and
alpha-raise context. Its independent worker switch clears both TT modes and
requires `LMR` and `AdaptiveLMR`. False restores the preceding policy exactly.
Full nominal-depth verification of every reduced alpha improvement remains
mandatory, including apparent beta cutoffs. No generator or inference kernel
changed. Persistent PV metadata fits the existing 40-byte TT entry.

Final correctness coverage includes 56 Debug checks (55 initial passes plus the
corrected new reset fixture passing 1/1), all 56 Release checks, 28 ASan/UBSan
checks with leak detection disabled, and 30 audit checks. Both lifecycle suites
passed. The new native test covers 7,740 root/interior verification/cancellation
cases and every certified tactical category; the independent board oracle
checked 4,372 positions and 95,734 legal children. The initial new fixture called
reset at the root rather than after playing a move; fixing that setup required
no runtime changes. One test indentation correction preserves identical C++
tokens. Existing compiler warning categories/counts match the parent Release
build; the new native test compiled without warnings, and sanitizer runs emitted
no diagnostics. No warnings were suppressed.

The frozen parent and same-binary off/on controls completed 480 legal D8 searches:
360 deterministic cold searches across three alternating trials, plus 120
retained-history searches after D6 warmup and a fixed legal two-ply continuation.
All 192 parent/disabled node/depth/score/best-move/full-PV signatures matched
exactly, including master-LMR-off and adaptive-LMR-off controls. The same network,
Hash 64, no book/experience/tablebases, one low-priority CPU and readiness barrier
were used throughout. Contextual LMR and broader futility were crossed together:

| ContextLMR | BroaderFutility | Cold nodes | Cold CPU median (s) | Retained nodes | Retained CPU (s) |
|---|---|---:|---:|---:|---:|
| Off | Off | 395,469 | 0.79 | 268,508 | 0.52 |
| On | Off | 295,740 | 0.57 | 300,225 | 0.54 |
| Off | On | 322,804 | 0.66 | 282,311 | 0.49 |
| On | On | 252,275 | 0.50 | 294,793 | 0.56 |

With default broader futility, enabling the new policy changes cold/retained
nodes by -21.85%/+4.42%; without it, -25.22%/+11.81%. The components are not
additive: broader futility reduces retained work with contextual LMR enabled,
although it increases retained work under the preceding LMR policy. Cold CPU
medians change -24.24%/-27.85% with broader futility on/off, and single retained
samples change +14.29%/+3.85%. Short runs, 100-Hz CPU ticks and control drift
preclude precise/general CPU or per-node throughput claims. Default retained
parent/off controls measure 0.52/0.49 s; enabled measures 0.56 s. No cause of
timing drift is established. These are search-allocation measurements, not
playing-strength or move-quality evidence.

Existing rows concentrate the default retained increase in roots 6/7/5
(+16,356/+10,240/+10,112 nodes), offset chiefly by roots 2/4 (-21,435/-7,257).
The net increase is 12,482 nodes. Cold root 3 adds 19,010 nodes and changes
`g7h5` (-33 cp) to `b2e5` (-38 cp); roots 7/0 save 35,820/22,574 nodes, with
legal best-move and score changes. These rows do not identify an objectively
better move or a single causal modifier. No position-specific guard, extra
diagnostic search or parameter tuning was added.

Actual production D8 searches recorded 22,903 contextual reductions, including
1,344 at the root, 3,684 early interior moves, 1,842 tactical moves, 730 checks
and 500 evasions; these subsets overlap. Corresponding disabled/master/adaptive
off counters were zero. Evidence is under private
`baselines/2026-10-21/context-lmr/`. Remaining Phase 1 work is multiple/negative
singular extensions with multicut, followed by the consolidated integration and
reference gate. No timed strength match or installation ran.

### Multiple and negative singular allocation with multicut (2026-10-21)

`SingularPolicy=true` completes the selective outcomes of `SingularExtensions`.
It uses the same compatible lower/exact main TT candidate, reduced excluded
verification, depth-seven eligibility and legal/terminal/clock/repetition/helper
guards described above. With the new control off, the single-extension policy
is preserved. Changing the control stops the worker and clears both TT modes;
workers snapshot it. Royal losers and fixed/aligned audit horizons disable it.

A non-PV fail-low allocates two extra plies when the alternative score is strictly
below `threshold-16`; quiet candidates receive three below `threshold-64`.
PV nodes receive one. These pawn-100 margins are provisional, without a tuning
or strength claim. Each line permits twelve multiple-extension events; its frame
inherits the count across legal and null edges, increments only in the extended
child and resets at the root. Siblings and same-position helper restoration
retain the caller's count. Existing iteration/ply limits still apply. With no
legal alternative, the helper returns `threshold-1`, granting exactly one ply.
Check and positive singular allocations overlap using the maximum.

When singular verification fails high, a non-PV, nondecisive alternative score
at or above the original beta supplies a selective fail-soft multicut return.
This writes no full-depth TT result and trains no history for an unsearched move.
Otherwise a TT score at or above beta assigns `-3+PV` plies to its candidate;
an expected cut node assigns `-2`. A negative allocation replaces a check
extension. Positive singular candidates keep their existing pruning/LMR
protection; nonsingular candidates use ordinary first-move guards. The original
node depth and the full nominal-allocation LMR verification contract remain.
No decisive reduced score is promoted to a new mate/tablebase certificate.
Audit builds add double/triple/negative/multicut counts to `audit_singular`.

Validation passed all 57 Debug tests (249.86s), all 57 Release tests (145.03s),
including both lifecycle suites, 29 selected ASan/UBSan checks (255.92s, leak
checks disabled), 31 audit checks (50.49s), and the independent oracle over
4,372 positions and 95,734 legal children (35.719s). Native tests cover 2,112
boundary cases and 768 PVS child/cancellation combinations, plus actual
only-legal-move handling, exception restoration, line ownership and controls.
Eight changed C++ files passed the brace scan. Release warning categories,
files and counts match the parent; the new test has no warnings or sanitizer
diagnostics. Final engine/test source stayed frozen throughout validation.

The frozen parent/off/on comparison crossed the new policy with contextual LMR
and included master-disabled controls. Twelve reserved roots, three alternating
cold D8 trials and one retained D6 warmup/fixed legal two-ply/D8 trial produced
384 legal completed searches. All 288 cold searches repeated deterministically;
144 parent/disabled node/depth/score/best/full-PV signatures matched exactly.
With contextual LMR, cold/retained nodes changed by -2.04%/-6.78%; without it,
-10.91%/-15.84%. Default cold CPU medians were equal (0.50s); retained changed
0.54s to 0.53s. Short, tick-quantized samples and control drift, including
identical noncontextual retained controls at 0.49s/0.81s, preclude precise or
general CPU/per-node claims. Retained best moves/scores sometimes change;
these search-work results establish neither strength nor move quality.

Production D8 activation covered negative allocation and multicut, with doubles
also active without contextual LMR. The same frozen corpus at D8 warmup/direct
D10 exercised all outcomes with defaults: 195 attempts, 84 extensions,
15 doubles, one triple, 49 negative allocations and 50 multicuts. Legacy/master
controls had zero new counts. No margins or positions were tuned. Evidence is
under `.context/baselines/2026-10-21/singular-policy/`. The remaining Phase 1 work
is the consolidated integration review and fixed-depth reference comparison
before Phase 2 or any strength testing.
