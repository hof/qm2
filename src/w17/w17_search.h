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
 * File:  search.h
 * Search methods for wild 17 (losers) chess. 
 *
 * Created on 13 oct 2014, 14:08
 */

#ifndef W17_SEARCH_H
#define	W17_SEARCH_H

#include "search.h"
#include "hashcodes.h"

class w17_search_t : public search_t {
public:

    int pvs(int alpha, int beta, int depth, bool cut_node = false) override;
    bool w17_is_draw();

    w17_search_t(const char * fen, const game_t * g = NULL,
            const search_control_t * control = NULL,
            const std::vector<U64> & position_history = {}) : search_t(fen, g, control, position_history, 17) {
        wild = 17;
        evaluation_network.reset();
        evaluation_accumulators.clear();
        book_name = "book.w17.bin";
        
        //use different hash codes for w17
        brd.stack->tt_key ^= hash::codes[13][17];
        for (U64 & key : _position_history) {
            key ^= hash::codes[13][17];
        }
        init_tt_history();
    }
    
    int w17_pvs(int alpha, int beta, int in_mate_search, int depth);
    U64 w17_tt_context(int in_mate_search) const;
    
};

#endif	/* W17_SEARCH_H */

