/* 
 * File:   move.h
 * Author: Hajewiet
 *
 * Created on 10 april 2011, 12:48
 */

#ifndef MOVE_H
#define	MOVE_H

#include <assert.h>
#include <string>
using std::string;

class TBoard;

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

    unsigned char piece;
    unsigned char ssq;
    unsigned char tsq;
    unsigned char capture;
    unsigned char promotion;
    unsigned char castle;
    bool en_passant;
    unsigned char special; 
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
        capture = capturedPiece;
    }
    
    inline void setPromotionCapture(char pc, char from, char to, char promotionPiece, char capturedPiece) {
        piece = pc;
        ssq = from;
        tsq = to;
        promotion = promotionPiece;
        capture = capturedPiece;
    }

    string asString();
    void fromString(TBoard * pos, const char * moveStr);

    inline int asInt() {
        int result = piece;
        result |= ssq << 4;
        result |= tsq << 10;
        result |= capture << 16;
        result |= promotion << 20;
        result |= castle << 24;
        result |= en_passant << 28;
        result |= special << 29;
        return result;
    }

    inline bool equals(TMove * move) {
        return ssq == move->ssq
                && tsq == move->tsq
                && piece == move->piece
                && promotion == move->promotion;
    }

    inline bool clear() {
        piece = 0;
    }
   
};

#endif	/* MOVE_H */

