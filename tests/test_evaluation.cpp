/* 
 * File:   evaluation_test.cpp
 * Author: Hajewiet
 *
 * Created on 31-mei-2011, 16:39:55
 */

#include <stdlib.h>
#include <iostream>
#include <cmath>
#include "bbmoves.h"
#include "board.h"
#include "movegen.h"
#include "engine.h"

/*
 * Simple C++ Test Suite
 */

void test1() {
    std::cout << "evaluation_test test 1" << std::endl;

    InitMagicMoves();
    TEngine * engine = new TEngine();
    THashTable *globalHashTable = new THashTable(128);
    engine->setHashTable(globalHashTable);
    engine->gameSettings.maxNodes = 5000000;
    TOutputHandler oh;

    
    engine->setOutputHandler(&oh);
    engine->newGame("1r4k1/2Q2pp1/4p3/4Pn1p/qr1PR3/5BPP/P2R2K1/8 b - - 2 30");
    
    engine->analyse();
    engine->think();

    delete engine;
    delete globalHashTable;
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% evaluation_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% test1 (evaluation_test)" << std::endl;
    test1();
    std::cout << "%TEST_FINISHED% time=0 test1 (evaluation_test)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;
    
    return (EXIT_SUCCESS);
}

