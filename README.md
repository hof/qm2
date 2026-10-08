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

Maxima currently only plays traditional chess. Plans are to support wild
variants again soon, mainly loser's and suicide chess.

### Development

The default build uses C++26 and requires a compiler that supports that mode
(tested with GCC 16), CMake 3.30 or newer, Ninja, and Python 3.7 or newer.
Release builds retain `-O3 -DNDEBUG -march=native`; Debug builds retain assertions
and debug symbols. Native CPU tuning means the binary targets the build machine.

With those tools on your PATH:

```bash
cmake --preset cpp26_release
cmake --build --preset cpp26_release --parallel
ctest --preset cpp26_release
./build/cpp26/release/src/maxima2
```

Use `cpp26_debug` for the corresponding Debug configure, build, and test commands.
Its executable is `build/cpp26/debug/src/maxima2`.

For an isolated Linux toolchain, install [micromamba](https://mamba.readthedocs.io/en/latest/installation/micromamba-installation.html), then:

```bash
export MAMBA_ROOT_PREFIX="$PWD/.tools/mamba"
export XDG_CACHE_HOME="$PWD/.tools/cache"
micromamba create -y -p .tools/cpp26 -f cpp26_environment.yml
micromamba run -p .tools/cpp26 cmake --preset cpp26_release
micromamba run -p .tools/cpp26 cmake --build --preset cpp26_release --parallel
micromamba run -p .tools/cpp26 ctest --preset cpp26_release
micromamba run -p .tools/cpp26 ./build/cpp26/release/src/maxima2
```

This workspace's local micromamba executable is `.tools/bootstrap/bin/micromamba`.
The compiler environment lives under `.tools/` and is excluded from Git.
The isolated build links to the C++ runtime in `.tools/cpp26/lib`.
C++26 is required by default; CMake will fail rather than silently select an older
standard. An explicit compatibility build can select `-DCMAKE_CXX_STANDARD=17`.

You can use [Visual Studio Code](https://code.visualstudio.com/) to develop and debug Maxima2 by installing the
[Remote Development Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.vscode-remote-extensionpack) and [Docker](https://www.docker.com/).

![Image of VS Code](vscode.png)

### Online

The latest development version of Maxima 2 plays as BugChess(C) on the
[http://freechess.org/](http://freechess.org/) chess server.

### License

Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma

This project is GPL'd. For complete details on our licensing please see the
included "LICENSE" file.
