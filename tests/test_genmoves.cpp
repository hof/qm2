/* 
 * File:   test_genmoves.cpp
 * Author: Hajewiet
 *
 * Created on 7-mei-2011, 9:59:40
 */

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

U64 moveGenerationPerft(TSearch *searchData, int depth) {
    U64 result = 0;
    TBoard * pos = searchData->pos;
    TMoveList * moveList = &searchData->stack->moveList;
    moveList->clear();
    genCaptures(pos, moveList, FULL_BOARD);
    genPromotions(pos, moveList);
    genCastles(pos, moveList);
    genQuietMoves(pos, moveList);
    for (TMove * move = moveList->first; move != moveList->last; move++) {
        if (pos->legal(move)) {
            if (depth <= 1) {
                result += 1;
            } else {
                pos->forward(move);
                searchData->stack++;
                result += moveGenerationPerft(searchData, depth - 1);
                searchData->stack--;
                pos->backward(move);

            }
        }
    }
    return result;
}

void movePerftDivide(TSearch * searchData, int depth) {
    TMoveList * moveList = &searchData->stack->moveList;
    TBoard * pos = searchData->pos;
    std::cout << pos->asFen() << std::endl;
    moveList->clear();
    genCaptures(pos, moveList, FULL_BOARD);
    genPromotions(pos, moveList);
    genCastles(pos, moveList);
    genQuietMoves(pos, moveList);
    for (TMove * move = moveList->first; move != moveList->last; move++) {
        std::cout << move->asString() << " ";
        if (pos->legal(move)) {
            pos->forward(move);
            searchData->stack++;
            std::cout << moveGenerationPerft(searchData, depth) << std::endl;
            searchData->stack--;
            pos->backward(move);
        } else {
            std::cout << "illegal" << std::endl;
            ;
        }
    }
}

/**
 * Move generaration test (perft test)
 */
void testMoveGeneration(string fen, int targetValues[], int maxDepth, TSearch * searchData) {
    std::cout << "\n\ntest_genmoves test testMoveGeneration " << fen << std::endl;
    searchData->pos->fromFen(fen.c_str());
    for (int i = 0; i < maxDepth; i++) {
        int count = moveGenerationPerft(searchData, i + 1);
        if (count != targetValues[i]) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=depth "
                    << i + 1 << " node mismatch: " << count << std::endl;
            movePerftDivide(searchData, i);
            break;
        }
    }
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

    THashTable * hashTable = new THashTable(0);
    TOutputHandler outputHandler;
    TSearch * searchData = new TSearch("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", hashTable, &outputHandler);



    InitMagicMoves();
    std::cout << "%SUITE_STARTING% test_genmoves" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% testMoveGeneration (test_genmoves)" << std::endl;

    //int targetValues0[] = {46, 2241};
    //testMoveGeneration("r3k2r/p1ppqpb1/bn2Pnp1/4N3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1", targetValues0, sizeof (targetValues0) / sizeof (int), searchData);

    int targetValues1[] = {20, 400, 8902, 197281, 4865609};
    int targetValues2[] = {48, 2039, 97862, 4085603};
    int targetValues3[] = {14, 191, 2812, 43238, 674624, 11030083};
    int totalNodes = arraySum(targetValues1, sizeof (targetValues1) / sizeof (int));
    totalNodes += arraySum(targetValues2, sizeof (targetValues2) / sizeof (int));
    totalNodes += arraySum(targetValues3, sizeof (targetValues3) / sizeof (int));

    begin = clock();
    testMoveGeneration("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", targetValues1, sizeof (targetValues1) / sizeof (int), searchData);
    testMoveGeneration("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", targetValues2, sizeof (targetValues2) / sizeof (int), searchData);
    testMoveGeneration("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", targetValues3, sizeof (targetValues3) / sizeof (int), searchData);
    now = clock();

    std::cout << totalNodes << " nodes generated in " << (now - begin) << "ms (" << U64(CLOCKS_PER_SEC * U64(totalNodes)) / (now - begin) << "nps)" << std::endl;

    std::cout << "%TEST_FINISHED% time=" << (now - begin) / CLOCKS_PER_SEC << " testMoveGeneration (test_genmoves)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=" << (now - begin) / CLOCKS_PER_SEC << std::endl;

    delete hashTable;
    delete searchData;

    return (EXIT_SUCCESS);
}

