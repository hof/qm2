/* 
 * File:   movegen.h
 * Author: Hajewiet
 *
 * Created on 10 april 2011, 22:46
 */

#ifndef W0MOVEGEN_H
#define	W0MOVEGEN_H

#include "move.h"
#include "board.h"

#define	MAX_MOVECHOICES 256
#define MAX_EXCLUDECHOICES 8

#define MOVE_EXCLUDED -32001
#define MOVE_ILLEGAL -32002
#define MOVE_INFINITY 32000
#define MOVE_LEGAL    31000

class TMoveList {
public:
    TMove _list[MAX_MOVECHOICES + 1];
    TMove _exclude[MAX_EXCLUDECHOICES + 1];
    int stage;
    int minimumScore;
    TMove * current;
    TMove * first;
    TMove * last;
    TMove * currentX;
    TMove * lastX;
    TMove * firstX;
    TMove * pop;

    inline void clear() {
        stage = 0;
        minimumScore = 0;
        current = first = last = &_list[0];
        currentX = firstX = lastX = &_exclude[0];
    }

    TMoveList() {
        clear();
        pop = &_list[MAX_MOVECHOICES];
    }

    inline bool excluded(TMove * move) {
        for (TMove * curX = firstX; curX != lastX; curX++) {
            if (move->equals(curX)) {
                return true;
            }
        }
        return false;
    }
    void copy(TMoveList * list);
};

void genMoves(TBoard * board, TMoveList * list);
void genPromotions(TBoard * board, TMoveList * list);
void genCastles(TBoard * board, TMoveList * list);
void genCaptures(TBoard * board, TMoveList * list, U64 targets);

void genQuietChecks(TBoard * board, TMoveList * list);
void genEvasions (TBoard * board, TMoveList * list);

#endif	/* W0MOVEGEN_H */

