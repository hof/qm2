/* 
 * File:   test_wac.cpp
 * Author: Hajewiet
 *
 * Created on 21-mei-2011, 1:01:08
 */


#include "engine.h"

/*
 * Simple C++ Test Suite
 */

const int NODES_PER_TEST = 20000000;

struct test_result_t {
    std::string fen;
    bool solved;
    int nodes;
    int score;
    move_t move;
    std::string expected_move;
    int expected_score;
};

test_result_t test_results[1024];
engine_t * global_engine;
int solved = 0;
int tested = 0;
U64 total_nodes = 0;
int total_score = 0;
int max_score = 0;

void test_fen(std::string fen, std::string move, int expected_score) {
    move_t bm;
    board_t brd;
    brd.init(fen.c_str());
    bm.set(&brd, move.c_str());
    engine_t * engine = global_engine;
    engine::settings()->test_for(&bm, expected_score, NODES_PER_TEST);
    engine->new_game(brd.to_string());
    std::cout << tested + 1 << ") " << brd.to_string() << " bm " << bm.to_string();
    engine->think();
    engine->stop_all();
    test_results[tested].fen = fen;
    test_results[tested].solved = engine->target_found();
    test_results[tested].nodes = engine->get_total_nodes();
    test_results[tested].move = engine->get_move();
    test_results[tested].score = engine->get_score();
    test_results[tested].expected_move = move;
    test_results[tested].expected_score = expected_score;
    tested++;
    std::cout << (engine->target_found() ? " ok" : " fail") << std::endl;
}

void test_positions() {
    test_fen("r7/3PK3/8/3k1p2/8/8/8/2R5 w - - 0 1", "d7d8q", 300);
    test_fen("8/k1b5/P4p2/1Pp2p1p/K1P2P1P/8/3B4/8 w - - 0 1", "b5b6", 300);
    test_fen("8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1", "b3b2", 300);
    test_fen("8/3P1k2/p4b2/1p3R2/3r3p/P4P1B/1KP5/8 b - - 0 1", "d4d5", -50);
    test_fen("8/p3k1p1/4r3/2ppNpp1/PP1P4/2P3KP/5P2/8 b - - 0 1", "e6e5", 300);
    test_fen("3r1k2/2R2p2/p7/1p1bPBp1/1P1p2P1/5P2/1P1K4/8 w - - 0 1", "c7a7", 0);
    test_fen("5bk1/1rQ4p/5pp1/2pP4/3n1PP1/7P/1q3BB1/4R1K1 w - - 0 1", "d5d6", 300);
    test_fen("3rk2r/2R2ppp/p3p3/1b2P2q/4QP2/4N3/1B4PP/6K1 w k - 0 1", "b2a3", 200);
    test_fen("5bk1/4pp1p/Q2p2p1/3P4/4PP2/2rBq1PP/P5K1/3R4 b - - 0 1", "h7h5", 200);   
    test_fen("1r3r1k/3p4/1p1Nn1R1/4Pp1q/pP3P1p/P7/5Q1P/6RK w - - 0 1", "f2e2", 300);
    test_fen("2rq1bk1/p4p1p/1p4p1/3b4/3B1Q2/8/P4PpP/3RR1K1 w - - 0 1", "e1e8", 300);
    test_fen("5rk1/2p4p/2p4r/3P4/4p1b1/1Q2NqPp/PP3P1K/R4R2 b - - 0 1", "f3g2", score::DEEPEST_MATE);
    test_fen("5r2/4q1pk/4Q1pp/P1pPp3/2p1P3/2P3B1/4b1PP/R5K1 b - - 0 1", "e7g5", 50);
    test_fen("2rq1rk1/pp3ppp/2n2b2/4NR2/3P4/PB5Q/1P4PP/3R2K1 w - - 0 1", "h3h7", score::DEEPEST_MATE);
    test_fen("r1bq3r/ppppR1p1/5n1k/3P4/6pP/3Q4/PP1N1PP1/5K1R w - - 0 1", "h4h5", score::DEEPEST_MATE);
    test_fen("5r1k/3b2p1/p6p/1pRpR3/1P1P2q1/P4pP1/5QnP/1B4K1 w - - 0 1", "h2h3", 300);
    test_fen("2kr3r/pp1q1ppp/5n2/1Nb5/2Pp1B2/7Q/P4PPP/1R3RK1 w - - 0 1", "b5a7", 300);
    test_fen("rq2rbk1/6p1/p2p2Pp/1p1Rn3/4PB2/6Q1/PPP1B3/2K3R1 w - - 0 1", "f4h6", 300);
    test_fen("2r1r2k/1q3ppp/p2Rp3/2p1P3/6QB/p3P3/bP3PPP/3R2K1 w - - 0 1", "h4f6", score::DEEPEST_MATE);
    test_fen("1rb1r1k1/p1p2ppp/5n2/2pP4/5P2/2QB4/qNP3PP/2KRB2R b - - 0 1", "e8e2", score::DEEPEST_MATE);
    test_fen("2r1r1k1/pp1q1ppp/3p1b2/3P4/3Q4/5N2/PP2RPPP/4R1K1 w - - 0 1", "d4g4", 300);
    test_fen("2k1r3/1p2Bq2/p2Qp3/Pb1p1p1P/2pP1P2/2P5/2P2KP1/1R6 w - - 0 1", "b1b5", 300);
    test_fen("2rr2k1/pb3pp1/1p1ppq1p/1P6/2PNP3/P2Q1P2/6PP/3R1RK1 b - - 0 1", "f6g5", 50);
    test_fen("4r1k1/p1qr1p2/2pb1Bp1/1p5p/3P1n1R/1B3P2/PP3PK1/2Q4R w - - 0 1", "c1f4", score::DEEPEST_MATE);    
    test_fen("r4rk1/1p2ppbp/p2pbnp1/q7/3BPPP1/2N2B2/PPP4P/R2Q1RK1 b - - 0 1", "e6g4", 50);
    test_fen("1br2rk1/1pqb1ppp/p3pn2/8/1P6/P1N1PN1P/1B3PP1/1QRR2K1 w - - 0 1", "c3e4", 300);
    test_fen("r4rk1/1p1qp2p/p2p3b/2pP4/P1P1B2P/1PN2n1b/1BQ4P/R3R2K b - - 0 1", "d7g4", 100);
    test_fen("2r2rk1/1p2pnb1/p2p3p/q2P2p1/5p1P/2P5/PP3PPB/1K1RQB1R b - - 0 1", "c8c3", 0);
    test_fen("r2qr1k1/5p2/bpnp3p/p1p3PB/P1P1n3/2P1B1P1/3N3P/R2Q1R1K b - - 0 1", "c6e5", -200);
    test_fen("r4rk1/p5p1/1p2p2p/3pPn1P/b1p3Bq/P1P3P1/2PN1P2/R2Q1RK1 b - - 0 1", "f5g3", 200);
    test_fen("1nbq1r1k/3rbp1p/p1p1pp1Q/1p6/P1pPN3/5NP1/1P2PPBP/R4RK1 w - - 0 1", "f3g5", score::DEEPEST_MATE);
    test_fen("nrq4r/2k1p3/1p1pPnp1/pRpP1p2/P1P2P2/2P1BB2/1R2Q1P1/6K1 w - - 0 1", "e3c5", 200);
    test_fen("r2qr1k1/5pb1/bpnp3p/p1p3pn/P1P1P3/2N1BPP1/1P1NB2P/R2Q1R1K w - - 0 1", "f3f4", 50);
    test_fen("r2q2k1/5pb1/bpnpr2p/p1p3pn/P1P1P3/2N1BPP1/1P1NB2P/R2Q1R1K b - - 0 1", "g7c3", -20); 
}

void handle_row(int ix) {
    test_result_t * result = &test_results[ix];
    int performance = 2 * (result->move.to_string() == result->expected_move);
    performance += performance && result->score >= result->expected_score;
    performance += performance && result->nodes < 10000000;
    performance += performance && result->nodes < 1000000;
    std::cout << std::setw(4) << (ix + 1) << " | ";
    std::cout << std::setw(6) << (result->solved ? "ok" : "fail") << " | ";
    std::cout << std::setw(5) << result->nodes / 1000 << " | ";
    std::cout << std::setw(5) << result->move.to_string() << " | ";
    std::cout << std::setw(5) << result->score << " | ";
    std::cout << std::setw(4) << performance << " | ";
    if (!result->solved || result->score < result->expected_score) {
        std::cout << result->expected_move << " " << result->expected_score;
    }
    std::cout << std::endl;
    total_nodes += result->nodes;
    max_score += 5;
    total_score += performance;
}

void test_report(double elapsed) {
    std::cout << "Results:\n\n";
    std::cout << "test | result | nodes | move  | score | perf | expected  \n";
    std::cout << "-----+--------+-------+-------+-------+------+-----------\n";
    for (int i = 0; i < tested; i++) {
        handle_row(i);
    }
    std::cout << "-----+--------+-------+-------+-------+------+-----------\n\n";
    std::cout << "performance: " << (100 * total_score / max_score) << "\n";
    std::cout << "total nodes: " << total_nodes / 1000 << "K\n";
    std::cout << "total time: " << elapsed << "s\n";
}

int main(int argc, char** argv) {
    clock_t begin;
    magic::init();
    global_engine = engine::instance();
    uci::silent(true);
    begin = clock();
    test_positions();
    double elapsed = (clock() - begin) / (1.0 * CLOCKS_PER_SEC);
    test_report(elapsed);
    return (EXIT_SUCCESS);
}