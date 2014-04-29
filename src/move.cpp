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
 * File: move.cpp
 * Move representation structure
 */

#include "move.h"
#include "defs.h"
#include "board.h"

string TMove::asString() { //default: xboard notation
    string result = "";
    int ssq = this->ssq;
    int tsq = this->tsq;
    if (ssq == tsq) {
        result = "null";
    } else {
        result = FILE_SYMBOL(ssq);
        result += RANK_SYMBOL(ssq);
        result += FILE_SYMBOL(tsq);
        result += RANK_SYMBOL(tsq);
        if (this->promotion) {
            const char PIECE_SYMBOL[13] = {'.', 'p', 'n', 'b', 'r', 'q', 'k', 'p', 'n', 'b', 'r', 'q', 'k'};
            result += PIECE_SYMBOL[this->promotion];
        }
    }
    return result;
}

void TMove::fromString(TBoard * pos, const char * moveStr) { //default: xboard notation
    int file = *moveStr++ -'a';
    int rank = *moveStr++ -'1';
    ssq = (rank << 3) | file;
    file = *moveStr++ -'a';
    rank = *moveStr++ -'1';
    tsq = (rank << 3) | file;
    piece = pos->matrix[ssq];
    assert(piece);
    capture = pos->matrix[tsq];
    castle = 0;
    en_passant = false;
    if (tsq == pos->stack->enpassant_sq 
            && (piece == WPAWN || piece == BPAWN)
            && pos->stack->enpassant_sq) {
        en_passant = true;
        capture = piece == WPAWN ? BPAWN : WPAWN;
    } else if (piece == WKING && ssq == e1) {
        if (tsq == g1) {
            castle = CASTLE_K;
        } else if (tsq == c1) {
            castle = CASTLE_Q;
        } 
    } else if (piece == BKING && ssq == e8) {
        if (tsq == g8) {
            castle = CASTLE_k;
        } else if (tsq == c8) {
            castle = CASTLE_q;
        }
    }
    promotion = 0;
    if (*moveStr) {
        switch (*moveStr) {
            case 'n':
                promotion = WKNIGHT;
                break;
            case 'b':
                promotion = WBISHOP;
                break;
            case 'r':
                promotion = WROOK;
                break;
            default:
                promotion = WQUEEN;
                break;
        }
        if (pos->stack->wtm == false) {
            promotion += WKING;
        }
    }
}
