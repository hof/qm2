# Position benchmark data

`bratko-kopec.epd` contains the 24 positions and expected coordinate moves from
Maxima's historical `tests/test_bratkokopec.cpp`, recovered with:

```bash
git show be07fda^:tests/test_bratkokopec.cpp
```

The standalone BK test was removed in `be07fda` (2014, "Combined performance
test"). The later performance test was reduced to a mate test in `b562253`
(2020). These targets preserve the original benchmark; alternatives have not
been exhaustively adjudicated. They are not mandatory correctness assertions.

The runner accepts SAN and legal UCI coordinate `bm` moves. This recovered
file retains its historical coordinate notation; JSON reports include SAN too.
See MAXIMA.md chapter 6.1 for dependency setup, commands, configuration, result
interpretation, and limitations.

## Initial baseline (2026-10-08)

Current Release build, GCC 16.2/C++26, 64 MiB hash, cold process per position,
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
