/* 
 * File:   test_bratkokopec.cpp
 * Author: Hajewiet
 *
 * Created on 21-mei-2011, 1:01:08
 */

#include <stdlib.h>
#include <iostream>
#include "bbmoves.h"
#include "board.h"
#include "movegen.h"
#include "engine.h"
#include "search.h"
#include <string.h>

/*
 * Simple C++ Test Suite
 */


bool testForMove(TEngine * engine, string fen, string move, int targetScore) {
    TMove bm;
    TBoard pos;
    pos.fromFen(fen.c_str());
    bm.fromString(&pos, move.c_str());
    engine->testPosition(bm, targetScore, 10 * 1000, 0);
    engine->newGame(pos.asFen());
    std::cout << "Position: " << pos.asFen() << " best move: " << bm.asString() << std::endl;
    engine->think();
    engine->stopAllThreads();
    return engine->getTestResult();
}

void test_bratkokopec(TEngine * engine) {
    std::cout << "test_positions test bratkokopec" << std::endl;

    static const int TEST_SIZE = 24;
    int solved = 0;
    int targetScore = 17;

    int results[TEST_SIZE];
    int current = 0;
    memset(results, 0, sizeof (results));

    results[current++] = testForMove(engine, "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - ", "d6d1", -SCORE_MATE);
    results[current++] = testForMove(engine, "3r1k2/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - -", "d4d5", -SCORE_MATE);
    /*
     * The key move is 1. d5, and the main line, as given by Kmoch, goes 
     * as follows: 
     * 1. d5!  cxd5 
     * 2. e5!  R6d7 
3. Nd4 
In the resulting position, white is a pawn down, but has secured 
approximately five advantages: 
1) Black has a backward pawn on the b-file. 
2) White has control of the open c-file. 
3) Black's pressure on the half-open d-file has been negated. 
4) White has a nice knight on d4. 
5) White has achieved an advantageous pawn formation known as a 
"quart-grip" on the e..h files. 
     */

    results[current++] = testForMove(engine, "2q1rr1k/3bbnnp/p2p1pp1/2pPp3/PpP1P1P1/1P2BNNP/2BQ1PRK/7R b - -", "f6f5", -SCORE_MATE);
    results[current++] = testForMove(engine, "rnbqkb1r/p3pppp/1p6/2ppP3/3N4/2P5/PPP1QPPP/R1B1KB1R w KQkq - ", "e5e6", -SCORE_MATE);
    results[current++] = testForMove(engine, "r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 w - -", "c3d5", -SCORE_MATE);
    results[current++] = testForMove(engine, "2r3k1/pppR1pp1/4p3/4P1P1/5P2/1P4K1/P1P5/8 w - -", "g5g6", -SCORE_MATE);
    results[current++] = testForMove(engine, "1nk1r1r1/pp2n1pp/4p3/q2pPp1N/b1pP1P2/B1P2R2/2P1B1PP/R2Q2K1 w - -", "h5f6", -SCORE_MATE);
    results[current++] = testForMove(engine, "4b3/p3kp2/6p1/3pP2p/2pP1P2/4K1P1/P3N2P/8 w - -", "f4f5", -SCORE_MATE);
    results[current++] = testForMove(engine, "2kr1bnr/pbpq4/2n1pp2/3p3p/3P1P1B/2N2N1Q/PPP3PP/2KR1B1R w - -", "f4f5", -SCORE_MATE);
    results[current++] = testForMove(engine, "3rr1k1/pp3pp1/1qn2np1/8/3p4/PP1R1P2/2P1NQPP/R1B3K1 b - -", "c6e5", -SCORE_MATE);
    results[current++] = testForMove(engine, "2r1nrk1/p2q1ppp/bp1p4/n1pPp3/P1P1P3/2PBB1N1/4QPPP/R4RK1 w - -", "f2f4", -SCORE_MATE);
    results[current++] = testForMove(engine, "r3r1k1/ppqb1ppp/8/4p1NQ/8/2P5/PP3PPP/R3R1K1 b - -", "d7f5", -SCORE_MATE);
    results[current++] = testForMove(engine, "r2q1rk1/4bppp/p2p4/2pP4/3pP3/3Q4/PP1B1PPP/R3R1K1 w - -", "b2b4", -SCORE_MATE);
    results[current++] = testForMove(engine, "rnb2r1k/pp2p2p/2pp2p1/q2P1p2/8/1Pb2NP1/PB2PPBP/R2Q1RK1 w - -", "d1d2", -SCORE_MATE);
    results[current++] = testForMove(engine, "2r3k1/1p2q1pp/2b1pr2/p1pp4/6Q1/1P1PP1R1/P1PN2PP/5RK1 w - -", "g4g7", -SCORE_MATE);
    results[current++] = testForMove(engine, "r1bqkb1r/4npp1/p1p4p/1p1pP1B1/8/1B6/PPPN1PPP/R2Q1RK1 w kq - ", "d2e4", -SCORE_MATE);
    results[current++] = testForMove(engine, "r2q1rk1/1ppnbppp/p2p1nb1/3Pp3/2P1P1P1/2N2N1P/PPB1QP2/R1B2RK1 b - -", "h7h5", -SCORE_MATE);
    results[current++] = testForMove(engine, "r1bq1rk1/pp2ppbp/2np2p1/2n5/P3PP2/N1P2N2/1PB3PP/R1B1QRK1 b - -", "c5b3", -SCORE_MATE);
    results[current++] = testForMove(engine, "3rr3/2pq2pk/p2p1pnp/8/2QBPP2/1P6/P5PP/4RRK1 b - -", "e8e4", -SCORE_MATE);
    results[current++] = testForMove(engine, "r4k2/pb2bp1r/1p1qp2p/3pNp2/3P1P2/2N3P1/PPP1Q2P/2KRR3 w - -", "g3g4", -SCORE_MATE);
    results[current++] = testForMove(engine, "3rn2k/ppb2rpp/2ppqp2/5N2/2P1P3/1P5Q/PB3PPP/3RR1K1 w - -", "f5h6", -SCORE_MATE);
    results[current++] = testForMove(engine, "2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - -", "b7e4", -SCORE_MATE);
    results[current++] = testForMove(engine, "r1bqk2r/pp2bppp/2p5/3pP3/P2Q1P2/2N1B3/1PP3PP/R4RK1 b kq - ", "f7f6", -SCORE_MATE);
    results[current++] = testForMove(engine, "r2qnrnk/p2b2b1/1p1p2pp/2pPpp2/1PP1P3/PRNBB3/3QNPPP/5RK1 w - -", "f2f4", -SCORE_MATE);


    for (int i = 0; i < TEST_SIZE; i++) {
        std::cout << "Position " << (i + 1) << ": " << (results[i] ? "Passed" : "Failed") << std::endl;
        solved += results[i];
    }
    std::cout << "Solved " << solved << " positions out of " << TEST_SIZE << std::endl;

    if (solved < targetScore) {
        std::cout << "%TEST_FAILED% time=0 testname=bratkokopec (test_bratkokopec) message=solved: " << solved << " out of 24" << std::endl;
    }
}



int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% test_positions" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    InitMagicMoves();
    TEngine * engine = new TEngine();
    THashTable *globalHashTable = new THashTable(256);
    TOutputHandler oh;
    engine->setHashTable(globalHashTable);
    engine->setOutputHandler(&oh);
    
    std::cout << "%TEST_STARTED% test_positions (test_bratkokopec)\n" << std::endl;
    test_bratkokopec(engine);
    std::cout << "%TEST_FINISHED% time=0 test_positions (test_bratkokopec)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    delete engine;
    delete globalHashTable;

    return (EXIT_SUCCESS);
}
