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

//#define ENABLE_KING_ATTACK_TRACE

#ifdef ENABLE_KING_ATTACK_TRACE

    void trace(std::string msg, bool us, int score) {
        std::cout << msg << (us ? " WHITE: " : " BLACK: ") << score << std::endl;
    }
#endif

#ifndef ENABLE_KING_ATTACK_TRACE 
#define trace(a,b,c) /* notn */
#endif

    const int16_t KING_SHELTER[24] = {//shelter attack value (pawns & kings)
        -50, -30, -15, -5, 0, 5, 10, 15,
        20, 25, 30, 35, 40, 45, 50, 55, 
        60, 65, 70, 75, 80, 85, 90, 95
    };

    const int16_t KING_ATTACK[64] = {
        0, 0, 1, 2, 3, 5, 7, 9, 
        12, 15, 18, 22, 26, 30, 35, 40, 
        45, 50, 56, 62, 69, 75, 82, 90, 
        98, 105, 114, 122, 131, 141, 150, 160, 
        170, 181, 192, 203, 214, 226, 238, 249, 
        261, 273, 285, 296, 308, 320, 332, 343, 
        355, 367, 379, 390, 402, 414, 426, 438, 
        449, 461, 473, 485, 496, 501, 501, 501
    };

    const uint8_t CLOSED_CENTER_MUL = 180;
    const int8_t FIANCHETTO_UNIT[2] = {1, 0}; //bad, good fianchetto
    const int8_t BACKRANK_UNIT[2] = {1, 0}; //bad, good backrank

    score_t * eval(search_t * s, bool us) {

        /*
         * 1. Return early if we have no queen or little attack force
         */

        score_t * result = &s->stack->pc_score[KING[us]];
        result->clear();
        const board_t * brd = &s->brd;

        if (brd->bb[QUEEN[us]] == 0) {
            return result;
        }

        const int my_attack_force = s->stack->mt->attack_force[us];

        if (my_attack_force <= 12) {
            return result;
        }

        /*
         * 2. Shelter score
         */

        //get the shelter units previously calculated in the pawn eval
        int shelter_units = s->stack->pt->king_attack[us];

        //add finachetto and weak backrank
        const bool them = !us;
        const bool backrank_ok = RANK[us][8] & brd->bb[ROOK[them]];
        const bool fianchetto_ok = up1(brd->bb[KING[them]], them) & (brd->bb[BISHOP[them]] | brd->bb[PAWN[them]]);
        shelter_units += FIANCHETTO_UNIT[fianchetto_ok];
        shelter_units += BACKRANK_UNIT[backrank_ok];
        
        trace("SHELTER ATTACK UNITS (fianchetto / backrank)", us, shelter_units);
        
        //consider material situation
        shelter_units += my_attack_force - s->stack->mt->attack_force[!us];

        //convert units to score using the king shelter table
        int shelter_ix = range(0, 23, shelter_units);
        int attack_score = KING_SHELTER[shelter_ix];
        trace("SHELTER ATTACK", us, attack_score);

        /*
         * 3. Adjust the shelter score for some cases:
         */

        //closed center
        if ((s->stack->pt->flags & pawn_table::FLAG_CLOSED_CENTER) != 0) {
            attack_score = (CLOSED_CENTER_MUL * attack_score) / 256;
            trace("CLOSED CENTER MUL", us, attack_score);
        }

        //interface parameter adjustment
        if (s->king_attack_shelter != 256) {
            attack_score = (s->king_attack_shelter * attack_score) / 256;
            trace("SHELTER TOTAL (UI MUL)", us, attack_score);
        }

        /*
         * 4. Calculate the total piece attack score
         */

        int attackers = 0;
        int weight = 0;
        U64 attacks = s->stack->attack[PAWN[us]] | s->stack->attack[KING[us]];
        U64 defends = s->stack->attack[PAWN[!us]];
        
        for (int pc = KNIGHT[us]; pc <= QUEEN[us]; pc++) {
            attackers += s->stack->king_attack[pc].mg;
            weight += s->stack->king_attack[pc].eg;
            attacks |= s->stack->attack[pc];
            defends |= s->stack->attack[pc > 6 ? pc - 6 : pc + 6];
        }

        if (attackers == 0) {
            result->set(attack_score, 0);
            trace("KING ATTACK (shelter only) ", us, attack_score);
        }
        
        //initialize attack units with the shelter score and piece attack weights
        int units = attack_score / 32; 
        units += (attackers * weight) / 2;
        
        //add units for attacked squares around the king that are not defended
        const U64 undefended_attacks = s->stack->attack[KING[!us]] & attacks & ~defends;
        units += popcnt0(undefended_attacks) * 3;
        
        //add units for contact checks
        const U64 safe_queen_contact_checks = undefended_attacks & s->stack->attack[QUEEN[us]]
            & (attacks ^ s->stack->attack[QUEEN[us]]);
        
        units += popcnt0(safe_queen_contact_checks) * 6;
        const int pc_attack_score = KING_ATTACK[range(0, 63, units)];
        
        trace("ATTACKERS", us, attackers);
        trace("WEIGHT", us, weight);
        trace("UNITS", us, units);
        trace("ATTACK (pieces)", us, pc_attack_score);
        
        attack_score += pc_attack_score;
        result->set(attack_score, 0);
         
        trace("KING ATTACK (total) ", us, attack_score);
        return result;
    }

}