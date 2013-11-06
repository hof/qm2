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
        std::cout << "\ndepth | move | pv | cut | all | pv+ | cut+ | all+" << std::endl;
        for (int move = 1; move <= 60; move++) {
            std::cout << std::setw(5) << depth << " | ";
            std::cout << std::setw(4) << move << " | ";
            std::cout << std::setw(2) << s->LMR[PVNODE][move][depth][0] << " | ";
            std::cout << std::setw(3) << s->LMR[CUTNODE][move][depth][0] << " | ";
            std::cout << std::setw(3) << s->LMR[ALLNODE][move][depth][0] << " | ";
            std::cout << std::setw(3) << s->LMR[PVNODE][move][depth][1] << " | ";
            std::cout << std::setw(4) << s->LMR[CUTNODE][move][depth][1] << " | ";
            std::cout << std::setw(3) << s->LMR[ALLNODE][move][depth][1] << std::endl;
            if (move > 5) {
                move += 5;
            }
        }
        if (depth > 5) {
            depth += 5;
        }
    }
    return (EXIT_SUCCESS);
}

