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

enum color_t {
    BLACK, WHITE
};

enum castle_flag_t {
    CASTLE_K = 1, CASTLE_Q = 2, CASTLE_k = 4, CASTLE_q = 8,
    CASTLE_WHITE = 3, CASTLE_BLACK = 12, CASTLE_ANY = 15, CASTLE_NONE = 0
};

enum endgame_t {
    OPP_BISHOPS, KBBKN, KBPSK, KNPK, KRKP, KQKP, KRPKR, KQPSKQ, KQPSKQPS
};

const piece_t PAWN[2] = {BPAWN, WPAWN};
const piece_t QUEEN[2] = {BQUEEN, WQUEEN};
const piece_t ROOK[2] = {BROOK, WROOK};
const piece_t BISHOP[2] = {BBISHOP, WBISHOP};
const piece_t KNIGHT[2] = {BKNIGHT, WKNIGHT};
const piece_t KING[2] = {BKING, WKING};

namespace board {
    const int PVAL[BKING + 1] = {
        0, 100, 325, 325, 500, 925, 10000, 100, 325, 325, 500, 925, 10000
    };
};

class board_stack_t {
public:
    int enpassant_sq;
    int castling_flags;
    int fifty_count;
    bool wtm; //white to move
    U64 tt_key; //transposition table (hash) key
    U64 material_hash;
    U64 pawn_hash;

    void do_flip();

    /**
     * Clears a board stack
     */
    void clear() {
        memset(this, 0, sizeof (board_stack_t));
    }

    /**
     * Copies a board stack
     * @param b_stack the board stack to copy
     */
    void copy(const board_stack_t * b_stack) {
        assert(this != b_stack);
        memcpy(this, b_stack, sizeof (board_stack_t));
    }
};


//Board representation structure

class board_t {
public:
    U64 bb[BPIECES + 1];
    int matrix[64];

    int ply;
    int root_ply;

    board_stack_t _stack[MAX_PLY + 1];
    board_stack_t * stack;

    void clear();
    void do_flip();
    bool is_draw();
    void init(const char* fen);
    std::string to_string();
    void forward(const move_t * move);
    void backward(const move_t * move);
    void forward();
    void backward();
    bool legal(move_t * move);
    bool valid(const move_t * move);
    int gives_check(const move_t * move);
    void add_piece(const int piece, const int sq, const bool hash);
    void remove_piece(const int piece, const int sq, const bool hash);
    void move_piece(const int piece, const int ssq, const int tsq, const bool hash);
    bool is_eg(endgame_t eg, bool us);
    U64 smallest_attacker(const U64 attacks, const bool wtm, int &piece);
    int see(const move_t * capture);
    bool is_gain(const move_t * move);
    int max_gain(const move_t * capture);
    int mvvlva(const move_t * capture);
    
    /**
     * Gets side to move
     * @return boolean 1: white to move, 0: black to move
     */
    bool us() {
        return stack->wtm;
    }
    
     /**
     * Gets reversed side to move
     * @return boolean 1: black to move, 0: white to move
     */
    bool them() {
        return !stack->wtm;
    }

    /**
     * Counts amount of piece for a given piece type
     * @param piece the piece type (WPAWN .. BKING)
     * @return amount of pieces
     */
    int count(const int piece) {
        return popcnt0(bb[piece]);
    }
    
    /**
     * Returns square location (a1..h8) given a piece 
     * @param pc piece type (WPAWN..BKING)
     * @return square_t a1..h8
     */
    int get_sq(const int pc) {
        return bsf(bb[pc]);
    }

    
    /**
     * Returns bitboard popupulated with all pieces
     * @return bitboard
     */
    U64 all() {
        return bb[ALLPIECES];
    }
    
    /**
     * Returns bitboard popupulated with empty squarse
     * @return bitboard
     */
    U64 empty() {
        return ~bb[ALLPIECES];
    }
    
    /**
     * Returns bitboard populated with all pieces
     * @param us white or black
     * @return bitboard
     */
    U64 all(const bool us) {
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
     * Returns a bitboard populated with pieces, not being pawns or kings for one side
     * @param us white: 1, black: 0
     * @return bitboard
     */
    U64 all_pieces(const bool us) {
        return all(us) ^ (bb[PAWN[us]] | bb[KING[us]]);
    }

    /**
     * Tests if a move captures the last piece (not pawn) of our opponent
     * @param move the move
     * @return boolean true if move captures the last piece left
     */
    bool captures_last_piece(const move_t * move) {
        const bool them = move->piece > WKING;
        return move->capture > PAWN[them] && has_one_piece(them);
    }

    /**
     * Test for pieces (excluding pawns and kings) for a given side 
     * @param us side to move, white(1) or black(0)
     * @return true if side "us" has pieces, false otherwise
     */
    bool has_pieces(const bool us) {
        return bb[ROOK[us]] || bb[QUEEN[us]] || bb[KNIGHT[us]] || bb[BISHOP[us]];
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
     * Test if site to move has only one piece left (beside the king)
     * @param us side to move, white(1) or black(0)
     * @return true if side to move has only one piece left
     */
    bool has_one_piece(const bool us) {
        return is_1(all_pieces(us));
    }

    /**
     * Test if a certain castling flag is set
     * @param flag the flag (castle right) to test for
     * @return true if the flag is set, false otherwise
     */
    bool has_castle_right(const int flag) {
        return stack->castling_flags & flag;
    }

    /**
     * Test if white or black side can castle short (King Side)
     * @param us side to move (1: white, 0: black)
     * @return true if short castle right applies
     */
    bool can_castle_ks(const bool us) {
        static const int KS_FLAG[2] = { CASTLE_k, CASTLE_K };
        return stack->castling_flags & KS_FLAG[us];
    }

    /**
     * Test if white or black side can castle short (King Side)
     * @param us side to move (1: white, 0: black)
     * @return true if short castle right applies
     */
    bool can_castle_qs(const bool us) {
        static const int QS_FLAG[2] = { CASTLE_q, CASTLE_Q };
        return stack->castling_flags & QS_FLAG[us];
    }

    /**
     * Test if white or black side can castle (any side)
     * @param us side to move (1: white, 0: black)
     * @return true if any castle right applies
     */
    bool can_castle(const bool us) {
        static const int CC_FLAG[2] = { CASTLE_BLACK, CASTLE_WHITE };
        return stack->castling_flags & CC_FLAG[us];
    }

    /**
     * Determines if shelter on the kingside is intact for castling
     * @param white side to move
     * @return true if pawn shelter is ok-ish
     */
    bool good_shelter_ks(const bool white) {
        if (white) {
            return (matrix[h2] == WPAWN && matrix[g2] == WPAWN)
                    || (matrix[f2] == WPAWN && matrix[h2] == WPAWN && matrix[g3] == WPAWN)
                    || (matrix[h3] == WPAWN && matrix[g2] == WPAWN && matrix[f2] == WPAWN);
        } else {
            return (matrix[h7] == BPAWN && matrix[g7] == BPAWN)
                    || (matrix[f7] == BPAWN && matrix[h7] == BPAWN && matrix[g6] == BPAWN)
                    || (matrix[h6] == BPAWN && matrix[g7] == BPAWN && matrix[f7] == BPAWN);
        }
    }

    /**
     * Determines if shelter on the queenside is intact for castling
     * @param white side to move
     * @return true if pawn shelter is ok-ish
     */
    bool good_shelter_qs(const bool white) {
        if (white) {
            return (matrix[a2] == WPAWN && matrix[b2] == WPAWN && matrix[c2] == WPAWN)
                    || (matrix[a2] == WPAWN && matrix[b3] == WPAWN && matrix[c2] == WPAWN);
        } else {
            return (matrix[a7] == BPAWN && matrix[b7] == BPAWN && matrix[c7] == BPAWN)
                    || (matrix[a7] == BPAWN && matrix[b6] == BPAWN && matrix[c7] == BPAWN);
        }
    }

    /**
     * Test if a side has the bishop pair
     * @param us side to test
     * @return true: side has bishop pair, false: side does not have the bishop pair
     */
    bool has_bishop_pair(const bool us) {
        const int pc = BISHOP[us];
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
     * @param us white(1) or black(0)
     * @return true if the square is attacked, false otherwise
     */
    bool is_attacked(const int sq, const bool us) {
        return bb[KNIGHT[us]] & KNIGHT_MOVES[sq]
                || bb[PAWN[us]] & PAWN_CAPTURES[!us][sq]
                || bb[KING[us]] & KING_MOVES[sq]
                || (bb[ROOK[us]] | bb[QUEEN[us]]) & magic::rook_moves(sq, bb[ALLPIECES])
                || (bb[BISHOP[us]] | bb[QUEEN[us]]) & magic::bishop_moves(sq, bb[ALLPIECES]);
    }

    /**
     * Return a bitboard with all attacks to a given square
     * @param sq the square to investigate
     * @return bitboard populated with all pieces attacking the square
     */
    U64 attacks_to(const int sq) {
        return (KNIGHT_MOVES[sq] & (bb[WKNIGHT] | bb[BKNIGHT]))
                | (PAWN_CAPTURES[BLACK][sq] & bb[WPAWN])
                | (PAWN_CAPTURES[WHITE][sq] & bb[BPAWN])
                | (KING_MOVES[sq] & (bb[WKING] | bb[BKING]))
                | (magic::bishop_moves(sq, bb[ALLPIECES]) & (bb[WBISHOP] | bb[WQUEEN] | bb[BBISHOP] | bb[BQUEEN]))
                | (magic::rook_moves(sq, bb[ALLPIECES]) & (bb[WROOK] | bb[WQUEEN] | bb[BROOK] | bb[BQUEEN]));
    }

    /**
     * Get all pawn attacks for white or black
     * @param white white (true) or black (false)
     * @return bitboard populated with pawn attacks
     */
    U64 pawn_attacks(const bool white) {
        return white ? UPLEFT1(bb[WPAWN]) | UPRIGHT1(bb[WPAWN])
                :
                DOWNLEFT1(bb[BPAWN]) | DOWNRIGHT1(bb[BPAWN]);
    }

    /**
     * Tests if a square is attacked by a pawn
     * @param sq the square to test
     * @param white attacked by white pawn (true) or black pawn
     * @return true if the square is attacked by a pawn
     */
    bool is_attacked_by_pawn(const int sq, const bool white) {
        return white ? PAWN_CAPTURES[BLACK][sq] & bb[WPAWN] : PAWN_CAPTURES[WHITE][sq] & bb[BPAWN];
    }

    /**
     * Tests if a square is safe for placing pawn
     * @param sq the square to test
     * @param us side to move: white or black
     * @return true if the square can safely be occupied by a pawn
     */
    bool pawn_is_safe(const int sq, const bool us) {
        U64 atck[2] = {PAWN_CAPTURES[WHITE][sq] & bb[BPAWN], PAWN_CAPTURES[BLACK][sq] & bb[WPAWN]};
        bool them = !us;
        return atck[them] == 0 || (is_1(atck[them]) && atck[us] != 0) || gt_1(atck[us]);
    }

    /**
     * A square is considered an outpost if it can't be attacked by their pawns
     * @param sq square
     * @param us side to move (white or black)
     * @return true if outpost, false otherwise
     */
    bool is_outpost(const int sq, const bool us) {
        U64 span_up = ADJACENT_FILES[FILE(sq)] & upward_ranks(RANK(sq), us);
        return (span_up & bb[PAWN[!us]]) == 0;
    }

    /**
     * Return max mate depth in ply, used for W17
     * The max mate depth is the amount of pieces for the side with the most 
     * pieces, multiplied by 2 and added 2 for one (extra) check
     */
    int max_mate_depth() {
        return 2 + 2 * MAX(popcnt(bb[WPIECES]), popcnt(bb[BPIECES]));
    }

    /**
     * Return max mate depth in ply, used for W17
     * The max mate depth is the amount of pieces for the side with the most 
     * pieces, multiplied by 2 and added 2 for one (extra) check
     */
    int max_mate_depth_us() {
        return 2 + 2 * (popcnt(bb[WPIECES + !stack->wtm]));
    }
};


#endif	/* BOARD_H */
