/* 
 * File:   test_search.cpp
 * Author: Twin
 *
 * Created on 14-aug-2011, 12:18:01
 */


#include "tests/test_search1.h"
#include <stdlib.h>
#include <iostream>
#include "engine.h"

/*
 * Simple C++ Test Suite
 */



int evaluateTest(TSearchData * sd, int a, int b) {
    return 0;
}

void testSearch(TEngine * engine) {
    std::cout << "test_search test Search" << std::endl;
    
    
    engine->newGame("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    engine->gameSettings.maxTimePerMove = 10000;
    engine->think();
    engine->stopAllThreads();
    //std::cout << "%TEST_FAILED% time=0 testname=testSearch (test_search) message=error message sample" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% test_search" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    InitMagicMoves();
    TEngine * engine = new TEngine();
    THashTable *globalHashTable = new THashTable(128);
    TOutputHandler oh;
    engine->setHashTable(globalHashTable);
    engine->setOutputHandler(&oh);


    std::cout << "%TEST_STARTED% testSearch (test_search)\n" << std::endl;
    testSearch(engine);
    std::cout << "%TEST_FINISHED% time=0 testSearch (test_search)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    delete engine;
    delete globalHashTable;
    return (EXIT_SUCCESS);
}

