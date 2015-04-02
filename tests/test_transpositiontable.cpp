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

void test_tt() {
    std::cout << "test_transpositiontable test tt" << std::endl;


    /*
     * Test pawn table store and retrieve
     */
    
    pawn_table::set_size(4);
    search_t * sd = new search_t("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    pawn_table::entry_t * pe = pawn_table::retrieve(sd->brd.stack->pawn_hash);
   
    //pawn entry should contain valid information for the starting position
    if (pe->key != sd->brd.stack->pawn_hash
            || pe->passers != 0
            || pe->score.mg != 0
            || pe->score.eg != 0
            || pe->king_attack[WHITE] != pe->king_attack[BLACK]
            || pe->flags != 0
            ) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=pawn table store/retrieve error 2" << std::endl;
        return;
    }

    /*
     * Test material table store and retrieve
     */
    material_table::set_size(4);
    int value1, value2, phase1, phase2, flags2;
    int flags1 = 4;
    value1 = 910;
    phase1 = 14;
    U64 key = sd->brd.stack->material_hash;
    material_table::store(key, value1, phase1, flags1);
    bool result = material_table::retrieve(key, value2, phase2, flags2);
    if (result == false
            || value1 != value2
            || phase1 != phase2
            || flags1 != flags2
            ) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=material table store/retrieve error" << std::endl;
        return;
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
        result = trans_table::retrieve(keys[i], ply, depth, score, move, flag);
        if (result == false
                || score != i + 10
                || move != i + 100
                || flag != 3) {
            std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=store/retrieve error" << std::endl;
            return;
        }
    }

    move_t * tmove = move::first(sd, 0);
    trans_table::store(sd->brd.stack->tt_key, sd->brd.root_ply, sd->brd.ply, 123, -12345, tmove->to_int(), 3);

    result = trans_table::retrieve(sd->brd.stack->tt_key, sd->brd.ply, 123, score, move, flag);


    if (result == false
            || score != -12345
            || flag != 3
            || move != tmove->to_int()) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=store/retrieve error" << std::endl;
        delete sd;
        return;
    }

    //testing the Lasker-Reichhelm Position
    //test if searching the position with a hashtable finished a depth 15 
    //search with less than 50.000 nodes

    magic::init();
    uci::silent(true);
    engine_t * engine = engine::instance();
    engine::settings()->max_depth = 15;
    engine->new_game("8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - -");
    engine->think();
    engine->stop_all();

    if (engine->get_total_nodes() > 50000) {
        std::cout << "%TEST_FAILED% time=0 testname=test_tt (test_transpositiontable) message=hashtable not effective" << std::endl;
    }
    delete sd;
}

int main(int argc, char** argv) {
    std::cout << "%SUITE_STARTING% test_transpositiontable" << std::endl;
    std::cout << "%SUITE_STARTED%" << std::endl;

    std::cout << "%TEST_STARTED% test_tt (test_transpositiontable)\n" << std::endl;
    test_tt();
    std::cout << "%TEST_FINISHED% time=0 test_tt (test_transpositiontable)" << std::endl;

    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    return (EXIT_SUCCESS);
}

