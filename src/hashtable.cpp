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
 * File: hashtable.cpp
 * Implementing the main hash table, material (imbalance) table and pawn hash table
 */

#include "hashtable.h"


trans_table_t::trans_table_t(int size_in_MB) {
    table = NULL;
    enabled = true;
    size_in_mb = -1;
    set_size(size_in_MB);
}

void trans_table_t::set_size(int size_in_MB) {
    if (size_in_mb == size_in_MB) {
        return;
    }
    if (table) {
        delete[] table;
    }
    int entry_size = sizeof (entry_t);
    int max_entries = (size_in_MB * 1024 * 1024) / entry_size;
    cluster_count = MAX(1, max_entries / BUCKETS);
    size = cluster_count * BUCKETS;
    table = new entry_t[size];
    size_in_mb = size_in_MB;
    clear();
}

int trans_table_t::make_score(int score, int ply) {
    if (score >= score::TABLEBASE - MAX_PLY) {
        return score + ply;
    } else if (score <= -score::TABLEBASE + MAX_PLY) {
        return score - ply;
    }
    return score;
}

int trans_table_t::unmake_score(int score, int ply) {
    if (score >= score::TABLEBASE - MAX_PLY) {
        return score - ply;
    } else if (score <= -score::TABLEBASE + MAX_PLY) {
        return score + ply;
    }
    return score;
}

void trans_table_t::store(U64 key, int age, int ply, int depth, int score, int move, int flags, U64 context, clock_range_t clocks, tt_mode_t mode) {
    assert(depth > 0);
    entry_t * best_entry = nullptr;
    entry_t * empty_entry = nullptr;
    int best_score = -score::INF;
    age %= 64;
    score = make_score(score, ply);
    U64 value = encode(age, depth, score, move, flags, clocks, mode);
    const int ix = index(key);
    for (int i = 0; i < BUCKETS; ++i) {
        entry_t & entry = table[ix + i];
        const int stored_depth = decode_depth(entry.value);
        if (!stored_depth) {
            if (!empty_entry) {
                empty_entry = &entry;
            }
            continue;
        }
        if ((entry.key ^ entry.value) == key) {
            if (!move) {
                value |= decode_move(entry.value);
            }
            // Scores from different contexts or clock domains are not competing
            // bounds. Keep the current search's result rather than stranding it.
            const clock_range_t stored_clocks = decode_clocks(entry.value);
            const bool same_domain = entry.context == context
                && stored_clocks.min == clocks.min && stored_clocks.max == clocks.max;
            const auto stored_mode = decode_mode(entry.value);
            const bool keep_main_score = decode_flag(entry.value)
                && stored_mode == tt_mode_t::main_search && mode != tt_mode_t::main_search
                && same_domain && age == decode_age(entry.value);
            const bool replace_score = flags != 0 && !keep_main_score && (!decode_flag(entry.value)
                || flags == score::EXACT || !same_domain || mode != stored_mode
                || age != decode_age(entry.value) || depth >= stored_depth);
            if (replace_score) {
                entry.context = context;
                entry.value = value;
            } else {
                // A useful move does not require replacing the deeper score.
                entry.value = (entry.value & ~U64(65535)) | decode_move(value);
            }
            entry.key = entry.value ^ key;
            return;
        }
        const int age_diff = (age + 64 - decode_age(entry.value)) % 64;
        const int replacement_score = decode_flag(entry.value)
            ? age_diff * 256 + 256 - stored_depth : score::INF;
        if (replacement_score > best_score) {
            best_score = replacement_score;
            best_entry = &entry;
        }
    }
    // Finish checking the cluster for a matching key before using an empty slot.
    best_entry = empty_entry ? empty_entry : best_entry;
    assert(best_entry != nullptr);
    if (!flags && decode_flag(best_entry->value)) {
        return;
    }
    best_entry->context = context;
    best_entry->value = value;
    best_entry->key = value ^ key;
}

bool trans_table_t::retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags, U64 context, int clock, clock_range_t * clocks, tt_mode_t mode) {
    assert(depth >= 0);
    move = 0;
    if (enabled) {
        int ix = index(key);
        int bix = ix;
        for (int i = 0; i < BUCKETS; i++) {
            bix = ix + i;
            entry_t & entry = table[bix];
            if (decode_depth(entry.value) && (entry.key ^ entry.value) == key) {
                move = decode_move(entry.value);
                score = unmake_score(decode_score(entry.value), ply);
                flags = decode_flag(entry.value);
                int entry_depth = decode_depth(entry.value);
                const clock_range_t stored_clocks = decode_clocks(entry.value);
                const auto stored_mode = decode_mode(entry.value);
                const bool compatible_mode = stored_mode == mode;
                const bool usable = compatible_mode && entry_depth >= depth && entry.context == context
                    && stored_clocks.contains(clock);
                if (usable && clocks) {
                    *clocks = stored_clocks;
                }
                return usable;
            }
        }
    }
    return false;
}

namespace trans_table {

    trans_table_t _global_table(TABLE_SIZE);

    void store(U64 key, int age, int ply, int depth, int score, int move, int flag, U64 context, clock_range_t clocks, tt_mode_t mode) {
        _global_table.store(key, age, ply, depth, score, move, flag, context, clocks, mode);
    }

    bool retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags, U64 context, int clock, clock_range_t * clocks, tt_mode_t mode) {
        return _global_table.retrieve(key, ply, depth, score, move, flags, context, clock, clocks, mode);
    }

    void clear() {
        _global_table.clear();
    }

    void set_size(int size_in_mb) {
        _global_table.set_size(size_in_mb);
    }

    void enable() {
        _global_table.enabled = true;
    }

    void disable() {
        _global_table.enabled = false;
    }
};