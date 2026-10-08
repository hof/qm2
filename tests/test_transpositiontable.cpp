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

    pawn_table::set_size(4);
    material_table::set_size(4);

    /*
     * Test pawn table store and retrieve
     */

    search_t * s = new search_t("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    evaluate(s);
    pawn_table::entry_t * pe = pawn_table::retrieve(s->brd.stack->pawn_hash);

    //pawn entry should contain valid information for the starting position
    if (pe->key != s->brd.stack->pawn_hash
            || pe->passers != 0
            || pe->score.mg != 0
            || pe->score.eg != 0
            || pe->king_attack[WHITE] != pe->king_attack[BLACK]
            || pe->flags != 0
            ) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=pawn table store/retrieve error 2" << std::endl;
        delete s;
        return false;
    }

    /*
     * Test material table store and retrieve
     */

    U64 key = s->brd.stack->material_hash;
    material_table::entry_t * me = material_table::retrieve(key);   

    //Starting attack force: queen (9), two rooks (10), four minors (12).
    //check for valid entry
    if (me->key != key
            || me->score != 0
            || me->phase != 0
            || me->flags != 96
            || me->attack_force[WHITE] != 31
            || me->attack_force[BLACK] != 31
            ) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=material table store/retrieve error" << std::endl;
        delete s;
        return false;
    }

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

