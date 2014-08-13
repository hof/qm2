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
 * File:  genPST.cpp
 * Generates Piece Square Tables
 *
 * Created on 02 aug 2014, 11:08
 */

#include <stdlib.h>
#include <iostream>
#include <cmath>
#include <iomanip>  //setw
#include "board.h"

pst_t PST;

void savePST(score_t scores[], int wpiece) {
    /*  tuned multipliers:        X    P    N    B    R    Q    K  */
    const double PCMUL_MG[7] = {0.0, 0.6, 1.0, 0.6, 1.0, 1.0, 1.0};
    const double PCMUL_EG[7] = {0.0, 1.0, 1.0, 0.6, 1.0, 1.0, 1.0};
    int tot_mg = 0;
    int tot_eg = 0;
    int count = 0;
    for (int sq = a1; sq <= h8; sq++) {
        if (wpiece == WPAWN && (sq < a2 || sq > h7)) {
            continue;
        }
        tot_mg += scores[sq].mg;
        tot_eg += scores[sq].eg;
        count++;
    }
    int avg_mg = tot_mg / count;
    int avg_eg = tot_eg / count;
    for (int sq = a1; sq <= h8; sq++) {
        if (wpiece == WPAWN && (sq < a2 || sq > h7)) {
            scores[sq].clear();
        } else {
            scores[sq].mg -= avg_mg;
            scores[sq].eg -= avg_eg;
            scores[sq].mg *= PCMUL_MG[wpiece];
            scores[sq].eg *= PCMUL_EG[wpiece];
            scores[sq].round();
        }
        int isq = FLIP_SQUARE(sq);
        PST[wpiece][isq].set(scores[sq]);
    }
}

void initPST() {
    score_t scores[64];
    const score_t mobility_weight[64] = {
        S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2),
        S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2),
        S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2), S(2, 2),
        S(2, 2), S(2, 2), S(2, 2), S(3, 2), S(3, 2), S(2, 2), S(2, 2), S(2, 2),
        S(1, 2), S(1, 2), S(1, 2), S(2, 2), S(2, 2), S(1, 2), S(1, 2), S(1, 2),
        S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2),
        S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2),
        S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2), S(1, 2)
    };
    const int ROOK_FILE_BONUS[8] = {-4, -4, 0, 4, 4, 0, -4, -4};
    const int PAWN_FILE[8] = {-15, -5, 0, 10, 10, 0, -5, -15};
    const int PROGRESS[8] = {-3, -2, -1, 0, 1, 2, 1, 0};
    const int KNIGHT_RANK[8] = {-2, -1, 0, 0, 1, 2, 0, -1};

    //Pawn
    for (int sq = a1; sq <= h8; sq++) {
        scores[sq].clear();
        U64 bbsq = BIT(sq);
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = WPAWN_CAPTURES[sq];
        while (caps) {
            int ix = pop(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        scores[sq].mul(8);
        scores[sq].mg += PAWN_FILE[FILE(sq)];
        if (bbsq & CENTER) {
            scores[sq].mg += 5;
        }
        scores[sq].eg = 0;
    }
    savePST(scores, WPAWN);

    //Knight
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = KNIGHT_MOVES[sq];
        while (caps) {
            int ix = pop(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        scores[sq].mul(3); //mobility is extra important because knights move slow
        scores[sq].add(KNIGHT_RANK[RANK(sq)]);
    }
    savePST(scores, WKNIGHT);

    //Bishop
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = BISHOP_MOVES[sq];
        while (caps) {
            int ix = pop(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        if (sq == g2 || sq == b2) {
            scores[sq].mg += 2;
        }
        scores[sq].mul(3);
    }
    savePST(scores, WBISHOP);

    //Rook
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].mg += ROOK_FILE_BONUS[FILE(sq)];
    }
    savePST(scores, WROOK);

    //Queen
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = QUEEN_MOVES[sq];
        while (caps) {
            int ix = pop(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        scores[sq].mg = ROOK_FILE_BONUS[FILE(sq)] / 2 + PROGRESS[RANK(sq)];
        scores[sq].mg += sq <= h1 ? -4 : 0;
    }
    savePST(scores, WQUEEN);

    //King
    for (int sq = a1; sq < 64; sq++) {
        scores[sq].clear();
        scores[sq].add(mobility_weight[FLIP_SQUARE(sq)]);
        U64 caps = KING_MOVES[sq];
        while (caps) {
            int ix = pop(caps);
            scores[sq].add(mobility_weight[FLIP_SQUARE(ix)]);
        }
        if (BIT(sq) & LARGE_CENTER) {
            scores[sq].eg += 4;
        }
        if (BIT(sq) & CENTER) {
            scores[sq].eg += 5;
        }
        if (BIT(sq) & EDGE) {
            scores[sq].eg -= 2;
        }
        scores[sq].eg += PROGRESS[RANK(sq)];
        scores[sq].mg = 0;
        scores[sq].eg *= 3;
    }
    savePST(scores, WKING);
}

int main(int argc, char** argv) {
    magic::init();
    initPST();
    
    std::string PIECE_NAME[WKING + 1] = {"", "PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING"};
    std::string PHASE_NAME[2] = {"MG", "EG"};
    for (int phase = 0; phase <= 1; phase++) {
        for (int pc = WPAWN; pc <= WKING; pc++) {
            std::cout << std::endl << "int8_t PST_" << PIECE_NAME[pc] << "_" << PHASE_NAME[phase] << "[64] = {" << std::endl;
            for (int sq = 0; sq < 64; sq++) {
                int file = FILE(sq);
                if (file == 0) {
                    std::cout << "   ";
                }
                int score = phase == 0 ? PST[pc][sq].mg : PST[pc][sq].eg;
                std::cout << std::setw(3) << score;
                if (sq < h8) {
                    std::cout << ", ";
                }
                if (file == 7) {
                    std::cout << std::endl;
                }
            }
            std::cout << "};" << std::endl;
        }
    }
    return (EXIT_SUCCESS);
}

