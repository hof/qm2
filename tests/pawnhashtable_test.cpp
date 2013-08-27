/* 
 * File:   pawnhashtable_test.cpp
 * Author: Twin
 *
 * Created on 2-jun-2011, 14:45:26
 */

#include <stdlib.h>
#include <iostream>

#include "bbmoves.h"
#include "hashtable.h"
#include "searchdata.h"
#include "search.h"
#include "engine.h"
#include "evaluate.h"

/*
 * Simple C++ Test Suite
 */



void test2() {
    std::cout << "pawnhashtable_test test 2" << std::endl;

    std::cout << "test_transpositiontable test tt" << std::endl;

    THashTable * hashTable = new THashTable(12);
    TOutputHandler oh;
    TSearchData * searchData = new TSearchData("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", PIECE_SQUARE_TABLE, hashTable, &oh);
    evaluatePawns(searchData);
    TEvalInfo * evalInfo = searchData->getEvalInfo();
    TPawnTableResult ptResult;
    memcpy(&ptResult, &evalInfo->pawnInfo, sizeof (TPawnTableResult));
    TPawnTableResult * ptStoredResult = searchData->getPawnTableResult();
    if (ptStoredResult->score != ptResult.score
            || ptStoredResult->bAttackRange != ptResult.bAttackRange
            || ptStoredResult->wAttackRange != ptResult.wAttackRange
            || ptStoredResult->weakPawns != ptResult.weakPawns) {

        std::cout << "%TEST_FAILED% time=0 testname=test2 (pawnhashtable_test) message=retrieved result does not match" << std::endl;
    }

    TPawnTableEntry ptEntry;
    ptResult.bAttackRange = RANK_1 | RANK_3 | RANK_5;
    ptResult.wAttackRange = RANK_8 | RANK_5 | RANK_3;
    ptResult.weakPawns = RANK_2 | RANK_7;
    ptResult.score = -900;
    ptResult.closedScore = 16;
    ptResult.openFilesB = 255;
    ptResult.openFilesW = (1<<7) | 1;
    ptResult.badBishopMaskB = (RANK_3|RANK_5|RANK_7|RANK_8);
    ptResult.badBishopMaskW = (RANK_1|RANK_2|RANK_5|RANK_7);
    ptEntry.set(&ptResult);
    TPawnTableResult ptResult2;
    ptResult2.set(&ptEntry);
    if (!ptResult2.equal(&ptResult)) {
        std::cout << "%TEST_FAILED% time=0 testname=test2 (pawnhashtable_test) message=boundary test failed" << std::endl;
    }

    delete searchData;
    delete hashTable;


}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% pawnhashtable_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;



    std::cout << "%TEST_STARTED% test2 (pawnhashtable_test)\n" << std::endl;
    test2();
    std::cout << "%TEST_FINISHED% time=0 test2 (pawnhashtable_test)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    return (EXIT_SUCCESS);
}

