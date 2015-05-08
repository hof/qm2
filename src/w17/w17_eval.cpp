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
 * File:  w17_eval.cpp
 * W17 evaluation
 *
 * Created on 23 oct 2014, 11:26
 */

#include "w17_search.h"
#include "score.h"

int w17_eval_material(w17_search_t * s);
score_t * w17_eval_pawns_and_kings(search_t * s);

int w17_evaluate(w17_search_t * s) {
    const bool us = s->brd.us();
    int result = w17_eval_material(s);
    if (s->stack->mt->flags == 16) {
        score_t * score = &s->stack->eval_score;
        score->set(w17_eval_pawns_and_kings(s));
        result += score->get(s->stack->mt->phase);
    }
    return us ? result : -result;
}

int w17_eval_material(w17_search_t * s) {

    /*
     * 1. Probe the material table
     */

    board_t * brd = &s->brd;
    s->stack->mt = material_table::retrieve(brd->stack->material_hash);
    if (s->stack->mt->key == brd->stack->material_hash) {
        return s->stack->mt->score;
    }

    /*
     * 2. Calculate game phase and flags
     */

    int result = 0;
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
    int phase = MAX(0, score::MAX_PHASE - wpieces - bpieces - wqueens - bqueens);
    int flags = 1 * (wpawns > 0) + 2 * (bpawns > 0) + 4 * (wpieces > 0) + 8 * (bpieces > 0);

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

    s->stack->mt->key = brd->stack->material_hash;
    s->stack->mt->phase = phase;
    s->stack->mt->flags = flags;
    s->stack->mt->score = result;
    return result;
}

const score_t PST_KING[64] = {
    S(-90, 0), S(-90, 5), S(-90, 10), S(-90, 15), S(-90, 15), S(-90, 10), S(-90, 5), S(-90, 0),
    S(-90, 5), S(-90, 10), S(-90, 15), S(-90, 20), S(-90, 20), S(-90, 15), S(-90, 10), S(-90, 5),
    S(-80, 10), S(-80, 15), S(-80, 20), S(-80, 25), S(-80, 25), S(-80, 20), S(-80, 15), S(-80, 10),
    S(-60, 15), S(-60, 20), S(-60, 25), S(-60, 30), S(-60, 30), S(-60, 25), S(-60, 20), S(-60, 15),
    S(-40, 15), S(-40, 20), S(-40, 25), S(-40, 30), S(-40, 30), S(-40, 25), S(-40, 20), S(-40, 15),
    S(-20, 10), S(-20, 15), S(-20, 20), S(-20, 25), S(-20, 25), S(-20, 20), S(-20, 15), S(-20, 10),
    S(0, 5), S(0, 10), S(0, 15), S(0, 20), S(0, 20), S(0, 15), S(0, 10), S(0, 5),
    S(10, 0), S(10, 5), S(10, 10), S(10, 15), S(10, 15), S(10, 10), S(10, 5), S(10, 0)
};

const score_t PST_PAWN[64] = {
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(80, 60), S(80, 60), S(60, 60), S(60, 60), S(60, 60), S(60, 60), S(80, 60), S(80, 60),
    S(40, 30), S(40, 30), S(30, 30), S(30, 30), S(30, 30), S(30, 30), S(40, 30), S(40, 30),
    S(20, 20), S(20, 20), S(15, 20), S(20, 20), S(20, 20), S(15, 20), S(20, 20), S(20, 20),
    S(15, 10), S(15, 10), S(10, 10), S(10, 10), S(10, 10), S(10, 10), S(15, 10), S(15, 10),
    S(5, 0), S(5, 0), S(5, 0), S(5, 0), S(5, 0), S(5, 0), S(5, 0), S(5, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0),
    S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0), S(0, 0)
};

score_t * w17_eval_pawns_and_kings(search_t * s) {

    /*
     * 1. Probe the hash table for the pawn score
     */
    
    s->stack->pt = pawn_table::retrieve(s->brd.stack->pawn_hash);
    if (s->stack->pt->key == s->brd.stack->pawn_hash) {
        return &s->stack->pt->score;
    }
    
    score_t pawn_score_us[2];
    for (int us = BLACK; us <= WHITE; us++) {

        /*
         * 2. Evaluate King Position
         */

        int kpos = s->brd.get_sq(KING[us]);
        pawn_score_us[us].add(PST_KING[ISQ(kpos, us)]);

        /*
         * 3. Evaluate Pawns
         */
        
        U64 pawns = s->brd.bb[PAWN[us]];
        while (pawns) {
            int sq = pop(pawns);
            pawn_score_us[us].add(PST_PAWN[ISQ(sq, us)]);
        }

    }
    
    s->stack->pt->key = s->brd.stack->pawn_hash;
    s->stack->pt->score.set(pawn_score_us[WHITE]);
    s->stack->pt->score.sub(pawn_score_us[BLACK]);
    return &s->stack->pt->score;
}
