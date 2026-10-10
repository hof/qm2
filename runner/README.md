# Maxima runner

Python CLI and chess orchestration package, kept separate from the C++ engine
within the Maxima repository. Requires Python 3.8+ and pins python-chess via
`chess==1.11.2`. Engine communication uses asyncio; boards, notation, PGN, EPD,
and Polyglot keys use python-chess.

From the repository root:

```bash
python3 -m venv .tools/runner
.tools/runner/bin/python -m pip install -e ./runner
./maxima build --release
./maxima test --release
./maxima suite:run --suite bratko-kopec --nodes 200000
```

Installing with `pip install ./runner` also provides the `maxima` console
command. Generic suites work outside the checkout with an explicit `--engine`.
Build/test helpers require a checkout, discovered from the current directory
or `MAXIMA_PROJECT_DIR`.

The async UCI adapter is `maxima_runner.uci.open_engine`; its sessions expose
configuration and streamed searches using `chess.Board` and
`chess.engine.Limit`. `maxima_runner.variants.royal_losers_board` implements
FICS royal losers with compulsory legal captures and orthodox king safety.
Sessions map that board to an advertised `Wild=losers` option and ordinary boards
to `Wild=standard`; unsupported variants/engines are rejected before search.
Losers CLI suites, PGN workflows, matches, and experience remain future work.
Maxima-specific experience storage lives separately in
`maxima_runner.experience`. The CLI owns repository paths and compiler helpers.

Offline tests:

```bash
.tools/runner/bin/python -m unittest discover -s runner/tests -v
```

Set `MAXIMA_ENGINE` to an absolute engine executable path to enable integration
checks. The independent move/state comparison also requires the sibling
`tests/board_oracle_probe` target from a `BUILD_TESTING=ON` build. It retains fixed
fixtures and seeds and compares both variants' move sets, child FENs, repetition
identity, incremental hashes, and make/unmake restoration. `./maxima test`
supplies the selected Debug or Release engine and runs both engine CTest and
runner checks.

See [MAXIMA.md](../MAXIMA.md#10-python-runner-and-project-cli) for the full
architecture, workflows, and JDrone migration roadmap.
