## Maxima 2, a chess playing program

Maxima 2 is the successor of [QueenMaxima and BugChess NL](http://www.github.com/hof/queenmaxima),
written by Erik van het Hof (hof) and Hermen Reitsma (hajewiet). We work on it about
once per decade, and every time we catch up with what computer chess learned in
between. See you in 2036.

**1996: QueenMaxima and BugChess NL.** The original engines, playing on the
internet chess servers of the time.

**2013–2015: Maxima 2.** A complete rewrite:
- 64-bit architecture with bitboards and magic bitboard move generation.
- Late Move Reductions (LMR) in the search; fewer extensions, more pruning.
- Tapered evaluation, tuned with an integrated self-matching test.
- More efficient code.
- Maxima became a UCI engine.

**2026: the NNUE decade.**
- NNUE evaluation, starting from the Stockfish 13 network.
- The search brought up to current standards (transposition table, null-move
  and reverse futility pruning, LMR from logarithmic tables), worth about
  +380 Elo with the same network.
- Syzygy tablebases: local up to five pieces, seven pieces online at the root.
- A multi-layer opening book that learns from every game and every opponent.
- MaxDrone, the runner that plays Maxima online on FICS and Lichess, with a
  WebSocket live feed and PGN upload for viewers and databases.
- C++26, and one command (`./maxima init`) to set up a fresh clone or a CI job.

Standard chess is the main target. Experimental losers-chess support lives
in `src/w17/`; more FICS wild variants will follow.

### Play online

- Lichess: Maxima plays as [MaximaEngine](https://lichess.org/@/MaximaEngine), a bot
  account that accepts standard bullet, blitz and rapid challenges. Challenge it!
- FICS: [freechess.org](https://www.freechess.org/) through MaxDrone.

### Getting started

```bash
git clone https://github.com/hof/qm2.git && cd qm2
./maxima init        # toolchain, network, builds, books, 5-man Syzygy; reruns skip what is ready
./maxima uci         # run the engine in this terminal
./maxima             # command help (a GUI or pipe gets the engine)
```

`maxima init` needs only git and Python 3.8+; it fetches GCC 16 and CMake through
an isolated toolchain when the system has none. For CI:
`./maxima init --no-debug --no-books --syzygy 0` and then `./maxima test`.

### Development

See [MAXIMA.md](MAXIMA.md#5-build-and-debugging) for requirements, the isolated
toolchain, Debug and Release modes, and validation. Engine CTest uses no third-party
Python dependencies; the [Python runner](MAXIMA.md#10-python-runner-and-project-cli)
adds suites, matches, experience tooling and online play.

```bash
cmake --preset cpp26_release
cmake --build --preset cpp26_release --parallel
ctest --preset cpp26_release
```

### Documentation and project commands

[MAXIMA.md](MAXIMA.md) is the full technical documentation. [AGENTS.md](AGENTS.md)
is the quick contributor entry.

```bash
./maxima build --release                          # configure and compile
./maxima test --release                           # engine and runner checks
./maxima suite:run --suite bratko-kopec --movetime 1000
./maxima match:run --help                         # engine matches and SPRT
./maxima experience:import --pgn completed.pgn
./maxima tablebases:syzygy:download --pieces 5 --dry-run
./maxima fics                                     # play on FICS as MaxDrone
./maxima lichess                                  # play on Lichess as a bot account
```

See [local tablebases](MAXIMA.md#7-cli-and-local-tablebases) for UCI configuration,
probe policies, storage selection, and download verification;
[persistent experience](MAXIMA.md#9-opening-books-and-persistent-experience) for the
learning book; [the FICS and Lichess clients](MAXIMA.md#10-python-runner-and-project-cli)
for online play; and [connectors](MAXIMA.md#11-connectors) for the live feed and PGN
upload.

### License

Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma

This project is GPL'd. For complete details on our licensing please see the
included "LICENSE" file.
