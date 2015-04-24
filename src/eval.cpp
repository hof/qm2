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
 * Evalution functions for traditional chess
 */

#include "eval.h"
#include "search.h"
#include "bits.h"
#include "score.h"

#include "eval_pst.h"
#include "eval_material.h"
#include "eval_pieces.h"
#include "eval_endgame.h"
#include "eval_pawns.h"
#include "eval_king_attack.h"

/**
 * Initialize the global Piece Square Tables (PST)
 */
pst_t PST;
void init_pst() {
    for (int sq = a1; sq <= h8; sq++) {
        PST[EMPTY][sq].clear();
        PST[BPAWN][sq].set(PST_PAWN_MG[sq], PST_PAWN_EG[sq]);
        PST[BKNIGHT][sq].set(PST_KNIGHT_MG[sq], PST_KNIGHT_EG[sq]);
        PST[BBISHOP][sq].set(PST_BISHOP_MG[sq], PST_BISHOP_EG[sq]);
        PST[BROOK][sq].set(PST_ROOK_MG[sq], PST_ROOK_EG[sq]);
        PST[BQUEEN][sq].set(PST_QUEEN_MG[sq], PST_QUEEN_EG[sq]);
        PST[BKING][sq].set(PST_KING_MG[sq], PST_KING_EG[sq]);
        int fsq = FLIP_SQUARE(sq);
        for (int pc = WPAWN; pc <= WKING; pc++) {
            PST[pc][fsq].set(PST[pc + 6][sq]);
        }
    }
    assert(PST[WKING][a1].equals(PST[BKING][a8]));
}

const score_t TEMPO[2] = {S(-10, 0), S(10, 0)};

/**
 * Main evaluation function (normal chess variant)
 * @param s search object
 * @return total evaluation score rounded to GRAIN_SIZE 
 */
int evaluate(search_t * s) {

    /*
     * Return immediately if in check or if a valid evaluation score is
     * available on the stack
     */

    if (s->stack->in_check && s->brd.ply < (MAX_PLY - 1)) {
        s->stack->eval_result = score::INVALID;
        return s->stack->eval_result;
    }

    if (s->stack->eval_result != score::INVALID) {
        return s->stack->eval_result;
    }

    /*
     * Calculate evaluation score. This needs to be done in the following order:
     * 1) Material balance. This also sets the game phase and material flags.
     * 2) Pawn structure. Sets pawns flags, mobility masks and passers.
     * 3) Piece evaluation. Stores king attack / defend information.
     * 4) Passed pawn, king attack and endgame evaluation. 
     * 
     * The score is interpolated between midgame and endgame value.
     */

    const bool wtm = s->brd.stack->wtm;
    int result = material::eval(s); //sets stack->mt->phase and material flags
    score_t * score = &s->stack->eval_score;
    score->set(TEMPO[wtm]);
    score->add(pawns::eval(s)); 
    score->add(pieces::eval(s)); 
    score->add(pawns::eval_passed_pawns(s, WHITE));
    score->sub(pawns::eval_passed_pawns(s, BLACK));
    score->add(king_attack::eval(s, WHITE));
    score->sub(king_attack::eval(s, BLACK));
    result += score->get(s->stack->mt->phase); 
    s->stack->eg_score = result;
    if (material::is_eg(s)) {
        result = eg::eval(s, result);
    }

    /*
     * Return score for current side to move and rounded to grain size
     */

    if (!wtm) {
        result = -result;
        s->stack->eg_score = -s->stack->eg_score;
    }

    result = (result / GRAIN_SIZE) * GRAIN_SIZE;
    s->stack->eval_result = result;
    assert(result > -10000 && result < 10000);
    return result;
}