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
 * File: endgame.cpp
 * Endgame evaluation functions for normal chess
 */

#include "eval_pawns.h"
#include "eval_material.h"
#include "eval.h"
#include "search.h"
#include "bits.h"

namespace pawns {

//#define ENABLE_PAWN_EVAL_TRACE

#ifdef ENABLE_PAWN_EVAL_TRACE

    void trace(std::string msg, int sq, const score_t & s) {
        if (sq >= 0) {
            std::cout << msg << " " << FILE_SYMBOL(sq) << RANK_SYMBOL(sq) << ": ";
        } else {
            std::cout << msg << ": ";
        }
        std::cout << "(" << s.mg << ", " << s.eg << ") " << std::endl;
    }
#endif

#ifndef ENABLE_PAWN_EVAL_TRACE 
#define trace(a,b,c) /* notn */
#endif

    const score_t ISOLATED[2] = {S(-25, -20), S(-15, -15)}; //open, closed file
    const score_t WEAK[2] = {S(-15, -15), S(-10, -10)}; //open, closed file
    const score_t DOUBLED = S(-10, -20);
    const score_t BLOCKED_CENTER_PAWN = S(-15, 0);
    const int PAWN_WIDTH_EG = 5;
    const int KING_ACTIVITY = 5; //endgame score for king attacking or defending pawns

    const score_t CANDIDATE[8] = {//rank
        S(0, 0), S(0, 5), S(0, 5), S(0, 10),
        S(0, 20), S(0, 40), S(0, 0), S(0, 0)
    };

    const int DUO[8] = {//or defended, indexed by rank 
        0, 0, 0, 0, 5, 25, 45, 0
    };

    // attack units regarding king position
    const int8_t SHELTER_KPOS[64] = {
        9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9,
        6, 6, 7, 8, 8, 7, 6, 6,
        4, 4, 5, 6, 6, 5, 4, 4,
        2, 2, 3, 4, 4, 3, 2, 2,
        1, 1, 2, 3, 3, 2, 1, 1,
        0, 0, 1, 2, 2, 1, 0, 0 //a1..h1
    };

    const int8_t SHELTER_OPEN_FILES[4] = {0, 2, 4, 5};
    const int8_t PAWN_SHIELD_GAPS[6] = {0, 1, 3, 5, 6, 7};
    const int8_t SIDE_PAWNS[3] = {0, -1, -2};
    const int8_t STORM_PAWNS[8] = {0, 1, 2, 3, 3, 4, 4, 4};

    int shelter_attack_units(const search_t * s, const int kpos_them, const bool them);

    /**
     * Main pawns and kings evaluation function
     */

    score_t * eval(search_t * s) {

        /*
         * 1. Probe the pawn hash table for the pawns and kings score
         */

        board_t * brd = &s->brd;
        s->stack->pt = pawn_table::retrieve(brd->stack->pawn_hash);
        pawn_table::entry_t * e = s->stack->pt;
        if (e->key == brd->stack->pawn_hash) {
            return &e->score;
        }

        /*
         * 2. Calculate the pawns and kings structural score
         */

        score_t pawn_score[2];
        e->passers = 0;
        e->flags = 0;
        e->king_attack[WHITE] = 0;
        e->king_attack[BLACK] = 0;
        int blocked_center_pawns = 0;
        const U64 pawns_all = brd->bb[WPAWN] | brd->bb[BPAWN];
        const int kpos[2] = {brd->get_sq(BKING), brd->get_sq(WKING)};

        for (int us = BLACK; us <= WHITE; us++) {
            pawn_score[us].clear();
            e->open_files[us] = 0xFF;
            e->count[us] = 0;
            U64 pawns_us = brd->bb[PAWN[us]];
            U64 pawns_them = brd->bb[PAWN[!us]];
            int step = PAWN_DIRECTION[us];
            U64 bb = pawns_us;
            bool them = !us;

            while (bb) {
                int sq = pop(bb);
                U64 bsq = BIT(sq);
                int f = FILE(sq);
                int r = RANK(sq);
                int r_us = us == WHITE ? r : 7 - r;
                U64 up = fill_up(bsq, us) ^ bsq;
                U64 af = ADJACENT_FILES[f];

                //set this pawn's characteristics
                bool isolated = (af & pawns_us) == 0;
                bool doubled = up & pawns_us;
                bool opposed = up & pawns_them;
                bool attacking = !isolated && (PAWN_CAPTURES[us][sq] & pawns_them);
                bool blocked = !attacking && (BIT(sq + step) & pawns_all) != 0;
                bool passed = !doubled && !opposed && 0 == (pawns_them & af & upward_ranks(r, us));
                bool defended = !isolated && (PAWN_CAPTURES[them][sq] & pawns_us);
                bool duo = defended || (af & RANKS[r] & pawns_us);
                bool weak = !isolated && !passed && !defended && !attacking && !doubled && (r_us + !blocked) < 6;

                //weak pawns: not weak if it can quickly move to a safe square
                if (weak && !blocked) {
                    assert(r_us > 0 && r_us < 5);
                    int next_sq = sq + step;
                    for (int stp = 0; stp < 2 + (r_us == 1); stp++) {

                        //get pawn attacks and defenders of the next square
                        U64 defs = PAWN_CAPTURES[them][next_sq] & pawns_us;
                        U64 atck = PAWN_CAPTURES[us][next_sq] & pawns_them;
                        next_sq += step;

                        //no attackers or defenders, not blocked: try another step
                        if (defs == 0 && atck == 0 && (BIT(next_sq) & pawns_all) == 0) {
                            continue;
                        }

                        //otherwise, it's only safe if #defs >= #atck
                        bool not_safe = defs == 0 || (is_1(defs) && gt_1(atck));
                        weak &= not_safe;
                        break;
                    }
                }

                //weak pawns: not weak if a pawn on its left side can protect it
                if (weak && r_us > 2 && f > 0) { //verify left side
                    assert(r_us < 6); //only rank 3 - 5 apply
                    int sq1 = sq - step - 1;
                    int sq2 = sq1 - step;
                    if ((BIT(sq2) & pawns_us) && !(BIT(sq1) & pawns_them)) {
                        weak &= !brd->pawn_is_safe(sq1, us);
                    } else if (r_us == 4 && (BIT(sq2 - step) & pawns_us)
                            && !((BIT(sq2) | BIT(sq1)) & pawns_all)) {
                        weak &= !brd->pawn_is_safe(sq1, us) && !brd->pawn_is_safe(sq2, us);
                    }
                }

                //weak pawns: not weak if a pawn on its right side can protect it
                if (weak && r_us > 2 && f < 7) { //verify right side
                    assert(r_us < 6); //only rank 3 - 5 apply
                    int sq1 = sq - step + 1;
                    int sq2 = sq1 - step;
                    if ((BIT(sq2) & pawns_us) && !(BIT(sq1) & pawns_them)) {
                        weak &= !brd->pawn_is_safe(sq1, us);
                    } else if (r_us == 4 && (BIT(sq2 - step) & pawns_us)
                            && !((BIT(sq2) | BIT(sq1)) & pawns_all)) {
                        weak &= !brd->pawn_is_safe(sq1, us) && !brd->pawn_is_safe(sq2, us);
                    }
                }

                //candidate passed pawns
                bool candidate = !passed && !weak && !opposed && !doubled && !isolated && !blocked;
                if (candidate && r_us < 5) {
                    U64 helpers = af & pawns_us & KING_MOVES[sq];
                    U64 sentries = af & pawns_them & upward_ranks(r, us);
                    assert(sentries); //no sentries indicates a passed pawn
                    candidate &= gt_1(helpers) || (is_1(helpers) && is_1(sentries));
                }

                //update the score
                pawn_score[us].add(PST::table[PAWN[us]][sq]);
                trace("PST", sq, PST::table[PAWN[us]][sq]);

                if (isolated) {
                    pawn_score[us].add(ISOLATED[opposed]);
                    trace("ISOLATED", sq, ISOLATED[opposed]);
                }

                if (weak && !isolated) {
                    pawn_score[us].add(WEAK[opposed]);
                    trace("WEAK", sq, WEAK[opposed]);
                }

                if (duo) {
                    pawn_score[us].add(DUO[r_us]);
                    trace("DUO", sq, DUO[r_us]);
                }

                if (doubled) {
                    pawn_score[us].add(DOUBLED);
                    trace("DOUBLED", sq, DOUBLED);
                }

                if (passed) {
                    e->passers |= bsq;
                    trace("PASSED", sq, S(0, 0));
                }

                if (candidate) {
                    pawn_score[us].add(CANDIDATE[r_us]);
                    trace("CANDIDATE", sq, CANDIDATE[r_us]);
                }

                if (blocked) {
                    blocked_center_pawns += bool(bsq & CENTER);
                    if (sq == d2 || sq == e2 || sq == d7 || sq == e7) {
                        pawn_score[us].add(BLOCKED_CENTER_PAWN);
                        trace("BLOCKED CENTER PAWN", sq, BLOCKED_CENTER_PAWN);
                    }
                }

                //update open lines mask
                e->open_files[us] &= ~(1 << f);
                e->count[us]++;
            }

            //pawn width: pawns far apart helps in the endgame
            e->width[us] = byte_width0(e->open_files[us] ^ 0xFF);
            pawn_score[us].add(0, PAWN_WIDTH_EG * e->width[us]);
            trace("PAWN WIDTH", -1, S(0, PAWN_WIDTH_EG * e->width[us]));

            /*
             * b) mobility and attack masks
             */

            e->mob[us] = ~(pawns_us | brd->pawn_attacks(them) | brd->bb[KING[us]]);
            e->attack[us] = e->mob[us] & (pawns_them | brd->bb[KING[them]]);

            /*
             * c) King score
             */

            //piece square table
            pawn_score[us].add(PST::table[KING[us]][kpos[us]]);
            trace("PST KING", kpos[us], PST::table[KING[us]][kpos[us]]);

            //king attacking pawns
            U64 king_atcks = KING_MOVES[kpos[us]] & e->attack[us];
            pawn_score[us].add(0, popcnt0(king_atcks) * KING_ACTIVITY);
            trace("KING ACTIVITY", kpos[us], S(0, popcnt0(king_atcks) * KING_ACTIVITY));

            //shelter
            int sau = shelter_attack_units(s, kpos[them], them);
            if (sau > 2 && brd->can_castle_ks(them)) {
                sau = MIN(sau, shelter_attack_units(s, ISQ(g8, them), them) + 2);
            }
            if (sau > 2 && brd->can_castle_qs(them)) {
                sau = MIN(sau, shelter_attack_units(s, ISQ(c8, them), them) + 2);
            }

            e->king_attack[us] += sau;
            trace("KING ATTACK UNITS (shelter)", kpos[them], sau);

        }

        if (blocked_center_pawns >= 3) {
            e->flags |= pawn_table::FLAG_CLOSED_CENTER;
            trace("CLOSED CENTER FLAG", e->flags, e->flags);
        }

        trace("WHITE ATTACKS UNITS (TOTAL)", kpos[BLACK], e->king_attack[WHITE]);
        trace("BLACK ATTACKS UNITS (TOTAL)", kpos[WHITE], e->king_attack[BLACK]);
        trace("WHITE PAWN SCORE (TOTAL)", -1, pawn_score[WHITE]);
        trace("BLACK PAWN SCORE (TOTAL)", -1, pawn_score[BLACK]);

        /*
         * 3. Return the total score from white's point of view
         */

        e->score.set(pawn_score[WHITE]);
        e->score.sub(pawn_score[BLACK]);
        e->key = brd->stack->pawn_hash;
        return &e->score;
    }

    /**
     * Calculate shelter attack units: the (weighted) amount of weaknesses in 
     * the pawn shelter protecting their king. This value is used later in the 
     * king attack evaluation.
     * @param s search object with search stack
     * @param kpos_them square location of their king
     * @param them side
     * @return amount of attack units, 0 for a perfect shelter to approx 10 or more for no shelter
     */
    int shelter_attack_units(const search_t * s, const int kpos_them, const bool them) {

        const bool us = !them;

        const board_t * brd = &s->brd;
        pawn_table::entry_t * e = s->stack->pt;

        //attack units - king position
        int units = SHELTER_KPOS[ISQ(kpos_them, them)];
        trace("KING ATTACK UNITS (POS)", kpos_them, units);

        //attack units - (half)open files
        U64 open_files_king = e->open_files[!them] & fill_south(KING_MOVES[kpos_them]);
        units += SHELTER_OPEN_FILES[popcnt0(open_files_king)];
        trace("KING ATTACK UNITS (OPEN FILES)", kpos_them, SHELTER_OPEN_FILES[popcnt0(open_files_king)]);

        //attack units - shield
        U64 pawn_shield_gaps = KING_MOVES[kpos_them] & upward_ranks(RANK(kpos_them), them) & ~brd->bb[PAWN[them]];
#ifdef ENABLE_PAWN_EVAL_TRACE
        bb_print("pawn shield gaps", pawn_shield_gaps);
#endif
        units += PAWN_SHIELD_GAPS[popcnt0(pawn_shield_gaps)];
        trace("KING ATTACK UNITS (pawn shield gaps)", kpos_them, PAWN_SHIELD_GAPS[popcnt0(pawn_shield_gaps)]);

        //attack units - pawns beside the king
        U64 side_pawns = KING_MOVES[kpos_them] & RANKS[RANK(kpos_them)] & brd->bb[PAWN[them]];
#ifdef ENABLE_PAWN_EVAL_TRACE
        bb_print("side_pawns", side_pawns);
#endif
        units += SIDE_PAWNS[popcnt0(side_pawns)];
        trace("KING ATTACK UNITS (side pawns)", kpos_them, SIDE_PAWNS[popcnt0(side_pawns)]);
        
        //storm pawns
        U64 storm_pawns = KING_ZONE[kpos_them] &  upward_ranks(RANK(kpos_them), them) & brd->bb[PAWN[us]];
        #ifdef ENABLE_PAWN_EVAL_TRACE
        bb_print("storm pawns", storm_pawns);
#endif
        units += STORM_PAWNS[popcnt0(storm_pawns)];
        trace("KING ATTACK UNITS (storm pawns)", kpos_them, STORM_PAWNS[popcnt0(storm_pawns)]);

        //attack units - mask of unsafe lines and squares (also diagonally)
        U64 king_attack_mask = (KING_ZONE[kpos_them] | magic::queen_moves(kpos_them, (brd->bb[WPAWN] | brd->bb[BPAWN]) | brd->bb[KING[us]]));
        king_attack_mask &= e->mob[us] & (RANK_3 | RANK_4 | RANK_5 | RANK_6);
#ifdef ENABLE_PAWN_EVAL_TRACE
        bb_print("king attack mask", king_attack_mask);
#endif
        units += popcnt0(king_attack_mask) / 2;
        trace("KING ATTACK UNITS (mask)", kpos_them, popcnt0(king_attack_mask) / 2);

        //@todo: storm pawns (threatening to open a line)

        trace("KING ATTACK UNITS (total)", kpos_them, units);

        return units;
    }

    /**
     * Evaluate passed pawns
     * @param s search object with search stack
     * @param us side to move: white(1) or black(0)
     * @return score
     */

    const uint8_t PP_MG[6] = {5, 5, 15, 35, 70, 130};
    const uint8_t PP_EG[6] = {10, 10, 15, 25, 50, 80};
    const uint8_t PP_DIST_US[6] = {0, 0, 3, 7, 12, 15};
    const uint8_t PP_DIST_THEM[6] = {0, 0, 5, 12, 20, 40};
    const uint8_t PP_ADVANCE[6] = {0, 0, 0, 20, 50, 120};

    score_t * eval_passed_pawns(search_t * sd, bool us) {

        score_t * result = &sd->stack->passer_score[us];
        result->clear();
        U64 passers = sd->stack->pt->passers & sd->brd.bb[PAWN[us]];
        if (passers == 0) {
            return result;
        }
        bool them = !us;
        int step = PAWN_DIRECTION[us];
        score_t bonus;
        while (passers) {
            int sq = pop(passers);
            int r = us == WHITE ? RANK(sq) - 1 : 6 - RANK(sq);
            assert(r >= 0 && r <= 5);
            bonus.set(PP_MG[r], PP_EG[r]);
            trace("PP Bonus", sq, bonus);
            result->add(bonus);

            //stop here (with just the base bonus) if the pawn is on rank 2(r=0) or 3(r=1)
            if (r < 2) {
                continue;
            }

            //king distance in the endgame
            int to = sq + step;
            int kdist_us_bonus = distance(sd->brd.get_sq(KING[us]), to) * PP_DIST_US[r];
            int kdist_them_bonus = distance(sd->brd.get_sq(KING[them]), to) * PP_DIST_THEM[r];
            trace("PP King Distance (us)", sq, S(distance(sd->brd.get_sq(KING[us]), to), -kdist_us_bonus));
            trace("PP King Distance (them)", sq, S(distance(sd->brd.get_sq(KING[them]), to), kdist_them_bonus));
            result->add(0, kdist_them_bonus - kdist_us_bonus);

            //advancing bonus
            int advance_bonus = PP_ADVANCE[r];
            for (int r_to = r + 1; r_to <= 6; r_to++) {
                if (BIT(to) & sd->brd.bb[ALLPIECES]) {
                    break; //the path to promotion is blocked by something
                }

                //calculate attack mask, temporarily unset the sq bit to include x-ray attacks
                sd->brd.bb[ALLPIECES] ^= BIT(sq); //
                U64 attacks = sd->brd.attacks_to(to);
                sd->brd.bb[ALLPIECES] ^= BIT(sq);

                //get defenders (them) and supporters (us)
                U64 defend = attacks & sd->brd.all(them);

                if (defend) {
                    U64 support = attacks & sd->brd.all(us);
                    if (support == 0 || (is_1(support) && gt_1(defend))) {
                        break; //passed pawn can't safely advance
                    }
                }
                result->add(advance_bonus);
                trace("PP Advance", sq, advance_bonus);
                to += step;
                advance_bonus = advance_bonus / 2;
            }
        }

        if (material::has_imbalance(sd, them)) {
            if (material::has_major_imbalance(sd)) {
                result->mul256(196);
            } else {
                result->mul256(232);
            }
        }
        return result;
    }
}