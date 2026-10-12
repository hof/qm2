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
./maxima suite:run --nodes 200000
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
Offline matches preserve both variants in PGNs. Losers suites, general PGN
imports, and experience remain future work.
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

Offline paired matches use `maxima match:run --engine-a PATH --engine-b PATH
--openings FILE --pairs 10 --seconds 10 --increment 0.1 --pgn games.pgn
--json results.json`. Add `--variant losers` for royal losers. The opening JSON
must declare `variant` (`standard` or `losers`) and an `openings` list of objects
with unique `id`, optional six-field `fen`, and optional legal UCI `moves`.
Each selected opening is played twice with swapped colors. The seed fixes
selection without replacement; clock-based engine moves need not repeat.
Read royal-losers match PGNs with `maxima_runner.matches.read_match_pgn`;
ordinary readers may not recognize this variant. See MAXIMA.md for result and
confidence-interval semantics.

Match profiles accept repeatable `--option-a NAME=VALUE` / `--option-b NAME=VALUE`
assignments, validated against each engine's advertised option types and bounds.
For example, compare the same binary with `--option-a LMR=true --option-b LMR=false`.
Shared resource/variant controls are protected. Use `--model-a OPTION=PATH` or
`--model-b OPTION=PATH` for external networks: paths become absolute and SHA-256
identities are recorded and checked throughout the match. See MAXIMA.md for the
full configuration and identity contract.
