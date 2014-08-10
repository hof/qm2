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
 * File: defs.h
 * Defines bitwise operators, types and commonly used macro's
 *
 * Created on 8 april 2011, 20:20
 * 
 * For 64 bits architectures, make sure to #define HARDWARE_64BITS
 * For hardware popcount support, make sure to #define HARDWARE_POPCOUNT
 */

#ifndef DEFS_H
#define	DEFS_H

#define HARDWARE_POPCOUNT
#define HARDWARE_64BITS

#include <assert.h>
#include <stdint.h>
#include <iostream>
#include <string.h>

#include "score.h"

typedef uint64_t U64;

#define C64(x) x##UL

static const U64 BIT32 = (C64(1) << 32);

#ifdef HARDWARE_64BITS

inline unsigned bitScanForward(U64 x) {
    assert(x);
    asm ("bsfq %0, %0" : "=r" (x) : "0" (x));
    return x;
}

inline unsigned bitScanReverse(U64 x) {
    assert(x);
    asm ("bsrq %0, %0" : "=r" (x) : "0" (x));
    return x;
}
#endif /* 64 bits bitscan */

#ifndef HARDWARE_64BITS

inline unsigned bitScanForward(U64 x) {
    assert(x);
    if (x < BIT32) {
        asm ("bsf %0, %0" : "=r" (x) : "0" (x));
        return x;
    }
    x >>= 32;
    asm ("bsf %0, %0" : "=r" (x) : "0" (x));
    return x + 32;
}

inline unsigned bitScanReverse(U64 x) {
    assert(x);
    if (x >= BIT32) {
        x >>= 32;
        asm ("bsr %0, %0" : "=r" (x) : "0" (x));
        return x + 32;
    }
    asm ("bsr %0, %0" : "=r" (x) : "0" (x));
    return x;
}
#endif /* 32 bits bitscan */

#define BSF(x) (bitScanForward(x))
#define BSR(x) (bitScanReverse(x))
#define BIT(sq) (C64(1) << (sq))

const U64 FILE_A = C64(0x0101010101010101);
const U64 FILE_B = FILE_A << 1;
const U64 FILE_C = FILE_A << 2;
const U64 FILE_D = FILE_A << 3;
const U64 FILE_E = FILE_A << 4;
const U64 FILE_F = FILE_A << 5;
const U64 FILE_G = FILE_A << 6;
const U64 FILE_H = FILE_A << 7;

const U64 NOT_FILE_A = ~(FILE_A);
const U64 NOT_FILE_H = ~(FILE_H);

const U64 RANK_1 = 0xFF;
const U64 RANK_2 = RANK_1 << (1 * 8);
const U64 RANK_3 = RANK_1 << (2 * 8);
const U64 RANK_4 = RANK_1 << (3 * 8);
const U64 RANK_5 = RANK_1 << (4 * 8);
const U64 RANK_6 = RANK_1 << (5 * 8);
const U64 RANK_7 = RANK_1 << (6 * 8);
const U64 RANK_8 = RANK_1 << (7 * 8);

const U64 RANK[2][9] = {
    { 0, RANK_8, RANK_7, RANK_6, RANK_5, RANK_4, RANK_3, RANK_2, RANK_1}, //blacks point of view
    { 0, RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8} //whites point of view
};

const U64 BACKRANKS[2] = {RANK_1 | RANK_2, RANK_7 | RANK_8};

const U64 FILES[8] = {
    FILE_A,
    FILE_B,
    FILE_C,
    FILE_D,
    FILE_E,
    FILE_F,
    FILE_G,
    FILE_H
};

const U64 RANKS[8] = {
    RANK_1,
    RANK_2,
    RANK_3,
    RANK_4,
    RANK_5,
    RANK_6,
    RANK_7,
    RANK_8
};

const U64 NEIGHBOUR_FILES[8] = {
    FILE_B,
    FILE_A | FILE_C,
    FILE_B | FILE_D,
    FILE_C | FILE_E,
    FILE_D | FILE_F,
    FILE_E | FILE_G,
    FILE_F | FILE_H,
    FILE_G
};

const U64 PAWN_SCOPE[8] = {
    FILE_A | FILE_B,
    FILE_A | FILE_B | FILE_C,
    FILE_B | FILE_C | FILE_D,
    FILE_C | FILE_D | FILE_E,
    FILE_D | FILE_E | FILE_F,
    FILE_E | FILE_F | FILE_G,
    FILE_F | FILE_G | FILE_H,
    FILE_G | FILE_H
};

const U64 RIGHT_FILE[8] = {
    FILE_B,
    FILE_C,
    FILE_D,
    FILE_E,
    FILE_F,
    FILE_G,
    FILE_H,
    0
};

const U64 FORWARD_RANKS[8] = {
    RANK_2 | RANK_3 | RANK_4 | RANK_5 | RANK_6 | RANK_7 | RANK_8,
    RANK_3 | RANK_4 | RANK_5 | RANK_6 | RANK_7 | RANK_8,
    RANK_4 | RANK_5 | RANK_6 | RANK_7 | RANK_8,
    RANK_5 | RANK_6 | RANK_7 | RANK_8,
    RANK_6 | RANK_7 | RANK_8,
    RANK_7 | RANK_8,
    RANK_8,
    0
};

const U64 BACKWARD_RANKS[8] = {
    0,
    RANK_1,
    RANK_1 | RANK_2,
    RANK_1 | RANK_2 | RANK_3,
    RANK_1 | RANK_2 | RANK_3 | RANK_4,
    RANK_1 | RANK_2 | RANK_3 | RANK_4 | RANK_5,
    RANK_1 | RANK_2 | RANK_3 | RANK_4 | RANK_5 | RANK_6,
    RANK_1 | RANK_2 | RANK_3 | RANK_4 | RANK_5 | RANK_6 | RANK_7
};

const U64 WHITE_SQUARES = C64(0x55AA55AA55AA55AA);
const U64 BLACK_SQUARES = C64(0xAA55AA55AA55AA55);

const U64 BLACK_SIDE = U64(RANK_8 | RANK_7 | RANK_6 | RANK_5);
const U64 WHITE_SIDE = U64(RANK_1 | RANK_2 | RANK_3 | RANK_4);
const U64 SIDE[2] = {BLACK_SIDE, WHITE_SIDE};
const U64 FULL_BOARD = U64(BLACK_SIDE | WHITE_SIDE);
const U64 QUEEN_WING = U64(FILE_A | FILE_B | FILE_C | FILE_D);
const U64 KING_WING = U64(FILE_E | FILE_F | FILE_G | FILE_H);
const U64 EDGE = U64(RANK_8 | RANK_1 | FILE_A | FILE_H);
const U64 OUTER = U64(RANK_7 | RANK_2 | FILE_B | FILE_G | EDGE);
const U64 LARGE_CENTER = U64(FULL_BOARD^OUTER);
const U64 CENTER = U64(LARGE_CENTER & ~(RANK_6 | RANK_3 | FILE_C | FILE_F));

const U64 ATTACKZONE[2] = {
    RANK_1 | (RANK_2 & ~EDGE) | (RANK_3 & LARGE_CENTER) | (RANK_4 & CENTER),
    RANK_8 | (RANK_7 & ~EDGE) | (RANK_6 & LARGE_CENTER) | (RANK_5 & CENTER)
};

#ifdef HARDWARE_POPCOUNT /* hardware popcount */

inline int popCount(U64 b) {
    __asm__("popcnt %1, %0" : "=r" (b) : "r" (b));
    return b;
}

inline unsigned popCount0(U64 b) {
    __asm__("popcnt %1, %0" : "=r" (b) : "r" (b));
    return b;
}
#endif /* end: hardware popcount */

#ifndef HARDWARE_POPCOUNT /* software popcount */

inline unsigned popCount(U64 x) {
    x = (x & C64(0x5555555555555555)) + ((x >> 1) & C64(0x5555555555555555));
    x = (x & C64(0x3333333333333333)) + ((x >> 2) & C64(0x3333333333333333));
    x = (x & C64(0x0F0F0F0F0F0F0F0F)) + ((x >> 4) & C64(0x0F0F0F0F0F0F0F0F));
    return (x * C64(0x0101010101010101)) >> 56;
}

inline unsigned popCount0(U64 x) {
    return (x == 0) ? 0 : popCount(x);
}
#endif /* end: software popcount */

inline unsigned popFirst(U64 & x) {
    assert(x);
    int sq = BSF(x);
    x ^= BIT(sq);
    return sq;
}

inline unsigned popLast(U64 & x) {
    assert(x);
    int sq = BSR(x);
    x ^= BIT(sq);
    return sq;
}

inline U64 northFill(U64 x) {
    x |= (x << 8);
    x |= (x << 16);
    x |= (x << 32);
    return x;
}

inline U64 southFill(U64 x) {
    x |= (x >> 8);
    x |= (x >> 16);
    x |= (x >> 32);
    return x;
}

inline U64 upFill(U64 x, bool w) {
    return w? northFill(x) : southFill(x);
}

#define POP(x) (popFirst(x))

inline bool max_1(U64 x) {
    return (x & (x - 1)) == 0;
}

inline bool is_1(U64 x) {
    return x != 0 && max_1(x);
}

inline bool gt_1(U64 x) {
    return x != 0 && !max_1(x);
}

#define FRONTFILL(x) (northFill(x))
#define FRONTFILL_B(x) (southFill(x))
#define BACKFILL(x) (southFill(x))
#define BACKFILL_B(x) (northFill(x))
#define FILEFILL(x) ((northFill(x)) | (southFill(x)))

#define UP1(x) ((x) << 8)
#define DOWN1(x) ((x) >> 8)
#define UP2(x) ((x) << 16)
#define DOWN2(x) ((x) >> 16)
#define RIGHT1(x) (((x) << 1) & NOT_FILE_A)
#define LEFT1(x) (((x) >> 1) & NOT_FILE_H)
#define UPRIGHT1(x) (((x) << 9) & NOT_FILE_A)
#define UPLEFT1(x) (((x) << 7) & NOT_FILE_H)
#define DOWNRIGHT1(x) (((x) >> 7) & NOT_FILE_A)
#define DOWNLEFT1(x) (((x) >> 9) & NOT_FILE_H)

const int8_t PAWNDIRECTION[2] = {-8, 8};

inline int forwardSq(int sq, bool white) {
    return sq + PAWNDIRECTION[white];
}

inline U64 forwardFill(int sq, bool white) {
    return white ? FRONTFILL(BIT(sq)) : BACKFILL(BIT(sq));
}

#define FILE(sq)            ((sq)&7)
#define RANK(sq)            ((sq)>>3)

#define PIECE_SYMBOL(piece) (char("  NBRQK"[piece]))
#define FILE_SYMBOL(sq)     (char(((sq)&7)+97))
#define RANK_SYMBOL(sq)     (char(((sq)>>3)+49))
#define MAX_PLY              128
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define ABS(x)              ((x)>=0?(x):(-(x)))    
#define FLIP_SQUARE(sq)     (((sq)^56))

#define ISQ(sq,w)           (((sq)^(bool(w)*56)))

#define PRINT_SQUARE(sq)    FILE_SYMBOL(sq) << RANK_SYMBOL(sq)

inline int RANGE(const int min, const int max, const int x) {
    if (x <= min) {
        return min;
    }
    if (x >= max) {
        return max;
    }
    return x;
}

inline int BB_WIDTH(const U64 occ) {
    if (occ == 0 || max_1(occ)) {
        return 0;
    }
    U64 x = southFill(occ) & RANK_1;
    return BSR(x) - BSF(x);
}

/**
 * Flip a bitboard vertically about the centre ranks.
 * Rank 1 is mapped to rank 8 and vice versa.
 * @param x any bitboard
 * @return bitboard x flipped vertically
 */
inline U64 flipBB(U64 x) {
    return ( (x << 56)) |
            ((x << 40) & C64(0x00ff000000000000)) |
            ((x << 24) & C64(0x0000ff0000000000)) |
            ((x << 8) & C64(0x000000ff00000000)) |
            ((x >> 8) & C64(0x00000000ff000000)) |
            ((x >> 24) & C64(0x0000000000ff0000)) |
            ((x >> 40) & C64(0x000000000000ff00)) |
            ((x >> 56));
}

inline bool WHITE_SQUARE(unsigned char sq) {
    return ((FILE(sq) + RANK(sq)) & 1);
}

inline bool BLACK_SQUARE(unsigned char sq) {
    return !WHITE_SQUARE(sq);
}

inline int distance_rank(int sq1, int sq2) {
    return ABS(RANK(sq1) - RANK(sq2));
}

inline int distance_file(int sq1, int sq2) {
    return ABS(FILE(sq1) - FILE(sq2));
}

inline int distance(int sq1, int sq2) {
    int drank = distance_rank(sq1, sq2);
    int dfile = distance_file(sq1, sq2);
    return MAX(drank, dfile);
}

inline bool opposition(int sq1, int sq2) {
    int drank = distance_rank(sq1, sq2);
    int dfile = distance_file(sq1, sq2);
    return (drank % 2) == 0 && (dfile % 2) == 0;
}

inline U64 forwardRanks(int sq, bool white) {
    return white ? FORWARD_RANKS[RANK(sq)] : BACKWARD_RANKS[RANK(sq)];
}

/*
 * For debugging
 */
inline void printBB(std::string title, U64 bb) {
    std::cout << title << std::endl;
    for (int sq = 0; sq < 64; sq++) {
        int rsq = FLIP_SQUARE(sq);
        if (FILE(rsq) == 0) {
            std::cout << std::endl;
        }
        if (BIT(rsq) & bb) {
            std::cout << "x";
        } else {
            std::cout << ".";
        }
    }
    std::cout << std::endl << std::endl;
}


#endif	/* DEFS_H */

