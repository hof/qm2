## Maxima 2, a chess playing program

Maxima 2 is a UCI chess engine by Erik van het Hof (hof) and Hermen Reitsma
(hajewiet). It plays online as [MaximaEngine](https://lichess.org/@/MaximaEngine)
on Lichess, where it accepts challenges, and on [FICS](https://www.freechess.org/).

### History

Maxima began in 1996 as BugChess, written in Delphi (Object Pascal) with its own
graphical interface. From the start it also played losers chess (FICS wild 17),
and its analysis showed, for example, that 1. a3 d6 is a win for White. It played the 18th Dutch Open Computer Chess Championship in
1998 on a Pentium II at 450 MHz, and spent the late nineties on ICC, first as
BugChess and later as QueenMaxima(C). That original code is on GitHub as
[QueenMaxima](http://www.github.com/hof/queenmaxima).

Between 2013 and 2015 we rewrote it from scratch as Maxima 2 and released it as
open source under the GPL. The rewrite brought it to 64 bits, with bitboards and
magic bitboard move generation, a search built on late move reductions with fewer
extensions and more pruning, and a tapered evaluation tuned by an integrated
self-matching test. Maxima also became a UCI engine.

In 2026 Maxima moved to a neural network evaluation (NNUE), starting from the
Stockfish 13 network, and its search was brought up to current standards: the
transposition table, null-move and reverse futility pruning, and late move
reductions from logarithmic tables. With the same network, the search work alone
is worth about 380 Elo. Endgames use Syzygy tablebases, locally up to five pieces
and online up to seven, and a multi-layer opening book learns from every game and
every opponent. MaxDrone, the runner that plays Maxima on FICS and Lichess, can
stream games live over WebSocket and post finished games to a database.

The opening book mixes several layers on every move: a broad base book, style
books (sharp gambits, solid drawing lines, experiments, and a mood that changes
weekly), and what Maxima learned from its own games, overall, against a group of
players (humans, masters or engines) and against this particular opponent. The
mix follows the rating gap: more gambits against weaker players, more solid
lines against stronger ones. A lost game is remembered, and its winning line may
be played back at the same opponent later.

The Chess Programming Wiki has more on [BugChess NL](https://www.chessprogramming.org/BugChess_NL)
and [Maxima2](https://www.chessprogramming.org/Maxima2).

Maxima plays standard chess and losers chess (`src/w17/`); more FICS wild
variants will follow.

### Getting started

A fresh clone needs only git and Python 3.8 or newer. `./maxima init` installs
the rest: an isolated GCC 16 and CMake toolchain when the system has none, the
network, the Release and Debug builds, the opening books and the five-piece
tablebases. Running it again skips whatever is already in place.

```bash
git clone https://github.com/hof/qm2.git && cd qm2
./maxima init
./maxima uci         # run the engine in this terminal
./maxima             # command help (a GUI or pipe gets the engine)
```

In CI, `./maxima init --no-debug --no-books --syzygy 0` followed by
`./maxima test` builds and checks the engine without the large downloads.

### Development

[MAXIMA.md](MAXIMA.md) is the full technical documentation, and
[AGENTS.md](AGENTS.md) the quick entry for contributors. The engine builds with
CMake presets and tests itself without third-party Python packages:

```bash
cmake --preset cpp26_release
cmake --build --preset cpp26_release --parallel
ctest --preset cpp26_release
```

The `maxima` command wraps these steps and adds the Python runner for position
suites, engine matches with SPRT, opening experience, tablebase downloads and
online play:

```bash
./maxima build --release
./maxima test --release
./maxima suite:run --suite bratko-kopec --movetime 1000
./maxima match:run --help
./maxima tablebases:syzygy:download --pieces 5 --dry-run
./maxima fics          # play on FICS through MaxDrone
./maxima lichess       # play on Lichess as a bot account
```

MAXIMA.md explains each part in depth: [build and debugging](MAXIMA.md#5-build-and-debugging),
[local tablebases](MAXIMA.md#7-cli-and-local-tablebases),
[opening books and experience](MAXIMA.md#9-opening-books-and-persistent-experience),
[the runner and online clients](MAXIMA.md#10-python-runner-and-project-cli), and
[connectors](MAXIMA.md#11-connectors) for the live feed and PGN upload.

### License

Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma

This project is GPL'd. For complete details on our licensing please see the
included "LICENSE" file.
