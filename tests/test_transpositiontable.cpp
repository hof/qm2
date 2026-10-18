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
 * File:   test_transpositiontable.cpp
 *
 * Created on 16-mei-2011, 21:56:13
 */

#include "engine.h"
#include "hashtable.h"
#include "hashcodes.h"

/*
 * Simple C++ Test Suite
 */

bool test_tt() {
    std::cout << "test_transpositiontable test tt" << std::endl;

    search_t * s = new search_t("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    //transposition table - test storing and retrieving
    trans_table::set_size(1);
    int age = 1;
    int ply = 2;
    int depth = 10;
    int score, move, flag;
    U64 keys[4] = {0x1000000123, 0x2000000123, 0x3000000123, 0x4000000123};
    for (int i = 0; i < 4; i++) { //test all buckets
        trans_table::store(keys[i], age, ply, depth, i + 10, i + 100, 3);
    }
    for (int i = 0; i < 4; i++) {
        bool result = trans_table::retrieve(keys[i], ply, depth, score, move, flag);
        if (result == false
                || score != i + 10
                || move != i + 100
                || flag != 3) {
            std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=store/retrieve error" << std::endl;
            delete s;
            return false;
        }
    }

    move_t * tmove = move::first(s, 0);
    trans_table::store(s->brd.stack->tt_key, s->brd.root_ply, s->brd.ply, 123, -12345, tmove->to_compact(), 3);

    bool result = trans_table::retrieve(s->brd.stack->tt_key, s->brd.ply, 123, score, move, flag);


    if (result == false
            || score != -12345
            || flag != 3
            || move != tmove->to_compact()) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=store/retrieve error" << std::endl;
        delete s;
        return false;
    }

    // Verify actual score reuse on the Lasker-Reichhelm position. A fixed node
    // ceiling conflates cache correctness with search policy and move ordering.
    trans_table::clear();
    const char * fen = "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1";
    search_t cold(fen);
    cold.stack->in_check = cold.brd.in_check();
    const int expected = cold.pvs(-score::INF, score::INF, 8);
    search_t warm(fen);
    warm.stack->in_check = warm.brd.in_check();
    const int actual = warm.pvs(-score::INF, score::INF, 8);
    if (cold.stop_all || warm.stop_all || cold.nodes <= 1 || warm.nodes != 1
            || actual != expected || warm.brd.to_string() != fen) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=matching-context score reuse failed" << std::endl;
        delete s;
        return false;
    }
    delete s;
    return true;
}

int main() {
    magic::init();
    std::cout << "%SUITE_STARTING% test_transpositiontable" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% test_tt (test_transpositiontable)\n" << std::endl;
    bool success = test_tt();
    std::cout << "%TEST_FINISHED% time=0 test_tt (test_transpositiontable)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

