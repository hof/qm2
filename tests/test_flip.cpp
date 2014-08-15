#include <stdlib.h>
#include <iostream>

#include "search.h"
#include "engine.h"

/*
 * Simple C++ Test Suite
 */

TEngine * global_engine;
bool stop_test;

bool flipTest(int test_num, const char * fen) {
    if (stop_test) {
        return false;
    }
    stop_test = true;
    board_t pos;
    pos.create(fen);
    
    std::string fen1 = pos.to_string();

    //flip
    pos.flip();
    std::string fen2 = pos.to_string();

    //flip back
    pos.flip();
    std::string fen3 = pos.to_string();

    //test 1: fen notations should be equal
    if (fen1 != fen3) {
        std::cout << "\ntesting position " << test_num << ": " << pos.to_string() << std::endl;
        std::cout << "%TEST_FAILED% time=0 testname=flip (flip_test) message=FEN does not match" << std::endl;
        std::cout << "original: " << fen1 << std::endl;
        std::cout << "flipped:  " << fen2 << std::endl;
        std::cout << "restored: " << fen3 << std::endl;
        return false;
    }

    //test 2: evaluation and phase should be equal
    TSearch * s1 = new TSearch(fen1.c_str());
    TSearch * s2 = new TSearch(fen2.c_str());
    int score1 = evaluate(s1);
    int phase1 = s1->stack->phase;
    int score2 = evaluate(s2);
    int phase2 = s1->stack->phase;
    delete s1;
    delete s2;
    if (score1 != score2 || phase1 != phase2) {
        std::cout << "\ntesting position " << test_num << ": " << pos.to_string() << std::endl;
        std::cout << "%TEST_FAILED% time=0 testname=flip (flip_test) message=Evaluation does not match" << std::endl;
        std::cout << "original: " << fen1 << " score: " << score1 << " phase: " << phase1 << std::endl;
        std::cout << "flipped:  " << fen2 << " score: " << score2 << " phase: " << phase2 << std::endl;
        global_engine->newGame(pos.to_string());
        global_engine->analyse();
        pos.flip();
        global_engine->newGame(pos.to_string());
        global_engine->analyse();
        return false;
    }

    global_engine->newGame(pos.to_string());
    global_engine->think();
    global_engine->stopAllThreads();
    int nodes1 = global_engine->getNodesSearched();
    score1 = global_engine->getScore();

    pos.flip();
    
    global_engine->newGame(pos.to_string());
    global_engine->think();
    global_engine->stopAllThreads();
    int nodes2 = global_engine->getNodesSearched();
    score2 = global_engine->getScore();
    
    if (score1 != score2) {
        std::cout << "\ntesting position " << test_num << ": " << pos.to_string() << std::endl;
        std::cout << "%TEST_FAILED% time=0 testname=flip (flip_test) message=Search does not match" << std::endl;
        std::cout << "original: " << fen1 << " score: " << score1 << " nodes: " << nodes1 << std::endl;
        std::cout << "flipped:  " << fen2 << " score: " << score2 << " nodes: " << nodes2 << std::endl;
        return false;
    }
    stop_test = false;
    return true;
}

void flipTestSuite(TEngine * engine, int depth) {
    engine->gameSettings.maxDepth = depth;
    flipTest(1, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    flipTest(2, "8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1");
    flipTest(3, "2kr3r/pp1q1ppp/5n2/1Nb5/2Pp1B2/7Q/P4PPP/1R3RK1 w - - 0 1");
    flipTest(4, "8/p3k1p1/4r3/2ppNpp1/PP1P4/2P3KP/5P2/8 b - - 0 1 ");
    flipTest(5, "r4rk1/1p2ppbp/p2pbnp1/q7/3BPPP1/2N2B2/PPP4P/R2Q1RK1 b - - 0 1");
    flipTest(6, "8/k1b5/P4p2/1Pp2p1p/K1P2P1P/8/3B4/8 w - - 0 1");
    flipTest(7, "2rq1bk1/p4p1p/1p4p1/3b4/3B1Q2/8/P4PpP/3RR1K1 w - - 0 1");
    flipTest(8, "r1bq3r/ppppR1p1/5n1k/3P4/6pP/3Q4/PP1N1PP1/5K1R w - - 0 1");
    flipTest(9, "5bk1/1rQ4p/5pp1/2pP4/3n1PP1/7P/1q3BB1/4R1K1 w - - 0 1");
    flipTest(10, "5rk1/2p4p/2p4r/3P4/4p1b1/1Q2NqPp/PP3P1K/R4R2 b - - 0 1");
    flipTest(11, "2r1r2k/1q3ppp/p2Rp3/2p1P3/6QB/p3P3/bP3PPP/3R2K1 w - - 0 1");
    flipTest(12, "8/8/8/1p5r/p1p1k1pN/P2pBpP1/1P1K1P2/8 b - - 0 1");
    flipTest(13, "1r3r1k/3p4/1p1Nn1R1/4Pp1q/pP3P1p/P7/5Q1P/6RK w - - 0 1");
    flipTest(14, "3r4/1p2k2p/p1b1p1p1/4Q1Pn/2B3KP/4pP2/PP2R1N1/6q1 b - - 0 1");
    flipTest(15, "2k1r3/1p2Bq2/p2Qp3/Pb1p1p1P/2pP1P2/2P5/2P2KP1/1R6 w - - 0 1");
    flipTest(16, "1rb1r1k1/p1p2ppp/5n2/2pP4/5P2/2QB4/qNP3PP/2KRB2R b - - 0 1");
    flipTest(17, "2r1r1k1/pp1q1ppp/3p1b2/3P4/3Q4/5N2/PP2RPPP/4R1K1 w - - 0 1");
    flipTest(18, "nrq4r/2k1p3/1p1pPnp1/pRpP1p2/P1P2P2/2P1BB2/1R2Q1P1/6K1 w - - 0 1");
    flipTest(19, "5r1k/3b2p1/p6p/1pRpR3/1P1P2q1/P4pP1/5QnP/1B4K1 w - - 0 1");
    flipTest(20, "1nbq1r1k/3rbp1p/p1p1pp1Q/1p6/P1pPN3/5NP1/1P2PPBP/R4RK1 w - - 0 1");
    flipTest(21, "4r1k1/p1qr1p2/2pb1Bp1/1p5p/3P1n1R/1B3P2/PP3PK1/2Q4R w - - 0 1");
    flipTest(22, "5r2/1p1RRrk1/4Qq1p/1PP3p1/8/4B3/1b3P1P/6K1 w - - 0 1");
    flipTest(23, "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - ");
    flipTest(24, "3r1k2/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - -");
    flipTest(25, "2q1rr1k/3bbnnp/p2p1pp1/2pPp3/PpP1P1P1/1P2BNNP/2BQ1PRK/7R b - -");
    flipTest(26, "rnbqkb1r/p3pppp/1p6/2ppP3/3N4/2P5/PPP1QPPP/R1B1KB1R w KQkq - ");
    flipTest(27, "r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 w - -");
    flipTest(28, "2r3k1/pppR1pp1/4p3/4P1P1/5P2/1P4K1/P1P5/8 w - -");
    flipTest(29, "1nk1r1r1/pp2n1pp/4p3/q2pPp1N/b1pP1P2/B1P2R2/2P1B1PP/R2Q2K1 w - -");
    flipTest(30, "4b3/p3kp2/6p1/3pP2p/2pP1P2/4K1P1/P3N2P/8 w - -");
    flipTest(31, "2kr1bnr/pbpq4/2n1pp2/3p3p/3P1P1B/2N2N1Q/PPP3PP/2KR1B1R w - -");
    flipTest(32, "3rr1k1/pp3pp1/1qn2np1/8/3p4/PP1R1P2/2P1NQPP/R1B3K1 b - -");
    flipTest(33, "2r1nrk1/p2q1ppp/bp1p4/n1pPp3/P1P1P3/2PBB1N1/4QPPP/R4RK1 w - -");
    flipTest(34, "r3r1k1/ppqb1ppp/8/4p1NQ/8/2P5/PP3PPP/R3R1K1 b - -");
    flipTest(35, "r2q1rk1/4bppp/p2p4/2pP4/3pP3/3Q4/PP1B1PPP/R3R1K1 w - -");
    flipTest(36, "rnb2r1k/pp2p2p/2pp2p1/q2P1p2/8/1Pb2NP1/PB2PPBP/R2Q1RK1 w - -");
    flipTest(37, "2r3k1/1p2q1pp/2b1pr2/p1pp4/6Q1/1P1PP1R1/P1PN2PP/5RK1 w - -");
    flipTest(38, "r1bqkb1r/4npp1/p1p4p/1p1pP1B1/8/1B6/PPPN1PPP/R2Q1RK1 w kq - ");
    flipTest(39, "r2q1rk1/1ppnbppp/p2p1nb1/3Pp3/2P1P1P1/2N2N1P/PPB1QP2/R1B2RK1 b - -");
    flipTest(40, "r1bq1rk1/pp2ppbp/2np2p1/2n5/P3PP2/N1P2N2/1PB3PP/R1B1QRK1 b - -");
    flipTest(41, "3rr3/2pq2pk/p2p1pnp/8/2QBPP2/1P6/P5PP/4RRK1 b - -");
    flipTest(42, "r4k2/pb2bp1r/1p1qp2p/3pNp2/3P1P2/2N3P1/PPP1Q2P/2KRR3 w - -");
    flipTest(43, "3rn2k/ppb2rpp/2ppqp2/5N2/2P1P3/1P5Q/PB3PPP/3RR1K1 w - -");
    flipTest(44, "2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - -");
    flipTest(45, "r1bqk2r/pp2bppp/2p5/3pP3/P2Q1P2/2N1B3/1PP3PP/R4RK1 b kq - ");
    flipTest(46, "r2qnrnk/p2b2b1/1p1p2pp/2pPpp2/1PP1P3/PRNBB3/3QNPPP/5RK1 w - -");
    flipTest(47, "r3k2r/p1p1P3/bp1b1ppp/3R4/8/1P2BN1P/P1P2PP1/4R1K1 w - - 0 21");
    flipTest(48, "k7/8/1K6/8/8/8/8/8 w - - 0 1");
    flipTest(49, "8/8/1k6/8/1K6/8/8/8 w - - 0 1");
    flipTest(50, "6k1/8/8/8/8/8/8/1K6 w - - 0 1");
    flipTest(51, "6k1/8/8/8/8/8/K7/8 w - - 0 1");
    flipTest(52, "7k/8/6K1/3N4/8/8/8/8 w - - 0 1");
    flipTest(53, "7k/8/6K1/8/5B2/8/8/8 w - - 0 1");
    flipTest(54, "5k2/8/2N2K2/8/5N2/8/8/8 w - - 0 1");
    flipTest(55, "8/8/8/8/4k3/8/7K/7R w - - 0 1");
    flipTest(56, "8/8/8/8/4k3/8/Q7/K7 w - - 0 1");
    flipTest(57, "8/8/8/8/4k3/8/8/K2B2B1 w - - 0 1");
    flipTest(58, "8/8/8/8/4k3/8/8/K2B2N1 w - - 0 1");
    flipTest(59, "7R/8/8/2n5/6k1/r7/4K3/8 w - - 0 78");
    flipTest(60, "8/K7/8/4k3/3n4/2N5/R7/8 w - - 0 1");
    flipTest(61, "6r1/K7/8/4k3/3n4/2N5/R7/1R6 w - - 0 1");
    flipTest(62, "8/8/8/4b3/3k4/2N5/RP6/K7 w - - 0 1");
    flipTest(63, "8/8/8/4b3/3k4/2N1q3/RP6/KQ6 w - - 0 1");
    flipTest(1601, "7k/8/8/8/8/8/P7/K7 w - - 0 1"); 
    flipTest(1602, "8/1k6/8/8/8/7K/7P/8 w - - 0 1");
    flipTest(1603, "8/2k5/8/8/8/7K/7P/8 w - - 0 1");
    flipTest(1701, "8/8/8/8/6k1/6bp/8/6K1 w - - 0 1");
    flipTest(1801, "8/Pk6/8/1N6/8/8/5K2/8 w - - 0 1");
}

int main(int argc, char** argv) {
    magic::init();
    global_engine = engine::instance();
    trans_table::disable();
    uci::silent(true);
    static const int MAX_TEST_DEPTH = 1;
    stop_test = false;

    clock_t begin;
    clock_t now;

    std::cout << "%SUITE_STARTING% flip_test" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;
    std::cout << "%TEST_STARTED% flip (flip_test)\n" << std::endl;
    std::cout << "flip_test flip" << std::endl;
    begin = clock();
    flipTestSuite(global_engine, MAX_TEST_DEPTH);
    now = clock();
    double elapsed = (now - begin) / CLOCKS_PER_SEC;
    std::cout << "%TEST_FINISHED% time=" << elapsed << " flip (flip_test)" << std::endl;
    std::cout << "%SUITE_FINISHED% time=0" << elapsed << std::endl;
    return (EXIT_SUCCESS);
}

