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

THashTable::THashTable(int totalSizeInMb) {
    const int ttEntrySize = sizeof (TTranspositionTableEntry);
    const int ttTableCount = 2;
    const int sizeOfMaterialTable = 1;
    const int sizeOfPawnTable = MIN(256, totalSizeInMb >> 2);
    const int sizeOfTranspositionTable = MAX(1, totalSizeInMb - sizeOfMaterialTable - sizeOfPawnTable);
    const int maxEntriesPerTranspositionTable = (sizeOfTranspositionTable * 1024 * 1024) / (ttTableCount * ttEntrySize);
    const int ttTableSize = maxEntriesPerTranspositionTable ? bsr(maxEntriesPerTranspositionTable) : 0;
    const int materialTableEntrySize = sizeof (TMaterialTableEntry);
    const int maxEntriesMaterialTable = (sizeOfMaterialTable * 1024 * 1024) / materialTableEntrySize;
    const int materialTableSize = maxEntriesMaterialTable ? bsr(maxEntriesMaterialTable) : 0;
    const int pawnTableEntrySize = sizeof (TPawnTableEntry);
    const int maxEntriesPawnTable = (sizeOfPawnTable * 1024 * 1024) / pawnTableEntrySize;
    const int pawnTableSize = maxEntriesPawnTable ? bsr(maxEntriesPawnTable) : 0;

    _ttSize = 1 << ttTableSize;
    alwaysReplaceTable = new TTranspositionTableEntry[_ttSize];
    depthPrefTable = new TTranspositionTableEntry[_ttSize];

    _mtSize = 1 << materialTableSize;
    materialTable = new TMaterialTableEntry[_mtSize];

    _ptSize = 1 << pawnTableSize;
    pawnTable = new TPawnTableEntry[_ptSize];

    _ttMaxHashKey = _ttSize - 1;
    _materialMaxHashKey = _mtSize - 1;
    _pawnMaxHashKey = _ptSize - 1;

    _repTableSize = 100;

    clear();
}

THashTable::~THashTable() {
    delete[] alwaysReplaceTable;
    delete[] depthPrefTable;
    delete[] pawnTable;
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
        TMove move;
        move.setMove(TTMOVE(hashedData));
        board_t * pos = searchData->pos;
        if (move.piece && pos->valid(&move) && pos->legal(&move)) {
            hashHit = true;
            searchData->stack->ttMove1.setMove(&move);
            int nodeType = TTFLAG(hashedData);
            hashedDepth1 = TTDEPTH(hashedData);
            searchData->stack->ttDepth1 = hashedDepth1;
            int hashedScore = TTSCORE(hashedData);
            if (hashedDepth1 >= depth &&
                    ((nodeType == TT_EXACT)
                    || (nodeType == TT_UPPERBOUND && hashedScore <= alpha)
                    || (nodeType == TT_LOWERBOUND && hashedScore >= beta))) {
                if (hashedScore > SCORE_MATE - MAX_PLY) {
                    hashedScore -= pos->current_ply;
                } else if (hashedScore < -SCORE_MATE + MAX_PLY) {
                    hashedScore += pos->current_ply;
                }
                searchData->stack->ttScore = hashedScore;
                assert(hashedScore > -SCORE_MATE && hashedScore < SCORE_MATE);
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
        TMove move;
        move.setMove(TTMOVE(hashedData2));
        board_t * pos = searchData->pos;
        bool equalMove = move.piece && move.equals(&searchData->stack->ttMove1);
        bool legal = equalMove || (pos->valid(&move) && pos->legal(&move));
        if (legal) {
            hashHit = true;
            if (!equalMove) {
                searchData->stack->ttMove2.setMove(&move);
            }
            int nodeType = TTFLAG(hashedData2);
            hashedDepth2 = TTDEPTH(hashedData2);
            searchData->stack->ttDepth2 = hashedDepth2;
            int hashedScore = TTSCORE(hashedData2);
            if (hashedDepth2 >= depth &&
                    ((nodeType == TT_EXACT)
                    || (nodeType == TT_UPPERBOUND && hashedScore <= alpha)
                    || (nodeType == TT_LOWERBOUND && hashedScore >= beta))) {
                if (hashedScore > SCORE_MATE - MAX_PLY) {
                    hashedScore -= pos->current_ply;
                } else if (hashedScore < -SCORE_MATE + MAX_PLY) {
                    hashedScore += pos->current_ply;
                }
                if (searchData->stack->ttScore == TT_EMPTY || hashedDepth2 > hashedDepth1) {
                    searchData->stack->ttScore = hashedScore;
                }
                assert(searchData->stack->ttScore > -SCORE_MATE && searchData->stack->ttScore < SCORE_MATE);
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
    if (searchData->stopSearch || score == SCORE_INVALID || score == -SCORE_INVALID || depth >= MAX_PLY) {
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

    if (score > SCORE_MATE - MAX_PLY) {
        score += searchData->pos->current_ply;
    } else if (score < -SCORE_MATE + MAX_PLY) {
        score -= searchData->pos->current_ply;
    }
    assert(score < SCORE_MATE && score > -SCORE_MATE);


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

void THashTable::mtLookup(TSearch * searchData) {
    searchData->materialTableProbes++;
    THashTable * hashTable = searchData->hashTable;
    TMaterialTableEntry * materialTable = hashTable->materialTable;
    U64 material_hash = searchData->pos->stack->material_hash;
    TMaterialTableEntry entry = materialTable[hashTable->getMaterialHashKey(material_hash)];
    if ((entry.key ^ entry.value) == material_hash) {
        searchData->materialTableHits++;
        searchData->stack->material_score = entry.value;
        searchData->stack->phase = entry.phase;
        searchData->stack->material_flags = entry.flags;
    } else {
        searchData->stack->material_score = SCORE_INVALID;
    }
}

void THashTable::mtStore(TSearch * searchData) {
    THashTable * hashTable = searchData->hashTable;
    TMaterialTableEntry * materialTable = hashTable->materialTable;
    U64 material_hash = searchData->pos->stack->material_hash;
    TMaterialTableEntry * entry = &materialTable[hashTable->getMaterialHashKey(material_hash)];
    entry->value = searchData->stack->material_score;
    entry->phase = searchData->stack->phase;
    entry->flags = searchData->stack->material_flags;
    entry->key = material_hash^entry->value;
}

void THashTable::ptLookup(TSearch * sd) {
    sd->pawnTableProbes++;
    THashTable * hashTable = sd->hashTable;
    U64 pawn_hash = sd->pos->stack->pawn_hash;
    TPawnTableEntry * entry = &hashTable->pawnTable[hashTable->getPawnHashKey(pawn_hash)];
    if ((entry->key ^ entry->pawn_score.mg) == pawn_hash) {
        sd->stack->pawn_score.set(entry->pawn_score);
        sd->stack->king_attack[WPAWN] = entry->king_attack[WHITE];
        sd->stack->king_attack[BPAWN] = entry->king_attack[BLACK];
        sd->stack->passers = entry->passers;
        sd->stack->pawn_flags = entry->pawn_flags;
        sd->pawnTableHits++;
    } else {
        sd->stack->pawn_score.set(SCORE_INVALID);
    }
}

void THashTable::ptStore(TSearch * sd) {
    THashTable * hashTable = sd->hashTable;
    U64 pawn_hash = sd->pos->stack->pawn_hash;
    TPawnTableEntry * entry = &hashTable->pawnTable[hashTable->getPawnHashKey(pawn_hash)];
    entry->pawn_score.set(sd->stack->pawn_score);
    entry->king_attack[WHITE] = sd->stack->king_attack[WPAWN];
    entry->king_attack[BLACK] = sd->stack->king_attack[BPAWN];
    entry->passers = sd->stack->passers;
    entry->pawn_flags = sd->stack->pawn_flags;
    entry->key = (pawn_hash ^ sd->stack->pawn_score.mg);
}

void THashTable::clear() {
    memset(depthPrefTable, 0, sizeof (TTranspositionTableEntry) * _ttSize);
    memset(alwaysReplaceTable, 0, sizeof (TTranspositionTableEntry) * _ttSize);
    memset(materialTable, 0, sizeof (TMaterialTableEntry) * _mtSize);
    memset(pawnTable, 0, sizeof (TPawnTableEntry) * _ptSize);
    memset(repTable, 0, sizeof (U64) * _repTableSize);
}