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
    engine->gameSettings.maxDepth = 1;
    TOutputHandler oh;

    
    engine->setOutputHandler(&oh);
    //engine->newGame("1r4k1/2Q2pp1/4p3/4Pn1p/qr1PR3/5BPP/P2R2K1/8 b - - 2 30");
    //engine->newGame("r1bq1rk1/1p2bppp/p1n1p3/8/2QP4/2N1BN1P/PP3PP1/R4RK1 w - - 4 14");
    engine->newGame("r2qkbnr/pp1np1pp/8/1Bp2b2/8/5N2/PPP2PPP/RNBQK2R w KQkq - 0 7");
    
    engine->analyse();
    
    engine->newGame("r2qkbnr/1p1np1pp/p7/1Bp2b2/8/5N2/PPP2PPP/RNBQ1RK1 w kq - 0 8");
    
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

