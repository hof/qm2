# Maxima quick entry

[MAXIMA.md](MAXIMA.md) is the full reference. Read its relevant chapters for: goals,
architecture, build setup, testing, performance practices, tablebases, and persistent experience.
Update that document when behavior or workflows change.
Read `.context/agents.md` when present for additional private local context;
`.context/` is ignored by Git.

## Essential conventions

- Improve playing strength, search correctness, speed, and reliability.
- Keep code clean and minimal; use snake_case for new project identifiers.
- Always use curly braces for `if` and `else` bodies, including single statements.
- Prioritize missing fundamentals and measured strength gains. Preserve useful
  chess optimizations, intrinsics, and assembly; measure hot-path changes.
- Keep worker state owned and shared caches synchronized. `volatile` is not
  thread synchronization.
- Preserve board, hash, repetition, clock, and search-stack invariants.
- Validate external input before changing engine state.
- Commit each completed, tested increment before starting the next.
- Keep commits focused. Every commit message is exactly one line using
  Conventional Commits: `type(scope): description`, with optional scope and
  no body or footer.
- Do not edit generated version headers; CMake generates `generated/maxima/version.h`
  in each build directory from `src/version.h.in`.
- Keep third-party decoder code isolated; project adapters follow our style.

## Build and checks

C++26, a supporting compiler (validated with GCC 16), CMake 3.30+, and Ninja:

```bash
cmake --preset cpp26_debug
cmake --build --preset cpp26_debug --parallel
ctest --preset cpp26_debug
```

Use `cpp26_release` for Release builds and checks. The local toolchain prefix is
`.tools/bootstrap/bin/micromamba run -p .tools/cpp26`; see `MAXIMA.md` for setup.
Engine lifecycle tests require Python 3.7+ with its standard library. The independent
runner package requires Python 3.8+ and python-chess (install with `pip install -e ./runner`).
Use `./maxima test` for engine and runner checks; `BUILD_TESTING=OFF` builds without Python.
See MAXIMA.md chapters 6.1 and 10 for setup and ownership boundaries.
Running the engine does not require Python.
Run checks appropriate to the change, including search lifecycle tests for
engine changes and sanitizer/race checks where relevant. Tests must return
nonzero on failure. Report measured results and remaining limitations honestly.
