/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 *  
 * File:   test_wac.cpp
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
int total_solved = 0;
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
    bool solved = engine->target_found();
    test_results[tested].fen = fen;
    test_results[tested].solved = solved;
    test_results[tested].nodes = engine->get_total_nodes();
    test_results[tested].move = engine->get_move();
    test_results[tested].score = engine->get_score();
    test_results[tested].expected_move = move;
    test_results[tested].expected_score = expected_score;
    tested++;
    total_solved += solved;
    std::cout << (solved ? " ok" : " fail") << std::endl;
}

void test_positions() {
    test_fen("3r1bN1/3p1p1p/pp6/5k2/5P2/P7/1P2PPBq/R2R1K2 w - - 1 0", "d1d5", score::DEEPEST_MATE);
}

void test_w17_positions() {
    int win_score = 1000;
    test_fen("rnbqkbnr/ppp1pppp/3p4/8/P7/8/1PPPPPPP/RNBQKBNR w KQkq - 0 2", "g2g4", win_score); //a4 d6
}

void handle_row(int ix) {
    test_result_t * result = &test_results[ix];
    int performance = 2 * (result->move.to_string() == result->expected_move);
    performance += performance && result->score >= result->expected_score;
    performance += performance && result->nodes < NODES_PER_TEST/2;
    performance += performance && result->nodes < NODES_PER_TEST/4;
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
    std::cout << "solved: " << total_solved << "/" << tested << "\n";
    std::cout << "performance: " << (100 * total_score / max_score) << "\n";
    std::cout << "total nodes: " << total_nodes / 1000 << "K\n";
    std::cout << "total time: " << elapsed << "s\n";
}

int main() {
    clock_t begin;
    magic::init();
    global_engine = engine::instance();
    uci::silent(true);
    begin = clock();
    test_positions();
    options::get_option("Wild")->value = 17;
    test_w17_positions();
    double elapsed = (clock() - begin) / (1.0 * CLOCKS_PER_SEC);
    test_report(elapsed);
    return (EXIT_SUCCESS);
}