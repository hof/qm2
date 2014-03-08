/* 
 * File:   depth_test.cpp
 * Author: Hajewiet
 *
 * Created on 27-jul-2011, 22:11:29
 */

#include <stdlib.h>
#include <iostream>
#include <iomanip>  //setw
#include <stdio.h>

#include "engine.h"
#include "search.h"
#include "defs.h"

/*
 * Simple C++ Test Suite
 */


int main(int argc, char** argv) {
    std::string fen = "";
    TSearch * s = new TSearch(fen.c_str(), NULL, NULL);
    for (int depth = 1; depth <= 60; depth++) {
        std::cout << "\ndepth | move | pv | nonpv " << std::endl;
        for (int move = 1; move <= 60; move++) {
            std::cout << std::setw(5) << depth << " | ";
            std::cout << std::setw(4) << move << " | ";
            std::cout << std::setw(2) << s->LMR[PVNODE][move][depth] << " | ";
            std::cout << std::setw(5) << s->LMR[0][move][depth] << std::endl;
            if (move > 9) {
                move += 9;
            }
        }
        if (depth > 9) {
            depth += 9;
        }
    }

    /* kingzone generator */
    for (int sq = 0; sq < 64; sq++) {
        if ((sq % 4) == 0) {
            std::cout << std::endl;
        }
        U64 zone = KingMoves[sq] | BIT(sq);
        U64 result = zone;
        int file = FILE(sq);
        int rank = RANK(sq);
        if (file > 0) {
           // result |= LEFT1(zone);
        }
        if (file < 7) {
          //  result |= RIGHT1(zone);
        }
        if (rank > 0) {
            result |= DOWN1(zone);
        }
        if (rank < 7) {
            result |= UP1(zone);
        }
        std::cout << "C64(0x" << std::setfill('0') << std::setw(16) << std::hex << result << "), ";  
    }
    /* */ 



    return (EXIT_SUCCESS);
}

