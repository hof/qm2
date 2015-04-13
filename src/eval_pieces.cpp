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
 * Piece (kight, bishop, rook, queen) evaluation functions for normal chess
 */

#include "eval_pieces.h"
#include "search.h"
#include "bits.h"
#include "score.h"

extern pst_t PST;

namespace pieces {

#define _ENABLE_PIECE_EVAL_TRACE

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

    const int8_t MOBILITY[32] = {
        -40, -20, -10, 0, 5, 10, 15, 20,
        20, 20, 20, 20, 20, 20, 20, 20,
        20, 20, 20, 20, 20, 20, 20, 20,
        20, 20, 20, 20, 20, 20, 20, 20
    };

    const int8_t ATTACKS[8] = {
        -5, 5, 10, 15, 20, 20, 20, 20
    };

    const int8_t ATTACKED[BKING + 1] = {
        0, 0, -30, -30, -40, -50, 0, 0, -30, -30, -40, -50, 0
    };

    const uint8_t OUTPOST[2][64] = {
        { //bishop
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 5, 5, 5, 5, 5, 5, 0,
            0, 5, 5, 10, 10, 5, 5, 0,
            0, 0, 5, 5, 5, 5, 0, 0,
            0, 0, 0, 5, 5, 0, 0, 0,
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

    const score_t BLOCKED_CENTER_PAWN[2] = {S(10, 0), S(-10, 0)};
    const int8_t VBISHOPPAIR = 50;
    const score_t TRAPPED_BISHOP = S(-60, -80);
    const score_t DEFENDED = S(5, 0);
    const score_t MAJOR_7TH[2] = {//rook, queen
        S(10, 20), S(5, 10)
    };

    const score_t SEMIOPEN_FILE[2] = {//rook, queen
        S(10, 10), S(0, 0)
    };

    const score_t OPEN_FILE[2] = {//rook, queen
        S(17, 17), S(5, 5)
    };

    const score_t CLOSED_FILE[2] = {//rook, queen
        S(-5, -5), S(0, 0)
    };
    
    const score_t SUPPORTED_PASSER[2] = { //rook, queen
        S(10, 20), S(5, 10)
    };

    const score_t CONNECTED_ROOKS(10, 20);
    const U64 PAT_BLOCKED_CENTER = BIT(d3) | BIT(e3) | BIT(d6) | BIT(e6);
    const U64 PAT_BACKRANKS[2] = {RANK_1 | RANK_2, RANK_7 | RANK_8};

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
        const int prev_pc = equal_pawns ? (s->stack - 1)->current_move.capture : EMPTY;
        const int prev_cap = equal_pawns ? (s->stack - 1)->current_move.piece : EMPTY;
        pawn_table::entry_t * pi = s->stack->pt;
        const int kpos[2] = {brd->get_sq(BKING), brd->get_sq(WKING)};
        const U64 occ = brd->pawns_kings();

        for (int pc = WKNIGHT; pc <= BQUEEN; pc++) {

            /*
             * Iterate through all pieces, skipping kings and pawns
             * 
             * If the evaluation was calculated on the previous ply and the board
             * "skeleton" of pawns and kings placement did not change, 
             * the score of the previous ply for this piece type can be re-used 
             * unless the piece moved or was captured. 
             * 
             */

            if (pc == WKING || pc == BPAWN) {
                continue;
            }

            if (equal_pawns && pc != prev_pc && pc != prev_cap) {
                s->stack->pc_score[pc] = (s->stack - 1)->pc_score[pc];
                s->stack->king_attack[pc] = (s->stack - 1)->king_attack[pc];
                assert(score::is_valid(s->stack->pc_score[pc]));
                assert(s->stack->king_attack[pc] >= 0 && s->stack->king_attack[pc] <= 127);
                result->add_us(s->stack->pc_score[pc], pc <= WKING);
                continue;
            }

            s->stack->king_attack[pc] = 0;
            s->stack->pc_score[pc].clear();

            if (brd->bb[pc] == 0) {
                continue;
            }

            bool us = pc <= WKING; //side to move
            score_t * sc = &s->stack->pc_score[pc];
            U64 bb_pc = brd->bb[pc];
            U64 moves;
            int ka_units = 0;
            int ka_squares = 0;
            do {
                int sq = pop(bb_pc);
                bool defended = brd->is_attacked_by_pawn(sq, us);
                bool is_minor = false;
                U64 bsq = BIT(sq);

                /*
                 * Add piece square table score
                 */

                sc->add(PST[pc][sq]);
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

                U64 safe_moves = moves & pi->mob[us];
                sc->add(MOBILITY[popcnt0(safe_moves)]);
                trace("MOBILITY", sq, sc);
                sc->add(ATTACKS[popcnt0(safe_moves & pi->attack[us])]);
                trace("ATTACKS", sq, sc);

                /*
                 * Hanging piece
                 */

                if (brd->is_attacked_by_pawn(sq, !us)) {
                    sc->add(ATTACKED[pc]);
                    trace("ATTACKED", sq, sc);
                }
                
                /*
                 * Minor pieces on outpost
                 */

                if (defended && is_minor) {
                    sc->add(DEFENDED);
                    trace("DEFENDED", sq, sc);
                    if (brd->is_outpost(sq, us)) {
                        sc->add(OUTPOST[pc == KNIGHT[us]][ISQ(sq, us)]);
                        trace("OUTPOST", sq, sc);
                    }
                }

                if (!is_minor) {
                    
                    bool ix = pc == QUEEN[us];

                    /*
                     * Major pieces on open files
                     */

                    if (!pi->is_open_file(sq, us)) {
                        sc->add(CLOSED_FILE[ix]);
                        trace("CLOSED FILE", sq, sc);
                        
                        //Rule of Tarrasch, support passed pawns
                        if (moves & pi->passers & fill_up(bsq, us)) {
                            sc->add(SUPPORTED_PASSER[ix]);
                            trace("SUPPOERTED PASSER", sq, sc);
                        }
                        
                    } else if (pi->is_open_file(sq, !us)) {
                        sc->add(OPEN_FILE[ix]);
                        trace("OPEN FILE", sq, sc);
                        if (pc == ROOK[us] && moves & bb_pc & FILES[FILE(sq)]) {
                            sc->add(CONNECTED_ROOKS);
                            trace("CONNECTED ROOKS", sq, sc);
                        }
                    } else {
                        sc->add(SEMIOPEN_FILE[ix]);
                        trace("SEMIOPEN FILE", sq, sc);
                    }

                    /*
                     * Major pieces on 7th rank
                     */

                    if ((bsq & RANK[us][7]) && (brd->bb[KING[!us]] & PAT_BACKRANKS[us])) {
                        sc->add(MAJOR_7TH[ix]);
                    }
                }

                /*
                 * King attacks info
                 */

                int king_attacks = popcnt0(moves & KING_ZONE[kpos[!us]]);
                ka_units += (king_attacks > 0);
                ka_squares += king_attacks;

            } while (bb_pc);

            /*
             * Bishop pair (skipped in closed positions)
             */

            if ((pc == WBISHOP || pc == BBISHOP) && brd->has_bishop_pair(us)
                    && (pi->flags & pawn_table::FLAG_CLOSED_CENTER) == 0) {
                sc->add(VBISHOPPAIR);
                trace("BISHOP PAIR", -1, sc);
            }

            /*
             * Add score and attack info to the totals
             */

            s->stack->king_attack[pc] = KA_ENCODE(ka_units, ka_squares);
            result->add_us(sc, us);
        }
        return result;
    }

    /*int eval_mate_threat_q(search_t * s, const U64 attacks_us, const int kpos_them, const bool us) {
        U64 kpos_bit = BIT(kpos_them);
        if ((kpos_bit & EDGE) == 0) {
            return 0;
        }
        U64 mate_squares = 0;
        if (kpos_bit & CORNER) {
            mate_squares = KING_MOVES[kpos_them];
        } else if (kpos_bit & RANK_1) {
            mate_squares = UP1(kpos_bit);
        } else if (kpos_bit & RANK_8) {
            mate_squares = DOWN1(kpos_bit);
        } else if (kpos_bit & FILE_A) {
            mate_squares = RIGHT1(kpos_bit);
        } else if (kpos_bit & FILE_H) {
            mate_squares = LEFT1(kpos_bit);
        }
        U64 target = mate_squares & attacks_us;
        if (target == 0) {
            return 0;
        }
        board_t * brd = &s->brd;
        bool them = !us;
        int result = 10;
        do {
            int sq = pop(target);
            if (brd->is_attacked_excl_queen(sq, us)) {
                result += 10;
                if (!brd->is_attacked_excl_king(sq, them)) {
                    return 200;
                }
            }
        } while (target);
        return result;
    }*/
}
/*

    //patterns
    if (BIT(sq) & BISHOP_PATTERNS[us]) {
        if (us == WHITE) {
           if ((sq == h7 && brd->matrix[g6] == BPAWN && brd->matrix[f7] == BPAWN)
                    || (sq == a7 && brd->matrix[b6] == BPAWN && brd->matrix[c7] == BPAWN)) {
                result->add(TRAPPED_BISHOP);
            }
        } 
        
    }
    
        //trapped rook pattern
        if (bitSq & ROOK_PATTERNS[us]) {
            int kpos_us = brd->get_sq(KING[us]);
            if (us == WHITE && (kpos_us == g1 || kpos_us == h1 || kpos_us == f1)
                    && (sq == h1 || sq == g1 || sq == h2 || sq == g2)) {
                result->add(TRAPPED_ROOK);
            } else if (us == WHITE && (kpos_us == a1 || kpos_us == b1 || kpos_us == c1)
                    && (sq == a1 || sq == b1 || sq == a2 || sq == b2)) {
                result->add(TRAPPED_ROOK);
            } else if (us == BLACK && (kpos_us == g8 || kpos_us == h8 || kpos_us == f8)
                    && (sq == h8 || sq == g8 || sq == h7 || sq == g7)) {
                result->add(TRAPPED_ROOK);
            } else if (us == BLACK && (kpos_us == a8 || kpos_us == b8 || kpos_us == c8)
                    && (sq == a8 || sq == b8 || sq == a7 || sq == b7)) {
                result->add(TRAPPED_ROOK);
            }
        }
    }
}


}
 */ 