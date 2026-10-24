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
 * File: hashtable.cpp
 * Implementing the main hash table, material (imbalance) table and pawn hash table
 */

#include "hashtable.h"
#include <sys/mman.h>

trans_table_t::trans_table_t(int size_in_MB) {
    table = nullptr;
    cluster_count = 0;
    enabled = true;
    size_in_mb = -1;
    set_size(size_in_MB);
}

void trans_table_t::set_size(int size_in_MB) {
    if (size_in_mb == size_in_MB) {
        return;
    }
    if (table) {
        ::operator delete[](table, std::align_val_t(TABLE_ALIGNMENT));
    }
    cluster_count = std::max<size_t>(1, size_t(size_in_MB) * 1024 * 1024 / sizeof(cluster_t));
    table = static_cast<cluster_t *>(::operator new[](sizeof(cluster_t) * cluster_count,
        std::align_val_t(TABLE_ALIGNMENT)));
    // Huge pages spare the page walk that random cluster accesses otherwise need.
    madvise(table, sizeof(cluster_t) * cluster_count, MADV_HUGEPAGE);
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

// A decisive score that cannot be realised before the fifty-move rule is
// returned as the best non-decisive bound.
int trans_table_t::unmake_score(int score, int ply, int clock) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    if (score >= decisive) {
        const int distance = score > score::DEEPEST_MATE ? score::MATE - score : score::TABLEBASE - score;
        return distance > 100 - clock ? decisive - 1 : score - ply;
    } else if (score <= -decisive) {
        const int distance = score < -score::DEEPEST_MATE ? score::MATE + score : score::TABLEBASE + score;
        return distance > 100 - clock ? -decisive + 1 : score + ply;
    }
    return score;
}

void trans_table_t::store(U64 key, int age, int ply, int depth, int score, int move, int bound, int eval, bool pv) {
    assert(depth >= 0 && depth < 255);
    assert(bound >= 0 && bound <= 3);
    const int current_age = age % AGES;
    auto & entries = cluster(key).entries;
    const uint16_t key_tag = tag(key);
    const auto age_distance = [current_age](const entry_t & entry) {
        return (AGES + current_age - trans_table_t::age(entry)) % AGES;
    };
    // Prefer the same position or an empty slot, else the shallowest, oldest entry.
    entry_t * slot = &entries[0];
    if (!(slot->depth == 0 || slot->tag == key_tag)) {
        for (int i = 1; i < BUCKETS; ++i) {
            entry_t & entry = entries[i];
            if (entry.depth == 0 || entry.tag == key_tag) {
                slot = &entry;
                break;
            }
            if (slot->depth - 4 * age_distance(*slot) > entry.depth - 4 * age_distance(entry)) {
                slot = &entry;
            }
        }
    }
    const bool same = slot->depth != 0 && slot->tag == key_tag;
    if (!move && same) {
        move = slot->move;
    }
    if (eval == score::INVALID && same) {
        eval = slot->eval;
    }
    const int distance = age_distance(*slot);
    const int stored_bound = slot->meta & 3;
    const int insert_priority = depth + bound + distance * distance / 4 + int(pv);
    const int record_priority = (slot->depth ? slot->depth - 1 : 0) + stored_bound;
    if (!same || (bound == score::EXACT && stored_bound != score::EXACT)
            || insert_priority * 3 >= record_priority * 2) {
        slot->tag = key_tag;
        slot->depth = uint8_t(depth + 1);
        slot->meta = uint8_t((current_age << 3) | (int(pv) << 2) | bound);
        slot->move = uint16_t(move);
        slot->score = int16_t(bound ? make_score(score, ply) : 0);
        slot->eval = int16_t(eval);
    } else if (move || eval != slot->eval) {
        // Keep the stronger score, but retain the newest move and evaluation.
        slot->move = uint16_t(move);
        slot->eval = int16_t(eval);
    }
}

bool trans_table_t::probe(U64 key, int ply, int clock, tt_hit_t & hit) {
    hit = {};
    if (!enabled) {
        return false;
    }
    const uint16_t key_tag = tag(key);
    for (const auto & entry : cluster(key).entries) {
        if (entry.depth != 0 && entry.tag == key_tag) {
            hit.move = entry.move;
            hit.depth = entry.depth - 1;
            hit.bound = entry.meta & 3;
            hit.pv = (entry.meta >> 2) & 1;
            hit.score = hit.bound ? unmake_score(entry.score, ply, clock) : score::INVALID;
            hit.eval = entry.eval;
            return true;
        }
    }
    return false;
}

namespace trans_table {

    trans_table_t _global_table(TABLE_SIZE);

    void store(U64 key, int age, int ply, int depth, int score, int move, int bound, int eval, bool pv) {
        if (_global_table.enabled) {
            _global_table.store(key, age, ply, depth, score, move, bound, eval, pv);
        }
    }

    bool probe(U64 key, int ply, int clock, tt_hit_t & hit) {
        return _global_table.probe(key, ply, clock, hit);
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
}
