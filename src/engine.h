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
 * File: engine.h
 * Implements the chess engine
 *
 * Created on 26 april 2011, 17:56
 */

#ifndef ENGINE_H
#define	ENGINE_H

#include <unistd.h>
#include <sys/time.h>
#include <threadsmanager.h>
#include "board.h"
#include "hashtable.h"
#include "move.h"
#include "outputhandler.h"
#include "evaluate.h"
#include "opponent.h"

class TInputHandler;
class book_t;
class TSearch;

struct TGameSettings {
    TOpponent opponent;
    int maxDepth;
    int maxNodes;
    move_t targetMove;
    int targetScore;
    U64 maxTimePerMove;
    int whiteTime;
    int blackTime;
    int whiteIncrement;
    int blackIncrement;
    int movesLeft;
    bool ponder;
    double learnFactor;

    void clear() {
        opponent.clear();
        maxDepth = MAX_PLY;
        maxTimePerMove = 0;
        targetScore = 0;
        whiteTime = 0;
        blackTime = 0;
        whiteIncrement = 0;
        blackIncrement = 0;
        movesLeft = 0;
        maxNodes = 0;
        ponder = false;
        learnFactor = 1.0;
    }
};

class TEngine : public TThreadsManager {
private:
    static void * _think(void * engineObjPtr);
    static void * _learn(void * engineObjPtr);
    std::string _rootFen;
    THashTable * _hashTable;
    TInputHandler * _inputHandler;
    TOutputHandler * _outputHandler;
    U64 _nodesSearched;
    bool _testSucces;
    move_t _resultMove;
    int _resultScore;
    volatile bool _engineStop;
    volatile bool _enginePonder;

public:
    TGameSettings gameSettings;
    void _create_start_positions(TSearch * root, book_t * book, std::string * pos, int &x, const int max);

    TEngine() : TThreadsManager() {
        gameSettings.clear();
        _nodesSearched = 0;
        _testSucces = false;
        _hashTable = NULL;
        _inputHandler = NULL;
        _outputHandler = NULL;
        _engineStop = false;
        _enginePonder = false;
        _rootFen = "";
        _resultMove.set(0);
        _resultScore = 0;
    }

    inline int CPUCount() {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    inline void think() {
        stop();
        _engineStop = false;
        if (_outputHandler) {
            _outputHandler->engineStop = false;
        }
        this->createThread(_think, this);
    }

    inline void learn() {
        _engineStop = false;
        if (_outputHandler) {
            _outputHandler->engineStop = false;
        }
        this->createThread(_learn, this);
    }

    inline void setPonder(bool doPonder) {
        _enginePonder = doPonder;
        if (_outputHandler) {
            _outputHandler->enginePonder = _enginePonder;
        }
    }

    inline void stop() {
        _engineStop = true;
        if (_outputHandler) {
            _outputHandler->engineStop = true;
        }
        this->stopAllThreads();
    }

    void testPosition(move_t bestMove, int score, int maxTime, int maxDepth);

    inline void newGame(std::string fen) {
        _resultMove.set(0);
        _resultScore = 0;
        setPosition(fen);
        clearHash();
    }

    inline void setOpponent(TOpponent * opponent) {
        gameSettings.opponent.copy(opponent);
    }

    inline void setPosition(std::string fen) {
        _rootFen = fen;
    }

    inline void setHashTable(THashTable * hashTable) {
        _hashTable = hashTable;
    }

    inline void setNodesSearched(U64 nodesSearched) {
        _nodesSearched = nodesSearched;
    }

    void setInputHandler(TInputHandler * inputHandler);

    void setOutputHandler(TOutputHandler * outputHandler);

    inline U64 getNodesSearched() {
        return _nodesSearched;
    }

    inline void setTestResult(bool testResult) {
        _testSucces = testResult;
    }

    inline void setMove(move_t * move) {
        _resultMove.set(move);
    }

    inline void setScore(int score) {
        _resultScore = score;
    }

    inline int getScore() {
        return _resultScore;
    }

    inline move_t getMove() {
        return _resultMove;
    }

    inline bool getTestResult() {
        return _testSucces;
    }

    inline void clearHash() {
        if (_hashTable) {
            _hashTable->clear();
        }
    }

    void analyse();


};

namespace engine {
    void stop();
    void go();
    void new_game(std::string fen);
    void set_position(std::string fen);
    void set_ponder(bool ponder);
    TGameSettings * game_settings();
}


#endif	/* ENGINE_H */

