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
 * File: pst.h
 * Piece Square Tables to include in the evaluation function
 */

#ifndef PST_H
#define	PST_H

int8_t PST_PAWN_MG[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 10, 10, 5, 0, -10,
    -10, 0, 10, 15, 15, 10, 0, -10,
    -10, 0, 5, 10, 10, 5, 0, -10,
    -10, 0, 0, 0, 0, 0, 0, -10,
    0, 0, 0, 0, 0, 0, 0, 0 // a1..h1
};

int8_t PST_PAWN_EG[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

int8_t PST_KING_MG[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

int8_t PST_KING_EG[64] = {
    0, 15, 20, 25, 25, 20, 15, 0,
    15, 25, 30, 35, 35, 30, 25, 15,
    20, 30, 35, 40, 40, 35, 30, 20,
    25, 35, 40, 45, 45, 45, 35, 25,
    25, 35, 40, 45, 45, 40, 35, 25,
    20, 30, 35, 40, 40, 35, 30, 20,
    15, 25, 30, 35, 35, 30, 25, 15,
    0, 15, 20, 25, 25, 20, 15, 0 // a1..h1
};

int8_t PST_KNIGHT_MG[64] = {
    -12, -6, 0, 0, 0, 0, -6, -12,
    -6, 0, 16, 16, 16, 16, 0, -6,
    0, 12, 26, 24, 24, 26, 12, 0,
    -4, 8, 16, 16, 16, 16, 8, -4,
    -8, 4, 12, 12, 12, 12, 4, -8,
    -12, 0, 10, 6, 6, 10, 0, -12,
    -18, -16, -6, -6, -6, -6, -16, -18,
    -22, -20, -16, -16, -16, -16, -20, -22
};

int8_t PST_BISHOP_MG[64] = {
    -2, -2, -2, 0, 0, -2, -2, -2,
    -4, 4, 4, 2, 2, 4, 4, -4,
    -6, 2, 10, 8, 8, 10, 2, -6,
    -6, 0, 6, 12, 12, 6, 0, -6,
    -6, 0, 6, 12, 12, 6, 0, -6,
    -6, 0, 6, 4, 4, 6, 0, -6,
    -4, 4, -2, -4, -4, -2, 4, -4,
    -2, -6, -10, -12, -12, -10, -6, -2
};

int8_t PST_ROOK_MG[64] = {
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4,
    -4, -4, 0, 4, 4, 0, -4, -4
};

int8_t PST_QUEEN_MG[64] = {
    -2, -2, 0, 2, 2, 0, -2, -2,
    0, 0, 2, 4, 4, 2, 0, 0,
    0, 0, 2, 4, 4, 2, 0, 0,
    0, 0, 2, 4, 4, 2, 0, 0,
    -2, -2, 0, 2, 2, 0, -2, -2,
    -2, -2, 0, 2, 2, 0, -2, -2,
    -4, -4, -2, 0, 0, -2, -4, -4,
    -8, -8, -6, -4, -4, -6, -8, -8
};

int8_t PST_KNIGHT_EG[64] = {
    -20, -14, -8, -8, -8, -8, -14, -20,
    -14, -8, 4, 4, 4, 4, -8, -14,
    -6, 6, 18, 18, 18, 18, 6, -6,
    -6, 6, 18, 18, 18, 18, 6, -6,
    -8, 4, 16, 16, 16, 16, 4, -8,
    -8, 4, 16, 16, 16, 16, 4, -8,
    -14, -8, 4, 4, 4, 4, -8, -14,
    -22, -16, -10, -10, -10, -10, -16, -22
};

int8_t PST_BISHOP_EG[64] = {
    -6, -6, -6, -6, -6, -6, -6, -6,
    -6, 0, 0, 0, 0, 0, 0, -6,
    -6, 0, 8, 8, 8, 8, 0, -6,
    -6, 0, 8, 14, 14, 8, 0, -6,
    -6, 0, 8, 14, 14, 8, 0, -6,
    -6, 0, 8, 8, 8, 8, 0, -6,
    -6, 0, 0, 0, 0, 0, 0, -6,
    -6, -6, -6, -6, -6, -6, -6, -6
};

int8_t PST_ROOK_EG[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

int8_t PST_QUEEN_EG[64] = {
    -4, -4, -4, -4, -4, -4, -4, -4,
    -4, 0, 0, 0, 0, 0, 0, -4,
    -4, 0, 4, 4, 4, 4, 0, -4,
    -4, 0, 4, 8, 8, 4, 0, -4,
    -4, 0, 4, 8, 8, 4, 0, -4,
    -4, 0, 4, 4, 4, 4, 0, -4,
    -4, 0, 0, 0, 0, 0, 0, -4,
    -4, -4, -4, -4, -4, -4, -4, -4
};



#endif	/* PST_H */

