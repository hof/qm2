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
 * File: movepicker.h
 * Move Picker class to use by the search. The move picker handles move 
 * generation in phases, typically following this order:
 * - Hash moves
 * - Killer Moves
 * - Captures
 * - Quiet Moves
 * All moves returned are legal. If the move picker does not return any move, 
 * it's a (stale)mate.
 *
 * Created on 11 mei 2011, 19:25
 */

#ifndef MOVEPICKER_H
#define	MOVEPICKER_H

#include <cstdlib>

#include "movegen.h"
class TSearch;

enum MovePickingStage {
    MATEKILLER,
    CAPTURES,
    PROMOTIONS,
    KILLER1,
    KILLER2,
    MINORPROMOTIONS,
    CASTLING,
    QUIET_MOVES,
    STOP
};

#define MVVLVA(m) (PIECE_VALUE[m->capture] + PIECE_VALUE[m->promotion] - m->piece)

class TMovePicker {
protected:

    move_t * popBest(board_t * pos, move::list_t * list);

public:

    move_t * pickNextMove(TSearch * searchData, int depth, int alpha, int beta);
    move_t * pickFirstMove(TSearch * searchData, int depth, int alpha, int beta);
    short countEvasions(TSearch * sd, move_t * firstMove);
    
    void push(TSearch * searchData, move_t * move, int score);
};

#endif	/* MOVEPICKER_H */

