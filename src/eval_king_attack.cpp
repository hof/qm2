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

    const int8_t ATTACK_WEIGHT[BKING + 1] = {
        // P  N  B  R  Q  K  
        0, 1, 3, 3, 5, 9, 2,
        1, 3, 3, 5, 9, 2
    };

    const int16_t KING_SHELTER[24] = {//shelter attack value (pawns & kings)
        -50, -40, -25, -10, 5, 15, 25, 35,
        45, 50, 55, 60, 65, 70, 75, 80,
        85, 90, 95, 100, 105, 110, 115, 120
    };

    const int16_t KING_ATTACK[64] = {
        0, 0, 0, 1, 2, 3, 5, 7, 
        9, 12, 15, 18, 22, 26, 30, 35, 
        40, 45, 50, 56, 62, 68, 75, 82, 
        90, 98, 105, 114, 123, 132, 141, 150, 
        160, 170, 180, 191, 202, 214, 226, 238, 
        250, 262, 274, 286, 296, 308, 320, 332, 
        344, 356, 368, 380, 392, 404, 414, 426, 
        438, 450, 462, 472, 482, 490, 495, 500
    };

    const uint8_t CLOSED_CENTER_MUL = 180;
    const int8_t FIANCHETTO_UNIT[2] = {1, 0}; //units for bad, good fianchetto
    const int8_t BACKRANK_UNIT[2] = {1, 0}; //units for bad, good backrank
    const int8_t QUEEN_CONTACT_CHECK = 20; //units
    const int8_t QUEEN_DISTANT_CHECK = 10; //units

    
    bool verify_defended(search_t * s, int sq, int exclude_sq, bool them) {
        const U64 bsq = BIT(sq);
        const U64 direct_attacks = s->stack->attack[PAWN[them]] | s->stack->attack[KNIGHT[them]];
        bool result = bsq & direct_attacks;
        if (!result) {
            U64 diag = magic::bishop_moves(sq, s->brd.all() & ~BIT(exclude_sq));
            result |= diag & (s->brd.bb[BISHOP[them]] | s->brd.bb[QUEEN[them]]);
        }
        if (!result) {
            U64 hv = magic::rook_moves(sq, s->brd.all() & ~BIT(exclude_sq));
            result |= hv & (s->brd.bb[ROOK[them]] | s->brd.bb[QUEEN[them]]);
        }
        return result;
    }
    
    bool verify_supported(search_t * s, int sq, int exclude_sq, bool us) {
        const U64 bsq = BIT(sq);
        const U64 direct_attacks = s->stack->attack[PAWN[us]] | s->stack->attack[KNIGHT[us]] | s->stack->attack[KING[us]];
        bool result = bsq & direct_attacks;
        if (!result) {
            U64 diag = magic::bishop_moves(sq, s->brd.all()) & ~BIT(exclude_sq);
            result |= diag & (s->brd.bb[BISHOP[us]] | s->brd.bb[QUEEN[us]]);
        }
        if (!result) {
            U64 hv = magic::rook_moves(sq, s->brd.all()) & ~BIT(exclude_sq);
            result |= hv & (s->brd.bb[ROOK[us]] | s->brd.bb[QUEEN[us]]);
        }
        return result;
    }
    
    score_t * eval(search_t * s, bool us) {

        /*
         * 1. Return early if we have no queen or little attack force
         */

        score_t * result = &s->stack->pc_score[KING[us]];
        result->clear();
        board_t * brd = &s->brd;

        if (brd->bb[QUEEN[us]] == 0) {
            return result;
        }

        const int my_attack_force = s->stack->mt->attack_force[us];

        if (my_attack_force < 12) {
            return result;
        }

        /*
         * 2. Shelter score
         */

        //get the shelter units previously calculated in the pawn eval
        int shelter_units = s->stack->pt->king_attack[us];
        trace("SHELTER ATTACK (pawns/kings)", us, shelter_units);

        //add finachetto and weak backrank
        const bool them = !us;
        const bool backrank_ok = RANK[us][8] & brd->bb[ROOK[them]];
        const bool fianchetto_ok = up1(brd->bb[KING[them]], them) & (brd->bb[BISHOP[them]] | brd->bb[PAWN[them]]);
        shelter_units += FIANCHETTO_UNIT[fianchetto_ok];
        shelter_units += BACKRANK_UNIT[backrank_ok];

        trace("SHELTER ATTACK (fianchetto / backrank)", us, FIANCHETTO_UNIT[fianchetto_ok] + BACKRANK_UNIT[backrank_ok]);

        //convert units to score using the king shelter table
        int shelter_ix = range(0, 23, shelter_units);
        int sh_attack_score = KING_SHELTER[shelter_ix];
        trace("SHELTER ATTACK (score)", us, sh_attack_score);

        /*
         * 3. Adjust the shelter score for some special cases:
         */

        //closed center
        if ((s->stack->pt->flags & pawn_table::FLAG_CLOSED_CENTER) != 0) {
            sh_attack_score = (CLOSED_CENTER_MUL * sh_attack_score) / 256;
            trace("SHELTER ATTACK (closed center adjustment)", us, sh_attack_score);
        }

        //interface parameter adjustment
        if (s->king_attack_shelter != 256) {
            sh_attack_score = (s->king_attack_shelter * sh_attack_score) / 256;
            trace("SHELTER ATTACK (interface adjustment)", us, sh_attack_score);
        }

        /*
         * 4. Calculate the total piece attack score
         */
        
        int attackers_count = 0;
        int attackers_weight = 0;
        U64 attacks = 0;
        U64 defends = 0;

        //get the piece attacks (using attack info from eval_pieces)
        for (int pc = KNIGHT[us]; pc <= QUEEN[us]; pc++) {
            defends |= s->stack->attack[pc > 6 ? pc - 6 : pc + 6];
            const int count = s->stack->king_attack[pc];
            if (count == 0) {
                continue;
            }
            attackers_count += count;
            attackers_weight += count * ATTACK_WEIGHT[pc];
            attacks |= s->stack->attack[pc];
        }
        
        //return immediately if no piece is attacking
        if (attackers_count == 0) {
            result->set(sh_attack_score, 0);
            trace("KING ATTACK (total) ", us, result->mg);
            return result;
        }
        
        const int ksq_them = brd->get_sq(KING[them]);
        const U64 king_zone = KING_ZONE[ksq_them];
        
        //include pawn defends
        defends |= s->stack->attack[PAWN[them]];
        
        //include pawn attacks
        if (s->stack->attack[PAWN[us]] & king_zone) {
            attackers_count++;
            attackers_weight += ATTACK_WEIGHT[PAWN[us]];
            attacks |= s->stack->attack[PAWN[us]];
        }

        //include king attacks
        if (s->stack->attack[KING[us]] & king_zone) {
            attackers_count++;
            attackers_weight += ATTACK_WEIGHT[KING[us]];
            attacks |= s->stack->attack[KING[us]];
        } 

        //initialize attack units with the shelter score and piece attack weights
        int units = sh_attack_score / 10;
        trace("PIECE ATTACK (shelter)", us, units);
        
        //include piece attack
        units += (attackers_count * attackers_weight) / 4;
        trace("PIECE ATTACK (count)", us, attackers_count);
        trace("PIECE ATTACK (weight)", us, attackers_weight);
        trace("PIECE ATTACK (piece units)", us, (attackers_count * attackers_weight) / 4);
        
        //include material imbalance
        units += my_attack_force - s->stack->mt->attack_force[!us];
        trace("PIECE ATTACK (imbalance)", us, my_attack_force - s->stack->mt->attack_force[!us]);
        
        //include amount of attacked squares around their king
        const U64 king_area = KING_MOVES[ksq_them];
        const U64 area_attacks = king_area & attacks;
        const U64 undefended_area_attacks = area_attacks & ~defends;
        units += 1 * popcnt0(area_attacks);
        units += 2 * popcnt0(undefended_area_attacks);
        trace("PIECE ATTACK (squares)", us, 1 * popcnt0(area_attacks));
        trace("PIECE ATTACK (undefended squares)", us, 2 * popcnt0(undefended_area_attacks));
        
        const U64 checks_diag = magic::bishop_moves(ksq_them, brd->all());
        const U64 checks_hv = magic::rook_moves(ksq_them, brd->all());
        
        //include queen checks
        if (s->stack->attack[QUEEN[us]] & king_area) {
            U64 queens = brd->bb[QUEEN[us]];
            while (queens) {
                int qsq = pop(queens);
                U64 queen_attacks = magic::queen_moves(qsq, s->brd.all()) & ~s->brd.all(us);
                U64 contact_checks = queen_attacks & king_area;
                while (contact_checks) {
                    units++;
                    trace("PIECE ATTACK (queen contact check)", us, 1);
                    int attack_sq = pop(contact_checks);
                    const bool defended = BIT(attack_sq) & defends;
                    if (defended && verify_defended(s, attack_sq, qsq, them)) {
                        continue;
                    }
                    units++;
                    trace("PIECE ATTACK (undefended queen contact check)", us, 1);
                    if (verify_supported(s, attack_sq, qsq, us)) {
                        units += QUEEN_CONTACT_CHECK;
                        trace("PIECE ATTACK (supported queen contact check)", us, QUEEN_CONTACT_CHECK);
                    }
                }
                U64 distance_checks = queen_attacks & (checks_diag | checks_hv) & ~king_area & s->stack->pt->mob[us];
                while (distance_checks) {
                    units++;
                    trace("PIECE ATTACK (queen distant check)", us, 1);
                    int attack_sq = pop(distance_checks);
                    const bool defended = BIT(attack_sq) & defends;
                    if (defended && verify_defended(s, attack_sq, qsq, them)) {
                        continue;
                    }
                    units += QUEEN_DISTANT_CHECK;
                    trace("PIECE ATTACK (undefended queen distant)", us, QUEEN_DISTANT_CHECK);
                }
            }
        }
              
        //convert units to score
        int pc_attack_score = KING_ATTACK[range(0, 63, units)];
        trace("PIECE ATTACK (total units)", us, units);
        trace("PIECE ATTACK (score)", us, pc_attack_score);
        
        //interface parameter adjustment
        if (s->king_attack_pieces != 256) {
            pc_attack_score = (s->king_attack_pieces * pc_attack_score) / 256;
            trace("PIECE ATTACK (interface adjustment)", us, pc_attack_score);
        }

        //return combined shelter + piece attack scores
        result->set(sh_attack_score + pc_attack_score, 0);
        trace("KING ATTACK (total) ", us, result->mg);
        return result;
    }

}