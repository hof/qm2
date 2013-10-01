/* 
 * File:   hashtable.h
 * Author: Hajewiet
 *
 * Created on 13 mei 2011, 21:42
 */

#ifndef HASHTABLE_H
#define	HASHTABLE_H

#include "defs.h"
#include "move.h"

class TSearch;

struct TMaterialTableEntry {
    U64 key;
    short value;
    unsigned char gamePhase;
};

struct TPawnTableEntry {
    U64 key;
    TScore pawnScore;
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
    int _materialMaxHashKey;
    int _pawnMaxHashKey;
    int _ttSize;
    int _mtSize;
    int _ptSize;
    int _repTableSize;
public:
    THashTable(int totalSizeInMb);
    ~THashTable();

    TTranspositionTableEntry * depthPrefTable;
    TTranspositionTableEntry * alwaysReplaceTable;
    TMaterialTableEntry * materialTable;
    TPawnTableEntry * pawnTable;
    U64 repTable[100];

    inline int getTTHashKey(U64 hashCode) {
        return hashCode & _ttMaxHashKey;
    }

    inline int getMaterialHashKey(U64 hashCode) {
        return hashCode & _materialMaxHashKey;
    }

    inline int getPawnHashKey(U64 pawnHashCode) {
        return pawnHashCode & _pawnMaxHashKey;
    }

    static void ttLookup(TSearch * searchData, int depth, int alpha, int beta);
    static void ttStore(TSearch * searchData, int move, int score, int depth, int alpha, int beta);
    static void mtLookup(TSearch * searchData);
    static void mtStore(TSearch * searchData, int value, int gamePhase);
    static void repStore(TSearch * searchData, U64 hashCode, int fiftyCount);
    static void ptLookup(TSearch * searchData);
    static void ptStore(TSearch * searchData, const TScore & pawnScore);

    void clear();


};


#endif	/* HASHTABLE_H */

