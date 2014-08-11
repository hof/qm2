/* 
 * File:   search_test.cpp
 * Author: Hajewiet
 *
 * Created on 3-sep-2011, 12:42:42
 */

#include <stdlib.h>
#include <iostream>

#include <stdlib.h>
#include <iostream>
#include <time.h>

#include "board.h"
#include "book.h"
#include "search.h"
#include "bbmoves.h"
#include "evaluate.h"
#include "hashtable.h"
#include "search.h"
#include "movegen.h"

/*
 * Simple C++ Test Suite
 */

U64 searchPerft(TSearch *searchData, int depth, int alpha, int beta) {
    U64 result = 0;
    TMovePicker * mp = searchData->movePicker;
    board_t * pos = searchData->pos;
    for (TMove * move = mp->pickFirstMove(searchData, depth, alpha, beta); move;
            move = mp->pickNextMove(searchData, depth, alpha, beta)) {
        if (depth <= 1) {
            result += 1;
        } else {
            bool givesCheck = pos->gives_check(move);
            searchData->forward(move, givesCheck);
            result += searchPerft(searchData, depth - 1, -beta, -alpha);
            searchData->backward(move);
        }
    }
    return result;
}

U64 rootPerft(TSearch *searchData, int depth) {
    U64 result = 0;
    for (int i = 0; i < searchData->root.MoveCount; i++) {
        TRootMove * rMove = &searchData->root.Moves[i];
        if (depth <= 1) {
            result += 1;
        } else {
            searchData->forward(&rMove->Move, rMove->GivesCheck);
            if (rMove->GivesCheck) {
                searchData->pos->stack->checker_sq = rMove->checker_sq;
                searchData->pos->stack->checkers = rMove->checkers;
            }
            result += searchPerft(searchData, depth - 1, -32000, 32000);
            searchData->backward(&rMove->Move);
        }
    }
    return result;
}

void divide(TSearch * searchData, int depth) {
    TMoveList * moveList = &searchData->stack->moveList;
    board_t * pos = searchData->pos;
    std::cout << pos->to_string() << std::endl;
    moveList->clear();
    TMovePicker * mp = searchData->movePicker;
    for (TMove * move = mp->pickFirstMove(searchData, depth, -32000, 32000); move;
            move = mp->pickNextMove(searchData, depth, -32000, 32000)) {
        std::cout << move->asString() << " ";
        searchData->forward(move, pos->gives_check(move));
        std::cout << searchPerft(searchData, depth, -32000, 32000) << std::endl;
        searchData->backward(move);

    }
}

/**
 * Move generaration test (perft test)
 */
void testMoveGeneration(string fen, int targetValues[], int maxDepth, THashTable * hashTable) {
    std::cout << "\n\ntest_genmoves test testMoveGeneration " << fen << std::endl;
    TOutputHandler outputHandler;
    TSearch * searchData = new TSearch(fen.c_str(), hashTable, &outputHandler);
    std::string fen2 = searchData->pos->to_string();
    if (fen2 != fen) {
        std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=basic fen mismatch " << fen2 << std::endl;
    }
    searchData->initRootMoves();
    for (int i = 0; i < maxDepth; i++) {
        int count = rootPerft(searchData, i + 1);
        if (count != targetValues[i]) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=depth "
                    << i + 1 << " node mismatch: " << count << std::endl;
            divide(searchData, i);
            break;
        }
        string fen_test = searchData->pos->to_string();
        if (fen.compare(fen_test) != 0) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=depth "
                    << i + 1 << ": fen mismatch " << fen_test << std::endl;
            break;
        }
    }
    delete searchData;
}

int arraySum(int anArray[], int arraySize) {
    int result = 0;
    for (int i = 0; i < arraySize; i++) {
        result += anArray[i];
    }
    return result;
}

int main(int argc, char** argv) {

    clock_t begin;
    clock_t now;

    THashTable * hashTable = new THashTable(8);

    magic::init();
    std::cout << "%SUITE_STARTING% test_genmoves" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% testMoveGeneration (test_genmoves)" << std::endl;

    int targetValues1[] = {20, 400, 8902, 197281, 4865609};
    int targetValues2[] = {48, 2039, 97862, 4085603};
    int targetValues3[] = {14, 191, 2812, 43238, 674624, 11030083};
    int targetValues4[] = {37, 1141, 42739, 1313295};
    
    int totalNodes = arraySum(targetValues1, sizeof (targetValues1) / sizeof (int));
    totalNodes += arraySum(targetValues2, sizeof (targetValues2) / sizeof (int));
    totalNodes += arraySum(targetValues3, sizeof (targetValues3) / sizeof (int));
    totalNodes += arraySum(targetValues4, sizeof (targetValues4) / sizeof (int));

    begin = clock();
    testMoveGeneration("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", targetValues1, sizeof (targetValues1) / sizeof (int), hashTable);
    testMoveGeneration("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", targetValues2, sizeof (targetValues2) / sizeof (int), hashTable);
    testMoveGeneration("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", targetValues3, sizeof (targetValues3) / sizeof (int), hashTable);
    testMoveGeneration("r5r1/p1q2p1k/1p1R2pB/3pP3/6bQ/2p5/P1P1NPPP/6K1 w - - 0 1", targetValues4, sizeof (targetValues4) / sizeof (int), hashTable);
    now = clock();

    std::cout << totalNodes << " nodes generated in " << (now - begin) << "ms (" << U64(CLOCKS_PER_SEC * U64(totalNodes)) / (now - begin) << "nps)" << std::endl;

    std::cout << "%TEST_FINISHED% time=" << (now - begin) / CLOCKS_PER_SEC << " testMoveGeneration (test_genmoves)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=" << (now - begin) / CLOCKS_PER_SEC << std::endl;

    delete hashTable;

    return (EXIT_SUCCESS);
}


