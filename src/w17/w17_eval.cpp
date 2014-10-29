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
 * File:  w17_eval.cpp
 * W17 evaluation
 *
 * Created on 23 oct 2014, 11:26
 */

#include "w17_search.h"

int w17_eval_material(w17_search_t * s);

int w17_evaluate(w17_search_t * s) {
    const bool us = s->brd.stack->wtm;
    int result = w17_eval_material(s);
    return us ? result : -result;
}

int w17_eval_material(w17_search_t * s) {

    /*
     * 1. Probe the material table
     */

    board_t * brd = &s->brd;
    int result, phase, flags;
    if (material_table::retrieve(brd->stack->material_hash, result, phase, flags)) {
        s->stack->phase = phase;
        s->stack->material_flags = flags;
        return result;
    }

    /*
     * 2. Calculate game phase and flags
     */

    int wpawns = brd->count(WPAWN);
    int bpawns = brd->count(BPAWN);
    int wknights = brd->count(WKNIGHT);
    int bknights = brd->count(BKNIGHT);
    int wbishops = brd->count(WBISHOP);
    int bbishops = brd->count(BBISHOP);
    int wrooks = brd->count(WROOK);
    int brooks = brd->count(BROOK);
    int wqueens = brd->count(WQUEEN);
    int bqueens = brd->count(BQUEEN);
    int wpieces = wknights + wbishops + wrooks + wqueens;
    int bpieces = bknights + bbishops + brooks + bqueens;
    phase = MAX(0, score::MAX_PHASE - wpieces - bpieces - wqueens - bqueens);
    flags = 1 * (wpawns > 0) + 2 * (bpawns > 0) + 4 * (wpieces > 0) + 8 * (bpieces > 0);
    
    /*
     * 3. Calculate material score based on game flags 
     */

    switch (flags) {
        case 0: //only kings left on the board
            assert(false);
            result = score::WIN;
            break;
        case 1: //only wpawns
            assert(false);
            result = -score::WIN;
            break;
        case 2: //only bpawns
            assert(false);
            result = score::WIN;
            break;
        case 3: //wpawns + bpawns
            result = 100 * (wpawns - bpawns) + bpawns - wpawns;
            break;
        case 4: //only wpieces
        case 5: //wpawns + wpieces
            assert(false);
            result = -score::WIN;
            break;
        case 6: //bpawns + wpieces
            result = score::WIN - wpieces * 100;
            break;
        case 7: //wpawns + bpawns + wpieces
            result = score::WIN / 2 + 100 * (bpawns + wpieces - wpawns);
            break;
        case 8: //only bpieces
            assert(false);
            result = -score::WIN;
            break;
        case 9: //wpawns + bpieces
            result = -score::WIN + bpieces * 100;
            break;
        case 10: //bpawns + bpieces
            assert(false);
            result = -score::WIN;
            break;
        case 11: //wpawns + bpawns + bpieces
            result = -score::WIN / 2 - 100 * (wpawns + bpieces - bpawns);
            break;
        case 12: //wpieces + bpieces
            result = (wpieces - bpieces) * 100;
            break;
        case 13: //wpawns + wpieces + bpieces
            result = -score::WIN + (bpieces * 100);
            break;
        case 14: //bpawns + wpieces + bpieces
            result = score::WIN - (wpieces * 100);
            break;
        case 15: //all
            result = 0;
            if (wpawns != bpawns) {
                result = 100 * (wpawns - bpawns);
                result += (wpawns > bpawns) ? bpawns : wpawns;
            }
            if (wpieces != bpieces) {
                result += 250 * (wpieces - bpieces);
                result += (wpieces > bpieces) ? bpieces : wpieces;
            }
            break;
        default:
            assert(false);
            result = 0;
            break;
    }

    /*
     * 5. Store results in material table and on the stack
     */
    
    s->stack->phase = phase;
    s->stack->material_flags = flags;
    material_table::store(brd->stack->material_hash, result, phase, flags);
    return result;
}
