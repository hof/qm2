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
    TSCORE_PCT pct;
    TSearch * s = new TSearch(fen.c_str(), pct, NULL, NULL);
    s->initLMR();
    for (int depth = 1; depth <= 60; depth++) {
        std::cout << "\ndepth | move | pv | nonpv | pv+ | nonpv+" << std::endl;
        for (int move = 1; move <= 60; move++) {
            std::cout << std::setw(5) << depth << " | ";
            std::cout << std::setw(4) << move << " | ";
            std::cout << std::setw(2) << s->LMR[0][PVNODE][move][depth] << " | ";
            std::cout << std::setw(5) << s->LMR[0][0][move][depth] << " | ";
            std::cout << std::setw(3) << s->LMR[1][PVNODE][move][depth] << " | ";
            std::cout << std::setw(4) << s->LMR[1][0][move][depth]  << std::endl;
            if (move > 9) {
                move += 9;
            }
        }
        if (depth > 9) {
            depth += 9;
        }
    }
    return (EXIT_SUCCESS);
}

