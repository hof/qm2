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
 * File: eval_pieces.cpp
 * Piece (knight, bishop, rook, queen) evaluation functions for normal chess
 */

#include "eval_pieces.h"
#include "search.h"
#include "bits.h"
#include "score.h"
#include "eval.h"

namespace pieces {

    //#define ENABLE_PIECE_EVAL_TRACE

#ifdef ENABLE_PIECE_EVAL_TRACE

    void trace(std::string msg, int sq, const score_t * s) {
        if (sq >= 0) {
            std::cout << msg << " " << FILE_SYMBOL(sq) << RANK_SYMBOL(sq) << ": ";
        } else {
            std::cout << msg << ": ";
        }
        std::cout << "(" << s->mg << ", " << s->eg << ") " << std::endl;
    }
#endif

#ifndef ENABLE_PIECE_EVAL_TRACE 
#define trace(a,b,c) /* notn */
#endif

    const int8_t ATTACK_WEIGHT[BKING + 1] = {
        //  x, p, n, b, r, q, k, p, n, b, r, q, k
        /**/0, 0, 3, 3, 5, 9, 0, 0, 3, 3, 5, 9, 0
    };

    const int8_t ATTACK_WEIGHT_SQUARE = 3;
    
    const int8_t MOBILITY[32] = {
        -50, -30, -20, -10, -5, 0, 0, 5,
        5, 5, 10, 10, 10, 10, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15
    };

    const int8_t ATTACKS[8] = {
        -5, 0, 5, 10, 10, 10, 10, 10
    };

    const int8_t ATTACKED[BKING + 1] = {
        0, 0, -30, -30, -50, -90, 0, 0, -30, -30, -50, -90, 0
    };

    const uint8_t OUTPOST[2][64] = {
        { //bishop
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 5, 5, 5, 5, 5, 5, 0,
            0, 5, 5, 10, 10, 5, 5, 0,
            0, 0, 5, 5, 5, 5, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0 //a1..h1
        },
        { //knight
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 5, 5, 0, 0, 0,
            0, 0, 10, 20, 20, 10, 0, 0,
            0, 0, 5, 10, 10, 5, 0, 0,
            0, 0, 0, 5, 5, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0 //a1..h1
        }
    };

    const score_t BLOCKED_CENTER_PAWN[2] = {//them, us
        S(10, 0), S(-10, 0)
    };

    const score_t VBISHOPPAIR = S(30, 50);
    const score_t DEFENDED = S(5, 0);
    const score_t ROOK_7TH = S(20, 30);
    const score_t SEMIOPEN_FILE = S(5, 0);
    const score_t OPEN_FILE = S(15, 5);
    const score_t CLOSED_FILE = S(-5, -5);
    const score_t SUPPORTED_PASSER = S(10, 20);
    const score_t CONNECTED_ROOKS(10, 20);
    const U64 PAT_BLOCKED_CENTER = BIT(d3) | BIT(e3) | BIT(d6) | BIT(e6);
    const U64 PAT_BACKRANKS[2] = {RANK_1 | RANK_2, RANK_7 | RANK_8};
    const U64 PAT_TRAPPED[2] = {(RANK_1 | RANK_2 | RANK_3) & EDGE, (RANK_6 | RANK_7 | RANK_8) & EDGE};
    const int8_t TRAPPED_PC = -35;

    /**
     * Piece evaluation routine
     * @param s search object
     * @return score_t * total piece evaluation score
     */
    score_t * eval(search_t * s) {
        score_t * result = &s->stack->pc_score[0];
        result->clear();
        board_t * brd = &s->brd;
        const bool equal_pawns = brd->ply > 0
                && brd->stack->pawn_hash == (brd->stack - 1)->pawn_hash
                && score::is_valid((s->stack - 1)->eval_result);
        const int prev_pc = equal_pawns ? (s->stack - 1)->current_move.capture : 0;
        const int prev_cap = equal_pawns ? (s->stack - 1)->current_move.piece : 0;
        pawn_table::entry_t * pi = s->stack->pt;
        const U64 occ = brd->pawns_kings();

        const int kpos[2] = {brd->get_sq(BKING), brd->get_sq(WKING)};
        const U64 king_zone[2] = {KING_ZONE[kpos[BLACK]], KING_ZONE[kpos[WHITE]]};

        s->stack->attack[BPAWN] = brd->pawn_attacks(BLACK);
        s->stack->attack[WPAWN] = brd->pawn_attacks(WHITE);
        s->stack->attack[BKING] = KING_MOVES[kpos[BLACK]];
        s->stack->attack[WKING] = KING_MOVES[kpos[WHITE]];

        for (int pc = WKNIGHT; pc <= BQUEEN; pc++) {

            /*
             * Iterate through all pieces, skipping kings and pawns
             */

            if (pc == WKING || pc == BPAWN) {
                continue;
            }

            /* 
             * If the evaluation was calculated on the previous ply and the board
             * "skeleton" of pawns and kings placement did not change, 
             * the score of the previous ply for this piece type can be re-used 
             * unless the piece moved or was captured. 
             */

            if (equal_pawns && pc != prev_pc && pc != prev_cap) {
                s->stack->pc_score[pc] = (s->stack - 1)->pc_score[pc];
                s->stack->king_attack[pc] = (s->stack - 1)->king_attack[pc];
                s->stack->attack[pc] = (s->stack - 1)->attack[pc];
                result->add_us(s->stack->pc_score[pc], pc <= WKING);
                assert(score::is_valid(s->stack->pc_score[pc].mg));
                assert(score::is_valid(s->stack->pc_score[pc].eg));
                assert(s->stack->king_attack[pc].mg >= 0);
                assert(s->stack->king_attack[pc].eg >= 0);
                continue;
            }

            /*
             * Calculate evaluation for this piece type
             */

            s->stack->pc_score[pc].clear();
            s->stack->king_attack[pc].clear();
            s->stack->attack[pc] = 0;

            if (brd->bb[pc] == 0) {
                continue;
            }

            bool us = pc <= WKING; //side to move
            score_t * sc = &s->stack->pc_score[pc];
            U64 bb_pc = brd->bb[pc];
            U64 moves;

            do {
                int sq = pop(bb_pc);
                bool defended = brd->is_attacked_by_pawn(sq, us);
                bool is_minor = false;
                U64 bsq = BIT(sq);

                /*
                 * Add piece square table score
                 */

                sc->add(PST::table[pc][sq]);
                trace("PST", sq, sc);

                /*
                 * Discourage blocking center pawns on e2, d2, e7 or d7
                 */

                if (bsq & PAT_BLOCKED_CENTER) {
                    if (sq == e3 && brd->matrix[e2] == WPAWN) {
                        sc->add(BLOCKED_CENTER_PAWN[us]);
                        trace("BLOCKED CENTER PAWN", sq, sc);
                    } else if (sq == d3 && brd->matrix[d2] == WPAWN) {
                        sc->add(BLOCKED_CENTER_PAWN[us]);
                        trace("BLOCKED CENTER PAWN", sq, sc);
                    } else if (sq == e6 && brd->matrix[e7] == BPAWN) {
                        sc->add(BLOCKED_CENTER_PAWN[!us]);
                        trace("BLOCKED CENTER PAWN", sq, sc);
                    } else if (sq == d6 && brd->matrix[d7] == BPAWN) {
                        sc->add(BLOCKED_CENTER_PAWN[!us]);
                        trace("BLOCKED CENTER PAWN", sq, sc);
                    }
                }

                /*
                 * Generate moves bitboard, only considering the board "skeleton"
                 * occupation of pawns and kings
                 */

                if (pc == WKNIGHT || pc == BKNIGHT) {
                    moves = KNIGHT_MOVES[sq];
                    is_minor = true;
                } else if (pc == WBISHOP || pc == BBISHOP) {
                    moves = magic::bishop_moves(sq, occ);
                    is_minor = true;
                } else if (pc == WROOK || pc == BROOK) {
                    moves = magic::rook_moves(sq, occ);
                } else {
                    assert(pc == WQUEEN || pc == BQUEEN);
                    moves = magic::queen_moves(sq, occ);
                }

                /*
                 * Mobility and activity
                 */

                const U64 safe_moves = moves & pi->mob[us];
                const int mob_cnt = popcnt0(safe_moves);
                sc->add(MOBILITY[mob_cnt]);
                trace("MOBILITY", sq, sc);
                sc->add(ATTACKS[popcnt0(safe_moves & pi->attack[us])]);
                trace("ATTACKS", sq, sc);
                s->stack->attack[pc] |= moves;


                /*
                 * Hanging piece
                 */

                if (brd->is_attacked_by_pawn(sq, !us)) {
                    sc->add(ATTACKED[pc]);
                    //todo: increase bonus if the piece is also pinned
                    trace("ATTACKED", sq, sc);
                }

                /*
                 * Trapped piece
                 */

                if (mob_cnt < 2 && !defended && (bsq & PAT_TRAPPED[us]) && max_1(moves & ~occ)) {
                    sc->add(TRAPPED_PC * (rank(sq, us) - 3));
                    trace("TRAPPED", sq, sc);
                }

                /*
                 * Minor piece on outpost
                 */

                if (defended && is_minor) {
                    sc->add(DEFENDED);
                    trace("DEFENDED", sq, sc);

                    if (brd->is_outpost(sq, us)) {
                        sc->add(OUTPOST[pc == KNIGHT[us]][ISQ(sq, us)]);
                        trace("OUTPOST", sq, sc);
                    }
                }

                /*
                 * Rooks
                 */

                if (pc == ROOK[us]) {

                    //Open / closed files
                    if (!pi->is_open_file(sq, us)) {
                        sc->add(CLOSED_FILE);
                        trace("CLOSED FILE", sq, sc);

                        //Rule of Tarrasch, support passed pawns
                        if (moves & pi->passers & fill_up(bsq, us)) {
                            sc->add(SUPPORTED_PASSER);
                            trace("SUPPORTED PASSER", sq, sc);
                        }
                    } else if (pi->is_open_file(sq, !us)) {
                        sc->add(OPEN_FILE);
                        trace("OPEN FILE", sq, sc);
                        if (moves & bb_pc & FILES[FILE(sq)]) {
                            sc->add(CONNECTED_ROOKS);
                            trace("CONNECTED ROOKS", sq, sc);
                        }
                    } else {
                        sc->add(SEMIOPEN_FILE);
                        trace("SEMIOPEN FILE", sq, sc);
                    }

                    //7th rank (if their king is on 7 or 8th rank)
                    if ((bsq & RANK[us][7]) && (brd->bb[KING[!us]] & PAT_BACKRANKS[us])) {
                        sc->add(ROOK_7TH);
                    }
                }

                /*
                 * King safety info 
                 */
                
                if (safe_moves & king_zone[!us]) {
                    s->stack->king_attack[pc].mg++;
                    s->stack->king_attack[pc].eg += ATTACK_WEIGHT[pc];
                    const int attacked_king_squares = popcnt0(safe_moves & s->stack->attack[KING[!us]]);
                    s->stack->king_attack[pc].eg += ATTACK_WEIGHT_SQUARE * attacked_king_squares;
                }


            } while (bb_pc);

            /*
             * Bishop pair (skipped in closed positions)
             */

            if (pc == BISHOP[us] && brd->has_bishop_pair(us)
                    && (pi->flags & pawn_table::FLAG_CLOSED_CENTER) == 0) {
                sc->add(VBISHOPPAIR);
                trace("BISHOP PAIR", -1, sc);
            }

            /*
             * Add score to the totals
             */

            result->add_us(sc, us);
        }
        return result;
    }
}