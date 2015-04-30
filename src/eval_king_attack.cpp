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
 * File: eval_king_attack.cpp
 * King attack evaluation. Evaluation terms:
 * 1) Structural shelter of the opponent (pawns, holes, back-rank, fianchetto)
 * 2) Pieces attacking the opponent's king
 * 3) Pieces defending the opponent's king  
 */

#include "eval_material.h"
#include "eval_pieces.h"
#include "search.h"
#include "bits.h"
#include "score.h"

namespace king_attack {

#define __ENABLE_KING_ATTACK_TRACE

#ifdef ENABLE_KING_ATTACK_TRACE

    void trace(std::string msg, bool us, int score) {
        std::cout << msg << (us ? " WHITE: " : " BLACK: ") << score << std::endl;
    }
#endif

#ifndef ENABLE_KING_ATTACK_TRACE 
#define trace(a,b,c) /* notn */
#endif


    const int8_t KING_ATTACK_UNIT[BKING + 1] = {
        //  x, p, n, b, r, q, k, p, n, b, r, q, k
        /**/0, 0, 1, 1, 2, 4, 0, 0, 1, 1, 2, 4, 0
    };

    const int8_t KING_DEFEND_UNIT[BKING + 1] = {
        //  x, p, n, b, r, q, k, p, n, b, r, q, k
        /**/0, 0, 3, 2, 1, 0, 0, 0, 3, 2, 1, 0, 0
    };

    const int16_t KING_SHELTER[24] = {//structural shelter value (pawns & kings)
        0, 15, 25, 30, 40, 50, 60, 75,
        90, 105, 125, 145, 160, 175, 190, 200,
        210, 220, 225, 235, 235, 240, 245, 250
    };

    const int16_t KING_SHELTER_MUL[8] = {
        128, 128, 128, 128, 128, 128, 196, 230
    };

    const int16_t KING_ATTACK_PCS[16] = {//multipliers
        0, 48, 128, 256, 384, 464, 496, 512,
        512, 512, 512, 512, 512, 512, 512, 512
    };

    const int16_t KING_ATTACK_UNITS[16] = {//multipliers
        0, 0, 0, 16, 48, 128, 256, 384,
        464, 496, 512, 512, 512, 512, 512, 512
    };

    const int8_t KING_ATTACK_OFFSET = 8; //perfectly castled king -9 units
    const int8_t KING_DEFEND_FIANCHETTO[2] = {-1, 1};
    const int8_t KING_DEFEND_BACKRANK[2] = {-1, 1};

    score_t * eval(search_t * s, bool us) {

        int pc = KING[us];
        score_t * result = &s->stack->pc_score[pc];
        result->clear();

        const int my_attack_force = s->stack->mt->attack_force[us];
        if (my_attack_force < 6) { //less than QR
            return result;
        }

        /*
         * 1. Shelter score
         */

        board_t * brd = &s->brd;
        int attack_score = 0;
        int shelter_ix = range(0, 23, KING_ATTACK_OFFSET + s->stack->pt->king_attack[us]);
        attack_score = KING_SHELTER[shelter_ix];
        trace("SHELTER ATTACK", us, KING_SHELTER[shelter_ix]);

        /*
         * 2. Reduce the shelter score for some cases:
         */

        //closed center
        if ((s->stack->pt->flags & pawn_table::FLAG_CLOSED_CENTER) != 0) {
            attack_score = attack_score / 2;
            trace("CLOSED CENTER MUL", us, attack_score);
        }
        
        if (my_attack_force < 8) {
            attack_score = (KING_SHELTER_MUL[my_attack_force] * attack_score) / 256;
            trace("PIECE FORCE MUL", us, attack_score);
        }

        //interface parameter adjustment
        attack_score = (s->king_attack_shelter * attack_score) / 256;
        trace("SHELTER TOTAL (UI MUL)", us, attack_score);

        /*
         * 3. Calculate the total piece attack score
         */

        int attack_units = 0;
        int defend_units = 0;
        int attack_pcs = 0;
        int defend_pcs = 0;
        int offs[2] = {-6, 6};

        //back rank protection and fianchetto
        const bool them = !us;
        const bool backrank_ok = RANK[us][8] & brd->bb[ROOK[them]];
        const bool fianchetto_ok = up1(brd->bb[KING[them]], them) & (brd->bb[PAWN[them]] | brd->bb[BISHOP[them]]);
        defend_units += KING_DEFEND_BACKRANK[backrank_ok];
        defend_units += KING_DEFEND_FIANCHETTO[fianchetto_ok];

        for (int pc = KNIGHT[us]; pc <= QUEEN[us]; pc++) {
            int a = KA_UNITS(s->stack->king_attack[pc]);
            int d = KD_UNITS(s->stack->king_attack[pc + offs[us]]);
            if (a) {
                attack_pcs += a;
                attack_units += a * KING_ATTACK_UNIT[pc];
            }
            if (d) {
                defend_pcs += d;
                defend_units += d * KING_DEFEND_UNIT[pc];
            }
        }

        int pc_attack_score = attack_units * 20 - defend_units * 5;
        pc_attack_score = (s->king_attack_pieces * pc_attack_score) / 256;
        const int pc_ix = attack_pcs + (attack_pcs == 0 && defend_pcs);
        trace("PIECE ATTACK (base) ", us, pc_attack_score);
        trace("PIECE ATTACK (mul) ", us, (KING_ATTACK_PCS[pc_ix] * pc_attack_score) / 256);
        attack_score += (KING_ATTACK_PCS[pc_ix] * pc_attack_score) / 256;
        result->set(attack_score, 0);
        trace("PIECE ATTACK (total) ", us, attack_score);
        return result;
    }

}