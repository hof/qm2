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

U64 searchPerft(search_t *s, int depth, int alpha, int beta) {
    U64 result = 0;
    board_t * brd = &s->brd;
    for (move_t * move = move::first(s, depth, alpha, beta); move;
            move = move::next(s, depth, alpha, beta)) {
        if (depth <= 1) {
            result += 1;
        } else {
            bool givesCheck = brd->gives_check(move);
            s->forward(move, givesCheck);
            result += searchPerft(s, depth - 1, -beta, -alpha);
            s->backward(move);
        }
    }
    return result;
}

U64 rootPerft(search_t *s, int depth) {
    U64 result = 0;
    for (int i = 0; i < s->root.move_count; i++) {
        root_move_t * rMove = &s->root.moves[i];
        if (depth <= 1) {
            result += 1;
        } else {
            s->forward(&rMove->move, rMove->gives_check);
            if (rMove->gives_check) {
                s->brd.stack->checker_sq = rMove->checker_sq;
                s->brd.stack->checkers = rMove->checkers;
            }
            result += searchPerft(s, depth - 1, -32000, 32000);
            s->backward(&rMove->move);
        }
    }
    return result;
}

void divide(search_t * s, int depth) {
    move::list_t * move_list = &s->stack->move_list;
    board_t * brd = &s->brd;
    std::cout << brd->to_string() << std::endl;
    move_list->clear();
    for (move_t * move = move::first(s, depth, -32000, 32000); move;
            move = move::next(s, depth, -32000, 32000)) {
        std::cout << move->to_string() << " ";
        s->forward(move, brd->gives_check(move));
        std::cout << searchPerft(s, depth, -32000, 32000) << std::endl;
        s->backward(move);

    }
}

/**
 * Move generaration test (perft test)
 */
void testMoveGeneration(std::string fen, int targetValues[], int maxDepth) {
    std::cout << "\n\ntest_genmoves test testMoveGeneration " << fen << std::endl;
    search_t * s = new search_t(fen.c_str());
    std::string fen2 = s->brd.to_string();
    if (fen2 != fen) {
        std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=basic fen mismatch " << fen2 << std::endl;
    }
    s->init_root_moves();
    for (int i = 0; i < maxDepth; i++) {
        int count = rootPerft(s, i + 1);
        if (count != targetValues[i]) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=depth "
                    << i + 1 << " node mismatch: " << count << std::endl;
            divide(s, i);
            break;
        }
        std::string fen_test = s->brd.to_string();
        if (fen.compare(fen_test) != 0) {
            std::cout << "%TEST_FAILED% time=0 testname=testMoveGeneration (test_genmoves) message=depth "
                    << i + 1 << ": fen mismatch " << fen_test << std::endl;
            break;
        }
    }
    delete s;
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
    testMoveGeneration("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", targetValues1, sizeof (targetValues1) / sizeof (int));
    testMoveGeneration("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", targetValues2, sizeof (targetValues2) / sizeof (int));
    testMoveGeneration("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", targetValues3, sizeof (targetValues3) / sizeof (int));
    testMoveGeneration("r5r1/p1q2p1k/1p1R2pB/3pP3/6bQ/2p5/P1P1NPPP/6K1 w - - 0 1", targetValues4, sizeof (targetValues4) / sizeof (int));
    now = clock();

    std::cout << totalNodes << " nodes generated in " << (now - begin) << "ms (" << U64(CLOCKS_PER_SEC * U64(totalNodes)) / (now - begin) << "nps)" << std::endl;

    std::cout << "%TEST_FINISHED% time=" << (now - begin) / CLOCKS_PER_SEC << " testMoveGeneration (test_genmoves)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=" << (now - begin) / CLOCKS_PER_SEC << std::endl;

    return (EXIT_SUCCESS);
}


