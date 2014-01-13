/* 
 * File:   evaluate.h
 * Author: Hajewiet
 *
 * Created on 13 mei 2011, 13:30
 */

#ifndef EVALUATE_H
#define	EVALUATE_H

#include "board.h"
#include <cstdlib>
#include <iostream>
#include "score.h"




class TSearch;

int evaluate(TSearch * searchData, int alpha, int beta);

void init_pst();
extern TSCORE_PST PST;

enum EVALUATION_CONSTANTS {
    MAX_PIECES = 16,
    MAX_CLOSED_POSITION = 32, //maximum value indicating how much closed the position is
    GRAIN_SIZE = 3, //powers of 2, used for rounding evaluation score down (and hopefully get more cutoffs)
    GRAIN = 0xFFFFFFFF & ~((1 << GRAIN_SIZE) - 1)
};

/*******************************************************************************
 * Generic evaluation bonuses
 *******************************************************************************/

const short VPAWN = 100;
const short VKNIGHT = 325;
const short VBISHOP = 325;
const short VROOK = 500;
const short VQUEEN = 925;
const short VKING = 20000;

const short PIECE_VALUE[13] = {
    0, VPAWN, VKNIGHT, VBISHOP, VROOK, VQUEEN, VKING,
    VPAWN, VKNIGHT, VBISHOP, VROOK, VQUEEN, VKING
};

/*******************************************************************************
 * Helper functions
 *******************************************************************************/

inline int cond(bool whiteValue, short value) {
    return whiteValue ? value : -value;
}

inline int cond(bool whiteCondition, bool blackCondition, short value) {
    if (whiteCondition != blackCondition) {
        return whiteCondition ? value : -value;
    }
    return 0;
}

inline int cond(bool whiteCondition, bool blackCondition, const short values[], unsigned char indexW, unsigned char indexB) {
    if (whiteCondition != blackCondition) {
        return whiteCondition ? values[indexW] : -values[indexB];
    }
    return 0;
}

#endif	/* EVALUATE_H */

