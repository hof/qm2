/** 
 * bbmoves.h
 * 
 * - Bitboards with (pseudo) moves per piece.
 * - Magic bitboards with credits and thanks to Pradyumna Kannan. The original 
 *   code for magic bitboards is available at his website. Note the function 
 *   InitMagicMoves() needs to be called before using the engine.
 */


#ifndef _bbmovesh
#define _bbmovesh

#include "defs.h"

extern const U64 KingMoves[64];
extern const U64 QueenMoves[64];
extern const U64 RookMoves[64];
extern const U64 BishopMoves[64];
extern const U64 KnightMoves[64];
extern const U64 WPawnMoves[64];
extern const U64 BPawnMoves[64];
extern const U64 WPawnCaptures[64];
extern const U64 BPawnCaptures[64];

/*
 * Magic Stuff
 */
#define MINIMAL_B_BITS_SHIFT(square) 55
#define MINIMAL_R_BITS_SHIFT(square) 52

extern const U64 magicmoves_r_magics[64];
extern const U64 magicmoves_r_mask[64];
extern const U64 magicmoves_b_magics[64];
extern const U64 magicmoves_b_mask[64];
extern const unsigned int magicmoves_b_shift[64];
extern const unsigned int magicmoves_r_shift[64];

extern U64 magicmovesbdb[64][1 << 9];
extern U64 magicmovesrdb[64][1 << 12];

static U64 MagicBishopMoves(const unsigned int square, const U64 occupancy) {
    return magicmovesbdb[square][(((occupancy) & magicmoves_b_mask[square]) * magicmoves_b_magics[square]) >> MINIMAL_B_BITS_SHIFT(square)];
}

static U64 MagicRookMoves(const unsigned int square, const U64 occupancy) {
    return magicmovesrdb[square][(((occupancy) & magicmoves_r_mask[square]) * magicmoves_r_magics[square]) >> MINIMAL_R_BITS_SHIFT(square)];
}

static U64 MagicQueenMoves(const unsigned int square, const U64 occupancy) {
    return MagicBishopMoves(square, occupancy) | MagicRookMoves(square, occupancy);
}

void InitMagicMoves(void);

#endif //_bbmovesh
