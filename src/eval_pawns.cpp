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
#include "eval.h"
#include "search.h"
#include "bits.h"

//#define ENABLE_PAWN_EVAL_TRACE

namespace pawns {

#ifdef ENABLE_PAWN_EVAL_TRACE

    void trace(std::string msg, int sq, const score_t & s) {
        std::cout << msg << " " << FILE_SYMBOL(sq) << RANK_SYMBOL(sq) << ": ";
        std::cout << "(" << s.mg << ", " << s.eg << ") " << std::endl;
    }
#endif

#ifndef ENABLE_PAWN_EVAL_TRACE 
#define trace(a,b,c) /* notn */
#endif

    uint8_t PFLAG_CLOSED_CENTER = 1;

    /*
     * Pawn structure bonuses
     */

    const score_t ISOLATED_PAWN[2] = {S(-20, -20), S(-10, -20)}; //opposed, open file

    const score_t DOUBLED_PAWN = S(-10, -20);

    const score_t CANDIDATE[8] = {//rank
        S(0, 0), S(5, 10), S(5, 10), S(10, 20),
        S(15, 40), S(30, 65), S(0, 0), S(0, 0)
    };

    /*
     * King x pawns bonus
     */

    const score_t KING_THREAT = S(0, 10); //king attacking pawns

    /*
     * King shelter, storm and attack units for later use in king attack eval.
     */

    // attack units regarding king position
    const int8_t SHELTER_KPOS[64] = {
        9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9, 9, 9, 9, 9,
        6, 6, 7, 8, 8, 7, 6, 6,
        3, 3, 4, 5, 5, 4, 3, 3,
        1, 1, 3, 4, 4, 3, 1, 1,
        0, 0, 1, 3, 3, 1, 0, 0,
        0, 0, 1, 2, 2, 1, 0, 0 //a1..h1
    };

    // attack units for pawns in front of our king
    const int8_t SHELTER_PAWN[64] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -2, -2, -1, -1, -1, -1, -2, -2,
        -3, -4, -2, -1, -1, -2, -4, -3, //a1..h1
        0, 0, 0, 0, 0, 0, 0, 0
    };

    // attack units for pawns in front of their king
    const int8_t STORM_PAWN[64] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1,
        -2, -2, -2, -2, -2, -2, -2, -2,
        0, 0, 0, 0, 0, 0, 0, 0, //a1..h1
        0, 0, 0, 0, 0, 0, 0, 0
    };

    // attack units for having open files on our king
    const int8_t SHELTER_OPEN_FILES[4] = {//amount of open files
        -1, 2, 3, 4
    };

    //attack units for open file on the edge (e.g. open h-line)
    const int8_t SHELTER_OPEN_EDGE_FILE = 3;

    //attack units for having the option to safely castle kingside
    const int8_t SHELTER_CASTLING_KINGSIDE = -3;

    //attack units for having the right to safely castle queenside
    const int8_t SHELTER_CASTLING_QUEENSIDE = -2;

    const uint8_t castle_flags[2] = {0, 0};


    /**
     * Main pawns and kings evaluation function
     */

    U64 evals_done = 0;
    U64 table_hits = 0;

    void print_stats() {
        double pct_1 = table_hits / (1 + evals_done / 100);
        std::cout << "info string pawn table hits: " << int(pct_1) << "%\n";

    }

    score_t * eval(search_t * s) {

        /*
         * 1. Probe the pawn hash table for the pawns and kings score
         */

        evals_done++;
        board_t * brd = &s->brd;
        s->stack->pawn_info = pawn_table::retrieve(brd->stack->pawn_hash);
        pawn_table::entry_t * e = s->stack->pawn_info;
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
        const U64 kzone[2] = {KING_ZONE[kpos[0]], KING_ZONE[kpos[1]]};

        for (int us = BLACK; us <= WHITE; us++) {
            pawn_score[us].clear();
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
                bool blocked = (BIT(sq + step) & pawns_all) != 0;
                bool passed = !doubled && !opposed && 0 == (pawns_them & af & upward_ranks(r, us));
                bool defended = !isolated && (brd->pawn_attacks(sq, them) & pawns_us);
                bool attacking = !isolated && (brd->pawn_attacks(sq, us) & pawns_them);
                bool weak = !isolated && !defended && !attacking && !doubled && (r_us + !blocked) < 6;

                //weak pawns: not weak if it can quickly move to a safe square
                if (weak && !blocked) {
                    assert(r_us > 0 && r_us < 5);
                    int next_sq = sq + step;
                    for (int stp = 0; stp < 2 + (r_us == 1); stp++) {

                        //get pawn attacks and defenders of the next square
                        U64 defs = brd->pawn_attacks(next_sq, them) & pawns_us;
                        U64 atck = brd->pawn_attacks(next_sq, us) & pawns_them;
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
                pawn_score[us].add(PST[WPAWN][ISQ(sq, us)]);
                trace("PST", sq, PST[WPAWN][ISQ(sq, us)]);

                if (isolated) {
                    pawn_score[us].add(ISOLATED_PAWN[opposed]);
                    trace("ISOLATED", sq, ISOLATED_PAWN[opposed]);
                } else if (weak) {
                    pawn_score[us].add(ISOLATED_PAWN[opposed]);
                    trace("WEAK", sq, ISOLATED_PAWN[opposed]);
                }
                if (doubled) {
                    pawn_score[us].add(DOUBLED_PAWN);
                    trace("DOUBLED", sq, DOUBLED_PAWN);
                }
                if (passed) {
                    e->passers |= bsq;
                    trace("PASSED", sq, S(0, 0));
                } else if (candidate) {
                    pawn_score[us].add(CANDIDATE[r_us]);
                    trace("CANDIDATE", sq, CANDIDATE[r_us]);
                }
                if (blocked && (bsq & CENTER)) {
                    blocked_center_pawns++;
                }

                //update attack units (storm and shelter pawns)
                if (BIT(sq) & kzone[them]) {
                    e->king_attack[us] += STORM_PAWN[ISQ(sq, us)];
                    trace("STORM UNITS (us)", sq, S(STORM_PAWN[ISQ(sq, us)], 0));
                }
                if (BIT(sq) & kzone[us]) {
                    e->king_attack[them] += SHELTER_PAWN[ISQ(sq, us)];
                    trace("SHELTER UNITS", sq, S(SHELTER_PAWN[ISQ(sq, us)], 0));
                }
            }

            /*
             * b) King score
             */

            //piece square table
            pawn_score[us].add(PST[WKING][ISQ(kpos[us], us)]);
            trace("PST KING", kpos[us], PST[WKING][ISQ(kpos[us], us)]);

            //threats: king attacking enemy pawns
            U64 king_atcks = KING_MOVES[kpos[us]] & s->stack->mob[us] & s->stack->attack[us];
            if (king_atcks) {
                pawn_score[us].add(KING_THREAT);
                trace("KING ATTACK", kpos[us], KING_THREAT);
            }

            //attack units - king position
            e->king_attack[them] += SHELTER_KPOS[ISQ(kpos[us], us)];
            trace("KING ATTACK UNITS (POS)", kpos[us], SHELTER_KPOS[ISQ(kpos[us], us)]);

            //attack units - castling options
            if (brd->can_castle_ks(us) && brd->good_shelter_ks(us)) {
                e->king_attack[them] += SHELTER_CASTLING_KINGSIDE;
                trace("KING ATTACK UNITS (CASTLE KS)", kpos[us], SHELTER_CASTLING_KINGSIDE);
            } else if (brd->can_castle_qs(us) && brd->good_shelter_qs(us)) {
                e->king_attack[them] += SHELTER_CASTLING_QUEENSIDE;
                trace("KING ATTACK UNITS (CASTLE QS)", kpos[us], SHELTER_CASTLING_QUEENSIDE);
            }

            //attack units - (half)open files
            U64 open_lines = fill_south(kzone[us]) & ~fill_south(pawns_us) & RANK_1;
            e->king_attack[them] += SHELTER_OPEN_FILES[popcnt0(open_lines)];
            trace("KING ATTACK UNITS (OPEN FILES)", kpos[us], SHELTER_OPEN_FILES[popcnt0(open_lines)]);
            if (open_lines & (FILE_A | FILE_H)) {
                e->king_attack[them] += SHELTER_OPEN_EDGE_FILE;
                trace("KING ATTACK UNITS (OPEN A/H FILE)", kpos[us], SHELTER_OPEN_EDGE_FILE);
            }
        }

        if (blocked_center_pawns >= 3) {
            e->flags |= PFLAG_CLOSED_CENTER;
            trace("CLOSED CENTER FLAG", e->flags, e->flags);
        }

        //pawn span bonus for the endgame
        //bb_width();

        trace("WHITE ATTACKS UNITS (TOTAL", kpos[BLACK], e->king_attack[WHITE]);
        trace("BLACK ATTACKS UNITS (TOTAL)", kpos[WHITE], e->king_attack[BLACK]);

        /*
         * 4. Persist pawn and king evaluation information on the stack and 
         * pawn hash table. Finally, return the total score from white's point of view
         */

        e->score.set(pawn_score[WHITE]);
        e->score.sub(pawn_score[BLACK]);
        e->key = brd->stack->pawn_hash;
        return &e->score;
    }

    /**
     * Evaluate passed pawns
     * @param s search object with search stack
     * @param us side to move: white(1) or black(0)
     * @return score
     */

    const uint8_t PP_MG[6] = {5, 5, 15, 40, 80, 140};
    const uint8_t PP_EG[6] = {10, 10, 15, 25, 45, 65};
    const uint8_t PP_DIST_US[6] = {0, 0, 2, 6, 12, 20};
    const uint8_t PP_DIST_THEM[6] = {0, 0, 5, 15, 20, 40};

    score_t * eval_passed_pawns(search_t * sd, bool us) {


        score_t * result = &sd->stack->passer_score[us];
        result->clear();
        U64 passers = sd->stack->pawn_info->passers & sd->brd.bb[PAWN[us]];
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

            //stop here (with just the base bonus) if the pawn is on rank 2(r=0), 3(r=1), or 4(r=2))
            if (r < 3) {
                continue;
            }

            //initialize bonus and rank
            bonus.half();
            int to = sq + step;

            //king distance
            int kdist_us_bonus = distance(sd->brd.get_sq(KING[us]), to) * PP_DIST_US[r];
            int kdist_them_bonus = distance(sd->brd.get_sq(KING[them]), to) * PP_DIST_THEM[r];
            trace("PP King Distance (us)", sq, S(distance(sd->brd.get_sq(KING[us]), to), -kdist_us_bonus));
            trace("PP King Distance (them)", sq, S(distance(sd->brd.get_sq(KING[them]), to), kdist_them_bonus));
            trace("PP King Distance", sq, S(0, kdist_them_bonus - kdist_us_bonus));
            result->add(0, kdist_them_bonus - kdist_us_bonus);

            //connected and defended passers
            U64 bit_sq = BIT(sq);
            U64 connection_mask = RIGHT1(bit_sq) | LEFT1(bit_sq);
            if (connection_mask & sd->brd.bb[PAWN[us]]) {
                result->add(10, 10 + r * 10);
                trace("PP Connected", sq, S(10, 10 + r * 10));
            } else {
                bit_sq = BIT(sq + PAWN_DIRECTION[them]);
                connection_mask = RIGHT1(bit_sq) | LEFT1(bit_sq);
                if (connection_mask & sd->brd.bb[PAWN[us]]) {
                    result->add(5, 5 + r * 5);
                    trace("PP Connected", sq, S(5, 5 + r * 5));
                }
            }

            //advancing
            do {
                if (BIT(to) & sd->brd.bb[ALLPIECES]) {
                    break; //blocked
                }
                sd->brd.bb[ALLPIECES] ^= BIT(sq); //to include rook/queen xray attacks from behind
                U64 attacks = sd->brd.attacks_to(to);
                sd->brd.bb[ALLPIECES] ^= BIT(sq);
                U64 defend = attacks & sd->brd.all(them);
                U64 support = attacks & sd->brd.all(us);
                if (defend) {
                    if (support == 0) {
                        break;
                    }
                    if (is_1(support) && gt_1(defend)) {
                        break;
                    }
                }
                result->add(bonus);
                trace("PP Advance", sq, bonus);
                to += step;
            } while (to >= a1 && to <= h8);
        }
        if (has_imbalance(sd, them)) {
            if (has_major_imbalance(sd)) {
                result->mul256(128);
            } else {
                result->mul256(196);
            }
        }
        return result;
    }

}
