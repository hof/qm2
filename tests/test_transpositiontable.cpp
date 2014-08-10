/* 
 * File:   test_transpositiontable.cpp
 * Author: Hajewiet
 *
 * Created on 16-mei-2011, 21:56:13
 */

#include <stdlib.h>
#include <iostream>

#include "bbmoves.h"
#include "hashtable.h"
#include "search.h"
#include "search.h"
#include "engine.h"
#include "evaluate.h"

/*
 * Simple C++ Test Suite
 */

void test_tt() {
    std::cout << "test_transpositiontable test tt" << std::endl;
    
    THashTable * hashTable = new THashTable(36);
    
    //testing the Lasker-Reichhelm Position
    TOutputHandler oh;
    TSearch * searchData = new TSearch("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", hashTable, &oh); 
    
    //test storing and retrieving
    TMove * move = searchData->movePicker->pickFirstMove(searchData, 0, -SCORE_INFINITE, SCORE_INFINITE);
    searchData->hashTable->ttStore(searchData, move->asInt(), -12345, 127, -SCORE_INFINITE, SCORE_INFINITE);
    searchData->hashTable->ttLookup(searchData, 127, -SCORE_INFINITE, SCORE_INFINITE);
    if (searchData->stack->ttScore == TT_EMPTY) {
       std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=store/retrieve error" << std::endl; 
    }

    
    //test if searching the position with a hashtable finished a depth 15 
    //search with less than 50.000 nodes
    magic::init();
    TEngine * engine = new TEngine();
    engine->setHashTable(hashTable);
    engine->setOutputHandler(&oh);
    engine->gameSettings.maxDepth = 15;
    engine->newGame("8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - -");
    engine->think();
    engine->stopAllThreads();
    if (engine->getNodesSearched() > 50000) {
       std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=hashtable not effective" << std::endl; 
    }
    delete engine;
    delete searchData;
    delete hashTable;
    
    
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% test_transpositiontable" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% test_tt (test_transpositiontable)\n" << std::endl;
    test_tt();
    std::cout << "%TEST_FINISHED% time=0 test_tt (test_transpositiontable)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    return (EXIT_SUCCESS);
}

