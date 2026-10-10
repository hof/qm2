## Maxima2, a chess playing program.

Maxima2 is the successor of [QueenMaxima and BugChess NL](http://www.github.com/hof/queenmaxima). The code has been
completely rewritten and modernized using new developments in computer chess from
the last decade.

The most notable changes are:
- 64 bits architecture with bitboards and move generation with magic bitboards.
- Using Late Move Reductions (LMR) in the search.
- Less extensions, more pruning.
- More efficient code.
- Integrated self-matching test for testing and tuning evaluation values.
- Tapered evaluation.
- Maxima is now an UCI engine.

Standard chess is the main target. Experimental losers-chess support lives
in `src/w17/`.

### Development

See [MAXIMA.md](MAXIMA.md#5-build-and-debugging) for requirements, isolated toolchain setup,
Debug and Release modes, and validation. Engine CTest uses no third-party Python
dependencies. Install the [Python runner](MAXIMA.md#10-python-runner-and-project-cli)
for suites, experience tooling, and combined checks.
Quick Release build:

```bash
cmake --preset cpp26_release
cmake --build --preset cpp26_release --parallel
ctest --preset cpp26_release
./maxima
```

### Online

The latest development version of Maxima 2 plays as BugChess(C) on the
[http://freechess.org/](http://freechess.org/) chess server.

### License

Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma

This project is GPL'd. For complete details on our licensing please see the
included "LICENSE" file.

### Documentation and project commands

[MAXIMA.md](MAXIMA.md) is the full technical documentation. [AGENTS.md](AGENTS.md)
is the quick contributor entry.

```bash
./maxima                                         # start the built engine
./maxima build --release                          # configure and compile
./maxima test --release                           # engine and runner checks
./maxima suite:run --suite bratko-kopec --movetime 1000
./maxima experience:import --pgn completed.pgn
./maxima experience:analyze --pgn completed.pgn --movetime 1000
./maxima tablebases:syzygy:download --pieces 4    # local three/four-piece tables
./maxima tablebases:syzygy:download --pieces 5 --dry-run
```

See [local tablebases](MAXIMA.md#7-cli-and-local-tablebases) for UCI configuration,
probe policies, storage selection, and download verification.

See [position suites](MAXIMA.md#61-position-suites-through-uci) for SAN/EPD input,
external engine selection, search budgets, and JSON reports.

See [persistent experience](MAXIMA.md#9-opening-books-and-persistent-experience)
for result statistics, opponent learning, saved analysis, and book configuration.
