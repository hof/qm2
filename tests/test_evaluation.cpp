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
int SCORE_DRAW_MIN = -10;
int SCORE_DRAW_MAX = 10;

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

    /*
     * KK positions (King vs King). Test for:
     * - Evaluation should be draw
     * - Having opposition
     * - Centralize / Progress
     */

    testEval(0, "8/8/8/2k5/8/3K4/8/8 w - - 0 1", 1, SCORE_DRAW_MAX); //white gets opposition
    testEval(1, "8/8/8/8/2k5/8/2K5/8 w - - 0 1", SCORE_DRAW_MIN, -1); //white looses opposition
    testEval(2, "8/8/8/2k5/8/3K4/8/8 w - - 0 1", 1, SCORE_DRAW_MAX); //white gets opposition

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
        testEval(7, "8/4k3/8/8/8/8/5K2/R7 w - - 0 2", SCORE_WIN, SCORE_MATE);
        testEval(8, "8/8/8/2k2p2/1q6/7R/2K5/8 w - - 0 29", -SCORE_MATE, -SCORE_WIN / 8);

        //no pawns, piece up but no mating power
        testEval(9, "8/K7/8/4k3/3p4/8/4B3/8 w - - 0 4", SCORE_DRAW_MIN, SCORE_DRAW_MAX);
    
        //no pawns, one minor extra
        testEval(10, "7R/8/8/2n5/6k1/r7/4K3/8 w - - 0 78", -VPAWN, -1);
    
        //one pawn, but no mating power and opp can sacrifice his piece
        testEval(11, "8/1K6/3k4/P2p4/8/4b3/4B3/8 w - - 27 1", SCORE_DRAW_MIN, SCORE_DRAW_MAX);

        //kpk
        testEval(12, "2k5/8/8/8/8/8/2P5/2K5 w - - 0 1", SCORE_WIN, SCORE_MATE);
        testEval(13, "2k5/8/8/8/8/2P5/8/2K5 w - - 0 1", 1, SCORE_DRAW_MAX);

        //kpkp - famous study by Réti
        testEval(14, "7K/8/k1P5/7p/8/8/8/8 w - - 0 1", SCORE_DRAW_MIN, SCORE_DRAW_MAX);
     */
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% evaluation_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% test1 (evaluation_test)" << std::endl;
    test_stop = false;
    InitMagicMoves();
    init_pst();
    globalHashTable = new THashTable(128);
    engine = new TEngine();
    engine->setHashTable(globalHashTable);
    engine->gameSettings.maxDepth = 20;
    engine->setOutputHandler(&oh);
    testEvaluationSuite();
    std::cout << "%TEST_FINISHED% time=0 test1 (evaluation_test)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    bool print_pst = false;
    if (print_pst) {
        std::string PIECE_NAME[WKING + 1] = {"", "PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING"};
        for (int phase = 0; phase <= 1; phase++) {
            for (int pc = WPAWN; pc <= WKING; pc++) {
                std::cout << std::endl << "Piece: " << PIECE_NAME[pc] << ": " << std::endl;
                for (int sq = 0; sq < 64; sq++) {
                    int file = FILE(sq);
                    int rank = RANK(sq);
                    if (file == 0) {
                        std::cout << (8 - rank) << " |";
                    }
                    int score = phase == 0 ? PST[pc][sq].mg : PST[pc][sq].eg;
                    std::cout << std::setw(3) << score << "|";
                    if (file == 7) {
                        std::cout << std::endl;
                    }
                }
            }
        }
    }


    delete engine;
    delete globalHashTable;
    return (EXIT_SUCCESS);
}

