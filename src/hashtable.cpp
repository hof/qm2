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


namespace material_table {

    table_t::table_t(int size_in_MB) {
        table = NULL;
        size_in_mb = -1;
        set_size(size_in_MB);
    }

    void table_t::set_size(int size_in_MB) {
        if (size_in_mb == size_in_MB) {
            return;
        }
        if (table) {
            delete[] table;
        }
        int entry_size = sizeof (entry_t);
        int max_entries = (size_in_MB * 1024 * 1024) / entry_size;
        size = 1 << (max_entries ? bsr(max_entries) : 0);
        table = new entry_t[size];
        max_hash_key = size - 1;
        size_in_mb = size_in_MB;
        clear();
    }

    table_t _global_table(TABLE_SIZE);

    entry_t * retrieve(U64 key) {
        return _global_table.retrieve(key);
    }

    void clear() {
        _global_table.clear();
    }

    void set_size(int size_in_mb) {
        _global_table.set_size(size_in_mb);
    }
};

namespace pawn_table {

    table_t::table_t(int size_in_MB) {
        table = NULL;
        size_in_mb = -1;
        set_size(size_in_MB);
    }

    void table_t::set_size(int size_in_MB) {
        if (size_in_mb == size_in_MB) {
            return;
        }
        if (table) {
            delete[] table;
        }
        int entry_size = sizeof (entry_t);
        int max_entries = (size_in_MB * 1024 * 1024) / entry_size;
        size = 1 << (max_entries ? bsr(max_entries) : 0);
        table = new entry_t[size];
        max_hash_key = size - 1;
        size_in_mb = size_in_MB;
        clear();
    }


    table_t _global_table(TABLE_SIZE);

    entry_t * retrieve(U64 key) {
        return _global_table.retrieve(key);
    }

    void clear() {
        _global_table.clear();
    }

    void set_size(int size_in_mb) {
        _global_table.set_size(size_in_mb);
    }
};

namespace rep_table {

    U64 _rep_table[100];

    void store(int fifty_count, U64 hash_code) {
        assert(fifty_count >= 0 && fifty_count < 100);
        _rep_table[fifty_count] = hash_code;
    };

    U64 retrieve(int fifty_count) {
        assert(fifty_count >= 0 && fifty_count < 100);
        return _rep_table[fifty_count];
    }
}

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
    size = 1 << (max_entries ? bsr(max_entries) : 0);
    table = new entry_t[size];
    max_hash_key = size - 1;
    size_in_mb = size_in_MB;
    clear();
}

int trans_table_t::make_score(int score, int ply) {
    if (score > score::DEEPEST_MATE) {
        return score + ply;
    } else if (score < -score::DEEPEST_MATE) {
        return score - ply;
    }
    return score;
}

int trans_table_t::unmake_score(int score, int ply) {
    if (score > score::DEEPEST_MATE) {
        return score - ply;
    } else if (score < -score::DEEPEST_MATE) {
        return score + ply;
    }
    return score;
}

void trans_table_t::store(U64 key, int age, int ply, int depth, int score, int move, int flags) {
    assert(depth > 0);
    entry_t * best_entry = NULL;
    int best_score = -score::INF;
    age = age % 64;
    score = make_score(score, ply);
    U64 value = encode(age, depth, score, move, flags);
    int ix = index(key);
    int bix = ix;
    for (int i = 0; i < BUCKETS; i++) {
        bix = (ix + i) & max_hash_key;
        entry_t & entry = table[bix];
        if ((entry.key ^ entry.value) == key) { //overwrite; note the entry did not work anyway)  
            entry.value = value;
            entry.key = value ^ key;
            return;
        }
        int s = 256 - decode_depth(entry.value);
        int age_diff = age - decode_age(entry.value);
        if (age_diff > 0) {
            s += age_diff * 256;
        } else if (age_diff < 0) {
            s += (63 + age_diff) * 256;
        }
        if (s > best_score) {
            best_score = s;
            best_entry = &entry;
        }
    }
    //no match; store in bucket with 1) oldest age and 2) lowest depth
    assert(best_entry != NULL);
    best_entry->value = value;
    best_entry->key = value ^ key;
}

bool trans_table_t::retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags) {
    assert(depth >= 0);
    move = 0;
    if (enabled) {
        int ix = index(key);
        int bix = ix;
        for (int i = 0; i < BUCKETS; i++) {
            bix = (ix + i) & max_hash_key;
            entry_t & entry = table[bix];
            if ((entry.key ^ entry.value) == key) {
                move = decode_move(entry.value);
                score = unmake_score(decode_score(entry.value), ply);
                flags = decode_flag(entry.value);
                int entry_depth = decode_depth(entry.value);
                return entry_depth >= depth;
            }
        }
    }
    return false;
}

namespace trans_table {

    trans_table_t _global_table(TABLE_SIZE);

    void store(U64 key, int age, int ply, int depth, int score, int move, int flag) {
        _global_table.store(key, age, ply, depth, score, move, flag);
    }

    bool retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags) {
        return _global_table.retrieve(key, ply, depth, score, move, flags);
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