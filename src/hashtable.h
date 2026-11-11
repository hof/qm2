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
#include <new>
#include <atomic>
#include <memory>

// A probed entry. Scores are relative to the probing node; eval is the raw
// static evaluation stored with the entry, or score::INVALID.
struct tt_hit_t {
    int move = 0;
    int depth = 0;
    int bound = 0;
    int score = score::INVALID;
    int eval = score::INVALID;
    bool pv = false;
    void * slot = nullptr; //found entry; a store of the same key may reuse it
};

class trans_table_t {
private:

    // Decoded snapshot: full key, depth (+1, zero marks an empty slot), age/pv/bound,
    // move, score and static evaluation.
    struct entry_t {
        U64 tag;
        uint8_t depth;
        uint8_t meta;
        uint16_t move;
        int16_t score;
        int16_t eval;
    };
    struct atomic_entry_t {
        std::atomic<U64> payload{0};
        std::atomic<U64> verifier{0};
    };
    static_assert(sizeof(atomic_entry_t) == 16);
    static_assert(std::atomic<U64>::is_always_lock_free);
    static entry_t load(const atomic_entry_t & entry);
    static void publish(atomic_entry_t & destination, const entry_t & entry);

    static const int BUCKETS = 4;
    static constexpr size_t TABLE_ALIGNMENT = 2 * 1024 * 1024;
    static const int AGES = 32;

    // Four independently published entries fill a cache line.
    struct alignas(64) cluster_t {
        atomic_entry_t entries[BUCKETS];
    };
    static_assert(sizeof(cluster_t) == 64);

    int size_in_mb;
    size_t cluster_count;
    cluster_t * table;

    cluster_t & cluster(U64 key) {
        return table[size_t((static_cast<__uint128_t>(key) * cluster_count) >> 64)];
    }

    static U64 tag(U64 key) {
        return key;
    }

    static int age(const entry_t & entry) {
        return entry.meta >> 3;
    }

    static int make_score(int score, int ply);
    static int unmake_score(int score, int ply, int clock);

public:
    bool enabled;

    trans_table_t(int size_in_MB);
    void set_size(int size_in_MB);

    // Moves use move_t::to_compact(). Depth 0 is quiescence; a zero bound
    // stores only a move and evaluation.
    void store(U64 key, int age, int ply, int depth, int score, int move, int bound, int eval, bool pv, void * found = nullptr);
    bool probe(U64 key, int ply, int clock, tt_hit_t & hit);

    void prefetch(U64 key) {
        __builtin_prefetch(&cluster(key));
    }

    ~trans_table_t() {
        ::operator delete[](table, std::align_val_t(TABLE_ALIGNMENT));
    }

    void clear() {
        for (size_t i = 0; i < cluster_count; ++i) {
            for (auto & entry : table[i].entries) {
                entry.payload.store(0, std::memory_order_relaxed);
                entry.verifier.store(0, std::memory_order_relaxed);
            }
        }
    }
};

namespace trans_table {
    extern trans_table_t _global_table;
    const int TABLE_SIZE = options::get_value("Hash");
    void store(U64 key, int age, int ply, int depth, int score, int move, int bound,
        int eval = score::INVALID, bool pv = false, void * found = nullptr);
    bool probe(U64 key, int ply, int clock, tt_hit_t & hit);

    // Starts loading the key's cluster so a probe soon after finds it cached.
    inline void prefetch(U64 key) {
        _global_table.prefetch(key);
    }

    void clear();
    void set_size(int size_in_MB);
    void enable();
    void disable();
};

#endif	/* HASHTABLE_H */

