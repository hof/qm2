
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <string.h>

#include <fstream>

#include "inputhandler.h"

//#define DO_LOG

/**
 * Handle input command from stdin
 * @param cmd input from stdin
 * @return true unless the input command requests an exit
 */
bool TInputHandler::handle(std::string cmd) {
    bool result = true;
    TInputParser parser(cmd);

#ifdef DO_LOG
    std::ofstream myfile;
    myfile.open("uci.log", std::ios::app);
    myfile << "> " << cmd << std::endl;
    myfile.close();
#endif

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
        } else if (token == "ponderhit") {
            result = handlePonderHit();
        } else if (token == "testeval") {
            result = handleTestEval(parser);
        } else if (token == "learn") {
            result = handleLearn(parser);
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
                        _hashSizeRequest = fromString<int>(value);
                    } else if (name == "UCI_Opponent") {
                        //value GM 2800 human Gary Kasparow"
                        //value none none computer Shredder"
                        _opponentString = value;
                        TInputParser optionParser(_opponentString);
                        std::string optionToken;

                        if (optionParser >> optionToken) { // title
                            _opponent.Title = optionToken;
                        }
                        if (optionParser >> optionToken) { //rating
                            _opponent.Rating = optionToken == "none" ? 0 : fromString<int>(optionToken);
                        }
                        if (optionParser >> optionToken) { //computer or human
                            _opponent.Computer = optionToken == "computer";
                        }
                        if (optionParser >> optionToken) { //name
                            _opponent.Name = optionToken;
                        }
                        engine()->setOpponent(&_opponent);
                        handled = true;
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
            hash->repTable[pos.stack->fiftyCount] = pos.stack->hashCode;
            while (parser >> token) {
                TMove move;
                move.fromString(&pos, token.c_str());
                pos.forward(&move);
                hash->repTable[pos.stack->fiftyCount] = pos.stack->hashCode;
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

/*
 * XLearn can be used to determine if a new evaluation or search 
 * feature gives better performance, and what is the ideal score
 * 
 */
bool TInputHandler::handleLearn(TInputParser& parser) {
    bool result = true;
    engine()->gameSettings.clear();
    engine()->setHashTable(hashTable());
    engine()->setOutputHandler(NULL);
    engine()->setPonder(false);
    engine()->setInputHandler(this);
    engine()->setPosition(_fen);
    engine()->learn();
    return result;
}

/*
 * XAnalyse can be used to test the evaluation function by analysing
 * a position and displaying the evaluation score for each evaluation
 * component
 */
bool TInputHandler::handleTestEval(TInputParser& parser) {
    bool result = true;
    engine()->gameSettings.clear();
    engine()->setHashTable(hashTable());
    engine()->setPonder(false);
    engine()->setInputHandler(this);
    engine()->setOutputHandler(outputHandler());
    engine()->gameSettings.maxDepth = 1;
    std::string token;
    _fen = "";
    while (parser >> token) {
        _fen += token;
        _fen += ' ';
    }
    if (_fen == "") {
        _fen = _defaultFen;
    }
    engine()->newGame(_fen);
    engine()->analyse();
    //engine()->think();
    return result;
}

