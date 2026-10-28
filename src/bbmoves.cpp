/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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
 * File: bbmoves.cpp
 * - Bitboards with (pseudo) moves per piece.
 * - Magic bitboards with credits and thanks to Pradyumna Kannan. 
 * The original code for magic bitboards is available at his website. 
 * Note the function magic::init() needs to be called before using the engine.
 */

#include "bbmoves.h"

namespace magic {
    
    /*
     * Magic stuff
     * @Credits Pradyumna Kannan
     */
    
#ifdef __BMI2__

    // PEXT indices are dense: allocate only the occupancy subsets of each square.
    alignas(64) U64 bishop_attack_data[5248];
    alignas(64) U64 rook_attack_data[102400];
    U64 * bishop_attack_table[64];
    U64 * rook_attack_table[64];
#else
    U64 _MAGIC_MOVES_BISHOP_DB[64][1 << 9];

    U64 _MAGIC_MOVES_ROOK_DB[64][1 << 12];
#endif

    U64 _init_rook_moves(const int square, const U64 occ) {
        U64 ret = 0;
        U64 bit;
        U64 rowbits = (((U64) 0xFF) << (8 * (square / 8)));
        bit = (((U64) (1)) << square);
        do {
            bit <<= 8;
            ret |= bit;
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        do {
            bit >>= 8;
            ret |= bit;
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        do {
            bit <<= 1;
            if (bit & rowbits) {
                ret |= bit;
            } else {
                break;
            }
        } while (!(bit & occ));
        bit = (((U64) (1)) << square);
        do {
            bit >>= 1;
            if (bit & rowbits) {
                ret |= bit;
            } else {
                break;
            }
        } while (!(bit & occ));
        return ret;
    }

    U64 _init_bishop_moves(const int square, const U64 occ) {
        U64 ret = 0;
        U64 bit;
        U64 bit2;
        U64 rowbits = (((U64) 0xFF) << (8 * (square / 8)));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit <<= 8 - 1;
            bit2 >>= 1;
            if (bit2 & rowbits) {
                ret |= bit;
            } else {
                break;
            }
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit <<= 8 + 1;
            bit2 <<= 1;
            if (bit2 & rowbits) {
                ret |= bit;
            } else {
                break;
            }
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit >>= 8 - 1;
            bit2 <<= 1;
            if (bit2 & rowbits) {
                ret |= bit;
            } else {
                break;
            }
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit >>= 8 + 1;
            bit2 >>= 1;
            if (bit2 & rowbits) {
                ret |= bit;
            } else {
                break;
            }
        } while (bit && !(bit & occ));
        return ret;
    }

    //used so that the original indices can be left as const so that the compiler can optimize better

#ifdef __BMI2__
#define _BISHOP_MAGIC_NOMASK2(square, occupancy) bishop_attack_table[square][_pext_u64(occupancy, _MAGIC_MOVES_BISHOP_MASK[square])]
#define _ROOK_MAGIC_NOMASK2(square, occupancy) rook_attack_table[square][_pext_u64(occupancy, _MAGIC_MOVES_ROOK_MASK[square])]
#else
#define _BISHOP_MAGIC_NOMASK2(square, occupancy) _MAGIC_MOVES_BISHOP_DB[square][((occupancy)*_MAGIC_MOVES_BISHOP_MAGICS[square])>>_MINIMAL_B_BITS_SHIFT(square)]
#define _ROOK_MAGIC_NOMASK2(square, occupancy) _MAGIC_MOVES_ROOK_DB[square][((occupancy)*_MAGIC_MOVES_ROOK_MAGICS[square])>>_MINIMAL_R_BITS_SHIFT(square)]
#endif

    /**
     * Initialize Magic Moves
     * Note it's required to run this function before using the engine
     */
    void init(void) {
#ifdef __BMI2__
        U64 * bishop_next = bishop_attack_data;
        U64 * rook_next = rook_attack_data;
#endif
        for (int square = 0; square < 64; ++square) {
            const U64 mask = _MAGIC_MOVES_BISHOP_MASK[square];
#ifdef __BMI2__
            bishop_attack_table[square] = bishop_next;
            bishop_next += U64(1) << popcnt0(mask);
#endif
            U64 occupied = 0;
            do {
                _BISHOP_MAGIC_NOMASK2(square, occupied) = _init_bishop_moves(square, occupied);
                occupied = (occupied - mask) & mask;
            } while (occupied);
        }
        for (int square = 0; square < 64; ++square) {
            const U64 mask = _MAGIC_MOVES_ROOK_MASK[square];
#ifdef __BMI2__
            rook_attack_table[square] = rook_next;
            rook_next += U64(1) << popcnt0(mask);
#endif
            U64 occupied = 0;
            do {
                _ROOK_MAGIC_NOMASK2(square, occupied) = _init_rook_moves(square, occupied);
                occupied = (occupied - mask) & mask;
            } while (occupied);
        }
#ifdef __BMI2__
        assert(bishop_next == bishop_attack_data + 5248);
        assert(rook_next == rook_attack_data + 102400);
#endif
    }
}
