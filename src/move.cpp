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
 * File: move.cpp
 * Move representation structure
 */

#include "move.h"
#include "board.h"
#include "score.h"

/**
 * Copies / sets a move from move object
 * @param move move object
 */
void move_t::set(move_t * move) {
    piece = move->piece;
    ssq = move->ssq;
    tsq = move->tsq;
    capture = move->capture;
    promotion = move->promotion;
    en_passant = move->en_passant;
    castle = move->castle;
    score = move->score;
}

/**
 * Copies / sets a move from move integer
 * @param move integer move
 */
void move_t::set(int move) {
    assert(move >= 0);
    piece = move & 0x0F;
    ssq = move >> 4 & 0x3F;
    tsq = move >> 10 & 0x3F;
    capture = move >> 16 & 0x0F;
    promotion = move >> 20 & 0x0F;
    castle = move >> 24 & 0x0F;
    en_passant = (move & (0x01 << 28)) == (0x01 << 28);
    score = score::INVALID;
}

/**
 * Copies/sets a move 
 * @param pc piece
 * @param from square where the piece is moving from (ssq)
 * @param to square where the piece is moving to (tsq)
 * @param captured_piece piece that is captured or 0 if no piece is captures
 * @param promotion_piece in case of promotion: the new piece type, 0 otherwise
 */
void move_t::set(int pc, int from, int to, int captured_piece, int promotion_piece) {
    piece = pc;
    ssq = from;
    tsq = to;
    promotion = promotion_piece;
    capture = captured_piece;
}

/**
 * Copies/sets a move from string
 * @param board the actual board position 
 * @param move_str move string in xboard notation
 */
void move_t::set(board_t * board, const char * move_str) {
    int file = *move_str++ -'a';
    int rank = *move_str++ -'1';
    ssq = (rank << 3) | file;
    file = *move_str++ -'a';
    rank = *move_str++ -'1';
    tsq = (rank << 3) | file;
    piece = board->matrix[ssq];
    assert(piece);
    capture = board->matrix[tsq];
    castle = 0;
    en_passant = false;
    if (tsq == board->stack->enpassant_sq
            && (piece == WPAWN || piece == BPAWN)
            && board->stack->enpassant_sq) {
        en_passant = true;
        capture = piece == WPAWN ? BPAWN : WPAWN;
    } else if (piece == WKING && ssq == e1) {
        if (tsq == g1) {
            castle = CASTLE_K;
        } else if (tsq == c1) {
            castle = CASTLE_Q;
        }
    } else if (piece == BKING && ssq == e8) {
        if (tsq == g8) {
            castle = CASTLE_k;
        } else if (tsq == c8) {
            castle = CASTLE_q;
        }
    }
    promotion = 0;
    if (*move_str) {
        switch (*move_str) {
            case 'n':
                promotion = WKNIGHT;
                break;
            case 'b':
                promotion = WBISHOP;
                break;
            case 'r':
                promotion = WROOK;
                break;
            default:
                promotion = WQUEEN;
                break;
        }
        if (board->stack->wtm == false) {
            promotion += WKING;
        }
    }
}

/**
 * Returns a move as an xboard notation string (e.g. g1f3, a7a8q)
 * @return string move string
 */
std::string move_t::to_string() {
    const char PIECE_SYMBOL[13] = {'.', 'p', 'n', 'b', 'r', 'q', 'k', 'p', 'n', 'b', 'r', 'q', 'k'};
    std::string result = "";
    if (piece == 0 || ssq == tsq) {
        result = "null";
    } else {
        result = FILE_SYMBOL(ssq);
        result += RANK_SYMBOL(ssq);
        result += FILE_SYMBOL(tsq);
        result += RANK_SYMBOL(tsq);
        if (promotion) {
            result += PIECE_SYMBOL[promotion];
        }
    }
    return result;
}

/**
 * Encodes and returns a move to a 32 bit integer
 * 0..3  | 4..9 | 10..5 | 16..19         | 20..23            | 24..27   | 28 | 
 * piece | from | to    | captured piece | promoted to piece | castling | ep | 
 * 
 * @return integer representing move
 */
int move_t::to_int() {
    int result = piece;
    result |= ssq << 4;
    result |= tsq << 10;
    result |= capture << 16;
    result |= promotion << 20;
    result |= castle << 24;
    result |= en_passant << 28;
    assert(result >= 0);
    return result;
}
