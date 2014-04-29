/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2014 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 *  
 * File: inputhandler.h
 * Handling text input from the console following the UCI protocol by Stefan-Meyer Kahlen.
 * See http://wbec-ridderkerk.nl/html/UCIProtocol.html
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
#include "opponent.h"

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
    TOpponent _opponent;
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
        _opponent.clear();
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
            _hashSize = _hashSizeRequest;
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

