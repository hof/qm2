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

U64 moveGenerationPerft(search_t *searchData, int depth) {
    U64 result = 0;
    board_t * pos = &searchData->brd;
    move::list_t * move_list = &searchData->stack->move_list;
    move_list->clear();
    move::gen_captures(pos, move_list, FULL_BOARD);
    move::gen_promotions(pos, move_list);
    move::gen_castles(pos, move_list);
    move::gen_quiet_moves(pos, move_list);
    for (move_t * move = move_list->first; move != move_list->last; move++) {
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

void movePerftDivide(search_t * searchData, int depth) {
    move::list_t * move_list = &searchData->stack->move_list;
    board_t * pos = &searchData->brd;
    std::cout << pos->to_string() << std::endl;
    move_list->clear();
    move::gen_captures(pos, move_list, FULL_BOARD);
    move::gen_promotions(pos, move_list);
    move::gen_castles(pos, move_list);
    move::gen_quiet_moves(pos, move_list);
    for (move_t * move = move_list->first; move != move_list->last; move++) {
        std::cout << move->to_string() << " ";
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
void testMoveGeneration(std::string fen, int targetValues[], int maxDepth, search_t * searchData) {
    std::cout << "\n\ntest_genmoves test testMoveGeneration " << fen << std::endl;
    searchData->brd.create(fen.c_str());
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

    trans_table::disable();
    search_t * searchData = new search_t("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");



    magic::init();
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

    delete searchData;

    return (EXIT_SUCCESS);
}

