/* 
 * File:   inputhandler.h
 * Author: Hajewiet
 *
 * Created on 21 juni 2011, 0:08
 */

#ifndef INPUTHANDLER_H
#define	INPUTHANDLER_H

#include <sstream>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "outputhandler.h"
#include "bbmoves.h"
#include "engine.h"
#include "hashtable.h"

class TInputHandler {
private:

    template<class T>
    T fromString(const std::string& s) {
        std::istringstream stream(s);
        T t;
        stream >> t;
        return t;
    }

    typedef std::istringstream TInputParser;
    bool handleGo(TInputParser &parser);
    bool handleUCI();
    bool handleSetOption(TInputParser &parser);
    bool handleIsReady();
    bool handleNewGame();
    bool handlePosition(TInputParser &parser);
    bool handleForward(TInputParser &parser);
    bool handlePonderHit();
    bool handleLearn(TInputParser &parser);
    bool handleTestEval(TInputParser &parser);
    TEngine * _engine;
    THashTable * _hashTable;
    TOutputHandler * _outputHandler;
    static const int DEFAULT_HASHSIZE = 128; //size in Mb
    int _hashSize;
    int _hashSizeRequest;
    std::string _defaultFen;
    std::string _opponentString;
    std::string _fen;
public:

    TInputHandler() {
        _engine = NULL;
        _hashTable = NULL;
        _hashSize = DEFAULT_HASHSIZE;
        _hashSizeRequest = _hashSize;
        _outputHandler = NULL;
        _defaultFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        _fen = _defaultFen;
        _opponentString = "";
    }

    ~TInputHandler() {
        if (_engine) {
            delete _engine;
        }
        if (_hashTable) {
            delete _hashTable;
        }
        if (_outputHandler) {
            delete _outputHandler;
        }
    }
    bool handle(std::string cmd);

    THashTable * hashTable() {
        if (_hashTable && _hashSize != _hashSizeRequest) {
            delete _hashTable;
            _hashTable = NULL;
            _hashSize = _hashSizeRequest;
        }
        if (_hashTable == NULL) {
            _hashTable = new THashTable(_hashSize);
        }
        return _hashTable;
    }

    THashTable * clearHashTable() {
        if (_hashTable) {
            _hashTable->clear();
        }
    }

    TEngine * engine() {
        if (_engine == NULL) {
            InitMagicMoves();
            _engine = new TEngine();
            _engine->newGame(_fen);
        }
        return _engine;
    }

    TOutputHandler * outputHandler() {
        if (_outputHandler == NULL) {
            _outputHandler = new TOutputHandler();
        }
        return _outputHandler;
    }

};

#endif	/* INPUTHANDLER_H */

