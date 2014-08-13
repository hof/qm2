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


    /*
     * Test pawn table store and retrieve
     */
    U64 key = hash::codes[WPAWN][e4];
    U64 passers1, passers2;
    score_t score1, score2;
    int king_attack1[2], king_attack2[2], flags1, flags2;
    passers1 = BIT(a2) | BIT(h2) | BIT(a7) | BIT(b7);
    score1.set(-12, 34);
    king_attack1[WHITE] = -56;
    king_attack1[BLACK] = 78;
    flags1 = 15;
    pawn_table::store(key, passers1, score1, king_attack1, flags1);
    bool result = pawn_table::retrieve(key, passers2, score2, king_attack2, flags2);
    if (result == false
            || passers1 != passers2  
            || score1.mg != score2.mg
            || score1.eg != score2.eg
            || king_attack1[WHITE] != king_attack2[WHITE] 
            || king_attack1[BLACK] != king_attack2[BLACK]
            || flags1 != flags2
            ) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=pawn table store/retrieve error" << std::endl;
    }
    
    /*
     * Test material table store and retrieve
     */
    int value1, value2, phase1, phase2;
    value1 = 910;
    phase1 = 14;
    material_table::store(key, value1, phase1, flags1);
    result = material_table::retrieve(key, value2, phase2, flags2);
    if (result == false
            || value1 != value2
            || phase1 != phase2
            || flags1 != flags2
            ) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=material table store/retrieve error" << std::endl;
    }

    THashTable * hashTable = new THashTable(36);

    //testing the Lasker-Reichhelm Position
    TSearch * searchData = new TSearch("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", hashTable, NULL);

    //test storing and retrieving
    move_t * move = searchData->movePicker->pickFirstMove(searchData, 0, -score::INF, score::INF);
    searchData->hashTable->ttStore(searchData, move->to_int(), -12345, 127, -score::INF, score::INF);
    searchData->hashTable->ttLookup(searchData, 127, -score::INF, score::INF);
    if (searchData->stack->ttScore == TT_EMPTY) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=store/retrieve error" << std::endl;
    }


    //test if searching the position with a hashtable finished a depth 15 
    //search with less than 50.000 nodes
    magic::init();
    TEngine * engine = new TEngine();
    engine->setHashTable(hashTable);
    engine->setOutputHandler(NULL);
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

