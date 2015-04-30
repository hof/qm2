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
        int wminors = wknights + wbishops;
        int bminors = bknights + bbishops;

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
            result += (wknights - bknights) * VKNIGHT;
            result += REDUNDANT_KNIGHT[wknights > 1];
            result -= REDUNDANT_KNIGHT[bknights > 1];
        }
        if (wbishops != bbishops) {
            result += (wbishops - bbishops) * VBISHOP;
        }
        if (wrooks != brooks) {
            result += (wrooks - brooks) * VROOK;
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
        bool balance = (wminors == bminors) && (wrooks + 2 * wqueens) == (brooks + 2 * bqueens);
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
        }

        /*
         * Pawns
         */

        result += (wpawns - bpawns) * VPAWN;

        /*
         * Attack power 
         */

        e->attack_force[WHITE] = 4 * wqueens + 2 * wrooks + wminors;
        e->attack_force[BLACK] = 4 * bqueens + 2 * brooks + bminors;

        assert(e->attack_force[WHITE] <= 36);
        assert(e->attack_force[BLACK] <= 36);

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