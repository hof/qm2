/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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
#include <vector>

/*
 * Losers df-pn: proof and disproof numbers of a streak position, the proof length once proven,
 * and how a df-pn call ends.
 */
struct dfpn_entry_t {
    U64 key;
    uint32_t pn, dn;
    int length;
};

struct dfpn_child_t {
    move_t move;
    U64 key; // 0: not in the table
    uint32_t pn, dn;
    int length;
    bool check;
};

struct dfpn_result_t {
    uint32_t pn, dn;
    int length;
    move_t move;
};

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
    }
    
    int w17_pvs(int alpha, int beta, int depth);
    bool streak_try(const move_t * move, int gives_check);
    dfpn_result_t dfpn(uint32_t thpn, uint32_t thdn, bool or_node);
    int streak_children(dfpn_child_t * children, bool or_node, bool & proven);
    static inline std::vector<dfpn_entry_t> dfpn_table; // Shared by the searches of one process: proofs stay facts
    U64 w17_tt_key() const;
    
};

#endif	/* W17_SEARCH_H */

