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

#ifndef MOVEGEN_H
#define	MOVEGEN_H

#include "board.h"

namespace move {

    const int MAX_MOVES = 255;
    const int MAX_EXCLUDES = 7;
    const int EXCLUDED = -32001;
    const int ILLEGAL = -32002;
    const int INF = 32000;
    const int LEGAL = 31000;

    class list_t {
    public:
        move_t _list[MAX_MOVES + 1];
        int stage;
        int minimum_score;
        move_t * current;
        move_t * first;
        move_t * last;
        move_t * best;

        list_t();
        void clear();
    };

    void gen_quiet_moves(board_t * board, list_t * list);
    void gen_promotions(board_t * board, list_t * list);
    void gen_castles(board_t * board, list_t * list);
    void gen_captures(board_t * board, list_t * list, U64 targets);
}

#endif	/* MOVEGEN_H */

