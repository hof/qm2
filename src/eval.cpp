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
 * Neural evaluation for standard chess
 */

#include "eval.h"
#include "search.h"
#include "progress.h"

int evaluate(search_t * search) {
    if (search->stack->in_check && search->brd.ply < (MAX_PLY - 1)) {
        search->stack->eval_result = score::INVALID;
        return search->stack->eval_result;
    }
    if (search->stack->eval_result != score::INVALID) {
        return search->corrected_static_eval(search->stack->eval_result);
    }
    assert(search->evaluation_network);
    const int result = damp_rule50(search->neural_static_score(), search->brd.stack->fifty_count);
    search->stack->eval_result = result;
    return search->corrected_static_eval(result);
}
