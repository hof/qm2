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
