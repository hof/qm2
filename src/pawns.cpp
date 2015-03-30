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
 * File: endgame.cpp
 * Endgame evaluation functions for normal chess
 */

#include "pawns.h"
#include "evaluate.h"
#include "search.h"
#include "bits.h"

//#define TRACE_EVAL

namespace pawns {

#ifdef TRACE_EVAL

    void trace_eval(std::string msg, int sq, const score_t & s) {
        std::cout << msg << " " << FILE_SYMBOL(sq) << RANK_SYMBOL(sq) << ": ";
        std::cout << "(" << s.mg << ", " << s.eg << ") " << std::endl;
    }
#endif

#ifndef TRACE_EVAL 
#define trace_eval(a,b,c) /* notn */
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
    U64 stack_hits = 0;
    U64 table_hits = 0;

    void print_stats() {
        double pct_1 = stack_hits / (1 + evals_done / 100);
        double pct_2 = table_hits / (1 + evals_done / 100);
        double pct_3 = (evals_done - stack_hits - table_hits) / (1 + evals_done / 100);
        std::cout << "info string stack: " << int(pct_1) << "%, " << "table: " << int(pct_2)
                << "%, " << "calc: " << int(pct_3) << "%\n";

    }

    score_t * eval(search_t * s) {

        /*
         * 1. If no pawn or king moved (the pawn hash is equal), 
         *    simply get the score and masks from the last stack record.  
         */

        evals_done++;
        if (s->stack->equal_pawns) {
            s->stack->pc_score[WPAWN].set((s->stack - 1)->pc_score[WPAWN]);
            s->stack->passers = (s->stack - 1)->passers;
            s->stack->king_attack[WPAWN] = (s->stack - 1)->king_attack[WPAWN];
            s->stack->king_attack[BPAWN] = (s->stack - 1)->king_attack[BPAWN];
            s->stack->pawn_flags = (s->stack - 1)->pawn_flags;
            stack_hits++;
            return &s->stack->pc_score[WPAWN];
        }

        /*
         * 2. Probe the hash table for the pawns and kings score
         */

        U64 passers;
        int flags;
        int king_attack[2];
        score_t pawn_score[2];
        board_t * brd = &s->brd;

        if (pawn_table::retrieve(brd->stack->pawn_hash, passers, pawn_score[WHITE], king_attack, flags)) {
            s->stack->passers = passers;
            s->stack->king_attack[BPAWN] = king_attack[BLACK];
            s->stack->king_attack[WPAWN] = king_attack[WHITE];
            s->stack->pawn_flags = flags;
            s->stack->pc_score[WPAWN].set(pawn_score[WHITE]);
            table_hits++;
            return &s->stack->pc_score[WPAWN];
        }


        /*
         * 3. Loop through sides, calculating pawn and king score
         */

        passers = 0;
        flags = 0;
        int blocked_center_pawns = 0;
        king_attack[WHITE] = 0;
        king_attack[BLACK] = 0;
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
                trace_eval("PST", sq, PST[WPAWN][ISQ(sq, us)]);

                if (isolated) {
                    pawn_score[us].add(ISOLATED_PAWN[opposed]);
                    trace_eval("ISOLATED", sq, ISOLATED_PAWN[opposed]);
                } else if (weak) {
                    pawn_score[us].add(ISOLATED_PAWN[opposed]);
                    trace_eval("WEAK", sq, ISOLATED_PAWN[opposed]);
                }
                if (doubled) {
                    pawn_score[us].add(DOUBLED_PAWN);
                    trace_eval("DOUBLED", sq, DOUBLED_PAWN);
                }
                if (passed) {
                    passers |= bsq;
                    trace_eval("PASSED", sq, S(0, 0));
                } else if (candidate) {
                    pawn_score[us].add(CANDIDATE[r_us]);
                    trace_eval("CANDIDATE", sq, CANDIDATE[r_us]);
                }
                if (blocked && (bsq & CENTER)) {
                    blocked_center_pawns++;
                }

                //update attack units (storm and shelter pawns)
                if (BIT(sq) & kzone[them]) {
                    king_attack[us] += STORM_PAWN[ISQ(sq, us)];
                    trace_eval("STORM UNITS (us)", sq, S(STORM_PAWN[ISQ(sq, us)], 0));
                }
                if (BIT(sq) & kzone[us]) {
                    king_attack[them] += SHELTER_PAWN[ISQ(sq, us)];
                    trace_eval("SHELTER UNITS", sq, S(SHELTER_PAWN[ISQ(sq, us)], 0));
                }
            }

            /*
             * b) King score
             */

            //piece square table
            pawn_score[us].add(PST[WKING][ISQ(kpos[us], us)]);
            trace_eval("PST KING", kpos[us], PST[WKING][ISQ(kpos[us], us)]);

            //threats: king attacking enemy pawns
            U64 king_atcks = KING_MOVES[kpos[us]] & s->stack->mob[us] & s->stack->attack[us];
            if (king_atcks) {
                pawn_score[us].add(KING_THREAT);
                trace_eval("KING ATTACK", kpos[us], KING_THREAT);
            }

            //attack units - king position
            king_attack[them] += SHELTER_KPOS[ISQ(kpos[us], us)];
            trace_eval("KING ATTACK UNITS (POS)", kpos[us], SHELTER_KPOS[ISQ(kpos[us], us)]);

            //attack units - castling options
            if (brd->can_castle_ks(us) && brd->good_shelter_ks(us)) {
                king_attack[them] += SHELTER_CASTLING_KINGSIDE;
                trace_eval("KING ATTACK UNITS (CASTLE KS)", kpos[us], SHELTER_CASTLING_KINGSIDE);
            } else if (brd->can_castle_qs(us) && brd->good_shelter_qs(us)) {
                king_attack[them] += SHELTER_CASTLING_QUEENSIDE;
                trace_eval("KING ATTACK UNITS (CASTLE QS)", kpos[us], SHELTER_CASTLING_QUEENSIDE);
            }

            //attack units - (half)open files
            U64 open_lines = fill_south(kzone[us]) & ~fill_south(pawns_us) & RANK_1;
            king_attack[them] += SHELTER_OPEN_FILES[popcnt0(open_lines)];
            trace_eval("KING ATTACK UNITS (OPEN FILES)", kpos[us], SHELTER_OPEN_FILES[popcnt0(open_lines)]);
            if (open_lines & (FILE_A | FILE_H)) {
                king_attack[them] += SHELTER_OPEN_EDGE_FILE;
                trace_eval("KING ATTACK UNITS (OPEN A/H FILE)", kpos[us], SHELTER_OPEN_EDGE_FILE);
            }
        }

        if (blocked_center_pawns >= 3) {
            flags |= PFLAG_CLOSED_CENTER;
            trace_eval("CLOSED CENTER FLAG", flags, flags);
        }

        trace_eval("WHITE ATTACKS UNITS (TOTAL", kpos[BLACK], king_attack[WHITE]);
        trace_eval("BLACK ATTACKS UNITS (TOTAL)", kpos[WHITE], king_attack[BLACK]);

        /*
         * 4. Persist pawn and king evaluation information on the stack and 
         * pawn hash table. Finally, return the total score from white's point of view
         */

        s->stack->king_attack[WPAWN] = king_attack[WHITE];
        s->stack->king_attack[BPAWN] = king_attack[BLACK];
        s->stack->pc_score[WPAWN].set(pawn_score[WHITE]);
        s->stack->pc_score[WPAWN].sub(pawn_score[BLACK]);
        s->stack->passers = passers;
        s->stack->pawn_flags = flags;
        pawn_table::store(brd->stack->pawn_hash, passers, s->stack->pc_score[WPAWN], king_attack, flags);
        return &s->stack->pc_score[WPAWN];
    }

}
