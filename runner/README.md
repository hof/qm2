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

For standard chess, add `--adjudicate-win-cp 1000` to stop a clearly decided
game once both engines agree on the winning side for six consecutive plies,
starting at full move 20. Scores must meet the threshold without bound flags;
missing scores or disagreement reset the sequence. Change the sequence length
with `--adjudicate-win-plies` and the earliest move with `--adjudicate-min-move`.
PGN/JSON records distinguish adjudication from a rules-based result and retain
the deciding score evidence. This is off by default; it does not replace
separate checks that the engine can convert won positions. Crashes and illegal
moves remain errors, not losses.

Match profiles accept repeatable `--option-a NAME=VALUE` / `--option-b NAME=VALUE`
assignments, validated against each engine's advertised option types and bounds.
For example, compare the same binary with `--option-a LMR=true --option-b LMR=false`.
Shared resource/variant controls are protected. Use `--model-a OPTION=PATH` or
`--model-b OPTION=PATH` for external networks: paths become absolute and SHA-256
identities are recorded and checked throughout the match. See MAXIMA.md for the
full configuration and identity contract.

For strength decisions, select paired logistic GSPRT before playing with
`--sprt 0 5` (engine A versus B), optionally `--sprt-alpha 0.05 --sprt-beta 0.05`.
`--pairs` caps the run. Decisions occur only after complete color-swapped pairs;
a capped test without a threshold crossing is inconclusive. Reports include
pentanomial counts, likelihood ratio, thresholds and decision. Historical
fixed-sample tests retain their original interpretation. No extra runtime
statistics dependency is needed; the isolated fast-chess port retains its MIT
license and upstream attribution.

For the first decision from either SPRT or a repeated-check confidence bound,
use `--sprt 0 10 --sprt-min-elo-stop`. The additional rule can establish a
minimum +10 Elo gain or a gain below that target, and reports which rule stopped
play. Error budgets are split; plain SPRT acceptance alone does not certify
at least +10 Elo. For that stricter claim, use `--min-elo 10` instead: success
requires the simultaneous 95% lower score bound to exceed the +10 Elo target.
Declare the pair cap before play. See MAXIMA.md for allocation, assumptions,
confidence and capped-inconclusive semantics.
