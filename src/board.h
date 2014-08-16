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
 * File: board.h
 * Board representation:
 * - Bitboards for each piece and all (white/black) occupied squares
 * - Matrix[64] 
 */

#ifndef BOARD_H
#define	BOARD_H

#include "bbmoves.h"
#include "move.h"

enum square_t {
    a1, b1, c1, d1, e1, f1, g1, h1,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a8, b8, c8, d8, e8, f8, g8, h8
};

enum piece_t {
    EMPTY, WPAWN, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING,
    BPAWN, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING,
    WPIECES, BPIECES, ALLPIECES = 0
};

const piece_t PAWN[2] = {BPAWN, WPAWN};
const piece_t QUEEN[2] = {BQUEEN, WQUEEN};
const piece_t ROOK[2] = {BROOK, WROOK};
const piece_t BISHOP[2] = {BBISHOP, WBISHOP};
const piece_t KNIGHT[2] = {BKNIGHT, WKNIGHT};
const piece_t KING[2] = {BKING, WKING};

enum color_t {
    BLACK, WHITE
};

enum castle_flag_t {
    CASTLE_K = 1, CASTLE_Q = 2, CASTLE_k = 4, CASTLE_q = 8,
    CASTLE_WHITE = 3, CASTLE_BLACK = 12, CASTLE_ANY = 15, CASTLE_NONE = 0
};

enum endgame_t {
    OPP_BISHOPS, KBBKN, KBPsK, KNPK
};

class board_stack_t {
public:
    int enpassant_sq;
    int castling_flags;
    int fifty_count;
    int checker_sq;
    bool wtm; //white to move
    U64 hash_code;
    U64 material_hash;
    U64 pawn_hash;
    U64 checkers;

    void clear();
    void flip();
    void copy(board_stack_t * b_stack);
};


//Board representation structure

class board_t {
public:
    U64 bb[BPIECES + 1];
    int matrix[64];

    int current_ply;
    int root_ply;

    board_stack_t _stack[MAX_PLY + 1];
    board_stack_t * stack;

    void clear();
    void flip();
    bool is_draw();
    void create(const char* fen);
    std::string to_string();

    void forward(move_t * move);
    void backward(move_t * move);
    void forward();
    void backward();
    bool legal(move_t * move);
    bool valid(move_t * move);
    int gives_check(move_t * move);

    void add_piece(int piece, int sq, bool hash);
    void remove_piece(int piece, int sq, bool hash);
    void move_piece(int piece, int ssq, int tsq, bool hash);

    bool is_eg(endgame_t eg, bool us);

    U64 smallest_attacker(U64 attacks, bool wtm, int &piece);
    int see(move_t * capture);
    int mvvlva(move_t * capture);

    /**
     * Counts amount of piece for a given piece type
     * @param piece the piece type (WPAWN .. BKING)
     * @return amount of pieces
     */
    int count(int piece) {
        return popcnt0(bb[piece]);
    }

    /**
     * Returns square location (a1..h8) given a piece 
     * @param pc piece type (WPAWN..BKING)
     * @return square_t a1..h8
     */
    int get_sq(int pc) {
        return bsf(bb[pc]);
    }

    /**
     * Returns different index in case of a bishop on black squares to calculate material hash key
     * @param piece piece to verify
     * @param sq location of the piece
     * @return 0 or 32
     */
    int _bishop_ix(int piece, int sq) {
        return ((piece == WBISHOP || piece == BBISHOP) && is_black_sq(sq)) << 5;
    }

    /**
     * Returns bitboard populated with all pieces
     * @param us white or black
     * @return bitboard
     */
    U64 all(bool us) {
        assert(BPIECES == WPIECES + 1);
        return bb[WPIECES + (!us)];
    }

    /**
     * Returns pawns and kings for any color
     * @return bitboard populated with pawns and kings
     */
    U64 pawns_kings() {
        return bb[WKING] | bb[BKING] | bb[WPAWN] | bb[BPAWN];
    }

    /**
     * Test for pieces (excluding pawns and kings) for a given side 
     * @param white white or black
     * @return true if side has pieces, false otherwise
     */
    bool has_pieces(bool white) {
        return white ?
                bb[WROOK] || bb[WKNIGHT] || bb[WBISHOP] || bb[WQUEEN]
                :
                bb[BROOK] || bb[BKNIGHT] || bb[BBISHOP] || bb[BQUEEN];
    }

    /**
     * Test if there are any pieces (excluding pawns and kings) on the board (both sides)
     * @return true if there are pieces, false otherwise
     */
    bool has_pieces() {
        return bb[WROOK] || bb[BROOK] || bb[WKNIGHT] || bb[BKNIGHT]
                || bb[WBISHOP] || bb[BBISHOP] || bb[WQUEEN] || bb[BQUEEN];
    }

    /**
     * Test if a certain castling flag is set
     * @param flag the flag (castle right) to test for
     * @return true if the flag is set, false otherwise
     */
    bool has_castle_right(int flag) {
        return stack->castling_flags & flag;
    }

    /**
     * Test if a side has the bishop pair
     * @param us side to test
     * @return true: side has bishop pair, false: side does not have the bishop pair
     */
    bool has_bishop_pair(bool us) {
        int pc = BISHOP[us];
        return (bb[pc] & WHITE_SQUARES) && (bb[pc] & BLACK_SQUARES);
    }

    /**
     * Test if the actual position is legal, e.g. no king can be captured
     * @return true: legal, false: not legal
     */
    bool legal() {
        return is_attacked(get_sq(KING[!stack->wtm]), stack->wtm) == false;
    }

    /**
     * Test if the king is in check
     * @return true: in check, false: not in check
     */
    bool in_check() {
        return is_attacked(get_sq(KING[stack->wtm]), !stack->wtm);
    }

    /**
     * Test if a square is attacked
     * @param sq the square to investigate
     * @param white white or black
     * @return true if the square is attacked, false otherwise
     */
    bool is_attacked(int sq, bool white) {
        return white ?
                bb[WKNIGHT] & KNIGHT_MOVES[sq]
                || bb[WPAWN] & BPAWN_CAPTURES[sq]
                || bb[WKING] & KING_MOVES[sq]
                || (bb[WBISHOP] | bb[WQUEEN]) & magic::bishop_moves(sq, bb[ALLPIECES])
                || (bb[WROOK] | bb[WQUEEN]) & magic::rook_moves(sq, bb[ALLPIECES])
                :
                bb[BKNIGHT] & KNIGHT_MOVES[sq]
                || bb[BPAWN] & WPAWN_CAPTURES[sq]
                || bb[BKING] & KING_MOVES[sq]
                || (bb[BBISHOP] | bb[BQUEEN]) & magic::bishop_moves(sq, bb[ALLPIECES])
                || (bb[BROOK] | bb[BQUEEN]) & magic::rook_moves(sq, bb[ALLPIECES]);
    }

    /**
     * Return a bitboard with all attacks to a given square
     * @param sq the square to investigate
     * @return bitboard populated with all pieces attacking the square
     */
    U64 attacks_to(int sq) {
        return (KNIGHT_MOVES[sq] & (bb[WKNIGHT] | bb[BKNIGHT]))
                | (BPAWN_CAPTURES[sq] & bb[WPAWN])
                | (WPAWN_CAPTURES[sq] & bb[BPAWN])
                | (KING_MOVES[sq] & (bb[WKING] | bb[BKING]))
                | (magic::bishop_moves(sq, bb[ALLPIECES]) & (bb[WBISHOP] | bb[WQUEEN] | bb[BBISHOP] | bb[BQUEEN]))
                | (magic::rook_moves(sq, bb[ALLPIECES]) & (bb[WROOK] | bb[WQUEEN] | bb[BROOK] | bb[BQUEEN]));
    }

    /**
     * Get all pawn attacks for white or black
     * @param white white (true) or black (false)
     * @return bitboard populated with pawn attacks
     */
    U64 pawn_attacks(bool white) {
        return white ? UPLEFT1(bb[WPAWN]) | UPRIGHT1(bb[WPAWN])
                :
                DOWNLEFT1(bb[BPAWN]) | DOWNRIGHT1(bb[BPAWN]);
    }

    /**
     * Get all pawn attacks from a square
     * @param sq square from which to attack
     * @param white white or black
     * @return bitboard populated with one or two pawn attacks
     */
    U64 pawn_attacks(int sq, bool white) {
        return white ? WPAWN_CAPTURES[sq] : BPAWN_CAPTURES[sq];
    }

    /**
     * Tests if a square is attacked by a pawn
     * @param sq the square to test
     * @param white attacked by white pawn (true) or black pawn
     * @return true if the square is attacked by a pawn
     */
    bool is_attacked_by_pawn(int sq, bool white) {
        return white ? BPAWN_CAPTURES[sq] & bb[WPAWN] : WPAWN_CAPTURES[sq] & bb[BPAWN];
    }

};


#endif	/* BOARD_H */
