/* 
 * File:   evaluation_test.cpp
 * Author: Hajewiet
 *
 * Created on 31-mei-2011, 16:39:55
 */

#include <stdlib.h>
#include <iostream>
#include <cmath>
#include <iomanip>  //setw
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

void testFail(int idx, std::string operation, int score, int bound, std::string fen, TEngine * engine) {
    test_stop = true;
    std::cout << "Test " << idx << ": " << fen;
    std::cout << ": " << score << " " << operation << " " << bound << " ";
    std::cout << std::endl;
    std::cout << "%TEST_FAILED% time=0 testname=evaluation (test_evaluation) message=evaluation test failed" << std::endl;
    engine->newGame(fen.c_str());
    engine->analyse();
}

int testEval(int idx, std::string fen, int min_score, int max_score) {
    if (test_stop) {
        return 0;
    }
    TSearch * s = new TSearch(fen.c_str(), globalHashTable, &oh);
    int score = evaluate(s);
    if (score < min_score) {
        testFail(idx, "<", score, min_score, fen, engine);
    } else if (score > max_score) {
        testFail(idx, ">", score, max_score, fen, engine);
    }
    delete s;
    return score;
}

void testKingAttackZero(int idx, std::string fen) {
    if (test_stop) {
        return;
    }
    TSearch * s = new TSearch(fen.c_str(), globalHashTable, &oh);
    evaluate(s);
    int score = s->stack->king_score[WHITE].mg;
    if (score != 0) {
        testFail(idx, "!=", 0, score, fen, engine);
    } else {
        score = s->stack->king_score[BLACK].mg;
        if (score != 0) {
            testFail(idx, "!=", 0, score, fen, engine);
        }
    }
    delete s;
}

void testEvaluationSuite() {

    const int DRAW_LIMIT = VPAWN / 10;
    const int SCORE_MAX = 2 * SCORE_WIN;

    /*
     * KK positions (King vs King). Test for:
     * - Evaluation should be draw
     * - Opposition, center and progress
     */
    testEval(1, "k7/8/1K6/8/8/8/8/8 w - - 0 1", 1, DRAW_LIMIT);
    testEval(2, "8/8/1k6/8/1K6/8/8/8 w - - 0 1", -DRAW_LIMIT, -1);
    testEval(3, "6k1/8/8/8/8/8/8/1K6 w - - 0 1", 1, DRAW_LIMIT);
    testEval(4, "6k1/8/8/8/8/8/K7/8 w - - 0 1", -DRAW_LIMIT, 1);

    /*
     * Endgames without pawns
     */

    //KNK, KBK, KNNK are drawn
    testEval(5, "7k/8/6K1/3N4/8/8/8/8 w - - 0 1", 1, DRAW_LIMIT);
    testEval(6, "7k/8/6K1/8/5B2/8/8/8 w - - 0 1", 1, DRAW_LIMIT);
    testEval(7, "5k2/8/2N2K2/8/5N2/8/8/8 w - - 0 1", 1, DRAW_LIMIT);

    //KRK, KQK, KBBK, KBNK and up are won
    testEval(8, "8/8/8/8/4k3/8/7K/7R w - - 0 1", SCORE_WIN, SCORE_MAX);
    testEval(9, "8/8/8/8/4k3/8/Q7/K7 w - - 0 1", SCORE_WIN, SCORE_MAX);
    testEval(10, "8/8/8/8/4k3/8/8/K2B2B1 w - - 0 1", SCORE_WIN, SCORE_MAX);
    testEval(11, "8/8/8/8/4k3/8/8/K2B2N1 w - - 0 1", SCORE_WIN, SCORE_MAX);

    //Extra minor piece
    testEval(12, "7R/8/8/2n5/6k1/r7/4K3/8 w - - 0 78", -VPAWN, -1);

    //Extra major piece
    testEval(13, "8/K7/8/4k3/3n4/2N5/R7/8 w - - 0 1", SCORE_WIN / 4, SCORE_WIN / 2);
    testEval(14, "6r1/K7/8/4k3/3n4/2N5/R7/1R6 w - - 0 1", SCORE_WIN / 8, SCORE_WIN / 4);

    /*
     * Endgames where we have one or more pieces and one ore more pawns, and opponent does not have pawns
     */

    //Decisive, clear winning advantage
    testEval(15, "8/8/8/4b3/3k4/2N5/RP6/K7 w - - 0 1", SCORE_WIN / 4, SCORE_WIN / 2);
    testEval(16, "8/8/8/4b3/3k4/2N1q3/RP6/KQ6 w - - 0 1", SCORE_WIN / 8, SCORE_WIN / 3);

    /*
     * KPK positions (King and pawn vas king)
     */

    //testEval(5, "2k5/8/8/8/8/8/2P5/2K5 w - - 0 1", SCORE_WIN, SCORE_MATE);
    //testEval(6, "2k5/8/8/8/8/2P5/8/2K5 w - - 0 1", 1, SCORE_DRAW_MAX);

    /*
     * KPKP
     */
    //testEval(14, "7K/8/k1P5/7p/8/8/8/8 w - - 0 1", SCORE_DRAW_MIN, SCORE_DRAW_MAX); //famous study by Réti

    /*
     * Opening
     */

    //start position: white has advantage of the first move
    //testEval(1, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, SCORE_DRAW_MAX);

    //king shelter
    //testKingAttackZero(2, "r1bq1rk1/ppppbppp/2n2n2/4p3/2B1P3/3P1N2/PPP2PPP/RNBQ1RK1 w - - 3 6");




    /*    
        testEval(5, "8/8/k5r1/2P5/PQ6/KP6/4q3/8 w - - 0 1", -350, -200);
        testEval(6, "8/8/4k3/3N1b2/3K1P2/8/8/8 w - - 7 1", 1, SCORE_DRAW_MAX);
       
        testEval(8, "8/8/8/2k2p2/1q6/7R/2K5/8 w - - 0 29", -SCORE_MATE, -SCORE_WIN / 8);

        //no pawns, piece up but no mating power
        testEval(9, "8/K7/8/4k3/3p4/8/4B3/8 w - - 0 4", SCORE_DRAW_MIN, SCORE_DRAW_MAX);
    
        
    
        //one pawn, but no mating power and opp can sacrifice his piece
        testEval(11, "8/1K6/3k4/P2p4/8/4b3/4B3/8 w - - 27 1", SCORE_DRAW_MIN, SCORE_DRAW_MAX);

        //kpk
        

        
     */
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% evaluation_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% test1 (evaluation_test)" << std::endl;
    test_stop = false;
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

