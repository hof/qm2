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
#include "progress.h"
#include <memory>

int main() {
    magic::init();
    bool success = true;
    for (const char * fen : {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "7k/8/8/8/8/8/3Q4/K7 w - - 0 1",
            "7k/8/8/8/8/8/3Q4/K7 b - - 80 1",
            "7k/8/8/8/8/8/3R4/K7 w - - 0 1"}) {
        auto search = std::make_unique<search_t>(fen);
        nnue::accumulator_t independent;
        const int expected = damp_rule50(nnue::to_centipawns(
            search->evaluation_network->evaluate(search->brd, independent)), search->brd.stack->fifty_count);
        const int initial = evaluate(search.get());
        success &= initial == expected && evaluate(search.get()) == expected;
        search->brd.do_flip();
        auto flipped = std::make_unique<search_t>(search->brd.to_string().c_str());
        nnue::accumulator_t flipped_accumulator;
        const int flipped_expected = damp_rule50(nnue::to_centipawns(
            flipped->evaluation_network->evaluate(flipped->brd, flipped_accumulator)),
            flipped->brd.stack->fifty_count);
        success &= evaluate(flipped.get()) == flipped_expected;
    }
    auto search = std::make_unique<search_t>("7k/8/8/8/8/8/3R4/K7 w - - 0 1");
    const int initial = evaluate(search.get());
    move_t move;
    move.set(&search->brd, "d2d3");
    search->forward(&move, search->brd.gives_check(&move));
    const int child = evaluate(search.get());
    auto fresh = std::make_unique<search_t>(search->brd.to_string().c_str());
    success &= evaluate(fresh.get()) == child;
    search->backward(&move);
    success &= evaluate(search.get()) == initial;
    if (!success) {
        std::cerr << "Neural score, perspective, cache or undo mismatch\n";
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
