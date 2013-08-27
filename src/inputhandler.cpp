
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <string.h>

#include <fstream>

#include "inputhandler.h"

/**
 * Handle input command from stdin
 * @param cmd input from stdin
 * @return true unless the input command requests an exit
 */
bool TInputHandler::handle(std::string cmd) {
    bool result = true;
    TInputParser parser(cmd);
    
    //std::ofstream myfile;
    //myfile.open("uci.log", std::ios::app);
    //myfile << "> " << cmd << std::endl;
    //myfile.close();
    
    std::string token;
    if (parser >> token) { // operator>>() skips any whitespace
        if (token == "quit" || token == "exit" || token == "bye") {
            if (_engine) {
                engine()->stop();
            }
            result = false;
        } else if (token == "stop") {
            engine()->stop();
        } else if (token == "go") {
            result = handleGo(parser);
        } else if (token == "uci") {
            result = handleUCI();
        } else if (token == "ucinewgame") {
            result = handleNewGame();
        } else if (token == "isready") {
            result = handleIsReady();
        } else if (token == "position") {
            result = handlePosition(parser);
        } else if (token == "forward") {
            result = handleForward(parser);
        } else if (token == "setoption") {
            result = handleSetOption(parser);
        } else if (token == "testrun") {
            result = handleTestrun(parser);
        } else if (token == "ponderhit") {
            result = handlePonderHit();
        }
    }
    return result;
}

/**
 * Handle the GO command 
 * @param parser inputparser
 * @return true
 */
bool TInputHandler::handleGo(TInputParser& parser) {
    bool result = true;
    std::string token;
    engine()->gameSettings.clear();
    while (parser >> token) {
        if (token == "infinite") {
            engine()->gameSettings.maxDepth = MAX_PLY;
        } else if (token == "ponder") {
            engine()->gameSettings.maxDepth = MAX_PLY;
            engine()->gameSettings.ponder = true;
        } else if (token == "wtime") {
            parser >> engine()->gameSettings.whiteTime;
        } else if (token == "btime") {
            parser >> engine()->gameSettings.blackTime;
        } else if (token == "winc") {
            parser >> engine()->gameSettings.whiteIncrement;
        } else if (token == "binc") {
            parser >> engine()->gameSettings.blackIncrement;
        } else if (token == "movestogo") {
            parser >> engine()->gameSettings.movesLeft;
        } else if (token == "depth") {
            parser >> engine()->gameSettings.maxDepth;
        } else if (token == "nodes") {
            parser >> engine()->gameSettings.maxNodes;
        } else if (token == "movetime") {
            parser >> engine()->gameSettings.maxTimePerMove;
        } else if (token == "searchmoves") {

        }
    }
    engine()->setHashTable(hashTable());
    engine()->setOutputHandler(outputHandler());
    engine()->setPonder(engine()->gameSettings.ponder);
    engine()->setInputHandler(this);
    engine()->setPosition(_fen);
    engine()->think();
    return result;
}

bool TInputHandler::handleUCI() {
    bool result = true;
    outputHandler()->sendID();
    outputHandler()->sendOptions();
    outputHandler()->sendUCIOk();
    return result;
}

bool TInputHandler::handleIsReady() {
    bool result = true;
    outputHandler()->sendReady();
    return result;
}

bool TInputHandler::handlePonderHit() {
    bool result = true;
    engine()->setPonder(false);
    return result;
}

bool TInputHandler::handleSetOption(TInputParser& parser) {
    bool result = true;
    bool handled = false;
    std::string token, name, value;
    name = "unknown";

    if ((parser >> token) || token == "name") {// operator>>() skips any whitespace
        if (parser >> name) {
            // Handle names with included spaces
            while (parser >> token && token != "value") {
                name += (" " + token);
            }

            if (token == "value") {
                if (parser >> value) {
                    // Handle values with included spaces
                    while (parser >> token) {
                        value += (" " + token);
                    }
                    //at this point, we have a name, value pair
                    if (name == "Hash") {
                        handled = true;
                        _hashSize = fromString<int>(value);
                    } else if (name == "UCI_Opponent") {
                        handled = true;
                        //todo
                        _opponentString = value;
                    }
                    
                    
                }
            } else {
                //toggle option
            }
        }
    }
    if (!handled) {
        outputHandler()->sendUnknownOption(name);
    }
    return result;
}

bool TInputHandler::handleNewGame() {
    _fen = _defaultFen;
    engine()->newGame(_fen);
	return true;
}

bool TInputHandler::handlePosition(TInputParser& parser) {
    bool result = true;
    std::string token;
    if (parser >> token) {
        if (token == "startpos") {
            _fen = _defaultFen;
            parser >> token;
        } else if (token == "fen") {
            _fen = "";
            while (parser >> token && token != "moves") {
                _fen += token;
                _fen += ' ';
            }
        } else {
            //should never happen
            _fen = _defaultFen;
            parser >> token;
        }
        TBoard pos;
        pos.fromFen(_fen.c_str());
        if (token == "moves") {
            THashTable * hash = hashTable();
            hash->repTable[pos.boardFlags->fiftyCount] = pos.boardFlags->hashCode;
            while (parser >> token) {
                TMove move;
                move.fromString(&pos, token.c_str());
                pos.forward(&move);
                hash->repTable[pos.boardFlags->fiftyCount] = pos.boardFlags->hashCode;
                if (pos.currentPly > MAX_PLY - 2) {
                    pos.fromFen(pos.asFen().c_str()); //reset to prevent overflow
                }
            }
        }
        _fen = pos.asFen();
    }
    return result;
}

bool TInputHandler::handleForward(TInputParser& parser) {
    bool result = true;
    std::string token;
    TBoard pos;
    pos.fromFen(_fen.c_str());
    while (parser >> token) {

        TMove move;
        move.fromString(&pos, token.c_str());
        pos.forward(&move);
    }
    _fen = pos.asFen();
    return result;
}

bool TInputHandler::handleTestrun(TInputParser& parser) {
    TEngine * engine2 = new TEngine();
    TEngine * engine1 = engine();
    engine1->gameSettings.clear();
    engine2->gameSettings.clear();


    THashTable * hash1 = new THashTable(64);
    THashTable * hash2 = new THashTable(64);

    engine1->setHashTable(hash1);
    engine2->setHashTable(hash2);

    engine1->setOutputHandler(NULL);
    engine2->setOutputHandler(NULL);
    engine1->setInputHandler(this);
    engine2->setInputHandler(this);

    TBoard * pos = new TBoard();

    engine1->gameSettings.maxDepth = 4;
    engine2->gameSettings.maxDepth = 4;
    engine1->gameSettings.learnParam = 1;
    engine2->gameSettings.learnParam = 2;
    engine1->gameSettings.learnFactor = 1.0;
    engine2->gameSettings.learnFactor = 1.0;

    TEngine * engine[2] = {engine1, engine2};

    const int GAMECOUNT = 100;
    const int MAXPLIES = 200;
    const int STOPSCORE = 150;
    const int LEARNSTEPS = 5;

    double learnSteps[LEARNSTEPS] = {0, 0.5, 1.0, 1.5, 2.0};
    double refinedSteps[LEARNSTEPS][LEARNSTEPS] = {
        { -0.4, -0.2, 0, 0.2, 0.4},
        { 0.2, 0.4, 0.5, 0.6, 0.8},
        { 0.6, 0.8, 1.0, 1.2, 1.4},
        { 1.2, 1.4, 1.5, 1.6, 1.8},
        { 1.6, 1.8, 2.0, 2.2, 2.4}
    };

    int bestScore = 0;
    int bestStep = 0;
    int bestWins = 0;
    double bestFactor = 0.0;
    double* stepsTable[2] = {learnSteps, refinedSteps[1]};


    for (int phase = 0; phase < 2; phase++) {
        double* steps = stepsTable[phase];
        int scores[3] = {0, 0, 0};

        for (int step = 0; step < LEARNSTEPS; step++) {
            int stats[3] = {0, 0, 0};
            U64 nodes[2] = {0, 0};

            engine1->gameSettings.learnFactor = steps[step];

            hash1->clear();
            hash2->clear();
            std::cout << "factor: " << engine1->gameSettings.learnFactor << " ";
            for (int game = 0; game < GAMECOUNT; game++) {
                pos->fromFen(_fen.c_str());
                int plyCount = 0;
                bool wtm = true;
                bool us = !wtm;
                bool them = !us;
                do {
                    if (pos->boardFlags->fiftyCount >= 100 || pos->isDraw() || plyCount >= MAXPLIES) {
                        stats[0]++;
                        break;
                    }
                    if (pos->currentPly > MAX_PLY - 10) {
                        pos->fromFen(pos->asFen().c_str());
                    }
                    engine[us]->newGame(pos->asFen());
                    engine[us]->think();
                    engine[us]->stopAllThreads();
                    int score = engine[us]->getScore();
                    nodes[engine[us] == engine2] += engine[us]->getNodesSearched();
                    if (score > 20000 || (score > STOPSCORE && engine[them]->getScore()<-STOPSCORE)) {
                        stats[1 + (engine[us] == engine2)]++;
                        break;
                    }
                    if (score < -20000 || (score<-STOPSCORE && engine[them]->getScore() > STOPSCORE)) {
                        stats[1 + (engine[them] == engine2)]++;
                        break;
                    }
                    TMove move = engine[us]->getMove();
                    pos->forward(&move);
                    plyCount++;
                    us = them;
                    them = !us;
                    wtm = !wtm;
                } while (true);
                if (engine[0] == engine1) {
                    engine[0] = engine2;
                    engine[1] = engine1;
                } else {
                    engine[0] = engine1;
                    engine[1] = engine2;
                }
            }
            int points = stats[1]*2 + stats[0];
            int maxPoints = GAMECOUNT * 2;
            int wins = stats[1] - stats[2];
            U64 totalNodes = nodes[0] + nodes[1];
            int score = (100 * points) / maxPoints;
            scores[step] = score;

            int nodePct = (100 * nodes[0]) / totalNodes;
            if (score >= bestScore && wins > bestWins) {
                bestScore = score;
                bestStep = step;
                bestWins = wins;
                bestFactor = steps[step];
            }
            std::cout << "games:" << GAMECOUNT << " win: " << stats[1] << " loss: " << stats[2] << " draw: " << stats[0] << " (" << score << "%, " << wins << ")" << " nodes: " << nodePct << "%" << std::endl;
        }
        if (phase == 0) {
            stepsTable[1] = refinedSteps[bestStep];
            bestFactor = 0;
            bestStep = 0;
            bestScore = 0;
            bestWins = 0;
        }

    }

    std::cout << "best factor: " << bestFactor << " (" << bestScore << "%)" << std::endl;

    delete engine2;
    delete hash1;
    delete hash2;
    delete pos;
}



