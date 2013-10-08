/* 
 * File:   test_wac.cpp
 * Author: Hajewiet
 *
 * Created on 21-mei-2011, 1:01:08
 */

#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <time.h>
#include "bbmoves.h"
#include "board.h"
#include "movegen.h"
#include "engine.h"
#include "search.h"

/*
 * Simple C++ Test Suite
 */

struct TTestResult {
    bool solved;
    int nodes;
    int score;
    TMove move;
};

TTestResult testForMove(TEngine * engine, string fen, string move, int targetScore) {
    TTestResult result;
    TMove bm;
    TBoard pos;
    pos.fromFen(fen.c_str());
    bm.fromString(&pos, move.c_str());

    engine->testPosition(bm, targetScore, 30 * 1000, 0);
    engine->newGame(pos.asFen());
    std::cout << "Position: " << pos.asFen() << " best move: " << bm.asString() << std::endl;

    engine->think();
    engine->stopAllThreads();
    result.solved = engine->getTestResult();
    result.nodes = engine->getNodesSearched();
    result.move = engine->getMove();
    result.score = engine->getScore();
    return result;
}

void test_wac(TEngine * engine) {
    std::cout << "test_positions test wac" << std::endl;

    static const int TEST_SIZE = 25;
    int solved = 0;
    int targetScore = TEST_SIZE;

    TTestResult results[TEST_SIZE];
    int current = 0;
    memset(results, 0, sizeof (results));

    /*   
    5bk1/1rQ4p/5pp1/2pP4/3n1PP1/7P/1q3BB1/4R1K1 w - - bm d6; id "WAC.155";
    2r1r2k/1q3ppp/p2Rp3/2p1P3/6QB/p3P3/bP3PPP/3R2K1 w - - bm Bf6; id "WAC.222";
    2b5/1r6/2kBp1p1/p2pP1P1/2pP4/1pP3K1/1R3P2/8 b - - bm Rb4; id "WAC.230";
    1nbq1r1k/3rbp1p/p1p1pp1Q/1p6/P1pPN3/5NP1/1P2PPBP/R4RK1 w - - bm Nfg5; id "WAC.293";
     */

    results[current++] = testForMove(engine, "r4rk1/p5p1/1p2p2p/3pPn1P/b1p3Bq/P1P3P1/2PN1P2/R2Q1RK1 b - - 0 19", "f5g3", 30);
    results[current++] = testForMove(engine, "2rr2k1/pb3pp1/1p1ppq1p/1P6/2PNP3/P2Q1P2/6PP/3R1RK1 b - - 1 1", "f6g5", 30);
    results[current++] = testForMove(engine, "2r2rk1/1p2pnb1/p2p3p/q2P2p1/5p1P/2P5/PP3PPB/1K1RQB1R b - -0 21", "c8c3", -SCORE_MATE);
    results[current++] = testForMove(engine, "rq2rbk1/6p1/p2p2Pp/1p1Rn3/4PB2/6Q1/PPP1B3/2K3R1 w - - 0 1", "f4h6", -SCORE_MATE);
    results[current++] = testForMove(engine, "1br2rk1/1pqb1ppp/p3pn2/8/1P6/P1N1PN1P/1B3PP1/1QRR2K1 w - - 0 1", "c3e4", 175);
    results[current++] = testForMove(engine, "1r3r1k/3p4/1p1Nn1R1/4Pp1q/pP3P1p/P7/5Q1P/6RK w - - 0 1", "f2e2", -SCORE_MATE);
    results[current++] = testForMove(engine, "1nbq1r1k/3rbp1p/p1p1pp1Q/1p6/P1pPN3/5NP1/1P2PPBP/R4RK1 w - - 0 1", "f3g5", -SCORE_MATE);
    results[current++] = testForMove(engine, "2kr3r/pp1q1ppp/5n2/1Nb5/2Pp1B2/7Q/P4PPP/1R3RK1 w - - 0 1", "b5a7", -SCORE_MATE);
    results[current++] = testForMove(engine, "8/8/8/1p5r/p1p1k1pN/P2pBpP1/1P1K1P2/8 b - - 0 1", "b5b4", -SCORE_MATE);
    results[current++] = testForMove(engine, "8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1", "b3b2", -SCORE_MATE);
    results[current++] = testForMove(engine, "8/p3k1p1/4r3/2ppNpp1/PP1P4/2P3KP/5P2/8 b - - 0 1 ", "e6e5", -SCORE_MATE);
    results[current++] = testForMove(engine, "r4rk1/1p2ppbp/p2pbnp1/q7/3BPPP1/2N2B2/PPP4P/R2Q1RK1 b - - 0 1", "e6g4", -SCORE_MATE);
    results[current++] = testForMove(engine, "8/k1b5/P4p2/1Pp2p1p/K1P2P1P/8/3B4/8 w - - 0 1", "b5b6", -SCORE_MATE);
    results[current++] = testForMove(engine, "2rq1bk1/p4p1p/1p4p1/3b4/3B1Q2/8/P4PpP/3RR1K1 w - - 0 1", "e1e8", -SCORE_MATE);
    results[current++] = testForMove(engine, "r1bq3r/ppppR1p1/5n1k/3P4/6pP/3Q4/PP1N1PP1/5K1R w - - 0 1", "h4h5", -SCORE_MATE);
    results[current++] = testForMove(engine, "5bk1/1rQ4p/5pp1/2pP4/3n1PP1/7P/1q3BB1/4R1K1 w - - 0 1", "d5d6", -SCORE_MATE);
    results[current++] = testForMove(engine, "5rk1/2p4p/2p4r/3P4/4p1b1/1Q2NqPp/PP3P1K/R4R2 b - - 0 1", "f3g2", -SCORE_MATE);
    results[current++] = testForMove(engine, "2r1r2k/1q3ppp/p2Rp3/2p1P3/6QB/p3P3/bP3PPP/3R2K1 w - - 0 1", "h4f6", -SCORE_MATE);
    results[current++] = testForMove(engine, "3r4/1p2k2p/p1b1p1p1/4Q1Pn/2B3KP/4pP2/PP2R1N1/6q1 b - - 0 1", "d8f8", -SCORE_MATE);
    results[current++] = testForMove(engine, "2k1r3/1p2Bq2/p2Qp3/Pb1p1p1P/2pP1P2/2P5/2P2KP1/1R6 w - - 0 1", "b1b5", -SCORE_MATE);
    results[current++] = testForMove(engine, "1rb1r1k1/p1p2ppp/5n2/2pP4/5P2/2QB4/qNP3PP/2KRB2R b - - 0 1", "e8e2", -SCORE_MATE);
    results[current++] = testForMove(engine, "2r1r1k1/pp1q1ppp/3p1b2/3P4/3Q4/5N2/PP2RPPP/4R1K1 w - - 0 1", "d4g4", -SCORE_MATE);
    results[current++] = testForMove(engine, "nrq4r/2k1p3/1p1pPnp1/pRpP1p2/P1P2P2/2P1BB2/1R2Q1P1/6K1 w - - 0 1", "e3c5", -SCORE_MATE);
    results[current++] = testForMove(engine, "5r1k/3b2p1/p6p/1pRpR3/1P1P2q1/P4pP1/5QnP/1B4K1 w - - 0 1", "h2h3", -SCORE_MATE);
    results[current++] = testForMove(engine, "4r1k1/p1qr1p2/2pb1Bp1/1p5p/3P1n1R/1B3P2/PP3PK1/2Q4R w - - 0 1", "c1f4", -SCORE_MATE);
    
    U64 totalNodes = 0;
    for (int i = 0; i < TEST_SIZE; i++) {
        std::cout << "Position " << (i + 1) << ": " << (results[i].solved ? "Passed" : "Failed")
                << " " << results[i].move.asString() 
                << " score: " << results[i].score
                << " nodes: " << results[i].nodes / 1000 << "K" << std::endl;
        solved += results[i].solved;
        totalNodes += results[i].nodes;
    }
    std::cout << "Solved " << solved << " positions out of " << TEST_SIZE << std::endl;
    std::cout << "Nodes " << totalNodes / 1000 << "K " << std::endl;
    if (solved < targetScore) {
        std::cout << "%TEST_FAILED% time=0 testname=wac (test_wac) message=solved: " << solved << " out of " << TEST_SIZE << std::endl;
    }
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% test_positions" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    clock_t begin;

    InitMagicMoves();
    TEngine * engine = new TEngine();
    THashTable *globalHashTable = new THashTable(128);
    TOutputHandler oh;
    engine->setHashTable(globalHashTable);
    engine->setOutputHandler(&oh);

    std::cout << "%TEST_STARTED% test_positions (test_wac)\n" << std::endl;
    begin = clock();
    test_wac(engine);
    double elapsed = (clock() - begin) / (1.0 * CLOCKS_PER_SEC);
    std::cout << "%TEST_FINISHED% time=" << elapsed << " test_positions (test_wac)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=" << elapsed << std::endl;

    delete globalHashTable;
    delete engine;

    return (EXIT_SUCCESS);
}
