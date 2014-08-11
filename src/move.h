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
 * File: move.h
 * Move representation structure
 *
 * Created on 10 april 2011, 12:48
 */

#ifndef MOVE_H
#define	MOVE_H

#include "bits.h"
using std::string;

class board_t;

class TMove {
public:
    //int _move; //[4: 0..3 piece]
    //[6: 4..9 from (ssq)]
    //[6: 10..15 to (tsq)]
    //[4: 16..19 captured]
    //[4: 20..23 changed into (promoted)]
    //[4: 24..27 castling]
    //[1: en-passant 28]
    //[3: special 29

    uint8_t piece;
    uint8_t ssq;
    uint8_t tsq;
    uint8_t capture;
    uint8_t promotion;
    uint8_t castle;
    bool en_passant;
    uint8_t special; 
    int score;

    void setMove(TMove * move) {
        piece = move->piece;
        ssq = move->ssq;
        tsq = move->tsq;
        capture = move->capture;
        promotion = move->promotion;
        en_passant = move->en_passant;
        castle = move->castle;
        special = move->special;
        score = 0;
    }

    void setMove(int move) {
        assert(move >= 0);
        piece = move & 0x0F;
        ssq = move >> 4 & 0x3F;
        tsq = move >> 10 & 0x3F;
        capture = move >> 16 & 0x0F;
        promotion = move >> 20 & 0x0F;
        en_passant = (move & (0x01 << 28)) == (0x01 << 28);
        castle = move >> 24 & 0x0F;
        special = move >> 29 & 0x07;
        score = 0;
    }

    inline void setMove(char pc, char from, char to) {
        piece = pc;
        ssq = from;
        tsq = to;
        promotion = 0;
        capture = 0;
    }
    
    inline void setPromotion(char pc, char from, char to, char promotionPiece) {
        piece = pc;
        ssq = from;
        tsq = to;
        promotion = promotionPiece;
        capture = 0;
    }
    
    inline void setCapture(char pc, char from, char to, char capturedPiece) {
        piece = pc;
        ssq = from;
        tsq = to;
        promotion = 0;
        capture = capturedPiece;;
    }
    
    inline void setPromotionCapture(char pc, char from, char to, char promotionPiece, char capturedPiece) {
        piece = pc;
        ssq = from;
        tsq = to;
        promotion = promotionPiece;
        capture = capturedPiece;
    }

    string asString();
    void fromString(board_t * pos, const char * moveStr);

    inline int asInt() {
        int result = piece;
        result |= ssq << 4;
        result |= tsq << 10;
        result |= capture << 16;
        result |= promotion << 20;
        result |= castle << 24;
        result |= en_passant << 28;
        result |= special << 29;
        assert(result >= 0);
        return result;
    }

    inline bool equals(TMove * move) {
        return ssq == move->ssq
                && tsq == move->tsq
                && piece == move->piece
                && promotion == move->promotion;
    }

    inline void clear() {
        piece = 0;
    }
   
};

#endif	/* MOVE_H */

