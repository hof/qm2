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
class TSearch;

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
    THREATMOVE,
    QUIET_MOVES,
    Q_EVASIONS,
    Q_HASH1,
    Q_HASH2,
    Q_CAPTURES,
    Q_PROMOTIONS,
    Q_QUIET_CHECKS,
    STOP
};

#define MVVLVA(m) (PIECE_VALUE[m->capture] + PIECE_VALUE[m->promotion] - m->piece)

class TMovePicker {
protected:

    TMove * popBest(TBoard * pos, TMoveList * list);

public:

    TMove * pickNextMove(TSearch * searchData, int depth, int alpha, int beta);

    TMove * pickFirstMove(TSearch * searchData, int depth, int alpha, int beta);
    TMove * pickFirstQuiescenceMove(TSearch * searchData, int qCheckDepth, int alpha, int beta);
    short countEvasions(TSearch * sd, TMove * firstMove);
    
    void push(TSearch * searchData, TMove * move, int score);
};

#endif	/* MOVEPICKER_H */

