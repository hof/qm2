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
 * File: hashtable.h
 * Implementing the main hash table, material (imbalance) table and pawn hash table
 *
 * Created on 13 mei 2011, 21:42
 */

#ifndef HASHTABLE_H
#define	HASHTABLE_H

#include "board.h"
#include "score.h"
#include "game.h"

// Clock values for which the searched result remains valid. Child constraints
// include quiescence and TT cutoffs; 101 denotes no upper limit.
struct clock_range_t {
    uint8_t min;
    uint8_t max;

    bool contains(int clock) const {
        return clock >= min && (max == 101 || clock <= max);
    }

    void include_child(clock_range_t child, int advance) {
        min = MAX(min, MAX(0, int(child.min) - advance));
        if (child.max != 101) {
            max = MIN(max, MAX(0, int(child.max) - advance));
        }
    }
};

class trans_table_t {
private:

    static const int BUCKETS = 4;

    struct entry_t {
        U64 key;
        U64 value;
        U64 context;
    };
    static_assert(sizeof(entry_t) == 24);

    int size_in_mb;
    int size;
    int cluster_count;
    entry_t * table;

    int index(U64 hash_code) {
        return int((static_cast<__uint128_t>(hash_code) * cluster_count) >> 64) * BUCKETS;
    }

    // Move 16, bound 2, score 16, depth 8, age 6, clock limits 7 each.
    U64 encode(int age, int depth, int score, int move, int flag, clock_range_t clocks) {
        assert(age >= 0 && age <= 63);
        assert(depth > 0 && depth <= 255);
        assert(score < score::INF && score > -score::INF);
        assert(move >= 0 && move <= 65535);
        assert(flag >= 0 && flag <= 3);
        assert(clocks.min <= clocks.max && clocks.max <= 101);
        return U64(move) | (U64(flag) << 16) | (U64(uint16_t(score)) << 18)
            | (U64(depth) << 34) | (U64(age) << 42)
            | (U64(clocks.min) << 48) | (U64(clocks.max) << 55);
    }

    uint16_t decode_move(U64 value) {
        return value & 65535;
    }

    uint8_t decode_flag(U64 value) {
        return (value >> 16) & 3;
    }

    int16_t decode_score(U64 value) {
        return (value >> 18) & 65535;
    }

    uint8_t decode_depth(U64 value) {
        return (value >> 34) & 255;
    }

    uint8_t decode_age(U64 value) {
        return (value >> 42) & 63;
    }

    clock_range_t decode_clocks(U64 value) {
        return {uint8_t((value >> 48) & 127), uint8_t((value >> 55) & 127)};
    }

    int make_score(int score, int ply);

    int unmake_score(int score, int ply);

public:
    bool enabled;

    trans_table_t(int size_in_MB);
    void set_size(int size_in_MB);

    // Moves use move_t::to_compact(); score validation retains both full keys.
    void store(U64 key, int age, int ply, int depth, int score, int move, int flag, U64 context = 0, clock_range_t clocks = {0, 101});
    bool retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags, U64 context = 0, int clock = 0, clock_range_t * clocks = nullptr);

    ~trans_table_t() {
        delete [] table;
    }

    void clear() {
        memset(table, 0, sizeof (entry_t) * size);
    }
};

namespace trans_table {
    const int TABLE_SIZE = options::get_value("Hash");
    void store(U64 key, int age, int ply, int depth, int score, int move, int flag, U64 context = 0, clock_range_t clocks = {0, 101});
    bool retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags, U64 context = 0, int clock = 0, clock_range_t * clocks = nullptr);
    void clear();
    void set_size(int size_in_MB);
    void enable();
    void disable();
};

#endif	/* HASHTABLE_H */

