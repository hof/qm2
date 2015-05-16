/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2015 Erik van het Hof and Hermen Reitsma 
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
    
    U64 _MAGIC_MOVES_BISHOP_DB[64][1 << 9];

    U64 _MAGIC_MOVES_ROOK_DB[64][1 << 12];

    U64 _init_occ(const int* squares, const int num_squares, const U64 linocc) {
        int i;
        U64 ret = 0;
        for (i = 0; i < num_squares; i++)
            if (linocc & (((U64) (1)) << i)) ret |= (((U64) (1)) << squares[i]);
        return ret;
    }

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
            if (bit & rowbits) ret |= bit;
            else break;
        } while (!(bit & occ));
        bit = (((U64) (1)) << square);
        do {
            bit >>= 1;
            if (bit & rowbits) ret |= bit;
            else break;
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
            if (bit2 & rowbits) ret |= bit;
            else break;
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit <<= 8 + 1;
            bit2 <<= 1;
            if (bit2 & rowbits) ret |= bit;
            else break;
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit >>= 8 - 1;
            bit2 <<= 1;
            if (bit2 & rowbits) ret |= bit;
            else break;
        } while (bit && !(bit & occ));
        bit = (((U64) (1)) << square);
        bit2 = bit;
        do {
            bit >>= 8 + 1;
            bit2 >>= 1;
            if (bit2 & rowbits) ret |= bit;
            else break;
        } while (bit && !(bit & occ));
        return ret;
    }

    //used so that the original indices can be left as const so that the compiler can optimize better

#define _BISHOP_MAGIC_NOMASK2(square, occupancy) _MAGIC_MOVES_BISHOP_DB[square][((occupancy)*_MAGIC_MOVES_BISHOP_MAGICS[square])>>_MINIMAL_B_BITS_SHIFT(square)]
#define _ROOK_MAGIC_NOMASK2(square, occupancy) _MAGIC_MOVES_ROOK_DB[square][((occupancy)*_MAGIC_MOVES_ROOK_MAGICS[square])>>_MINIMAL_R_BITS_SHIFT(square)]

    /**
     * Initialize Magic Moves
     * Note it's required to run this function before using the engine
     */
    void init(void) {
        int i;

        const int _BITPOS64_DB[64] = {
            63, 0, 58, 1, 59, 47, 53, 2,
            60, 39, 48, 27, 54, 33, 42, 3,
            61, 51, 37, 40, 49, 18, 28, 20,
            55, 30, 34, 11, 43, 14, 22, 4,
            62, 57, 46, 52, 38, 26, 32, 41,
            50, 36, 17, 19, 29, 10, 13, 21,
            56, 45, 25, 31, 35, 16, 9, 12,
            44, 24, 15, 8, 23, 7, 6, 5
        };
        
        for (i = 0; i < 64; i++) {
            int squares[64];
            int numsquares = 0;
            U64 temp = _MAGIC_MOVES_BISHOP_MASK[i];
            while (temp) {
                U64 bit = temp&-temp;
                squares[numsquares++] = _BITPOS64_DB[(bit * C64(0x07EDD5E59A4E28C2)) >> 58];
                temp ^= bit;
            }
            for (temp = 0; temp < (((U64) (1)) << numsquares); temp++) {
                U64 tempocc = _init_occ(squares, numsquares, temp);
                _BISHOP_MAGIC_NOMASK2(i, tempocc) = _init_bishop_moves(i, tempocc);

            }
        }
        for (i = 0; i < 64; i++) {
            int squares[64];
            int numsquares = 0;
            U64 temp = _MAGIC_MOVES_ROOK_MASK[i];
            while (temp) {
                U64 bit = temp&-temp;
                squares[numsquares++] = _BITPOS64_DB[(bit * C64(0x07EDD5E59A4E28C2)) >> 58];
                temp ^= bit;
            }
            for (temp = 0; temp < (((U64) (1)) << numsquares); temp++) {
                U64 tempocc = _init_occ(squares, numsquares, temp);
                _ROOK_MAGIC_NOMASK2(i, tempocc) = _init_rook_moves(i, tempocc);
            }
        }
    }
}
