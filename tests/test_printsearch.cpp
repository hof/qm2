/* 
 * File:   test_printsearch.cpp
 * Author: Twin
 *
 * Created on 18-sep-2011, 13:09:00
 */

#include <stdlib.h>
#include <iostream>

#include "engine.h"

/*
 * Simple C++ Test Suite
 */

void test_printsearch(TEngine * engine) {
    std::cout << "test_printsearch test_printsearch" << std::endl;
    engine->newGame("2kr3r/pp1q1ppp/5n2/1Nb5/2Pp1B2/7Q/P4PPP/1R3RK1 w - - 0 1");
    engine->gameSettings.maxDepth = 7;
#ifdef PRINTSEARCH
    engine->gameSettings.examineSearch = 3;
    engine->gameSettings.examinePath[0].setMove(WKNIGHT, b5, a7);
    engine->gameSettings.examinePath[1].setMove(BBISHOP, c5, a7);
    engine->gameSettings.examinePath[2].setMove(WQUEEN, h3, a3);
#endif
    engine->think();
}

int main(int argc, char** argv) {
    InitMagicMoves();
    TOutputHandler oh;
    TEngine * engine = new TEngine();
    THashTable *globalHashTable = new THashTable(256);
    engine->setHashTable(globalHashTable);
    engine->setOutputHandler(&oh);

    clock_t begin;
    clock_t now;
    begin = clock();

    std::cout << "%SUITE_STARTING% test_printsearch" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% test_printsearch (test_printsearch)\n" << std::endl;
    test_printsearch(engine);
    now = clock();
    int elapsed = (now - begin) / CLOCKS_PER_SEC;

    delete engine;
    delete globalHashTable;

    std::cout << "%TEST_FINISHED% time=" << elapsed << " test_printsearch (test_printsearch)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << elapsed << std::endl;

    return (EXIT_SUCCESS);
}

