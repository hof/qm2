/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2014 Erik van het Hof and Hermen Reitsma 
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

#include "score.h"
#include "move.h"

class TSearch;

class material_table_t {
private:

    struct entry_t {
        U64 key;
        int16_t value;
        uint8_t phase;
        uint8_t flags;
    };

    int size;
    int max_hash_key;
    entry_t * table;

    int index(U64 hash_code) {
        return hash_code & max_hash_key;
    }

public:
    bool enabled;

    material_table_t(int size_in_MB);
    void set_size(int size_in_MB);
    void store(U64 key, int value, int phase, int flags);
    bool retrieve(U64 key, int & value, int & phase, int & flags);

    ~material_table_t() {
        delete [] table;
    }

    void clear() {
        memset(table, 0, sizeof (entry_t) * size);
    }

};

namespace material_table {
    const int TABLE_SIZE = 1; //MB
    void store(U64 key, int value, int phase, int flags);
    bool retrieve(U64 key, int & value, int & phase, int & flags);
    void clear();
    void set_size(int size_in_MB);
};

class pawn_table_t {
private:

    struct entry_t {
        U64 key;
        U64 passers;
        score_t score;
        int8_t king_attack[2];
        uint8_t flags;
    };

    int size;
    int max_hash_key;
    entry_t * table;

    int index(U64 hash_code) {
        return hash_code & max_hash_key;
    }

public:
    bool enabled;

    pawn_table_t(int size_in_MB);
    void set_size(int size_in_MB);
    void store(U64 key, U64 passers, score_t score, int king_attack[2], int flags);
    bool retrieve(U64 key, U64 & passers, score_t & score, int (& king_attack)[2], int & flags);

    ~pawn_table_t() {
        delete [] table;
    }

    void clear() {
        memset(table, 0, sizeof (entry_t) * size);
    }
};

namespace pawn_table {
    const int TABLE_SIZE = 4; 
    void store(U64 key, U64 passers, score_t score, int king_attack[2], int flags);
    bool retrieve(U64 key, U64 & passers, score_t & score, int (& king_attack)[2], int & flags);
    void clear();
    void set_size(int size_in_MB);
};

enum TranspositionTableEntryType {
    TT_LOWERBOUND = 1,
    TT_UPPERBOUND = 2,
    TT_EXACT = 3,
    TT_EMPTY = 32002
};

#define TTMOVE(h)  ((h) & 0x0FFFFFFFF)
#define TTFLAG(h)  (((h) >> 32) & 3)
#define TTSCORE(h) ((signed short) (((h) >> 34) & 0x0FFFF))
#define TTDEPTH(h)  (((h) >> 50) & 255)
#define TTPLY(h)    (((h) >> 58) & 63)

struct TTranspositionTableEntry {
    U64 key;
    U64 value;
    //32: bit 0..31 : move  
    //2: bit 32..33: flag (upperbound, lowerboard, exact, 1,2 or 3)
    //16: bit 34..49: score (-32K upto 32K)
    //7: bit 50..57: depth (upto 255 half ply)
    //7: bit 58..63: gameply (upto 63) for aging

    static inline U64 encode(int gamePly, int depth, int score, int flag, int move) {
        assert(gamePly < 64);
        assert(depth < 255);
        assert(score < 32000 && score > -32000);
        assert(flag <= 3);
        U64 result = move | (U64(flag) << 32) | (U64((unsigned short) score) << 34) | (U64(depth) << 50) | (U64(gamePly) << 58);
        return result;
    }
};

class THashTable {
protected:
    int _ttMaxHashKey;
    int _ttSize;
    int _repTableSize;
public:
    THashTable(int totalSizeInMb);
    ~THashTable();

    TTranspositionTableEntry * depthPrefTable;
    TTranspositionTableEntry * alwaysReplaceTable;

    
    U64 repTable[100];

    inline int getTTHashKey(U64 hashCode) {
        return hashCode & _ttMaxHashKey;
    }

    static void ttLookup(TSearch * searchData, int depth, int alpha, int beta);
    static void ttStore(TSearch * searchData, int move, int score, int depth, int alpha, int beta);
    static void repStore(TSearch * searchData, U64 hashCode, int fiftyCount);
    
    void clear();


};


#endif	/* HASHTABLE_H */

