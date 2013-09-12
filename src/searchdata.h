/* 
 * File:   searchdata.h
 * Author: Hajewiet
 *
 * Created on 13 mei 2011, 14:08
 */

#ifndef SEARCHDATA_H
#define	SEARCHDATA_H

#include "board.h"
#include "move.h"
#include "movegen.h"
#include "movepicker.h"
#include "hashtable.h"
#include "outputhandler.h"
#include "search.h"
#include "timemanager.h"
#include "evaluate.h"
#include <string.h>
#include <cstdlib>
#include <iostream>

enum NodeType {
    UNKNOWN = 0,
    PVNODE = 1,
    CUTNODE = 2,
    ALLNODE = 3
};

enum SearchResultType {
    EXACT = 0,
    FAILLOW = 1,
    FAILHIGH = 2
};

#define HISTORY_MAX 5000

class TRootMove {
public:
    TMove Move;
    bool GivesCheck;
    bool Active;
    int Nodes;
    int PV;
    int Value;
    int InitialValue;
    int SEE;
    int checkerSq;
    U64 checkers;

    void init(TMove * move, int initialValue, bool givesCheck, bool active, int see) {
        Nodes = 0;
        PV = 0;
        Value = -SCORE_INFINITE;
        InitialValue = initialValue;
        Move.setMove(move);
        GivesCheck = givesCheck;
        Active = active;
        SEE = see;
    }

    int compare(TRootMove * rMove) {
        int result = PV - rMove->PV;
        if (result) {
            return result;
        }
        result = Value - rMove->Value;
        if (result) {
            return result;
        }
        result = Nodes - rMove->Nodes;
        if (result) {
            return result;
        }
        result = InitialValue - rMove->InitialValue;
        if (result) {
            return result;
        }
        result = SEE - rMove->SEE;
        return result;
    }
};

class TRoot {
public:
    TRootMove Moves[MAX_MOVECHOICES + 1];
    int MoveCount;
    int FiftyCount;
    bool InCheck;
    void sortMoves();
};

struct TSearchStack {
    TMoveList moveList;
    NodeType nodeType;
    TMove move;
    TMove bestMove;
    
    U64 hashCode;
    bool inCheck;

    TMove mateKiller;
    TMove killer1;
    TMove killer2;

    int pvCount;
    TMove pvMoves[MAX_PLY + 1];
    
    int ttScore;
    TMove ttMove1;
    TMove ttMove2;

    int gamePhase;
    int evaluationScore;
    int scores[MAX_EVALUATION_COMPONENTS];

    int reduce;

    U64 captureMask;

};

class TSearchData {
protected:
    TSearchStack _stack[MAX_PLY + 1];
public:
    TBoard * pos;
    TSearchStack * stack;
    TSearchStack * rootStack;

    U64 nodes;
    U64 maxNodes;
    U64 hashProbes;
    U64 hashHits;
    U64 evaluations;
    U64 fullEvaluations;
    U64 pawnEvals;
    U64 materialTableHits;
    U64 materialTableProbes;
    U64 pawnTableHits;
    U64 pawnTableProbes;

    bool ponder;
    bool stopSearch;
    int nodesUntilPoll;
    bool skipNull;
    TMove excludedMove;
    int selDepth;
    int learnParam;
    int evaluationComponents;
    double learnFactor;
    TRoot root;


    TMovePicker * movePicker;
    THashTable * hashTable;
    TOutputHandler * outputHandler;
    TTimeManager * timeManager;
    int history[BKING + 1][64];

    TMoveList tempList;

    TSearchData(const char * fen,
            const short pieceSquareTables[WKING][2][64],
            THashTable * globalHashTable,
            TOutputHandler * outputH) {
        pos = new TBoard();
        pos->setPieceSquareTables(pieceSquareTables);
        pos->fromFen(fen);
        memset(history, 0, sizeof (history));
        hashTable = globalHashTable;
        outputHandler = outputH;
        movePicker = new TMovePicker();
        timeManager = new TTimeManager();
        nodes = 0;
        hashProbes = 0;
        hashHits = 0;
        evaluations = 0;
        fullEvaluations = 0;
        pawnEvals = 0;
        materialTableHits = 0;
        materialTableProbes = 0;
        pawnTableHits = 0;
        pawnTableProbes = 0;
        learnParam = 0;
        stopSearch = false;
        ponder = false;
        skipNull = false;
        excludedMove.clear();
        nodesUntilPoll = NODESBETWEENPOLLS;
        maxNodes = 0;
        selDepth = 0;
        learnFactor = 1.0;
        evaluationComponents = MAX_EVALUATION_COMPONENTS;
        rootStack = stack = &_stack[0];
        stack->evaluationScore = SCORE_UNKNOWN;
        stack->nodeType = PVNODE;
#ifdef PRINTSEARCH
        examineSearch = 0;
#endif
        assert(hashTable);
    }

    ~TSearchData() {
        delete pos;
        delete movePicker;
        delete timeManager;
    }

    inline void updateKillers(TMove * move) {
        if (!stack->killer1.equals(move)) {
            stack->killer2.setMove(&stack->killer1);
            stack->killer1.setMove(move);
        }
    }

    inline void updatePV(TMove * move) {
        stack->pvMoves[0].setMove(move);
        memcpy(stack->pvMoves + 1, (stack + 1)->pvMoves, (stack + 1)->pvCount * sizeof (TMove));
        stack->pvCount = (stack + 1)->pvCount + 1;
    }

    

    inline TSearchStack * getStack(int ply) {
        assert(ply >= 0 && ply <= MAX_PLY);
        return &_stack[ply];
    }

    inline void forward() { //null move
        skipNull = true;
        stack->move.setMove(0);
        stack++;
        stack->inCheck = false;
        stack->evaluationScore = (stack-1)->evaluationScore;
        stack->gamePhase = (stack - 1)->gamePhase;
        for (int x = 0; x < evaluationComponents; x++) {
            stack->scores[x] = (stack-1)->scores[x];
        }
        pos->forward();
        assert(stack == &_stack[pos->currentPly]);
    }

    inline void backward() { //null move
        stack--;
        pos->backward();
        skipNull = false;
        assert(stack == &_stack[pos->currentPly]);
    }

    inline void forward(TMove * move, bool givesCheck) {
        stack->move.setMove(move);
        stack++;
        stack->inCheck = givesCheck;
        stack->evaluationScore = SCORE_UNKNOWN;
        pos->forward(move);
        assert(stack->gamePhase >= 0 && stack->gamePhase <= 16);
        assert(stack == &_stack[pos->currentPly]);
    }

    inline void backward(TMove * move) {
        stack--;
        pos->backward(move);
        assert(stack == &_stack[pos->currentPly]);
    }

    inline NodeType setNodeType(int alpha, int beta) {
        NodeType type = PVNODE;
        if (alpha + 1 == beta) {
            assert(stack != rootStack);
            type = (stack - 1)->nodeType == CUTNODE ? ALLNODE : CUTNODE;
        }
        stack->nodeType = type;
        return type;
    }

    inline void updateHistoryScore(TMove * move, int depth) {
        int pc = move->piece;
        int tsq = move->tsq;
        depth = depth / ONE_PLY;
        history[pc][tsq] += depth * ABS(depth);
        if (ABS(history[pc][tsq]) > HISTORY_MAX) {
            for (int pc = WPAWN; pc <= BKING; pc++) {
                for (int sq = a1; sq <= h8; sq++) {
                    history[pc][sq] >>= 1;
                }
            }
        }
    }

    /*
     * Helper function to reset the search stack. 
     * This is used for self-playing games, where  
     * MAX_PLY is not sufficient to hold a chess game. 
     * Note: in UCI mode this is not an issue, because each position
     * starts with a new stack. 
     */
    inline void resetStack() {
        pos->currentPly = 0;
        stack = rootStack;
        stack->evaluationScore = SCORE_UNKNOWN;
        pos->_boardFlags[0].copy(this->pos->boardFlags);
        pos->boardFlags = &this->pos->_boardFlags[0];
        nodes = 0;
        stack->pvCount = 0;
    }

    std::string getPVString();
    void poll();
    void printMovePath();
    int initRootMoves();
    void debugPrint();
};


#endif	/* SEARCHDATA_H */

