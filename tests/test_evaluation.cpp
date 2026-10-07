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
 * File:   evaluation_test.cpp
 *
 * Created on 31-mei-2011, 16:39:55
 */

#include "engine.h"

namespace {

struct evaluation_case_t {
    const char * name;
    const char * fen;
    int min_score;
    int max_score;
};

bool test_evaluation(const evaluation_case_t & test) {
    pawn_table::clear();
    material_table::clear();
    search_t search(test.fen);
    const int cold_score = evaluate(&search);
    search_t cached_search(test.fen);
    const int warm_score = evaluate(&cached_search);
    search.brd.do_flip();
    const std::string flipped_fen = search.brd.to_string();
    search_t flipped_search(flipped_fen.c_str());
    const int flipped_score = evaluate(&flipped_search);
    const bool success = cold_score >= test.min_score && cold_score <= test.max_score
        && warm_score == cold_score && flipped_score == cold_score;
    if (!success) {
        std::cerr << test.name << ": expected [" << test.min_score << ", "
            << test.max_score << "], cold=" << cold_score << ", warm="
            << warm_score << ", flipped=" << flipped_score << "\n";
    }
    return success;
}

bool test_incremental_evaluation() {
    search_t search("7k/8/8/8/8/8/3R4/K7 w - - 0 1");
    const int original_score = evaluate(&search);
    move_t move;
    move.set(&search.brd, "d2d3");
    search.forward(&move, search.brd.gives_check(&move));
    const int incremental_score = evaluate(&search);
    const std::string child_fen = search.brd.to_string();
    pawn_table::clear();
    material_table::clear();
    search_t fresh_search(child_fen.c_str());
    const int fresh_score = evaluate(&fresh_search);
    search.backward(&move);
    const int restored_score = evaluate(&search);
    const bool success = incremental_score == fresh_score && restored_score == original_score;
    if (!success) {
        std::cerr << "incremental evaluation: child=" << incremental_score
            << ", fresh=" << fresh_score << ", restored=" << restored_score
            << ", original=" << original_score << "\n";
    }
    return success;
}

}

int main() {
    magic::init();
    // Broad bands check material advantage and score perspective without freezing tuning.
    const evaluation_case_t cases[] = {
        {"starting position", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0, 30},
        // Won endgames include the engine's WIN bonus as well as material.
        {"white queen advantage", "7k/8/8/8/8/8/3Q4/K7 w - - 0 1", 500, 10000},
        {"black facing queen advantage", "7k/8/8/8/8/8/3Q4/K7 b - - 0 1", -10000, -500},
        {"white rook advantage", "7k/8/8/8/8/8/3R4/K7 w - - 0 1", 200, 10000},
        {"black facing rook advantage", "7k/8/8/8/8/8/3R4/K7 b - - 0 1", -10000, -200},
    };
    bool success = true;
    for (const evaluation_case_t & test : cases) {
        success &= test_evaluation(test);
    }
    success &= test_incremental_evaluation();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
