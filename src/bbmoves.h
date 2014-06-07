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
 * 
 * File: bbmoves.h
 * - Bitboards with (pseudo) moves per piece.
 * - Magic bitboards with credits and thanks to Pradyumna Kannan. The original 
 *   code for magic bitboards is available at his website. 
 * 
 * Note the function InitMagicMoves() needs to be called before using the engine.
 */

#ifndef _bbmovesh
#define _bbmovesh

#include "defs.h"

extern const U64 KING_MOVES[64];
extern const U64 KING_ZONE[64];
extern const U64 QUEEN_MOVES[64];
extern const U64 ROOK_MOVES[64];
extern const U64 BISHOP_MOVES[64];
extern const U64 KNIGHT_MOVES[64];
extern const U64 WPAWN_MOVES[64];
extern const U64 BPAWN_MOVES[64];
extern const U64 WPAWN_CAPTURES[64];
extern const U64 BPAWN_CAPTURES[64];

/*
 * Magic Stuff
 */
#define MINIMAL_B_BITS_SHIFT(square) 55
#define MINIMAL_R_BITS_SHIFT(square) 52

extern const U64 MAGIC_MOVES_ROOK_MAGICS[64];
extern const U64 MAGIC_MOVES_ROOK_MASK[64];
extern const U64 MAGIC_MOVES_BISHOP_MAGICS[64];
extern const U64 MAGIC_MOVES_BISHOP_MASK[64];
extern const unsigned int MAGIC_MOVES_BISHOP_SHIFT[64];
extern const unsigned int MAGIC_MOVES_ROOK_SHIFT[64];

extern U64 MAGIC_MOVES_BISHOP_DB[64][1 << 9];
extern U64 MAGIC_MOVES_ROOK_DB[64][1 << 12];

/**
 * Return Bishop Moves given a square and board occupancy
 * @param sq the square
 * @param occ occupancy bitboard
 * @return bitboard populated with bishop moves
 */
static U64 MagicBishopMoves(const unsigned int sq, const U64 occ) {
    return MAGIC_MOVES_BISHOP_DB[sq][(((occ) & MAGIC_MOVES_BISHOP_MASK[sq]) * MAGIC_MOVES_BISHOP_MAGICS[sq]) >> MINIMAL_B_BITS_SHIFT(sq)];
}

/**
 * Return Rook Moves given a square and board occupancy
 * @param sq the square
 * @param occ occupancy bitboard
 * @return bitboard populated with rook moves
 */
inline U64 MagicRookMoves(const unsigned int sq, const U64 occ) {
    return MAGIC_MOVES_ROOK_DB[sq][(((occ) & MAGIC_MOVES_ROOK_MASK[sq]) * MAGIC_MOVES_ROOK_MAGICS[sq]) >> MINIMAL_R_BITS_SHIFT(sq)];
}

/**
 * Return Queen Moves given a square and board occupancy
 * @param sq the square
 * @param occ occupancy bitboard
 * @return bitboard populated with queen moves
 */
inline U64 MagicQueenMoves(const unsigned int sq, const U64 occ) {
    return MagicBishopMoves(sq, occ) | MagicRookMoves(sq, occ);
}

void InitMagicMoves(void); //initialize magic moves (required)

#endif //_bbmovesh
