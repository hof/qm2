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
    int win_score = 1000;
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/8/P7/1PPPPPPP/RNBQKBNR w KQkq - 0 2", "g2g4", win_score); //a3 d6
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/P7/8/1PPPPPPP/RNBQKBNR w KQkq - 0 2", "g2g4", win_score); //a4 d6
    test_fen("rnbqkbnr/1ppppppp/p7/8/1P6/8/P1PPPPPP/RNBQKBNR w KQkq - 0 2", "b4b5", win_score); //b4 a6
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/1P6/8/P1PPPPPP/RNBQKBNR w KQkq - 0 2", "g2g4", win_score); //b4 d6
    test_fen("rnbqkbnr/pppp1ppp/8/4p3/1P6/8/P1PPPPPP/RNBQKBNR w KQkq e6 0 2", "h2h3", win_score); //b4 e5
    test_fen("rnbqkbnr/ppppp1pp/8/5p2/1P6/8/P1PPPPPP/RNBQKBNR w KQkq f6 0 2", "e2e4", win_score); //b4 f5
    test_fen("rnbqkbnr/ppppppp1/8/7p/1P6/8/P1PPPPPP/RNBQKBNR w KQkq h6 0 2", "g2g4", win_score); //b4 h5
    test_fen("rnbqkb1r/pppppppp/5n2/8/1P6/8/P1PPPPPP/RNBQKBNR w KQkq - 1 2", "e2e4", win_score); //b4 Nf6
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2", "c4c5", win_score); //c4 d6
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2", "c4c5", win_score); //c4 d6
    test_fen("rnbqkbnr/1ppppppp/8/p7/8/3P4/PPP1PPPP/RNBQKBNR w KQkq a6 0 2", "c1h6", win_score); //d3 a5
    test_fen("rnbqkbnr/1ppppppp/8/p7/3P4/8/PPP1PPPP/RNBQKBNR w KQkq a6 0 2", "c1h6", win_score); //d4 a5
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", "g2g4", win_score); //d4 d6
    test_fen("rnbqkbnr/ppppp1pp/5p2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", "c1g5", win_score); //d4 f6
    test_fen("rnbqkbnr/ppppp1pp/8/5p2/3P4/8/PPP1PPPP/RNBQKBNR w KQkq f6 0 2", "e2e4", win_score); //d4 f5
    test_fen("rnbqkbnr/ppppp1pp/5p2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4e5", win_score); //e4 f6, Qh5 also works
    test_fen("rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "f2f4", win_score); //e4 Nf6
    test_fen("rnbqkbnr/ppp1pppp/8/3p4/5P2/8/PPPPP1PP/RNBQKBNR w KQkq d6 0 2", "e2e4", win_score); //f4 d5
    test_fen("rnbqkbnr/ppppp1pp/8/5p2/5P2/8/PPPPP1PP/RNBQKBNR w KQkq f6 0 2", "e2e4", win_score); //f4 f5
    test_fen("rnbqkbnr/ppppppp1/8/7p/5P2/8/PPPPP1PP/RNBQKBNR w KQkq h6 0 2", "g2g4", win_score); //f4 h5
    test_fen("rnbqkb1r/pppppppp/5n2/8/5P2/8/PPPPP1PP/RNBQKBNR w KQkq - 1 2", "e2e4", win_score); //f4 Nf6
    test_fen("rnbqkbnr/ppp1pppp/8/3p4/8/6P1/PPPPPP1P/RNBQKBNR w KQkq d6 0 2", "e2e4", win_score); //g3 s5
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/6P1/8/PPPPPP1P/RNBQKBNR w KQkq - 0 2", "a2a4", win_score); //g4 d6
    test_fen("rnbqkbnr/ppp1pppp/8/3p4/6P1/8/PPPPPP1P/RNBQKBNR w KQkq d6 0 2", "e2e4", win_score); //g4 d5
    test_fen("rnbqkbnr/ppp1pppp/8/3p4/8/7P/PPPPPPP1/RNBQKBNR w KQkq d6 0 2", "e2e4", win_score); //h3 d5
    test_fen("rnbqkbnr/pppp1ppp/8/4p3/8/7P/PPPPPPP1/RNBQKBNR w KQkq e6 0 2", "f2f4", win_score); //h3 e5
    test_fen("rnbqkbnr/ppppp1pp/8/5p2/8/7P/PPPPPPP1/RNBQKBNR w KQkq f6 0 2", "e2e4", win_score); //h3 f5
    test_fen("rnbqkbnr/ppppp1pp/8/5p2/7P/8/PPPPPPP1/RNBQKBNR w KQkq f6 0 2", "e2e4", win_score); //h4 f5
    test_fen("rnbqkbnr/ppppppp1/8/7p/7P/8/PPPPPPP1/RNBQKBNR w KQkq h6 0 2", "g2g4", win_score); //h4 h5
    test_fen("rnq1kb1r/1b1pn3/p4p1p/p5p1/P2Pp3/NP2P3/4NPPP/R1B1Q1RK w kq - 1 14", "e1a5", win_score);
    test_fen("2r1k2r/4p3/p6n/8/p3p2p/8/P2P3P/RN1K3R b - - 3 1", "c8d8",  win_score);
    test_fen("8/4k3/1PP4n/7P/8/N1P5/1P1K4/2Q2BN1 w - - 0 23", "c1e1", win_score);
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
    global_engine->set_wild(17);
    uci::silent(true);
    begin = clock();
    test_positions();
    double elapsed = (clock() - begin) / (1.0 * CLOCKS_PER_SEC);
    test_report(elapsed);
    return (EXIT_SUCCESS);
}