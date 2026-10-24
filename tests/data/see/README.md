# Legal exchange certificates

Regenerate with an environment containing python-chess:

```bash
python generate_legality.py
python generate_legality.py --check
```

The C++ test consumes the checked-in certificates without requiring Python.
The generator uses python-chess legal moves and push/pop, and computes material
changes from whole board positions. It independently evaluates exchanges on
the initial move's target square, choosing the least valuable legal recapturer.
Equal values are ordered by piece type then source square; promotion choices
prefer the queen. Either side can decline the exchange, even in check: this
is an explicit static material model, not a complete chess search.

All cases are also compared with exhaustive same-square material minimax over
every legal recapturer. The targeted cases require agreement; differences in
the random sample are reported rather than assuming the least-valuable model
is universally exact. The current 1,851-case sample has no such differences.

Thirty-six targeted cases cover both colors, pinned pawns/knights/bishops/rooks,
captures along the pin line, released and newly created pins, legal and illegal
king recaptures, pinned defenders of king-capture squares, en passant discovered
checks, initial promotions and recapture promotions. The remaining cases come
from seeded legal games and include captures and quiet moves.

The native test verifies scores and unchanged piece bitboards, matrix, stack
pointer, ply, clocks and hashes. A separate live-search regression verifies
that the pinned-pawn witness survives qsearch SEE pruning with neural evaluation.

Each certificate also checks the threshold overload against its independent
score: below/at/above equality, zero/one, both integer extremes, and thresholds
from -1500 through 1500 in steps of 25. This supplies 236,928 comparisons.
Castling's zero score has a separate boundary check. The full-score and threshold
interfaces must preserve board state and agree on the same exchange model.
