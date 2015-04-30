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

namespace material_table {

    const int TABLE_SIZE = 8; //MB

    struct entry_t {
        U64 key;
        int16_t score;
        uint8_t phase; 
        uint8_t attack_force[2];
        uint8_t flags;
    };

    class table_t {
    private:
        int size_in_mb;
        int size;
        int max_hash_key;
        entry_t * table;

        int index(U64 hash_code) {
            return hash_code & max_hash_key;
        }

    public:
        table_t(int size_in_MB);
        void set_size(int size_in_MB);

        ~table_t() {
            delete [] table;
        }

        void clear() {
            memset(table, 0, sizeof (entry_t) * size);
        }

        entry_t * retrieve(U64 key) {
            return &table[index(key)];
        }

    };

    entry_t * retrieve(U64 key);
    void clear();
    void set_size(int size_in_MB);
};


namespace pawn_table {
    
    const int TABLE_SIZE = 64;
    
    const uint8_t FLAG_CLOSED_CENTER = 1;

    struct entry_t {
        U64 key;
        U64 passers;
        U64 mob[2];
        U64 attack[2];
        score_t score; //32
        int8_t king_attack[2]; //16 (48)
        uint8_t open_files[2]; //16 (64)
        uint8_t flags; //8 
        uint8_t width[2]; //16 (24)
        uint8_t count[2]; //16 (40)

        bool is_open_file(int sq, bool us) {
            return open_files[us] & (1 << FILE(sq));
        }
    };

    class table_t {
    private:
        int size_in_mb;
        int size;
        int max_hash_key;
        entry_t * table;

        int index(U64 hash_code) {
            return hash_code & max_hash_key;
        }

    public:
        table_t(int size_in_MB);
        void set_size(int size_in_MB);

        ~table_t() {
            delete [] table;
        }

        void clear() {
            memset(table, 0, sizeof (entry_t) * size);
        }

        entry_t * retrieve(U64 key) {
            return &table[index(key)];
        }

    };

    //global access
    entry_t * retrieve(U64 key);
    void clear();
    void set_size(int size_in_mb);

};

namespace rep_table {
    void store(int fifty_count, U64 hash_code);
    U64 retrieve(int fifty_count);
};

class trans_table_t {
private:

    static const int BUCKETS = 4;

    struct entry_t {
        U64 key;
        U64 value;
    };

    int size_in_mb;
    int size;
    int max_hash_key;
    entry_t * table;

    int index(U64 hash_code) {
        return hash_code & max_hash_key;
    }

    /**
     * Encodes ply, depth, score, flag and move into single U64 integer
     * 0..31 | 32..33 (flag)        | 34..49 | 50..57 | 58..63
     * move  | up=1, low=2, exact=3 | score  | depth  | age (using root ply)
     */
    U64 encode(int age, int depth, int score, int move, int flag) {
        assert(age >= 0 && age <= 63);
        assert(depth >= 0 && depth <= 255);
        assert(score < score::INF);
        assert(score > -score::INF);
        assert(flag >= 0 && flag <= 3);
        U64 result = move | (U64(flag) << 32) | (U64((unsigned short) score) << 34) | (U64(depth) << 50) | (U64(age) << 58);
        return result;
    }

    uint32_t decode_move(U64 x) {
        return x & 0x0FFFFFFFF;
    }

    uint8_t decode_flag(U64 x) {
        return (x >> 32) & 3;
    }

    int16_t decode_score(U64 x) {
        return (x >> 34) & 0x0FFFF;
    }

    uint8_t decode_depth(U64 x) {
        return (x >> 50) & 255;
    }

    uint8_t decode_age(U64 x) {
        return (x >> 58) & 63;
    }

    int make_score(int score, int ply);

    int unmake_score(int score, int ply);

public:
    bool enabled;

    trans_table_t(int size_in_MB);
    void set_size(int size_in_MB);

    void store(U64 key, int age, int ply, int depth, int score, int move, int flag);
    bool retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags);

    ~trans_table_t() {
        delete [] table;
    }

    void clear() {
        memset(table, 0, sizeof (entry_t) * size);
    }
};

namespace trans_table {
    const int TABLE_SIZE = options::get_value("Hash");
    void store(U64 key, int age, int ply, int depth, int score, int move, int flag);
    bool retrieve(U64 key, int ply, int depth, int & score, int & move, int & flags);
    void clear();
    void set_size(int size_in_MB);
    void enable();
    void disable();
};

#endif	/* HASHTABLE_H */

