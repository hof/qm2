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
    engine->gameSettings.maxDepth = 20;
    TOutputHandler oh;

    
    engine->setOutputHandler(&oh);
    //engine->newGame("1r4k1/2Q2pp1/4p3/4Pn1p/qr1PR3/5BPP/P2R2K1/8 b - - 2 30");
    //engine->newGame("3R4/k7/2K5/8/8/8/5b2/8 w - - 79 111");   
    engine->newGame("1R6/1brk2p1/4p2p/p1P1Pp2/P7/6P1/1P4P1/2R3K1 w - - 0 1");
    engine->analyse();

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

