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
 * File:  genKPK.cpp
 * Generates KPK bitbase
 *
 * Created on 04 aug 2014, 11:08
 * 
 * @Credits This code is inspired on the KPK generator in Glaurung by Tord Romstad.
 */

#include <stdlib.h>
#include <iostream>
#include <cmath>
#include <iomanip>  //setw
#include "bbmoves.h"
#include "board.h"
#include "movegen.h"

namespace kpk_bitbase {

    // The possible pawns squares are 24, the first 4 files and ranks from 2 to 7
    const int MAX_IDX = 24 * 64; // psq * wksq = 1536

    // Each entry stores results of 64 positions, one per bit (the black king square)
    U64 KPK_BB[2][MAX_IDX]; //wtm, psq + wksq (24Kb)

    /**
     * Calculate table index
     * 
     * @return {int} table index (range: 0 .. MAX_IDX) 
     * 
     * | 0-5        | 6-7            | 8-10
     * | wk (0..63) | wp file (0..3) | wp rank (0..5)
     */
    inline int index(int wk, int wp) {
        int result = wk + (FILE(wp) << 6) + ((RANK(wp) - 1) << 8);
        assert(result >= 0 && result < MAX_IDX);
        assert(FILE(wp) <= 3);
        assert(RANK(wp) <= 6);
        return result;
    }

    enum result_t {
        UNKNOWN = 0,
        DRAW = 1,
        WIN = 2,
        INVALID = 3
    };

    uint8_t eval(bool wtm, int wk, int bk, int wp) {

        /*
         * 1. Verify the position is legal
         */
        if (wk == bk || wk == wp || bk == wp) {
            return INVALID;
        }
        if (distance(wk, bk) == 1) {
            return INVALID; //king captures king
        }
        if (wtm && (WPAWN_CAPTURES[wp] & BIT(bk)) != 0) {
            return INVALID; //wp captures bk
        }

        /*
         * 2. White: pawn promotes: win, stalemate: draw
         */
        if (wtm) {
            int tsq = wp + 8;
            if (tsq == wk || tsq == bk) { //pawn is blocked
                U64 king_moves_w = KING_MOVES[wk] & ~KING_MOVES[bk] & ~BIT(wp);
                if (king_moves_w == 0) {
                    return DRAW; //stalemate
                }
                return UNKNOWN;
            }
            if (wp < a7) {
                return UNKNOWN;
            }
            if ((KING_MOVES[wk] & BIT(tsq)) != 0 || (KING_MOVES[bk] & BIT(tsq)) == 0) {
                return WIN; //pawn can promote without being captured
            }
            return UNKNOWN;
        }

        /*
         * 3. Black: if it's a stalemate or the pawn can be captured it's a draw
         */
        U64 attacks_w = KING_MOVES[wk] | WPAWN_CAPTURES[wp];
        U64 king_moves_bk = KING_MOVES[bk] & ~attacks_w;
        if (king_moves_bk == 0) {
            return DRAW; //stalemate
        }
        if ((KING_MOVES[bk] & BIT(wp)) != 0 && (attacks_w & BIT(wp)) == 0) {
            return DRAW; //bk captures wp
        }
        return UNKNOWN;
    }

    uint8_t classify_w(int wk, int bk, int wp, uint8_t t[2][64][MAX_IDX]) {
        assert(wk != wp && distance(wk, bk) > 1);
        bool draw = true;
        int tsq = wp + 8;
        do { //pawn moves
            if (tsq == wk || tsq == bk || tsq >= a8) {
                break;
            }
            int next = t[BLACK][bk][index(wk, tsq)];
            assert(next != INVALID);
            if (next == WIN) {
                return WIN;
            }
            draw &= next == DRAW;
            if (tsq >= a4) {
                break;
            }
            tsq += 8; //double pawn push
        } while (true);
        U64 king_moves_w = KING_MOVES[wk] & ~(KING_MOVES[bk] | BIT(wp));
        while (king_moves_w) {
            tsq = pop(king_moves_w);
            assert(tsq != wp && distance(tsq, bk) > 1);
            int next = t[BLACK][bk][index(tsq, wp)];
            assert(next != INVALID);
            if (next == WIN) {
                return WIN;
            }
            draw &= next == DRAW;
        }
        return draw ? DRAW : UNKNOWN;
    }

    uint8_t classify_b(int wk, int bk, int wp, uint8_t t[2][64][MAX_IDX]) {
        assert(wk != wp && distance(wk, bk) > 1);
        bool win = true;
        int idx = index(wk, wp);
        U64 attacks_w = KING_MOVES[wk] | WPAWN_CAPTURES[wp];
        U64 king_moves_b = KING_MOVES[bk] & ~attacks_w;
        assert(king_moves_b != 0);
        while (king_moves_b) {
            int tsq = pop(king_moves_b);
            assert(tsq != wp && tsq != wk && distance(wk, tsq) > 1);
            int next = t[WHITE][tsq][idx];
            assert(next != INVALID);
            if (next == DRAW) {
                return DRAW;
            }
            win &= next == WIN;
        }
        return win ? WIN : UNKNOWN;
    }
    
    bool probe(bool wtm, int wk, int bk, int wp) {
        if (FILE(wp) <= 3) {
            return KPK_BB[wtm][index(wk, wp)] & BIT(bk);
        }
        return KPK_BB[wtm][index(wk ^ 7, wp ^ 7)] & BIT(bk ^ 7);
    }

    void init() {
        bool success;
        uint8_t temp[2][64][MAX_IDX];
        int step = 0;
        int unknowns;
        do {
            unknowns = 0;
            success = false;
            //iterate though all possible positions
            for (int wk = a1; wk <= h8; wk++) {
                for (int bk = a1; bk <= h8; bk++) {
                    for (int wp = a2; wp <= h7; wp++) {
                        if (FILE(wp) > 3) {
                            continue;
                        }
                        int idx = index(wk, wp);
                        uint8_t * result_w = &temp[WHITE][bk][idx];
                        uint8_t * result_b = &temp[BLACK][bk][idx];
                        if (step == 0) { //initialize
                            KPK_BB[WHITE][idx] = 0;
                            KPK_BB[BLACK][idx] = 0;
                            *result_w = eval(WHITE, wk, bk, wp);
                            *result_b = eval(BLACK, wk, bk, wp);
                            success = true;
                        } else {
                            if (*result_w == UNKNOWN) {
                                unknowns++;
                                *result_w = classify_w(wk, bk, wp, temp);
                                success |= *result_w != UNKNOWN;
                            }
                            if (*result_b == UNKNOWN) {
                                unknowns++;
                                *result_b = classify_b(wk, bk, wp, temp);
                                success |= *result_b != UNKNOWN;
                            }
                        }
                        if (*result_w == WIN) {
                            KPK_BB[WHITE][idx] |= BIT(bk);
                        }
                        if (*result_b == WIN) {
                            KPK_BB[BLACK][idx] |= BIT(bk);
                        }
                    }
                }
            }
            step++;
        } while (success);
        std::cout << "Generated KPK bitbase in " << (step - 1) << " steps\n";
        std::cout << "Unknown classifications: " << unknowns;
        std::cout << "\nTesting (K,P,K) = (h3, b7, h2): WIN=" << probe(WHITE, h3, b7, h2);
    }
}

int main(int argc, char** argv) {
    magic::init();
    kpk_bitbase::init();
    
    std::cout << "\nconst U64 KPK_BB[2][" << kpk_bitbase::MAX_IDX << "] = {";
    for (int color = BLACK; color <= WHITE; color++) {
        std::cout << "\n    {";
        for (int idx = 0; idx < kpk_bitbase::MAX_IDX; idx++) {
            if (idx % 4 == 0) {
                std::cout << "\n        ";
            }
            std::cout << "C64(0x";
            //C64(0x0000000000000302)
            std::cout << std::setfill('0') << std::setw(16) << std::hex << (uint64_t)kpk_bitbase::KPK_BB[color][idx];
            std::cout << ")";
            if (idx < kpk_bitbase::MAX_IDX-1) {
                std::cout << ", ";
            } 
        }
        std::cout << "\n    }";
        if (color == BLACK) {
            std::cout << ", ";
        }
        std::cout << std::endl;
    }
    std::cout << "};\n";
    
    return (EXIT_SUCCESS);
}