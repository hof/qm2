/* 
 * File:   engine.h
 * Author: Hajewiet
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

class TInputHandler;

struct TGameSettings {
    int maxDepth;
    int maxNodes;
    TMove targetMove;
    int targetScore;
    U64 maxTimePerMove;
    int whiteTime;
    int blackTime;
    int whiteIncrement;
    int blackIncrement;
    int movesLeft;
    bool ponder;
    int learnParam;
    double learnFactor;


    void clear() {
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
#ifdef PRINTSEARCH
        examineSearch = false;
#endif
        learnParam = 0;
        learnFactor = 1.0;
    }
};

class TEngine : public TThreadsManager {
private:
    static void * _think(void * engineObjPtr);
    string _rootFen;
    THashTable * _hashTable;
    TInputHandler * _inputHandler;
    TOutputHandler * _outputHandler;
    U64 _nodesSearched;
    bool _testSucces;
    TMove _resultMove;
    int _resultScore;
    volatile bool _engineStop;
    volatile bool _enginePonder;

public:
    TGameSettings gameSettings;

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
        _resultMove.setMove(0);
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

    void testPosition(TMove bestMove, int score, int maxTime, int maxDepth);

    inline void newGame(string fen) {
        _resultMove.setMove(0);
        _resultScore = 0;
        setPosition(fen);
        clearHash();
    }

    inline void setPosition(string fen) {
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

    inline void setMove(TMove * move) {
        _resultMove.setMove(move);
    }

    inline void setScore(int score) {
        _resultScore = score;
    }

    inline int getScore() {
        return _resultScore;
    }

    inline TMove getMove() {
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

#endif	/* ENGINE_H */

