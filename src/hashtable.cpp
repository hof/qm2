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
 * File: hashtable.cpp
 * Implementing the main hash table, material (imbalance) table and pawn hash table
 */

#include "hashtable.h"
#include "search.h"

material_table_t::material_table_t(int size_in_MB) {
    table = NULL;
    enabled = true;
    set_size(size_in_MB);
}

void material_table_t::set_size(int size_in_MB) {
    if (table) {
        delete[] table;
    }
    int entry_size = sizeof (entry_t);
    int max_entries = (size_in_MB * 1024 * 1024) / entry_size;
    size = 1 << (max_entries ? bsr(max_entries) : 0);
    table = new entry_t[size];
    max_hash_key = size - 1;
}

bool material_table_t::retrieve(U64 key, int & value, int & phase, int & flags) {
    entry_t & entry = table[index(key)];
    if ((entry.key ^ entry.value) != key || !enabled) {
        return false;
    }
    value = entry.value;
    phase = entry.phase;
    flags = entry.flags;
    return true;
}

void material_table_t::store(U64 key, int value, int phase, int flags) {
    entry_t & entry = table[index(key)];
    entry.value = value;
    entry.phase = phase;
    entry.flags = flags;
    entry.key = key ^ value;
}

namespace material_table {

    material_table_t _global_table(TABLE_SIZE);

    void store(U64 key, int value, int phase, int flags) {
        _global_table.store(key, value, phase, flags);
    }

    bool retrieve(U64 key, int & value, int & phase, int & flags) {
        return _global_table.retrieve(key, value, phase, flags);
    }

    void clear() {
        _global_table.clear();
    }

    void set_size(int size_in_mb) {
        _global_table.set_size(size_in_mb);
    }

};

pawn_table_t::pawn_table_t(int size_in_MB) {
    table = NULL;
    enabled = true;
    set_size(size_in_MB);
}

void pawn_table_t::set_size(int size_in_MB) {
    if (table) {
        delete[] table;
    }
    int entry_size = sizeof (entry_t);
    int max_entries = (size_in_MB * 1024 * 1024) / entry_size;
    size = 1 << (max_entries ? bsr(max_entries) : 0);
    table = new entry_t[size];
    max_hash_key = size - 1;
}

void pawn_table_t::store(U64 key, U64 passers, score_t score, int king_attack[2], int flags) {
    entry_t & entry = table[index(key)];
    entry.key = key ^ score.eg;
    entry.passers = passers;
    entry.score.set(score);
    entry.king_attack[BLACK] = king_attack[BLACK];
    entry.king_attack[WHITE] = king_attack[WHITE];
    entry.flags = flags;
}

bool pawn_table_t::retrieve(U64 key, U64 & passers, score_t & score, int (& king_attack)[2], int & flags) {
    entry_t & entry = table[index(key)];
    if ((entry.key ^ entry.score.eg) != key || !enabled) {
        return false;
    }
    passers = entry.passers;
    score.set(entry.score);
    king_attack[BLACK] = entry.king_attack[BLACK];
    king_attack[WHITE] = entry.king_attack[WHITE];
    flags = entry.flags;
    return true;
}

namespace pawn_table {

    pawn_table_t _global_table(TABLE_SIZE);

    void store(U64 key, U64 passers, score_t score, int king_attack[2], int flags) {
        _global_table.store(key, passers, score, king_attack, flags);
    }

    bool retrieve(U64 key, U64 & passers, score_t & score, int (& king_attack)[2], int & flags) {
        return _global_table.retrieve(key, passers, score, king_attack, flags);
    }

    void clear() {
        _global_table.clear();
    }

    void set_size(int size_in_mb) {
        _global_table.set_size(size_in_mb);
    }

};

THashTable::THashTable(int totalSizeInMb) {
    const int ttEntrySize = sizeof (TTranspositionTableEntry);
    const int ttTableCount = 2;

    const int sizeOfPawnTable = MIN(256, totalSizeInMb >> 2);
    const int sizeOfTranspositionTable = MAX(1, totalSizeInMb - sizeOfPawnTable);
    const int maxEntriesPerTranspositionTable = (sizeOfTranspositionTable * 1024 * 1024) / (ttTableCount * ttEntrySize);
    const int ttTableSize = maxEntriesPerTranspositionTable ? bsr(maxEntriesPerTranspositionTable) : 0;


    _ttSize = 1 << ttTableSize;
    alwaysReplaceTable = new TTranspositionTableEntry[_ttSize];
    depthPrefTable = new TTranspositionTableEntry[_ttSize];

    

    _ttMaxHashKey = _ttSize - 1;

   

    _repTableSize = 100;

    clear();
}

THashTable::~THashTable() {
    delete[] alwaysReplaceTable;
    delete[] depthPrefTable;
}

void THashTable::ttLookup(TSearch * searchData, int depth, int alpha, int beta) {
    bool hashHit = false;
    int hashedDepth1 = 0;
    searchData->stack->ttScore = TT_EMPTY;
    searchData->stack->ttMove1.piece = EMPTY;
    searchData->stack->ttMove2.piece = EMPTY;
    THashTable * hashTable = searchData->hashTable;

    U64 hash_code = searchData->pos->stack->hash_code;
    int hashKey = hashTable->getTTHashKey(hash_code);

    /*
     * 1) Probe the depth preferred table
     */
    searchData->hashProbes++;
    TTranspositionTableEntry * ttEntry = &hashTable->depthPrefTable[hashKey];
    U64 hashedKey = ttEntry->key;
    U64 hashedData = ttEntry->value;
    if ((hashedKey ^ hashedData) == hash_code) {
        move_t move;
        move.set(TTMOVE(hashedData));
        board_t * pos = searchData->pos;
        if (move.piece && pos->valid(&move) && pos->legal(&move)) {
            hashHit = true;
            searchData->stack->ttMove1.set(&move);
            int nodeType = TTFLAG(hashedData);
            hashedDepth1 = TTDEPTH(hashedData);
            searchData->stack->ttDepth1 = hashedDepth1;
            int hashedScore = TTSCORE(hashedData);
            if (hashedDepth1 >= depth &&
                    ((nodeType == TT_EXACT)
                    || (nodeType == TT_UPPERBOUND && hashedScore <= alpha)
                    || (nodeType == TT_LOWERBOUND && hashedScore >= beta))) {
                if (hashedScore > score::MATE - MAX_PLY) {
                    hashedScore -= pos->current_ply;
                } else if (hashedScore < -score::MATE + MAX_PLY) {
                    hashedScore += pos->current_ply;
                }
                searchData->stack->ttScore = hashedScore;
                assert(hashedScore > -score::MATE && hashedScore < score::MATE);
            }
        }
    }

    /*
     * 2) Probe the always replace table
     */
    ttEntry = &hashTable->alwaysReplaceTable[hashKey];
    U64 hashedData2 = ttEntry->value;
    int hashedDepth2 = 0;
    hashedKey = ttEntry->key;
    if ((hashedKey ^ hashedData2) == hash_code) {
        move_t move;
        move.set(TTMOVE(hashedData2));
        board_t * pos = searchData->pos;
        bool equalMove = move.piece && move.equals(&searchData->stack->ttMove1);
        bool legal = equalMove || (pos->valid(&move) && pos->legal(&move));
        if (legal) {
            hashHit = true;
            if (!equalMove) {
                searchData->stack->ttMove2.set(&move);
            }
            int nodeType = TTFLAG(hashedData2);
            hashedDepth2 = TTDEPTH(hashedData2);
            searchData->stack->ttDepth2 = hashedDepth2;
            int hashedScore = TTSCORE(hashedData2);
            if (hashedDepth2 >= depth &&
                    ((nodeType == TT_EXACT)
                    || (nodeType == TT_UPPERBOUND && hashedScore <= alpha)
                    || (nodeType == TT_LOWERBOUND && hashedScore >= beta))) {
                if (hashedScore > score::MATE - MAX_PLY) {
                    hashedScore -= pos->current_ply;
                } else if (hashedScore < -score::MATE + MAX_PLY) {
                    hashedScore += pos->current_ply;
                }
                if (searchData->stack->ttScore == TT_EMPTY || hashedDepth2 > hashedDepth1) {
                    searchData->stack->ttScore = hashedScore;
                }
                assert(searchData->stack->ttScore > -score::MATE && searchData->stack->ttScore < score::MATE);
            }
        }
    }
    searchData->hashHits += hashHit;
}

void THashTable::repStore(TSearch * searchData, U64 hash_code, int fiftyCount) {
    if (fiftyCount < 100 && fiftyCount >= 0) {
        searchData->hashTable->repTable[fiftyCount] = hash_code;
    }
}

void THashTable::ttStore(TSearch * searchData, int move, int score, int depth, int alpha, int beta) {
    if (searchData->stopSearch || score == score::INVALID || score == -score::INVALID || depth >= MAX_PLY) {
        return;
    }

    THashTable * hashTable = searchData->hashTable;
    int root_ply = searchData->pos->root_ply % 64;
    int flags = score >= beta ? TT_LOWERBOUND : (score <= alpha ? TT_UPPERBOUND : TT_EXACT);
    assert(flags);

    U64 hash_code = searchData->pos->stack->hash_code;

    assert(move > 0 || depth == 0);

    int hashKey = hashTable->getTTHashKey(hash_code);

    TTranspositionTableEntry * ttEntry = &hashTable->depthPrefTable[hashKey];
    U64 hashValue = ttEntry->value;

    if (score > score::MATE - MAX_PLY) {
        score += searchData->pos->current_ply;
    } else if (score < -score::MATE + MAX_PLY) {
        score -= searchData->pos->current_ply;
    }
    assert(score < score::MATE && score > -score::MATE);


    U64 newHashValue = ttEntry->encode(root_ply, depth, score, flags, move);

    int hashedGamePly = TTPLY(hashValue);
    int hashedDepth = TTDEPTH(hashValue);


    if (root_ply != hashedGamePly || hashedDepth <= depth) {

        ttEntry->key = (hash_code ^ newHashValue);
        ttEntry->value = newHashValue;

    }
    ttEntry = &hashTable->alwaysReplaceTable[hashKey];
    ttEntry->key = (hash_code ^ newHashValue);
    ttEntry->value = newHashValue;
}


void THashTable::clear() {
    memset(depthPrefTable, 0, sizeof (TTranspositionTableEntry) * _ttSize);
    memset(alwaysReplaceTable, 0, sizeof (TTranspositionTableEntry) * _ttSize);
    memset(repTable, 0, sizeof (U64) * _repTableSize);
}