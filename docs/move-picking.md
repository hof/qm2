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
killers, countermove, positive-history quiets, deferred negative-SEE noisy
moves, then remaining quiets. Quiet castling candidates share quiet ordering;
qsearch retains its existing castling policy. Positive means strictly greater
than zero. Stage separation does not assert tactical correctness of SEE or
promise a cutoff. Scores are cached when each batch is generated. Returned
hint codes suppress duplicates without making generated king-safety tests.
Countermoves are keyed by the previous colored piece and destination; no
continuation/countermove context is used after a null move or at a cold root.

Quiet scores sum three signed, bounded history entries:

- Main: side, source square, destination square.
- One-ply continuation: previous colored piece/destination and current colored
  piece/destination. Root and null predecessors provide no continuation entry.
- Pawn: an incremental colored-pawn hash modulo 512, then current colored
  piece/destination. Collisions deliberately share heuristic scores.

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
