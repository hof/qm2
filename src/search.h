/* 
 * File:   search.h
 * Author: Hajewiet
 *
 * Created on 3 mei 2011, 15:20
 */

#ifndef SEARCH_H
#define	SEARCH_H

#include "defs.h"
#include "evaluate.h"

class TSearchData;

enum SEARCH_CONSTANTS {
    NODESBETWEENPOLLS = 5000,
    ONE_PLY = 2,
    HALF_PLY = 1,
    LOW_DEPTH = ONE_PLY*3,
    HIGH_DEPTH = ONE_PLY*8
};

static const int FUTILITY_MARGIN[7] = {
    2*VPAWN, 
    VKNIGHT, VKNIGHT,
    VROOK, VROOK,
    VQUEEN, VQUEEN
};

/**
 * Null Move Reduction Depth (R)
 * @param depth current search depth left
 * @param plusScore eval - beta
 * @return R
 */
static inline int NullReduction(int depth, int plusScore) {
    int R = 2*ONE_PLY
            + HALF_PLY * (depth > 7 * ONE_PLY)
            + HALF_PLY * (depth > 14 * ONE_PLY);
    if (plusScore > VPAWN/2) {
        R += HALF_PLY;
        if (plusScore > VPAWN) {
            R += HALF_PLY;
        }
    }
    return R;
}

static inline int MateScore(int score) {
    return (score >= -SCORE_MATE && score <= -SCORE_MATE + MAX_PLY)
            || (score >= SCORE_MATE - MAX_PLY && score <= SCORE_MATE);
}

int pvs_root(TSearchData *searchData, int alpha, int beta, int depth);
int pvs(TSearchData *searchData, int alpha, int beta, int depth);
int asearch(TSearchData *searchData, int alpha, int beta, int depth);
int qsearch(TSearchData *searchData, int alpha, int beta, int qPly);
int extension(TBoard * pos, TMove * move, int stage, int depth, bool givesCheck);



#endif	/* SEARCH_H */

