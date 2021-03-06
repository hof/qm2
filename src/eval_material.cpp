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
 * File: eval_material.cpp
 * Evaluates material balance
 */

#include "eval_material.h"
#include "board.h"
#include "search.h"
#include "bits.h"
#include "score.h"

namespace material {

    enum piece_val_t {
        VPAWN = 100,
        VKNIGHT = 325,
        VBISHOP = 325,
        VROOK = 500,
        VQUEEN = 975,
        VKING = 20000,
    };

    const short REDUNDANT_ROOK[2] = {0, -10};
    const short REDUNDANT_KNIGHT[2] = {0, -5};
    const short REDUNDANT_QUEEN[2] = {0, -20};

    const short MINOR_PAWNS_ADJUSTMENT[17] = {
        -5, -5, -5, -5, -5, -5, -5, -5, -5,
        0, 0, 5, 5, 5, 5, 5, 5
    };
    
    const short IMBALANCE[9][9] = { 
       //-4,-3,-2,-1, = +1 +2 +3 +4 minor pieces (white's point of view)
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //-4
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //-3
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //-2
        { 0, 0, 0, 0, 0, 0, 50, 75, 95 }, //-1
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //major pieces in balance
        { -95, -75,-50, 0, 0, 0, 0, 0, 0 }, //+1
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //+2
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }, //+3
        { 0, 0, 0, 0, 0, 0, 0, 0, 0 }  //+4 major pieces
    };

    enum mflag_t {
        MFLAG_EG = 128,
        MFLAG_MATING_POWER_W = 64,
        MFLAG_MATING_POWER_B = 32,
        MFLAG_MAJOR_IMBALANCE_W = 16,
        MFLAG_MINOR_IMBALANCE_W = 8,
        MFLAG_MAJOR_IMBALANCE_B = 4,
        MFLAG_MINOR_IMBALANCE_B = 2
    };

    bool has_mating_power(search_t * s, bool us) {
        const int flags = s->stack->mt->flags;
        return us == WHITE ? (flags & MFLAG_MATING_POWER_W) != 0
                : (flags & MFLAG_MATING_POWER_B) != 0;
    }

    bool has_imbalance(search_t * s, bool us) {
        static const int IMB_FLAGS[2] = {
            MFLAG_MINOR_IMBALANCE_B | MFLAG_MAJOR_IMBALANCE_B,
            MFLAG_MINOR_IMBALANCE_W | MFLAG_MAJOR_IMBALANCE_W
        };
        return (s->stack->mt->flags & IMB_FLAGS[us]) != 0;
    }

    bool has_major_imbalance(search_t * s) {
        static const int IMB_MAJOR = MFLAG_MAJOR_IMBALANCE_W | MFLAG_MAJOR_IMBALANCE_B;
        return (s->stack->mt->flags & IMB_MAJOR) != 0;
    }

    bool is_eg(search_t * s) {
        return (s->stack->mt->flags & MFLAG_EG) != 0;
    }

    /**
     * Evaluate material score and set the current game phase
     * @param sd search meta-data object
     */
    int eval(search_t * s) {

        /*
         * 1. Probe the material table
         */

        board_t * brd = &s->brd;
        s->stack->mt = material_table::retrieve(brd->stack->material_hash);
        material_table::entry_t * e = s->stack->mt;
        if (e->key == brd->stack->material_hash) {
            return e->score;
        }
        e->key = brd->stack->material_hash;

        /*
         * 2. Calculate material value and store in material hash table
         */

        const int wpawns = brd->count(WPAWN);
        const int bpawns = brd->count(BPAWN);
        const int wknights = brd->count(WKNIGHT);
        const int bknights = brd->count(BKNIGHT);
        const int wbishops = brd->count(WBISHOP);
        const int bbishops = brd->count(BBISHOP);
        const int wrooks = brd->count(WROOK);
        const int brooks = brd->count(BROOK);
        const int wqueens = brd->count(WQUEEN);
        const int bqueens = brd->count(BQUEEN);
        const int wminors = wknights + wbishops;
        const int bminors = bknights + bbishops;
        const int wmajors = wrooks + 2 * wqueens;
        const int bmajors = brooks + 2 * bqueens;
        const int minor_pawn_adj = MINOR_PAWNS_ADJUSTMENT[wpawns + bpawns];

        /*
         * Game phase
         */

        e->phase = MAX(0, score::MAX_PHASE /* 16 */
                - wminors - bminors /* max: 8 */
                - wrooks - brooks /* max:4 */
                - 2 * (wqueens + bqueens)) /* max: 4 */;

        /*
         * Material count evaluation
         */

        int result = 0;
        if (wknights != bknights) {
            result += (wknights - bknights) * (VKNIGHT + minor_pawn_adj);
            result += REDUNDANT_KNIGHT[wknights > 1];
            result -= REDUNDANT_KNIGHT[bknights > 1];
        }
        if (wbishops != bbishops) {
            result += (wbishops - bbishops) * (VBISHOP - minor_pawn_adj) ;
        }
        if (wrooks != brooks) {
            const int rook_eval_mg = (wrooks - brooks) * (VROOK - 40);
            const int rook_eval_eg = (wrooks - brooks) * (VROOK + 40);
            result += score::interpolate(rook_eval_mg, rook_eval_eg, e->phase);
            result += REDUNDANT_ROOK[wrooks > 1];
            result -= REDUNDANT_ROOK[brooks > 1];
        }
        if (wqueens != bqueens) {
            result += (wqueens - bqueens) * VQUEEN;
            result += REDUNDANT_QUEEN[wqueens > 1];
            result -= REDUNDANT_QUEEN[bqueens > 1];
        }

        /*
         * Material balance flags 
         */

        e->flags = 0;
        bool balance = (wminors == bminors) && (wmajors == bmajors);
        if (!balance) { //material imbalance
            if (result > 400) {
                e->flags |= MFLAG_MAJOR_IMBALANCE_W;
            } else if (result > 100) {
                e->flags |= MFLAG_MINOR_IMBALANCE_W;
            } else if (result < -400) {
                e->flags |= MFLAG_MAJOR_IMBALANCE_B;
            } else if (result < -100) {
                e->flags |= MFLAG_MINOR_IMBALANCE_B;
            }
            int minors_ix = range(0, 8, 4 + wminors - bminors);
            int majors_ix = range(0, 8, 4 + wmajors - bmajors);
            result += IMBALANCE[majors_ix][minors_ix];
        }

        /*
         * Pawns
         */

        result += (wpawns - bpawns) * VPAWN;

        /*
         * Attack power 
         */

        e->attack_force[WHITE] = 9 * wqueens + 5 * wrooks + 3 * wminors;
        e->attack_force[BLACK] = 9 * bqueens + 5 * brooks + 3 * bminors;

        /*
         * Mating power flags, endgame flags
         */

        const bool mating_power_w = wrooks || wqueens || wminors > 2 || (wminors == 2 && wbishops > 0);
        const bool mating_power_b = brooks || bqueens || bminors > 2 || (bminors == 2 && bbishops > 0);

        if (mating_power_w) {
            e->flags |= MFLAG_MATING_POWER_W;
        }
        if (mating_power_b) {
            e->flags |= MFLAG_MATING_POWER_B;
        }
        if (wpawns <= 1 || bpawns <= 1 || !mating_power_w || !mating_power_b) {
            e->flags |= MFLAG_EG;
        }

        e->score = result;
        return e->score;
    }

}