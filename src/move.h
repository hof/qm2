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
 * File: move.h
 * Move representation structure
 *
 * Created on 10 april 2011, 12:48
 */

#ifndef MOVE_H
#define	MOVE_H

#include "bits.h"

struct board_t;

class move_t {
public:
    uint8_t piece;
    uint8_t ssq;
    uint8_t tsq;
    uint8_t capture;
    uint8_t promotion;
    uint8_t castle;
    bool en_passant;
    uint8_t special;
    int score;

    void set(move_t * move);
    void set(int move);
    void set(int pc, int from, int to, int captured_piece, int promotion_piece);
    void set(board_t * pos, const char * move_str);
    std::string to_string();
    int to_int();

    /**
     * Sets a quiet move
     * @param pc piece
     * @param from source square
     * @param to target square
     */
    void set(int pc, int from, int to) {
        set(pc, from, to, 0, 0);
    }

    /**
     * Sets a simple capture move
     * @param pc piece
     * @param from from square
     * @param to target square
     * @param captured_piece captured piece 
     */
    void set(int pc, int from, int to, int captured_piece) {
        set(pc, from, to, captured_piece, 0);
    }

    /**
     * Tests if two moves are equal
     * @param move move to test
     * @return true if equal, false otherwise
     */
    bool equals(move_t * move) {
        return ssq == move->ssq
                && tsq == move->tsq
                && piece == move->piece
                && promotion == move->promotion;
    }

    /**
     * Empties a move 
     */
    void clear() {
        piece = 0;
    }

};

#endif	/* MOVE_H */

