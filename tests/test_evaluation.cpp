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
#include "search.h"

/*
 * Simple C++ Test Suite
 */

THashTable *globalHashTable;
TEngine * engine;
TOutputHandler oh;
bool test_stop;
int tests_passed;

void testFail(std::string msg, int score, std::string fen, TEngine * engine) {
    test_stop = true;
    std::cout << "Test " << tests_passed << ": " << fen << " " << msg << " " << score << std::endl;
    std::cout << "%TEST_FAILED% time=0 testname=evaluation (test_evaluation) message=evaluation test failed" << std::endl;
    engine->newGame(fen.c_str());
    engine->analyse();
}

void testEval(std::string fen, int min_score, int max_score) {
    if (test_stop) {
        return;
    }
    tests_passed++;
    TSearch * s = new TSearch(fen.c_str(), globalHashTable, &oh);
    int score = evaluate(s);
    if (score < min_score) {
        testFail("score < ", min_score, fen, engine);
    }
    if (score > max_score) {
        testFail("score > ", max_score, fen, engine);
    }
    delete s;
}

void testKingAttackZero(std::string fen) {
    if (test_stop) {
        return;
    }
    tests_passed++;
    TSearch * s = new TSearch(fen.c_str(), globalHashTable, &oh);
    evaluate(s);
    int score = s->stack->king_score[WHITE].mg;
    if (score != 0) {
        testFail("score (king attack white) 0 != ", score, fen, engine);
    }
    score = s->stack->king_score[BLACK].mg;
    if (score != 0) {
        testFail("score (king attack black) 0 != ", score, fen, engine);
    }
    delete s;
}

void testEvaluationSuite() {
    /*
     * Opening
     */
    testEval("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0, 20);
    
    /*
     * End games
     */
    testEval("8/8/k5r1/2P5/PQ6/KP6/4q3/8 w - - 0 1", -350, -200); 
    testEval("8/8/4k3/3N1b2/3K1P2/8/8/8 b - - 7 1", -10, 10);
    testEval("6k1/8/8/8/8/8/8/1K6 w - - 2 2", -10, 10);
    testEval("8/4k3/8/8/8/8/5K2/R7 w - - 0 2", SCORE_WIN, SCORE_MATE);
    testEval("2k5/8/8/8/8/2P5/8/2K5 w - - 0 1", -10, 10);
    testEval("2k5/8/8/8/8/8/2P5/2K5 w - - 0 1", SCORE_WIN, SCORE_MATE);
    testEval("7K/8/k1P5/7p/8/8/8/8 w - - 0 1", -10, 10);
    
    /*
     * King Attack
     */
    testKingAttackZero("r1bq1rk1/ppppbppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 w - - 3 6");
    
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% evaluation_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% test1 (evaluation_test)" << std::endl;
    test_stop = false;
    tests_passed = 0;
    InitMagicMoves();
    globalHashTable = new THashTable(128);
    engine = new TEngine();
    engine->setHashTable(globalHashTable);
    engine->gameSettings.maxDepth = 20;
    engine->setOutputHandler(&oh);
    testEvaluationSuite();
    std::cout << "%TEST_FINISHED% time=0 test1 (evaluation_test)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    delete engine;
    delete globalHashTable;
    return (EXIT_SUCCESS);
}

