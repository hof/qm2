/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
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
 * File: evaluate.cpp
 * Exact KPK bitbase adapter
 */

#include "eval_endgame.h"
#include "eval_kpk_bb.h"

namespace eg {
    int probe_kpk(board_t & board) {
        const U64 pawns = board.bb[WPAWN] | board.bb[BPAWN];
        if (board.has_pieces() || !is_1(pawns)) {
            return -1;
        }
        const bool us = board.bb[WPAWN] != 0;
        return KPK::probe(board.us() == us, board.get_sq(KING[us]),
            board.get_sq(KING[!us]), board.get_sq(PAWN[us]), us == BLACK);
    }

}
