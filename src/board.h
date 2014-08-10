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
 * - Piece placement arrays for each piece
 */

#ifndef BOARD_H
#define	BOARD_H

#include "defs.h"
#include "move.h"
#include "bbmoves.h"
#include "hashcodes.h"
#include "score.h"
#include <string>
using std::string;

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

namespace board {

    struct stack_t {
        int enpassant_sq;
        int castling_flags;
        int fifty_count;
        int checker_sq;
        bool wtm; //white to move
        U64 hash_code;
        U64 material_hash;
        U64 pawn_hash;
        U64 checkers;

        void clear() {
            enpassant_sq = 0;
            castling_flags = 0;
            fifty_count = 0;
            wtm = true;
            hash_code = 0;
            material_hash = 0;
            pawn_hash = 0;
            checkers = 0;
        }

        void flip() {
            wtm = !wtm;
            HASH_STM(hash_code);
            if (enpassant_sq) {
                enpassant_sq = FLIP_SQUARE(enpassant_sq);
            }
            checkers = flipBB(checkers);
            uint8_t flags = castling_flags;
            castling_flags = 0;
            if (flags & CASTLE_K) {
                castling_flags |= CASTLE_k;
            }
            if (flags & CASTLE_Q) {
                castling_flags |= CASTLE_q;
            }
            if (flags & CASTLE_k) {
                castling_flags |= CASTLE_K;
            }
            if (flags & CASTLE_q) {
                castling_flags |= CASTLE_Q;
            }
        }

        void copy(stack_t * bFlags) {
            enpassant_sq = bFlags->enpassant_sq;
            castling_flags = bFlags->castling_flags;
            fifty_count = bFlags->fifty_count;
            wtm = bFlags->wtm;
            hash_code = bFlags->hash_code;
            material_hash = bFlags->material_hash;
            pawn_hash = bFlags->pawn_hash;
        }
    };
}

//Board representation structure

struct TBoard {
    int current_ply;
    int root_ply;

    board::stack_t _stack[MAX_PLY + 1];
    board::stack_t * stack;

    /*U64 boards[WPAWN];
    U64 boards[BPAWN];
    U64 boards[WKNIGHT];
    U64 boards[BKNIGHT];
    U64 boards[WBISHOP];
    U64 boards[BBISHOP];
    U64 boards[WROOK];
    U64 boards[BROOK];
    U64 boards[WQUEEN];
    U64 boards[BQUEEN];
    U64 boards[WKING];
    U64 boards[BKING];
    U64 white_pieces;
    U64 black_pieces;
    U64 boards[ALLPIECES];*/
    U64 boards[BPIECES + 1];
    /*U64 * bishops[2];
    U64 * rooks[2];
    U64 * pawns[2];
    U64 * queens[2];
    U64 * knights[2];
    U64 * kings[2];*/

    unsigned char matrix[64];

    void clear();

    void flip();

    inline int count(int piece) {
        return popCount0(boards[piece]);
    }
    
    inline int get_sq(int pc) {
        return BSF(boards[pc]);
    }
    
    inline U64 get_bb(int pc) {
        return boards[pc];
    }

    //for keeping info about bishop pair on different colored squares in material hash key

    inline int _bishop_ix(int piece, int sq) {
        return ((piece == WBISHOP || piece == BBISHOP) && BLACK_SQUARE(sq)) << 5;
    }

    inline int topPiece(bool us) {
        for (int pc = QUEEN[us]; pc >= PAWN[us]; pc--) {
            if (boards[pc] != 0) {
                return pc;
            }
        }
        return EMPTY;
    }

    inline U64 pawnsAndKings() {
        return boards[WKING] | boards[BKING] | boards[WPAWN] | boards[BPAWN];
    }

    inline U64 allPawns() {
        return boards[WPAWN] | boards[BPAWN];
    }

    inline bool stmHasQueen() {
        return boards[QUEEN[stack->wtm]];
    }

    inline U64 closedFiles() {
        return FILEFILL(boards[WPAWN]) & FILEFILL(boards[BPAWN]);
    }

    inline U64 openFiles() {
        return ~(FILEFILL(boards[WPAWN]) | FILEFILL(boards[BPAWN]));
    }

    inline U64 halfOpenOrOpenFile(bool white) {
        return white ? ~FILEFILL(boards[WPAWN]) : ~FILEFILL(boards[BPAWN]);
    }

    inline U64 halfOpenFiles(bool white) {
        return halfOpenOrOpenFile(white) ^ openFiles();
    }

    inline U64 all(bool wtm) {
        return wtm? boards[WPIECES] : boards[BPIECES];
    }

    inline U64 getPieces(bool wtm) {
        return wtm ?
                boards[WROOK] | boards[WKNIGHT] | boards[WBISHOP] | boards[WQUEEN]
                :
                boards[BROOK] | boards[BKNIGHT] | boards[BBISHOP] | boards[BQUEEN];
    }

    inline bool hasPieces(bool wtm) {
        return wtm ?
                boards[WROOK] || boards[WKNIGHT] || boards[WBISHOP] || boards[WQUEEN]
                :
                boards[BROOK] || boards[BKNIGHT] || boards[BBISHOP] || boards[BQUEEN];
    }

    inline bool hasPieces() {
        return boards[WROOK] || boards[BROOK] || boards[WKNIGHT] || boards[BKNIGHT]
                || boards[WBISHOP] || boards[BBISHOP] || boards[WQUEEN] || boards[BQUEEN];
    }

    inline bool hasMajors() {
        return boards[WROOK] || boards[BROOK] || boards[WQUEEN] || boards[BQUEEN];
    }

    inline bool hasPawns() {
        return boards[WPAWN] ||boards[BPAWN];
    }

    inline bool onlyPawns() {
        return hasPieces() == false;
    }

    bool oppBishopsEG() {
        if (boards[WROOK] || boards[BROOK]
                || boards[WKNIGHT] || boards[WKNIGHT]
                || boards[WQUEEN] || boards[BQUEEN]
                || boards[WBISHOP] == 0 || boards[BBISHOP] == 0
                || gt_1(boards[WBISHOP]) || gt_1(boards[BBISHOP])) {
            return false;
        }
        return bool(boards[WBISHOP] & WHITE_SQUARES) == bool(boards[BBISHOP] & BLACK_SQUARES);
    }

    bool isKBBKN(bool us) {
        if (boards[WPAWN] || boards[BPAWN]
                || boards[WROOK] || boards[BROOK]
                || boards[WQUEEN] || boards[BQUEEN]
                || boards[KNIGHT[us]] || boards[BISHOP[!us]]
                || gt_1(boards[KNIGHT[us]])) {
            return false;
        }
        return bishopPair(us);
    }

    bool isKBPsK(bool us) {
        return boards[PAWN[!us]] == 0 && boards[PAWN[us]] != 0
                && boards[WROOK] == 0 && boards[BROOK] == 0
                && boards[WQUEEN] == 0 && boards[BQUEEN] == 0
                && boards[WKNIGHT] == 0 && boards[BKNIGHT] == 0
                && is_1(boards[BISHOP[us]]) && boards[BISHOP[!us]] == 0;
    }

    bool isKNPK(bool us) {
        return boards[PAWN[!us]] == 0 && is_1(boards[PAWN[us]])
                && boards[WROOK] == 0 && boards[BROOK] == 0
                && boards[WQUEEN] == 0 && boards[BQUEEN] == 0
                && boards[WBISHOP] == 0 && boards[BBISHOP] == 0
                && is_1(boards[KNIGHT[us]]) && boards[KNIGHT[!us]] == 0;
    }

    

    void fromFen(const char* fen);
    string asFen();

    /**
     * Update the board representation when adding a piece (e.g. a promotion)
     * @param piece the piece type to be added
     * @param sq the location square of the piece (a1..h8)
     */
    inline void addPiece(int piece, int sq) {
        U64 bit = BIT(sq);
        boards[piece] ^= bit;
        boards[WPIECES + (piece > WKING)] ^= bit;
        boards[ALLPIECES] ^= bit;
        matrix[sq] = piece;
    }

    /**
     * Add a new piece to the board structure and update hash codes
     * @param piece the piece type (wknight..bking) to add
     * @param sq the piece location (a1..h8)
     */
    inline void addPieceFull(int piece, int sq) {
        HASH_ADD_PIECE(stack->material_hash, piece, count(piece) + _bishop_ix(piece, sq));
        HASH_ADD_PIECE(stack->hash_code, piece, sq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_ADD_PIECE(stack->pawn_hash, piece, sq);
        }
        addPiece(piece, sq);
    }

    /**
     * Update the board representation when removing a piece (e.g. a capture)
     * @param piece the piece type to be removed
     * @param sq the square location of the piece (a1..h8)
     */
    inline void removePiece(int piece, int sq) {
        U64 bit = BIT(sq);
        boards[piece] ^= bit;
        boards[WPIECES + (piece > WKING)] ^= bit;
        boards[ALLPIECES] ^= bit;
        matrix[sq] = EMPTY;
    }

    /**
     * Remove a piece from the board representation and hash keys
     * @param piece the piece type to be removed
     * @param sq the square location of the piece (a1..h8)
     */
    inline void removePieceFull(int piece, int sq) {
        removePiece(piece, sq);
        HASH_REMOVE_PIECE(stack->material_hash, piece, count(piece) + _bishop_ix(piece, sq));
        HASH_REMOVE_PIECE(stack->hash_code, piece, sq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_REMOVE_PIECE(stack->pawn_hash, piece, sq);
        }
    }

    /**
     * Update the board representation when moving a piece on the board
     * @param piece the moving piece type
     * @param ssq source square, where the piece if moving from
     * @param tsq target square, where the piece is moving to
     */
    inline void movePiece(int piece, int ssq, int tsq) {
        U64 updateMask = BIT(ssq) | BIT(tsq);
        boards[piece] ^= updateMask;
        boards[WPIECES + (piece > WKING)] ^= updateMask;
        boards[ALLPIECES] ^= updateMask;
        matrix[ssq] = EMPTY;
        matrix[tsq] = piece;
    }

    /**
     * Update the board representation and hash codes when moves a piece
     * @param piece the moving piece type
     * @param ssq source square, where the piece if moving from
     * @param tsq target square, where the piece is moving to
     */
    inline void movePieceFull(int piece, int ssq, int tsq) {
        movePiece(piece, ssq, tsq);
        HASH_MOVE_PIECE(stack->hash_code, piece, ssq, tsq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_MOVE_PIECE(stack->pawn_hash, piece, ssq, tsq);
        }
    }

    void forward(TMove * move); //make a move

    void backward(TMove * move); //unmake a move

    void forward(); //do a nullmove 

    void backward(); //undo nullmove

    /**
     * Verify if a square is attacked by black pieces
     * @param sq the square (a1..h8)
     * @return true: attacked, false: not attacked
     */
    inline bool attackedByBlack(int sq) {
        return boards[BKNIGHT] & KNIGHT_MOVES[sq]
                || boards[BPAWN] & WPAWN_CAPTURES[sq]
                || boards[BKING] & KING_MOVES[sq]
                || (boards[BBISHOP] | boards[BQUEEN]) & magic::bishop_moves(sq, boards[ALLPIECES])
                || (boards[BROOK] | boards[BQUEEN]) & magic::rook_moves(sq, boards[ALLPIECES]);
    }

    /**
     * Verify if a square is attacked by white pieces
     * @param sq the square (a1..h8)
     * @return true: attacked, false: not attacked
     */
    inline bool attackedByWhite(int sq) {
        return boards[WKNIGHT] & KNIGHT_MOVES[sq]
                || boards[WPAWN] & BPAWN_CAPTURES[sq]
                || boards[WKING] & KING_MOVES[sq]
                || (boards[WBISHOP] | boards[WQUEEN]) & magic::bishop_moves(sq, boards[ALLPIECES])
                || (boards[WROOK] | boards[WQUEEN]) & magic::rook_moves(sq, boards[ALLPIECES]);
    }

    inline bool attackedBy(int sq, bool us) {
        return us == WHITE ? attackedByWhite(sq) : attackedByBlack(sq);
    }

    /**
     * Get a bitboard of all attacking pieces (not pawns) to a square
     * @param sq the square (a1..h8) to investigate 
     * @return bitboard populated with pieces attacking the square
     */
    inline U64 pieceAttacksTo(int sq) {
        return (KNIGHT_MOVES[sq] & (boards[WKNIGHT] | boards[BKNIGHT]))
                | (KING_MOVES[sq] & (boards[WKING] | boards[BKING]))
                | (magic::bishop_moves(sq, boards[ALLPIECES]) & (boards[WBISHOP] | boards[WQUEEN] | boards[BBISHOP] | boards[BQUEEN]))
                | (magic::rook_moves(sq, boards[ALLPIECES]) & (boards[WROOK] | boards[WQUEEN] | boards[BROOK] | boards[BQUEEN]));
    }

    /**
     * Test if a castling flag is set or not
     * @param flag the flag to test
     * @return true: flag is set, false: otherwise 
     */
    inline bool castleRight(int flag) {
        return stack->castling_flags & flag;
    }

    /**
     * Get the current game ply 
     * @return game ply number
     */
    inline int getGamePly() {
        return root_ply + current_ply;
    }

    /**
     * Test if a side has the bishop pair
     * @param white side to test
     * @return true: side has bishop pair, false: side does not have the bishop pair
     */
    inline bool bishopPair(bool us) {
        int pc = BISHOP[us];
        return (boards[pc] & WHITE_SQUARES) && (boards[pc] & BLACK_SQUARES);
    }

    bool isDraw(); //verify if the position on the board is a trivial draw

    /**
     * Test if the actual position is legal, e.g. no king can be captured
     * @return true: legal, false: not legal
     */
    inline bool legal() {
        return stack->wtm ? (attackedByWhite(get_sq(BKING)) == false) :
                (attackedByBlack(get_sq(WKING)) == false);
    }

    bool legal(TMove * move); //test if a move is legal in the actual position

    bool valid(TMove * move); //test if a move is valid in the actual position

    /**
     * Test if the king is in check
     * @return true: in check, false: not in check
     */
    inline bool inCheck() {
        return stack->wtm ? attackedByBlack(get_sq(WKING)) :
                attackedByWhite(get_sq(BKING));
    }

    int givesCheck(TMove * move); //test if a move checks the opponent's king

    /**
     * Test is a pawn is pushed to the 7th or 8th rank for white, or 1st/2nd for black
     * @param move the move to test
     * @return true: pawn is pushed to promotion rank or is promoting, false: otherwise
     */
    inline bool push7th(TMove * move) {
        return (move->piece == WPAWN && move->tsq >= h7)
                || (move->piece == BPAWN && move->tsq <= h2);
    }

    /**
     * Return a bitboard with white pawns that can promote
     * @return populated bitboard of promoting pawns
     */
    inline U64 promotingWhitePawns() {
        return boards[WPAWN] & RANK_7;
    }

    /**
     * Return a bitboard with black pawns that can promote
     * @return populated bitboard of promoting pawns
     */
    inline U64 promotingBlackPawns() {
        return boards[BPAWN] & RANK_2;
    }

    /**
     * Return a bitboard with pawns that can promote for the current side to move
     * @return populated bitboard of promoting pawns
     */
    inline bool promotingPawn() {
        return stack->wtm ? promotingWhitePawns() : promotingBlackPawns();
    }

    /**
     * Test if a position has promoting pawns
     * @param wtm the side to test
     * @return true: side has promoting pawns, false otherwise
     */
    inline bool promotingPawns(bool wtm) {
        return wtm ? promotingWhitePawns() : promotingBlackPawns();
    }

    /**
     * Return a bitboard with all attacks to a given square
     * @param sq the square to investigate
     * @return bitboard populated with all pieces attacking the square
     */
    inline U64 attacksTo(int sq) {
        return (KNIGHT_MOVES[sq] & (boards[WKNIGHT] | boards[BKNIGHT]))
                | (BPAWN_CAPTURES[sq] & boards[WPAWN])
                | (WPAWN_CAPTURES[sq] & boards[BPAWN])
                | (KING_MOVES[sq] & (boards[WKING] | boards[BKING]))
                | (magic::bishop_moves(sq, boards[ALLPIECES]) & (boards[WBISHOP] | boards[WQUEEN] | boards[BBISHOP] | boards[BQUEEN]))
                | (magic::rook_moves(sq, boards[ALLPIECES]) & (boards[WROOK] | boards[WQUEEN] | boards[BROOK] | boards[BQUEEN]));
    }

    inline U64 whitePawnAttacks() {
        return (UPLEFT1(boards[WPAWN]) | UPRIGHT1(boards[WPAWN]));
    }

    inline U64 blackPawnAttacks() {
        return (DOWNLEFT1(boards[BPAWN]) | DOWNRIGHT1(boards[BPAWN]));
    }

    inline U64 pawnAttacks(bool white) {
        return white ? whitePawnAttacks() : blackPawnAttacks();
    }

    inline U64 pawnAttacks(int sq, bool white) {
        return white ? WPAWN_CAPTURES[sq] : BPAWN_CAPTURES[sq];
    }

    inline bool attackedByWhitePawn(int sq) {
        return BPAWN_CAPTURES[sq] & boards[WPAWN];
    }

    inline bool attackedByBlackPawn(int sq) {
        return WPAWN_CAPTURES[sq] & boards[BPAWN];
    }

    inline bool attackedByPawn(int sq, bool white) {
        return white ? attackedByWhitePawn(sq) : attackedByBlackPawn(sq);
    }

    inline bool attackedByOpponentPawn(int sq) {
        return stack->wtm ? attackedByBlackPawn(sq) : attackedByWhitePawn(sq);
    }

    U64 getSmallestAttacker(U64 attacks, bool wtm, uint8_t &piece);

    int SEE(TMove * capture);

};


#endif	/* BOARD_H */
