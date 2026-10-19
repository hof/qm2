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
