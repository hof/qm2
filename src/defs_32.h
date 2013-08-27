/* 
 * File:   defs.h
 * Author: Hajewiet
 *
 * Created on 8 april 2011, 20:20
 */

#ifndef DEFS_H
#define	DEFS_H

#include <assert.h>

typedef unsigned long long U64;

#define C64(x) x##ULL

static const U64 BIT32 = (C64(1) << 32);

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
        return x+32;
    } 
    asm ("bsr %0, %0" : "=r" (x) : "0" (x));
    return x;
}

#define BSF(x) (bitScanForward(x))
#define BSR(x) (bitScanReverse(x))

inline unsigned popCount(U64 x) {
    x = x - ((x >> 1) & C64(0x5555555555555555));
    x = (x & C64(0x3333333333333333)) + ((x >> 2) & C64(0x3333333333333333));
    x = (x + (x >> 4)) & C64(0x0f0f0f0f0f0f0f0f);
    x = (x * C64(0x0101010101010101)) >> 56;
    return x;
}

inline unsigned popCount(U64 x, bool checkZero) {
    return (checkZero && (!x)) ? 0 : popCount(x);
}

#define BIT(sq) (C64(1) << (sq))

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

#define POP(x)              (popFirst(x))

#define FILE(sq)            ((sq)&7)
#define RANK(sq)            ((sq)>>3)

#define PIECE_SYMBOL(piece) (char("  NBRQK"[piece]))
#define FILE_SYMBOL(sq)     (char(((sq)&7)+97))
#define RANK_SYMBOL(sq)     (char(((sq)>>3)+49))
#define MAX_PLY              128
#define MAX(x,y)            ((x)>=(y)?(x):(y))
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define ABS(x)              ((x)>=0?(x):(-(x)))    
#define FLIP_SQUARE(sq)     (((sq)^56))

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

const U64 FILE_A = C64(0x0101010101010101);
const U64 FILE_B = FILE_A << 1;
const U64 FILE_C = FILE_A << 2;
const U64 FILE_D = FILE_A << 3;
const U64 FILE_E = FILE_A << 4;
const U64 FILE_F = FILE_A << 5;
const U64 FILE_G = FILE_A << 6;
const U64 FILE_H = FILE_A << 7;

const U64 RANK_1 = 0xFF;
const U64 RANK_2 = RANK_1 << (1 * 8);
const U64 RANK_3 = RANK_1 << (2 * 8);
const U64 RANK_4 = RANK_1 << (3 * 8);
const U64 RANK_5 = RANK_1 << (4 * 8);
const U64 RANK_6 = RANK_1 << (5 * 8);
const U64 RANK_7 = RANK_1 << (6 * 8);
const U64 RANK_8 = RANK_1 << (7 * 8);

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

#endif	/* DEFS_H */

