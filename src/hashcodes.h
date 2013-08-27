// QueenMaxima, a chess playing program. 
// Copyright (C) 1996-2009 Erik van het Hof and Hermen Reitsma 
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
// 

#ifndef HASHCODES_H 
#define HASHCODES_H 

#include "defs.h"

#define HASH_PIECE(key,pc,sq) ((key) ^= HashNumbers[pc][sq])
#define HASH_ADD_PIECE(key,pc,sq) ((key) ^= HashNumbers[pc][sq])
#define HASH_REMOVE_PIECE(key,pc,sq) ((key) ^= HashNumbers[pc][sq])
#define HASH_MOVE_PIECE(key,pc,ssq,tsq) ((key) ^= HashNumbers[pc][ssq] ^ HashNumbers[pc][tsq])
#define HASH_EP(key,sq) ((key) ^= HashNumbers[0][sq]) 
#define HASH_CASTLE_K(key) ((key) ^= C64(0x47bc71a493da706e)) 
#define HASH_CASTLE_Q(key) ((key) ^= C64(0x6338be439fd357dc))
#define HASH_CASTLE_k(key) ((key) ^= C64(0x6fed622e98f98b7e))
#define HASH_CASTLE_q(key) ((key) ^= C64(0xce107ca2947d2d58))
#define HASH_STM(key) ((key) ^= C64(0xe7d626abae228509))
#define HASH_EXCLUDED_MOVE(key) ((key) ^= C64(0xb3a62fabadd68509))

extern const U64 HashNumbers[14][64];

#endif 
