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
 * Neural evaluation and search metadata for standard chess
 */

#include "eval.h"
#include "search.h"
#include "progress.h"

search_metadata_t search_metadata(const board_t & board) {
    search_metadata_t result{};
    const U64 white = board.bb[WPAWN];
    const U64 black = board.bb[BPAWN];
    const U64 white_files = white | ((white & ~FILE_A) >> 1) | ((white & ~FILE_H) << 1);
    const U64 black_files = black | ((black & ~FILE_A) >> 1) | ((black & ~FILE_H) << 1);
    result.passed_pawns = (white & ~(fill_south(white | black_files) >> 8))
        | (black & ~(fill_north(black | white_files) << 8));
    result.endgame = max_1(white) || max_1(black);
    if (!result.endgame) {
        for (bool us : {false, true}) {
            if (board.bb[ROOK[us]] || board.bb[QUEEN[us]]) {
                continue;
            }
            const int minors = popcnt0(board.bb[KNIGHT[us]] | board.bb[BISHOP[us]]);
            if (minors < 2 || (minors == 2 && !board.bb[BISHOP[us]])) {
                result.endgame = true;
                break;
            }
        }
    }
    return result;
}

int evaluate(search_t * search) {
    if (search->stack->in_check && search->brd.ply < (MAX_PLY - 1)) {
        search->stack->eval_result = score::INVALID;
        return search->stack->eval_result;
    }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    if (search->nnue_audit.mode == nnue::audit_mode_t::off) {
        ++search->nnue_audit.counters.calls;
        if (search->stack->eval_result != score::INVALID) {
            ++search->nnue_audit.counters.cached_returns;
        }
    } else {
        const int result = nnue::audit_evaluate(*search);
        if (search->stack->eval_result == score::INVALID) {
            search->stack->metadata = search_metadata(search->brd);
            search->stack->eval_result = result;
        }
        return search->corrected_static_eval(result);
    }
#endif
    if (search->stack->eval_result != score::INVALID) {
        return search->corrected_static_eval(search->stack->eval_result);
    }
    assert(search->evaluation_network);
    search->stack->metadata = search_metadata(search->brd);
    const int result = damp_rule50(search->neural_static_score(), search->brd.stack->fifty_count);
    search->stack->eval_result = result;
    return search->corrected_static_eval(result);
}
