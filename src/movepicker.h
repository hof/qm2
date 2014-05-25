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
    short countEvasions(TSearch * sd, TMove * firstMove);
    
    void push(TSearch * searchData, TMove * move, int score);
};

#endif	/* MOVEPICKER_H */

