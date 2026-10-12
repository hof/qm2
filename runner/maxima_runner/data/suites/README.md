# Position benchmark data

## Contemporary default

`modern_2026.epd` is the default 22-position diagnostic corpus, frozen on
2026-10-11 from the 2026 Tata Steel Masters broadcasts (rounds 1, 7 and 13).
The source PGNs are bundled under `sources/`. JSON run reports identify both
corpus and executable by SHA-256; input changes during a run reject the report. Positions are sampled at fixed
plies, with at least eight legal choices, at most two positions per game and
at most eight per round. Selection does not consult Maxima's results.

Stockfish 19 supplies the targets. A 200,000-node cold MultiPV screen is followed
by independent cold two-million and eight-million-node passes, using one thread,
64 MiB hash, MultiPV 2 and no tablebases. The same best move must survive all
passes, with at least 50 cp over the runner-up and scores within +/-1000 cp;
the last completed MultiPV depth must be at least 18. Mate targets are excluded
because they need a separate solution certificate. Incomplete/aspiration bounds
are excluded; both PVs must finish at the same depth and replay legally.

`modern_2026.json` records each source game/ply, reference binary hash, settings,
completed depths, top alternatives, scores and PVs at both verification budgets.
The reference is the [official Stockfish 19 release](https://github.com/official-stockfish/Stockfish/releases/tag/sf_19).
The downloaded x86-64 universal archive was checked against its published
SHA-256: `9defc0d4e55d49c65a6d042f3e571a39fcea499ade6dbe741b53b8c65e03611f`.
The source broadcast headers are preserved as published; their individual date
fields are metadata, not independently verified event dates.

Rebuild with the runner environment and an explicit Stockfish 19 executable:

```bash
.tools/runner/bin/python runner/tools/build_modern_suite.py \
    --engine /path/to/stockfish19 \
    --source runner/maxima_runner/data/suites/sources/tata-steel-2026-round1.pgn \
    --source runner/maxima_runner/data/suites/sources/tata-steel-2026-round7.pgn \
    --source runner/maxima_runner/data/suites/sources/tata-steel-2026-round13.pgn \
    --output /tmp/modern_2026.epd --manifest /tmp/modern_2026.json \
    --date 2026-10-11
```

Regeneration requires substantial reference-engine work. Review any changed
labels instead of overwriting the frozen corpus automatically. Offline runner
checks verify source hashes, game replay, position identity, labels, both search
budgets/margins and every recorded PV without requiring Stockfish.

These are finite reference searches, not exhaustive best-move proofs. The corpus
covers a small sample from one event; a higher solved count does not establish
Elo. Use paired matches for strength decisions and independent rules/terminal/
state regressions for correctness. Refresh labels when stronger analysis disputes
a target. Keep this diagnostic corpus distinct from NNUE training data.

### Data attribution and license

Recent-game positions and bundled source PGNs are adapted from Lichess broadcasts:
[round 1](https://lichess.org/broadcast/tata-steel-chess-2026-masters/round-1/FRGTkE8Z),
[round 7](https://lichess.org/broadcast/tata-steel-chess-2026-masters/round-7/vhLqH6fn),
[round 13](https://lichess.org/broadcast/tata-steel-chess-2026-masters/round-13/yuTondoz).
They are distributed under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/),
following the [Lichess broadcast data license](https://database.lichess.org/#broadcasts).
Changes: clocks/comments removed by the export, trailing blank lines normalized,
sampled positions converted to
EPD, and new reference-engine labels/verification metadata added. This data
license applies to the modern corpus and source PGNs, not the runner code or
historical BK data.

## Historical Bratko–Kopec suite

`bratko-kopec.epd` retains 22 positions and expected coordinate moves from
Maxima's historical `tests/test_bratkokopec.cpp`, recovered with:

```bash
git show be07fda^:tests/test_bratkokopec.cpp
```

The standalone BK test was removed in `be07fda` (2014, "Combined performance
test"). The later performance test was reduced to a mate test in `b562253`
(2020). Historical IDs are retained. BK.03 and BK.09 were removed after reviewing
[Vinvin and Dann Corbit's 2015 discussion](https://talkchess.com/forum3/viewtopic.php?f=6&t=57166):
BK.03's `...f5` target is disputed, and BK.09's `f5` is not uniquely best in
the reported analysis. BK.07 remains, with its target still open to review.
Remaining targets have not been exhaustively adjudicated and are not mandatory
correctness assertions. Historical 24-position scores below are not comparable
to the filtered suite or the new default corpus.

The runner accepts SAN and legal UCI coordinate `bm` moves. This recovered
file retains its historical coordinate notation; JSON reports include SAN too.
See MAXIMA.md chapter 6.1 for dependency setup, commands, configuration, result
interpretation, and limitations.

## Historical 24-position baseline (2026-10-08)

The original, unfiltered 24-position suite used a Release build, GCC 16.2/C++26, 64 MiB hash, cold process per position,
opening book and Syzygy disabled, historical BK targets:

| Requested UCI budget | Solved |
| --- | --- |
| `go movetime 1000` | 17/24 |
| `go nodes 200000` | 15/24 |

These are observations, not required thresholds. The executable identifies as
`Maxima 2.0.0-master-290d7d1-Release` and includes the uncommitted Syzygy integration.
Maxima reports visited plus pruned nodes but enforces the limit against visited
nodes. Time results depend on the machine and system load; compare new runs under
the same conditions. JSON reports from this run were saved to
`/tmp/maxima-bk-time-baseline.json` and `/tmp/maxima-bk-nodes-baseline.json` locally;
they are not part of the repository.
