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
 * File: movegen.cpp
 * Move generators
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

void genQuietMoves(TBoard * board, TMoveList * list);
void genPromotions(TBoard * board, TMoveList * list);
void genCastles(TBoard * board, TMoveList * list);
void genCaptures(TBoard * board, TMoveList * list, U64 targets);

#endif	/* W0MOVEGEN_H */

