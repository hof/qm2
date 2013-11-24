/* 
 * File:   depth_test.cpp
 * Author: Hajewiet
 *
 * Created on 27-jul-2011, 22:11:29
 */

#include <stdlib.h>
#include <iostream>

#include "engine.h"
#include "evaluate.h"

/*
 * Simple C++ Test Suite
 */

void print_score(short score, int sq) {
    if (sq % 8 == 0) {
        std::cout << std::endl;
    }
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

void print_pct(TSCORE_PST & pct) {
    const std::string PIECE_NAME[13] = {"EMTPY", "White Pawn", "White Knight",
        "White Bishop", "White Rook", "White Queen", "White King", "Black Pawn",
        "Black Knight", "Black Bishop", "Black Rook", "Black Queen", "Black King"};

    for (int piece = 1; piece < 13; piece++) {
        std::cout << std::endl;
        std::cout << PIECE_NAME[piece] << " Middlegame:";
        for (int sq = a1; sq < 64; sq++) {
            print_score(pct[piece][FLIP_SQUARE(sq)].mg, sq);
        }
        std::cout << std::endl << std::endl;
        std::cout << PIECE_NAME[piece] << " Endgame:";
        for (int sq = a1; sq < 64; sq++) {
            print_score(pct[piece][FLIP_SQUARE(sq)].eg, sq);
        }
        std::cout << std::endl << std::endl;
    }
}

int main(int argc, char** argv) {
    InitMagicMoves();
    init_pct();
    print_pct(PST);
    return (EXIT_SUCCESS);
}

