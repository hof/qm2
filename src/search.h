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
 * File:  search.h
 * Search methods for traditional chess. 
 * Maxima uses a fail-soft principle variation search (PVS) with
 * - null move pruning 
 * - futility pruning
 * - late move reductions (LMR) and late move pruning (LMP)
 *
 * Created on 13 mei 2011, 14:08
 */

#ifndef SEARCH_H
#define	SEARCH_H

#include "defs.h"
#include "evaluate.h"
#include "board.h"
#include "move.h"
#include "movegen.h"
#include "movepicker.h"
#include "hashtable.h"
#include "outputhandler.h"
#include "timemanager.h"
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

enum SEARCH_CONSTANTS {
    NODESBETWEENPOLLS = 5000,
    ONE_PLY = 2,
    HALF_PLY = 1,
    LOW_DEPTH = ONE_PLY * 3,
    MEDIUM_DEPTH = ONE_PLY * 5,
    HIGH_DEPTH = ONE_PLY * 8,
    VERY_HIGH_DEPTH = ONE_PLY * 16
};

#define HISTORY_MAX 5000

#define NOTPV(a,b) (((a) + 1) >= (b))
#define ISPV(a,b) (((a) + 1) < (b))

class TRootMove {
public:
    TMove Move;
    bool GivesCheck;
    int Nodes;
    int PV;
    int Value;
    int InitialValue;
    int SEE;
    int checker_sq;
    U64 checkers;

    void init(TMove * move, int initialValue, bool givesCheck, int see) {
        Nodes = 0;
        PV = 0;
        Value = -SCORE_INFINITE;
        InitialValue = initialValue;
        Move.setMove(move);
        GivesCheck = givesCheck;
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
    void matchMoves(TMoveList * list);
};

struct TSearchStack {
    TMoveList moveList;
    NodeType nodeType;
    TMove move;
    TMove bestMove;

    U64 hash_code;
    bool inCheck;

    TMove mateKiller;
    TMove killer1;
    TMove killer2;

    int pvCount;
    TMove pvMoves[MAX_PLY + 1];

    int ttScore;
    int ttDepth1;
    int ttDepth2;
    TMove ttMove1;
    TMove ttMove2;

    //eval info
    short phase;
    bool equal_pawns; //false if a pawn or king has moved
    short eval_result;
    short eg_score;
    TScore eval_score;
    short material_score;
    uint8_t material_flags;
    uint8_t pawn_flags;
    TScore pawn_score;
    TScore knight_score[2];
    TScore bishop_score[2];
    TScore rook_score[2];
    TScore queen_score[2];
    TScore king_score[2];
    TScore passer_score[2];
    
    U64 passers;
    U64 mob[2];
    U64 attack[2];
    U64 king_attack_zone[2];
    int8_t king_attack[BKING+1];
    U64 captureMask;
};

class TSearch {
protected:
    TSearchStack _stack[MAX_PLY + 1];
public:
    TBoard * pos;
    TSearchStack * stack;
    TSearchStack * rootStack;

    U64 nodes;
    U64 pruned_nodes;
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
    U64 evalTableHits;
    U64 evalTableProbes;

    bool ponder;
    bool stopSearch;
    int nodesUntilPoll;
    bool skipNull;
    int selDepth;
    double learnFactor;
    TRoot root;
    
    TScore drawContempt;

    TMovePicker * movePicker;
    THashTable * hashTable;
    TOutputHandler * outputHandler;
    TTimeManager * timeManager;
    int history[BKING + 1][64];
    
    short LMR[32][64]; //depth,  move number

    TMoveList tempList;

    TSearch(const char * fen,       
            THashTable * globalHashTable,
            TOutputHandler * outputH) {
        pos = new TBoard();
        pos->fromFen(fen);
        memset(history, 0, sizeof (history));
        InitPST();
        hashTable = globalHashTable;
        outputHandler = outputH;
        movePicker = new TMovePicker();
        timeManager = new TTimeManager();
        nodes = 0;
        pruned_nodes = 0;
        hashProbes = 0;
        hashHits = 0;
        evaluations = 0;
        fullEvaluations = 0;
        drawContempt = -50;
        pawnEvals = 0;
        materialTableHits = 0;
        materialTableProbes = 0;
        pawnTableHits = 0;
        pawnTableProbes = 0;
        evalTableHits = 0;
        pawnTableHits = 0;
        learnFactor = 1.0;
        stopSearch = false;
        ponder = false;
        skipNull = false;
        nodesUntilPoll = NODESBETWEENPOLLS;
        maxNodes = 0;
        selDepth = 0;
        learnFactor = 1.0;
        rootStack = stack = &_stack[0];
        stack->eval_result = SCORE_INVALID;
        stack->nodeType = PVNODE;
        assert(hashTable);
    }

    ~TSearch() {
        delete pos;
        delete movePicker;
        delete timeManager;
    }

    inline void clearHistory() {
        memset(history, 0, sizeof (history));
        
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
        stack->eval_result = SCORE_INVALID;
        pos->forward();
        assert(stack == &_stack[pos->current_ply]);
    }

    inline void backward() { //null move
        stack--;
        pos->backward();
        skipNull = false;
        assert(stack == &_stack[pos->current_ply]);
    }

    inline void forward(TMove * move, bool givesCheck) {
        stack->move.setMove(move);
        stack++;
        stack->inCheck = givesCheck;
        stack->eval_result = SCORE_INVALID;
        pos->forward(move);
        assert(stack == &_stack[pos->current_ply]);
    }

    inline void backward(TMove * move) {
        stack--;
        pos->backward(move);
        assert(stack == &_stack[pos->current_ply]);
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
        pos->current_ply = 0;
        stack = rootStack;
        stack->eval_result = SCORE_INVALID;
        pos->_stack[0].copy(this->pos->stack);
        pos->stack = &this->pos->_stack[0];
        nodes = 0;
        pruned_nodes = 0;
        stack->pvCount = 0;
    }

    std::string getPVString();
    void poll();
    bool pondering();
    void printMovePath();
    int initRootMoves();

    inline int MateScore(int score) {
        return (score >= -SCORE_MATE && score <= -SCORE_MATE + MAX_PLY)
                || (score >= SCORE_MATE - MAX_PLY && score <= SCORE_MATE);
    }
    int pvs_root(int alpha, int beta, int depth);
    int pvs(int alpha, int beta, int depth);
    int qsearch(int alpha, int beta, int depth);
    
    inline int drawScore() {
        return 0;;
    }
    
    int extendMove(TMove * move, int gives_check);
    
    inline bool passedPawn(TMove * move) {
        return BIT(move->ssq) & stack->passers;
    }

    void debug_print_search(int alpha, int beta);
};






#endif	/* SEARCH_H */

