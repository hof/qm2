/* 
 * File:   movepicker.h
 * Author: Hajewiet
 *
 * Created on 11 mei 2011, 19:25
 */

#ifndef MOVEPICKER_H
#define	MOVEPICKER_H

#include <cstdlib>

#include "movegen.h"
#include "search.h"
class TSearchData;

enum MovePickingStage {
    HASH1,
    HASH2,
    IID,
    MATEKILLER,
    CAPTURES,
    PROMOTIONS,
    KILLER1,
    KILLER2,
    MINORPROMOTIONS,
    CASTLING,
    NON_CAPTURES,
    Q_EVASIONS,
    Q_CAPTURES,
    Q_PROMOTIONS,
    Q_QUIET_CHECKS,
    STOP
};

#define MVVLVA(m) (PIECE_VALUE[m->capture]+PIECE_VALUE[m->promotion]-m->piece)

class TMovePicker {
protected:

    TMove * popBest(TBoard * pos, TMoveList * list);

public:

    TMove * pickNextMove(TSearchData * searchData, int depth, int alpha, int beta, int gap);

    TMove * pickFirstMove(TSearchData * searchData, int depth, int alpha, int beta, int gap);
    TMove * pickFirstQuiescenceMove(TSearchData * searchData, int qPly, int alpha, int beta, int gap);
    
    void push(TSearchData * searchData, TMove * move, int score);
    
};

#endif	/* MOVEPICKER_H */

