/* 
 * File:   depth_test.cpp
 * Author: Hajewiet
 *
 * Created on 27-jul-2011, 22:11:29
 */

#include <stdlib.h>
#include <iostream>

#include "engine.h"

/*
 * Simple C++ Test Suite
 */

void print_score(short score, int sq, int avg) {
    if (sq % 8 == 0) {
        std::cout << std::endl;
    }
    int grain = 0xFFFFFFFF & ~((1 << 1) - 1);
    score = (score-avg) & grain;
    if (score >= 0 && score < 100) {
        std::cout << " ";
    }
    if (score >= 0 && score < 10) {
        std::cout << " "; //prefix
    }
    if (score < 0 && score > -10) {
        std::cout << " "; //prefix
    }
    if (score < 1000 && score > -100) {
        std::cout << " "; //prefix
    }
    std::cout << score;
    if (sq < 63) {
        std::cout << ", ";
    }
}

void print_scores(std::string piece, TScore scores[64], TScore avg) {
    std::cout << piece << std::endl;
    std::cout << "Middlegame" << std::endl;
    int a = MIN(avg.mg, avg.eg);
    for (int sq = a1; sq < 64; sq++) {
        print_score(scores[FLIP_SQUARE(sq)].mg, sq, a);
    }
    std::cout << std::endl << std::endl << "Endgame" << std::endl;
    for (int sq = a1; sq < 64; sq++) {
        print_score(scores[FLIP_SQUARE(sq)].eg, sq, a);
    }
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    InitMagicMoves();

    //calculate middle game pawn PCT
    TScore scores[64];
    TScore total;

    const short mobility_scale[2][64] = {
        {
            6, 7, 8, 9, 9, 8, 7, 6,
            5, 6, 7, 8, 8, 7, 6, 5,
            4, 5, 7, 7, 7, 7, 5, 4,
            3, 4, 6, 8, 8, 6, 4, 3,
            2, 3, 5, 7, 7, 5, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            1, 2, 3, 4, 4, 3, 2, 1
        },
        {
            4, 5, 7, 7, 7, 7, 5, 4,
            4, 5, 7, 7, 7, 7, 5, 4,
            3, 4, 5, 7, 7, 5, 4, 3,
            2, 3, 5, 7, 7, 5, 3, 2,
            2, 3, 5, 7, 7, 5, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            2, 3, 4, 5, 5, 4, 3, 2,
            1, 2, 3, 4, 4, 3, 2, 1
        }
    };
    
    total = 0;

    //Pawn
    
    for (int sq = a1; sq < 64; sq++) {
        scores[sq] = 0;
        U64 bbsq = BIT(sq);
        if ((bbsq & RANK_1) || (bbsq & RANK_8)) {
            continue;
        }
        if (bbsq & RANK_2) { //extra mobility
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(FILE(sq) + 16));
        }
        if (bbsq & (FILE_A | FILE_H)) {
            scores[sq] -= 10;
        }
        int rank = RANK(sq) - 1;
        scores[sq].eg += 2 * rank*rank; //moving pawns forward to promote

        if (sq == e2 || sq == d2) {
            scores[sq].mg -= 10;
        }
        if (sq == c2 || sq == f2) {
            scores[sq].mg -= 5;
        }
        if (bbsq & FRONTFILL(CENTER)) {
            scores[sq] += 5;
        }
        U64 caps = WPawnCaptures[sq] | WPawnMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        total += scores[sq];
    }
    print_scores("PAWN", scores, total/64);

    //Knight
    total = 0;
    for (int sq = a1; sq < 64; sq++) {
        scores[sq] = 0;
        U64 bbsq = BIT(sq);
        U64 caps = KnightMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq] *= 1.5; //mobility is extra important because knights move slow
        total += scores[sq];
    }
    print_scores("KNIGHT", scores, total/64);
    
    //Bishop
    total = 0;
    for (int sq = a1; sq < 64; sq++) {
        scores[sq] = 0;
        U64 bbsq = BIT(sq);
        U64 caps = BishopMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq] *= 0.5; //mobility is less important because bishops move fast
        total += scores[sq];
    }
    print_scores("BISHOP", scores, total/64);
    
    //Rook
    total = 0;
    for (int sq = a1; sq < 64; sq++) {
        scores[sq] = 0;
        U64 bbsq = BIT(sq);
        U64 caps = RookMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq] *= 0.5; //mobility is less important because rooks move fast
        
        if (bbsq & (RANK_7 | RANK_8)) {
            scores[sq] += 20; //extra bonus for 7th / 8th rank
            scores[sq].eg -= 10;
        }
        if (bbsq & (RANK_6)) {
            scores[sq].mg += 5; 
            scores[sq].eg += 2; 
        }
        total += scores[sq];
    }
    print_scores("ROOK", scores, total/64);

    
        
    //Queen
    total = 0;
    for (int sq = a1; sq < 64; sq++) {
        scores[sq] = 0;
        U64 bbsq = BIT(sq);
        U64 caps = QueenMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg *= 0.15; //mobility is less important because queens move fast
        scores[sq].eg *= 0.25; //mobility is less important because queens move fast
        total += scores[sq];
    }
    print_scores("QUEEN", scores, total/64);
    
    
    //King
    total = 0;
    for (int sq = a1; sq < 64; sq++) {
        scores[sq] = 0;
        U64 bbsq = BIT(sq);
        U64 caps = KingMoves[sq];
        while (caps) {
            int ix = POP(caps);
            scores[sq].add_ix64(&mobility_scale, FLIP_SQUARE(ix));
        }
        scores[sq].mg *= 0; 
        int protection = -20*RANK(sq);
        if (bbsq & CENTER) {
           protection -= 20;
        }
        if (bbsq & FILEFILL(CENTER)) {
           protection -= 10;
        }
        if (bbsq & LARGE_CENTER) {
           protection -= 10;
        }
        if (bbsq & FILEFILL(LARGE_CENTER)) {
           protection -= 5;
        }
        if (bbsq & EDGE) {
            protection += 5;
        }
        scores[sq].mg += protection;
    }
    print_scores("King", scores, total/64);

    return (EXIT_SUCCESS);
}

