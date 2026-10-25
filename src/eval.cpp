/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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
 * Neural evaluation for standard chess
 */

#include "search.h"
#include "nnue.h"
#include <algorithm>

// The network's own scale: material, tempo and shuffle damping follow the
// NNUE path of the evaluation that trained alongside this network.
int scale_network_score(int raw, const board_t & board, int halfmoves) {
    constexpr int pawn = 126, knight = 781, bishop = 825, rook = 1276, queen = 2538, tempo = 28;
    const int clock = std::clamp(halfmoves, 0, 100);
    const int material = 2 * pawn * popcnt0(board.bb[WPAWN] | board.bb[BPAWN])
        + knight * popcnt0(board.bb[WKNIGHT] | board.bb[BKNIGHT])
        + bishop * popcnt0(board.bb[WBISHOP] | board.bb[BBISHOP])
        + rook * popcnt0(board.bb[WROOK] | board.bb[BROOK])
        + queen * popcnt0(board.bb[WQUEEN] | board.bb[BQUEEN]);
    const int value = raw * (641 + material / 32 - 4 * clock) / 1024 + tempo;
    return nnue::to_centipawns(value * (100 - clock) / 100);
}

int evaluate(search_t * search) {
    if (search->stack->in_check && search->brd.ply < (MAX_PLY - 1)) {
        search->stack->eval_result = score::INVALID;
        return search->stack->eval_result;
    }
    if (search->stack->eval_result != score::INVALID) {
        return search->corrected_static_eval(search->stack->eval_result);
    }
    assert(search->evaluation_network);
    const int result = scale_network_score(search->neural_static_score(), search->brd,
        search->brd.stack->fifty_count);
    search->stack->eval_result = result;
    return search->corrected_static_eval(result);
}
