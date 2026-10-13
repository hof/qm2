# Independent HalfKP reference

Unmodified source from https://github.com/dshawul/nnue-probe at
`edcd97e998b0299f952786c68fe37f04a7ed7f0d`, GPL-3.0 (see LICENSE).
Daniel Shawul's probe adapts the CFish NNUE implementation. This copy is used
only by optional real-network correctness tests, never by the engine.

Maxima's isolated CPU implementation is `src/nnue.cpp`. It implements the same
legacy HalfKP file format and integer arithmetic with immutable model ownership
and search-owned accumulators. Newer Stockfish architectures are unsupported.

Bootstrap weights: official Stockfish network `nn-62ef826d1a6d.nnue`, CC0:
https://github.com/official-stockfish/networks

Full SHA-256: `62ef826d1a6d11b9e814188025aa02a60815c037292e0ef9bbb9bf4f724e5e63`.
Weights are downloaded separately and are not committed.

The model is the default of official Stockfish `sf_13`, commit
`3597f1942ec6f2cfbd50b905683739b0900ff5dd`. Its raw `/16` output is in
Stockfish internal units, despite the probe header's approximate-centipawn wording.
Maxima applies the official sf_13 UCI conversion `raw * 100 / PawnValueEg`,
with `PawnValueEg=208`, and then clamps outside reserved score ranges.
References: `src/types.h`, `src/uci.cpp` and `src/evaluate.h` at that tag:
https://github.com/official-stockfish/Stockfish/tree/sf_13/src
The separate Stockfish evaluation wrapper's material/tempo/rule50 heuristics are
not part of this unit conversion and are not imported.
