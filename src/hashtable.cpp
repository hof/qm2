#include "hashtable.h"
#include "search.h"
#include "search.h"

THashTable::THashTable(int totalSizeInMb) {
    const int ttEntrySize = sizeof (TTranspositionTableEntry);
    const int ttTableCount = 2;
    const int sizeOfMaterialTable = 1;
    const int sizeOfPawnTable = MIN(256, totalSizeInMb >> 2);
    const int sizeOfEvalTable = 1;
    const int sizeOfTranspositionTable = MAX(1, totalSizeInMb - sizeOfMaterialTable - sizeOfPawnTable - sizeOfEvalTable);
    const int maxEntriesPerTranspositionTable = (sizeOfTranspositionTable * 1024 * 1024) / (ttTableCount * ttEntrySize);
    const int ttTableSize = maxEntriesPerTranspositionTable ? bitScanReverse(maxEntriesPerTranspositionTable) : 0;
    const int materialTableEntrySize = sizeof (TMaterialTableEntry);
    const int maxEntriesMaterialTable = (sizeOfMaterialTable * 1024 * 1024) / materialTableEntrySize;
    const int materialTableSize = maxEntriesMaterialTable ? bitScanReverse(maxEntriesMaterialTable) : 0;
    const int pawnTableEntrySize = sizeof (TPawnTableEntry);
    const int maxEntriesPawnTable = (sizeOfPawnTable * 1024 * 1024) / pawnTableEntrySize;
    const int pawnTableSize = maxEntriesPawnTable ? bitScanReverse(maxEntriesPawnTable) : 0;
    const int evalTableEntrySize = sizeof (TEvalTableEntry);
    const int maxEntriesEvalTable = (sizeOfEvalTable * 1024 * 1024) / evalTableEntrySize;
    const int evalTableSize = maxEntriesEvalTable ? bitScanReverse(maxEntriesEvalTable) : 0;

    _ttSize = 1 << ttTableSize;
    alwaysReplaceTable = new TTranspositionTableEntry[_ttSize];
    depthPrefTable = new TTranspositionTableEntry[_ttSize];

    _mtSize = 1 << materialTableSize;
    materialTable = new TMaterialTableEntry[_mtSize];

    _ptSize = 1 << pawnTableSize;
    pawnTable = new TPawnTableEntry[_ptSize];

    _etSize = 1 << evalTableSize;
    evalTable = new TEvalTableEntry[_etSize];

    _ttMaxHashKey = _ttSize - 1;
    _materialMaxHashKey = _mtSize - 1;
    _pawnMaxHashKey = _ptSize - 1;
    _evalMaxHashKey = _etSize - 1;


    _repTableSize = 100;

    clear();
}

THashTable::~THashTable() {
    delete[] alwaysReplaceTable;
    delete[] depthPrefTable;
    delete[] pawnTable;
    delete[] evalTable;
}

void THashTable::ttLookup(TSearch * searchData, int depth, int alpha, int beta) {
    bool hashHit = false;
    int hashedDepth1 = 0;
    searchData->stack->ttScore = TT_EMPTY;
    searchData->stack->ttMove1.piece = EMPTY;
    searchData->stack->ttMove2.piece = EMPTY;
    THashTable * hashTable = searchData->hashTable;

    U64 hashCode = searchData->pos->boardFlags->hashCode;
    int hashKey = hashTable->getTTHashKey(hashCode);

    /*
     * 1) Probe the depth preferred table
     */
    searchData->hashProbes++;
    TTranspositionTableEntry * ttEntry = &hashTable->depthPrefTable[hashKey];
    U64 hashedKey = ttEntry->key;
    U64 hashedData = ttEntry->value;
    if ((hashedKey ^ hashedData) == hashCode) {
        TMove move;
        move.setMove(TTMOVE(hashedData));
        TBoard * pos = searchData->pos;
        if (move.piece && pos->valid(&move) && pos->legal(&move)) {
            hashHit = true;
            searchData->stack->ttMove1.setMove(&move);
            int nodeType = TTFLAG(hashedData);
            hashedDepth1 = TTDEPTH(hashedData);
            int hashedScore = TTSCORE(hashedData);
            if (hashedDepth1 >= depth &&
                    ((nodeType == TT_EXACT)
                    || (nodeType == TT_UPPERBOUND && hashedScore <= alpha)
                    || (nodeType == TT_LOWERBOUND && hashedScore >= beta))) {
                if (hashedScore > SCORE_MATE - MAX_PLY) {
                    hashedScore -= pos->currentPly;
                } else if (hashedScore < -SCORE_MATE + MAX_PLY) {
                    hashedScore += pos->currentPly;
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
    hashedKey = ttEntry->key;
    if ((hashedKey ^ hashedData2) == hashCode) {
        TMove move;
        move.setMove(TTMOVE(hashedData2));
        TBoard * pos = searchData->pos;
        bool equalMove = move.piece && move.equals(&searchData->stack->ttMove1);
        bool legal = equalMove || (pos->valid(&move) && pos->legal(&move));
        if (legal) {
            hashHit = true;
            if (!equalMove) {
                searchData->stack->ttMove2.setMove(&move);
            }
            int nodeType = TTFLAG(hashedData2);
            int hashedDepth2 = TTDEPTH(hashedData2);
            int hashedScore = TTSCORE(hashedData2);
            if (hashedDepth2 >= depth &&
                    ((nodeType == TT_EXACT)
                    || (nodeType == TT_UPPERBOUND && hashedScore <= alpha)
                    || (nodeType == TT_LOWERBOUND && hashedScore >= beta))) {
                if (hashedScore > SCORE_MATE - MAX_PLY) {
                    hashedScore -= pos->currentPly;
                } else if (hashedScore < -SCORE_MATE + MAX_PLY) {
                    hashedScore += pos->currentPly;
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

void THashTable::repStore(TSearch * searchData, U64 hashCode, int fiftyCount) {
    if (fiftyCount < 100 && fiftyCount >= 0) {
        searchData->hashTable->repTable[fiftyCount] = hashCode;
    }
}

void THashTable::ttStore(TSearch * searchData, int move, int score, int depth, int alpha, int beta) {
    if (searchData->stopSearch || score == SCORE_INVALID || score == -SCORE_INVALID || depth >= MAX_PLY) {
        return;
    }

    THashTable * hashTable = searchData->hashTable;
    int rootPly = searchData->pos->rootPly % 64;
    int flags = score >= beta ? TT_LOWERBOUND : (score <= alpha ? TT_UPPERBOUND : TT_EXACT);
    assert(flags);

    U64 hashCode = searchData->pos->boardFlags->hashCode;

    assert(move > 0 || depth == 0);

    int hashKey = hashTable->getTTHashKey(hashCode);

    TTranspositionTableEntry * ttEntry = &hashTable->depthPrefTable[hashKey];
    U64 hashValue = ttEntry->value;

    if (score > SCORE_MATE - MAX_PLY) {
        score += searchData->pos->currentPly;
    } else if (score < -SCORE_MATE + MAX_PLY) {
        score -= searchData->pos->currentPly;
    }
    assert(score < SCORE_MATE && score > -SCORE_MATE);


    U64 newHashValue = ttEntry->encode(rootPly, depth, score, flags, move);

    int hashedGamePly = TTPLY(hashValue);
    int hashedDepth = TTDEPTH(hashValue);


    if (rootPly != hashedGamePly || hashedDepth <= depth) {

        ttEntry->key = (hashCode ^ newHashValue);
        ttEntry->value = newHashValue;

    }
    ttEntry = &hashTable->alwaysReplaceTable[hashKey];
    ttEntry->key = (hashCode ^ newHashValue);
    ttEntry->value = newHashValue;
}

void THashTable::mtLookup(TSearch * searchData) {
    searchData->materialTableProbes++;
    THashTable * hashTable = searchData->hashTable;
    TMaterialTableEntry * materialTable = hashTable->materialTable;
    U64 materialHash = searchData->pos->boardFlags->materialHash;
    TMaterialTableEntry entry = materialTable[hashTable->getMaterialHashKey(materialHash)];
    if ((entry.key ^ entry.value) == materialHash) {
        searchData->materialTableHits++;
        searchData->stack->scores[SCORE_MATERIAL].set(entry.value);
        searchData->stack->gamePhase = entry.gamePhase;
    } else {
        searchData->stack->scores[SCORE_MATERIAL].set(SCORE_INVALID);
    }
}

void THashTable::mtStore(TSearch * searchData, int value, int gamePhase) {
    THashTable * hashTable = searchData->hashTable;
    TMaterialTableEntry * materialTable = hashTable->materialTable;
    U64 materialHash = searchData->pos->boardFlags->materialHash;
    TMaterialTableEntry * entry = &materialTable[hashTable->getMaterialHashKey(materialHash)];
    entry->value = (short) value;
    entry->gamePhase = (char) gamePhase;
    entry->key = materialHash^value;
}

void THashTable::ptLookup(TSearch * searchData) {
    searchData->pawnTableProbes++;
    THashTable * hashTable = searchData->hashTable;
    U64 pawnHash = searchData->pos->boardFlags->pawnHash;
    TPawnTableEntry * entry = &hashTable->pawnTable[hashTable->getPawnHashKey(pawnHash)];
    if ((entry->key ^ entry->pawnScore.mg) == pawnHash) {
        searchData->stack->scores[SCORE_PAWNS].set(entry->pawnScore);
        searchData->stack->scores[SCORE_SHELTER_W].set(entry->shelterScoreW);
        searchData->stack->scores[SCORE_SHELTER_B].set(entry->shelterScoreB);
        searchData->pawnTableHits++;
    } else {
        searchData->stack->scores[SCORE_PAWNS].set(SCORE_INVALID);
    }
}

void THashTable::ptStore(TSearch * searchData, const TScore & pawnScore, const TScore & shelter_w, const TScore & shelter_b) {
    THashTable * hashTable = searchData->hashTable;
    U64 pawnHash = searchData->pos->boardFlags->pawnHash;
    TPawnTableEntry * entry = &hashTable->pawnTable[hashTable->getPawnHashKey(pawnHash)];
    entry->pawnScore.set(pawnScore);
    entry->shelterScoreW.set(shelter_w);
    entry->shelterScoreB.set(shelter_b);
    entry->key = (pawnHash ^ pawnScore.mg);
}

void THashTable::etLookup(TSearch * searchData, const short piece, const short square) {
    searchData->evalTableProbes++;
    THashTable * hashTable = searchData->hashTable;
    U64 evalHash = searchData->pos->boardFlags->pawnHash;
    HASH_ADD_PIECE(evalHash, piece, square);
    TEvalTableEntry * entry = &hashTable->evalTable[hashTable->getEvalHashKey(evalHash)];
    if ((entry->key ^ entry->score.mg) == evalHash) {
        searchData->stack->scores[SCORE_EVAL].set(entry->score);
        searchData->evalTableHits++;
    } else {
        searchData->stack->scores[SCORE_EVAL].set(SCORE_INVALID);
    }
}

void THashTable::etStore(TSearch * searchData, const TScore & score, const short piece, const short square) {
    THashTable * hashTable = searchData->hashTable;
    U64 evalHash = searchData->pos->boardFlags->pawnHash;
    HASH_ADD_PIECE(evalHash, piece, square);
    TEvalTableEntry * entry = &hashTable->evalTable[hashTable->getEvalHashKey(evalHash)];
    entry->score.set(score);
    entry->key = (evalHash ^ score.mg);
}

void THashTable::clear() {
    memset(depthPrefTable, 0, sizeof (TTranspositionTableEntry) * _ttSize);
    memset(alwaysReplaceTable, 0, sizeof (TTranspositionTableEntry) * _ttSize);
    memset(materialTable, 0, sizeof (TMaterialTableEntry) * _mtSize);
    memset(pawnTable, 0, sizeof (TPawnTableEntry) * _ptSize);
    memset(evalTable, 0, sizeof (TEvalTableEntry) * _etSize);
    memset(repTable, 0, sizeof (U64) * _repTableSize);
}