# Move generation, picking and search

Standard-chess generators produce pseudo-legal moves, including special-move
flags. The picker orders candidates and suppresses duplicates. It does not
establish king safety or prove that a position is mate or stalemate.

PVS and quiescence apply their move-pruning rules before `try_forward`.
That operation makes the candidate, checks the moving king, and immediately
undoes rejected moves. Rejections do not enter searched-move counters, history
updates, PVs or transposition-table clock constraints. Castling additionally
requires safe origin/transit squares, checked by search before making it.
Root, book and legal-move oracle consumers explicitly filter candidates.

Royal-losers retains its existing legal picker and compulsory-capture policy.
External UCI move validation remains independent of the interior search picker.

Relevant reading: [CPW Legal Move](https://chessprogramming.org/Legal_Move),
[Move Ordering](https://chessprogramming.org/Move_Ordering), and
[History Heuristic](https://chessprogramming.org/History_Heuristic).
Pseudo-legal generation with make/test/unmake is a design choice, not a proven
speed improvement. Validate special moves, terminal scores, make/unmake, hashes,
repetition, search counters and lifecycle; compare Release performance and
playing strength separately.

The standard picker stages are TT, SEE-nonnegative captures/promotions, two
killers, countermove, positive-score quiets, deferred negative-SEE noisy
moves, then remaining quiets. Quiet castling candidates share quiet ordering;
qsearch retains its existing castling policy. Positive means strictly greater
than zero. Stage separation does not assert tactical correctness of SEE or
promise a cutoff. Scores are cached when each batch is generated. Returned
hint codes suppress duplicates without making generated king-safety tests.
Countermoves are keyed by the previous colored piece and destination; no
continuation/countermove context is used after a null move or at a cold root.

Learned quiet scores sum signed, bounded history entries:

- Main: side, source square, destination square.
- Continuation: colored piece/destination pairs from one, two and four plies
  back and the current move. Contexts stop at the root or any intervening null.
  Immediate and longer contexts have independent controls.
- Pawn: an incremental colored-pawn hash modulo 512, then current colored
  piece/destination. Collisions deliberately share heuristic scores.

The deferred quiet batch adds optional ordering context to that learned score.
`ThreatOrdering` rewards escaping attacks by lesser pieces and penalizes entering
them: pawn attacks for minors (4000 units), pawn/minor attacks for rooks (8000),
and pawn/minor/rook attacks for queens (12000). Equal threatened status cancels.
Batch-local unions use parent occupancy and include pinned attackers; these are
heuristic priorities, not safety certificates. Pawns/kings have no threat offset.
`CheckOrdering` adds 10000 for a quiet check, including a discovered check, when
`see(move, 0)` passes. Castling receives neither context offset. Constants remain
provisional. Both switches default true, are snapshotted by workers, and clear
TT results on change. Disabling both restores learned-only scoring. Good noisy
stages, early hints and final legality retain their contracts. Quiescence scores
its existing quiet candidate domain without generating additional move classes.
The shared `move_history_score` accessor returns learned history only; contextual
bonuses are neither learned nor passed to history-based search decisions.

The worker owns approximately 2.2 MiB of 16-bit history entries. On a quiet
cutoff, reward the winner and penalize previously searched ordinary quiets with
bounded gravity. Keep the provisional bonus `32 * clamp(depth, 1, 8)^2` and
bound 16384; these are not claimed tuned. Protect winner slots from aliasing
penalties and update history/countermoves before mutating killer slots.
The legacy piece/destination history remains only for royal-losers.

Capture history adds 11,648 bytes of worker-owned signed entries, indexed by
colored moving piece, destination and captured-piece type. En passant uses the
victim recorded in the move; non-capturing promotions use the empty-victim slot.
Promotion choices share their original pawn context. On a completed normal PVS
beta cutoff, reward a noisy winner and penalize previously searched legal noisy
alternatives, including when a quiet move wins. A winner's shared table slot is
protected from penalties. Use the same provisional gravity bound and depth bonus
as quiet history. TT, static/null, qsearch and helper-search cutoffs do not train
this table; cancellation and rejected/pruned moves supply no parent update.

With `CaptureHistory` enabled, noisy ordering combines 16 times the existing
material score with capture history. Positive/negative stage offsets preserve
lazy SEE classification: a negative-SEE move stays deferred irrespective of its
history. Deferred moves combine 16 times SEE with history. Quiescence uses the
same ordering without learning. These scales are provisional, not tuned.
`CaptureHistory=false` skips scoring and learning and retains the previous noisy
ordering, including for already nonzero retained entries. The setting is captured
by each worker, and a policy change stops search and clears both TT modes.
Royal-losers keeps its existing ordering and does not allocate this table.

After joining the previous standard worker, transfer table ownership and copy
countermoves into its successor. Normal `position`/`go` requests retain learning;
`ucinewgame` releases the old worker and starts cold. No mutable tables are
shared between running workers. Per-entry gravity provides aging; no full-table
per-search clearing or scaling is added. Strength remains unmeasured.

The empty-tactical qsearch path returns the same fail-low bound as a loop
that rejects all illegal candidates. Exact mate/stalemate scores take priority.
A regression and before/after-legality paired searches cover this contract.

Validation on 2026-10-19: 37 Debug and 37 Release checks, three focused
ASan/UBSan checks, independent oracle comparisons for 4,348 positions and
95,618 legal children, UCI history lifetime/reset checks, and 72 paired D7
legality-placement signatures matching scores, best moves, full PVs and nodes.
Leak detection was disabled for the sanitizer checks.

A small cold diagnostic (12 reserved D7 roots, three alternating trials) found
that the combined candidate used 22.8% more median process CPU and searched
4.1% more nodes than the frozen original. Positive quiets before bad noisy
moves searched 1.1% more nodes than late quiets with the same history code;
their timing differences were small relative to trial variation. These are
CPU-clock observations under a duty throttle, not unthrottled wall NPS or an
Elo result. The refactor remains an experiment with no established strength gain.


Capture-history validation (2026-10-20): 43 Debug and 43 Release checks, eight
focused ASan/UBSan checks with leak detection disabled, eight audit checks, and
an independent oracle covering 4,372 positions/95,734 legal children. A 12-root
D8 matrix completed 288 legal searches; all 96 disabled signatures exactly
matched the frozen parent. Enabled cold nodes increased 13.92% with LMR and
1.20% without it; retained-history nodes increased 6.06%/0.95%. Initial cold
median CPU increased 58.89%/1.18%; an additional three-pair LMR-on confirmation
measured +23.77% (2.65 to 3.28 seconds). Samples are short and variable and show
no efficiency benefit. The different node effects with LMR on/off require
integrated validation; no strength gain or tuned constants are established.
See MAXIMA.md for the full protocol and remaining acceptance limits.


Threat/check ordering validation (2026-10-20): 44 Debug and 44 Release checks,
nine focused ASan/UBSan checks (leak detection disabled), nine audit checks,
and the independent 4,372-position oracle passed. All 480 D8 searches completed
legally; 96 disabled signatures matched the frozen parent exactly. With LMR,
the combined policy reduced nodes 9.20% cold and 5.14% retained versus the
disabled candidate. Without LMR, node changes were -1.04%/+1.92%. CPU results
were mixed across cold/retained and LMR settings; no general speed or strength
gain is established. See MAXIMA.md for timing, controls and audit results.
