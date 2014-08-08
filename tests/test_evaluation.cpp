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

    const int DRAW_MAX = VPAWN / 10;
    const int SCORE_MAX = 2 * SCORE_WIN;

    /*
     * KK positions (King vs King). Test for:
     * - Evaluation should be draw
     * - Opposition, center and progress
     */
    testEval(1000, "k7/8/1K6/8/8/8/8/8 w - - 0 1", 1, DRAW_MAX);
    testEval(1002, "8/8/1k6/8/1K6/8/8/8 w - - 0 1", -DRAW_MAX, -1);
    testEval(1003, "6k1/8/8/8/8/8/8/1K6 w - - 0 1", 1, DRAW_MAX);
    testEval(1004, "6k1/8/8/8/8/8/K7/8 w - - 0 1", -DRAW_MAX, 1);

    /*
     * Endgames without pawns
     */

    //KNK, KBK, KNNK (draw)
    testEval(1101, "7k/8/6K1/3N4/8/8/8/8 w - - 0 1", 1, DRAW_MAX);
    testEval(1102, "7k/8/6K1/8/5B2/8/8/8 w - - 0 1", 1, DRAW_MAX);
    testEval(1103, "5k2/8/2N2K2/8/5N2/8/8/8 w - - 0 1", 1, DRAW_MAX);

    //KRK, KQK, KBBK, KBNK (win)
    testEval(1201, "8/8/8/8/4k3/8/7K/7R w - - 0 1", SCORE_WIN, SCORE_MAX); //KRK
    testEval(1202, "8/8/8/8/4k3/8/Q7/K7 w - - 0 1", SCORE_WIN, SCORE_MAX); //KQK
    testEval(1203, "8/8/8/8/4k3/8/8/K2B2B1 w - - 0 1", SCORE_WIN, SCORE_MAX); //KBBK
    testEval(1204, "K7/2kB4/8/8/8/8/8/5N2 w - - 0 1", SCORE_WIN, SCORE_MAX); //longest KBNK
    
    //Extra minor piece (draw) 
    testEval(1301, "8/8/5k2/4n3/8/2K5/1R6/8 w - - 0 1", 1, VPAWN); //KRKN
    testEval(1301, "7R/8/8/2n5/6k1/r7/4K3/8 w - - 0 78", -VPAWN, -1);
    
    //KBBKN exception
    testEval(1351, "K7/8/8/8/4n3/4k3/8/B6B w - - 0 1", SCORE_WIN / 2, SCORE_WIN); //KBBKN is mostly won

    //Extra major piece (won)
    testEval(1401, "8/K7/8/4k3/3n4/2N5/R7/8 w - - 0 1", SCORE_WIN / 4, SCORE_WIN / 2);
    testEval(1402, "6r1/K7/8/4k3/3n4/2N5/R7/1R6 w - - 0 1", SCORE_WIN / 8, SCORE_WIN / 4);
    testEval(1403, "8/8/8/4kr2/8/8/8/3QK3 w - - 0 1", SCORE_WIN / 8, SCORE_WIN / 3); //KQKR
    testEval(1404, "8/8/8/4kr2/8/8/4P3/3QK3 w - - 0 1", SCORE_WIN / 8, SCORE_WIN / 3); //KQPKR

    /*
     * Opponent has no pawns
     */
    
    //KRPK and better
    testEval(1501, "8/6k1/8/8/8/8/PK6/R7 w - - 0 1", SCORE_WIN, SCORE_MAX);
    testEval(1502, "8/8/8/4b3/3k4/2N5/RP6/K7 w - - 0 1", SCORE_WIN / 4, SCORE_WIN / 2);
    testEval(1503, "8/8/8/4b3/3k4/2N1q3/RP6/KQ6 w - - 0 1", SCORE_WIN / 8, SCORE_WIN / 3);
    
    //KPK
    testEval(1601, "7k/8/8/8/8/8/P7/K7 w - - 0 1", SCORE_WIN/2, SCORE_WIN); 
    testEval(1602, "8/1k6/8/8/8/7K/7P/8 w - - 0 1", SCORE_WIN/2, SCORE_WIN);
    testEval(1603, "8/2k5/8/8/8/7K/7P/8 w - - 0 1", 1, DRAW_MAX);
    testEval(1604, "2k5/8/8/8/8/8/2P5/2K5 w - - 0 1", SCORE_WIN/2, SCORE_WIN);
    testEval(1605, "2k5/8/8/8/8/2P5/8/2K5 w - - 0 1", 1, DRAW_MAX);
    
    //KBPK
    testEval(1701, "8/8/8/8/6k1/6bp/8/6K1 w - - 0 1", -DRAW_MAX, -1);
    
    //KNPK
    testEval(1801, "8/Pk6/8/1N6/8/8/5K2/8 w - - 0 1", 1, DRAW_MAX);
    
    //
    
    /*
     * Opponent has one pawn
     */
    
    //KPKP
    //testEval(14, "7K/8/k1P5/7p/8/8/8/8 w - - 0 1", SCORE_DRAW_MIN, SCORE_DRAW_MAX); //famous study by Réti
    
    /*
     * Opponent has two pawns
     */
    

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
     * 
     * testEval("8/8/8/pB1k4/P5r1/3K4/8/8 w - - 13 32", -VPAWN, -1); //KBPKRP -> draw
        
     */
    
    /*

add<KRKP>
add<KQKP>
add<KNPKB>
add<KRPKR>
add<KRPKB>
add<KBPKB>
add<KBPKN>
add<KBPPKB>
add<KRPPKRP>
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

