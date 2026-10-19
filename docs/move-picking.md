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
