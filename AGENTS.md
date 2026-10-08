# Maxima2 project guidance

Maxima2 is a C++ chess engine with a UCI interface, bitboard move generation,
classical search and evaluation, and experimental losers-chess support.

## Project goals

- Improve playing strength, correctness, performance, and reliability.
- Preserve the engine's focus on speed: efficient chess algorithms, high search
  throughput, and compilation tuned for the target CPU.
- Explore a learned evaluator, such as NNUE, with correctness tests and measured
  playing-strength gains guiding integration into search.
- Add multithreaded search with explicit ownership of worker state, safe shared
  caches, and reliable cancellation.
- Make the code cleaner through focused refactoring: reduce global mutable
  state, encapsulate invariants, clarify interfaces, and remove misleading or
  unused behavior.
- Preserve chess correctness and existing functionality while improving the
  engine.

These goals guide development; they do not select a parallel search algorithm
or require every task to advance every goal.

## Commit conventions

Every commit message must be a single line using Conventional Commits:
`type(scope): description`. The scope is optional. Do not include a commit body
or footer.

Examples:

- `fix: validate UCI move input`
- `refactor(search): clarify state ownership`
- `feat(search): add worker threads`

Keep commits focused and independently reviewable.

## Engineering conventions

- Keep code clean, minimal, and readable. Prefer small, explicit functions and
  simple data structures; introduce abstractions only when they simplify the
  current implementation.
- Use snake_case for new project-defined identifiers and filenames. Preserve
  externally defined names, such as UCI commands and options.
- Use Stockfish as inspiration for chess algorithms, playing-strength
  improvements, and testing practices. Express useful ideas in Maxima2's own
  style rather than adopting Stockfish's code style or architecture wholesale.
- Prioritize missing engine fundamentals and substantial measured strength gains
  over complex changes aimed at marginal Elo improvements.
- Keep heuristics understandable and independently testable. Justify added
  complexity with measured benefits.
- Treat performance as a first-class requirement. Preserve useful low-level
  techniques, including intrinsics and assembly; use profiling and benchmarks
  to guide hot-path changes and compiler choices. Compare Release builds with
  identical positions, limits, and compiler flags, checking NPS, time to depth,
  and playing strength rather than assuming any one metric proves improvement.
- Prefer incremental changes with clear ownership and explicit dependencies.
- Keep board representations, hashes, ply counters, and search stacks consistent
  through operations that own their invariants.
- Validate external input before changing engine state. Assertions supplement
  validation; they do not replace it.
- Use atomics or locks for shared mutable state. `volatile` is not thread
  synchronization.
- Support search and evaluation improvements with correctness checks and
  relevant performance or playing-strength evidence. Distinguish measured
  results from assumptions.

## Repository overview

- `src/`: engine, UCI handling, board state, move generation, search, evaluation,
  hash tables, time management, and Polyglot opening-book support.
- `src/w17/`: losers-chess search and evaluation.
- `tests/`: tests for bit operations, move generation, search, static exchange
  evaluation, Polyglot keys, hash tables, evaluation, board flipping, and mates.
- `gen/`: KPK bitbase generator.

CMake generates `src/version.h` from `src/version.h.in`. Do not edit the generated
header manually.

## Build and validation

The default build uses C++26. It requires a compiler supporting that mode
(validated with GCC 16), CMake 3.30 or newer, and Ninja for the presets.
The UCI lifecycle tests require Python 3.7 or newer. `cpp26_environment.yml`
defines an isolated Linux compiler environment; see README.md for setup.

Run from the repository root:

```bash
cmake --preset cpp26_debug
cmake --build --preset cpp26_debug --parallel
ctest --preset cpp26_debug
```

Tests must return a nonzero exit code when a check fails, so both direct execution
and CTest detect failures. Detailed test output is available in
`build/cpp26/debug/Testing/Temporary/LastTest.log`. Use `cpp26_release` for
Release checks. When using the local toolchain, prefix these commands with
`.tools/bootstrap/bin/micromamba run -p .tools/cpp26`.

- Run checks appropriate to the change. For engine behavior changes, cover
  relevant move generation, make/unmake behavior, UCI commands, and search
  lifecycle scenarios.
- For concurrency changes, check cancellation, repeated searches, race
  conditions, and scaling across worker counts.
- Use sanitizer builds when investigating memory safety or undefined behavior;
  inspect diagnostics as well as test exit status.
- For documentation-only changes, review the content and diff; a build is not
  necessary.
