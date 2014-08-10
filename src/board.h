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

enum color_t {
    BLACK, WHITE
};

const piece_t PAWN[2] = {BPAWN, WPAWN};
const piece_t QUEEN[2] = {BQUEEN, WQUEEN};
const piece_t ROOK[2] = {BROOK, WROOK};
const piece_t BISHOP[2] = {BBISHOP, WBISHOP};
const piece_t KNIGHT[2] = {BKNIGHT, WKNIGHT};
const piece_t KING[2] = {BKING, WKING};

#define WHITEPIECE(pc) ((pc)<=WKING)

#define CASTLE_K    1 //white kingside castle
#define CASTLE_Q    2 //white queenside castle
#define CASTLE_k    4 //black kingsside castle
#define CASTLE_q    8 //black queenside castle
#define CASTLE_WHITE    (CASTLE_K|CASTLE_Q)
#define CASTLE_BLACK    (CASTLE_k|CASTLE_q)
#define CASTLE_ANY      (CASTLE_WHITE|CASTLE_BLACK)

#define MAX_PIECECOUNT 10

//for keeping info about bishop pair on different colored squares in material hash key
#define BISHOP_IX(piece,sq) (((piece==WBISHOP||piece==BBISHOP)&&BLACK_SQUARE(sq))<<5)

//Structure with metadata, e.g. side to move, kept on a stack

struct TBoardStack {
    int enpassant_sq; //enpassant square
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

    void copy(TBoardStack * bFlags) {
        enpassant_sq = bFlags->enpassant_sq;
        castling_flags = bFlags->castling_flags;
        fifty_count = bFlags->fifty_count;
        wtm = bFlags->wtm;
        hash_code = bFlags->hash_code;
        material_hash = bFlags->material_hash;
        pawn_hash = bFlags->pawn_hash;
    }
};

//Board representation structure

struct TBoard {
    int current_ply;
    int root_ply;

    TBoardStack _stack[MAX_PLY + 1];
    TBoardStack * stack;

    U64 white_pawns;
    U64 black_pawns;
    U64 white_knights;
    U64 black_knights;
    U64 white_bishops;
    U64 black_bishops;
    U64 white_rooks;
    U64 black_rooks;
    U64 white_queens;
    U64 black_queens;
    U64 white_kings;
    U64 black_kings;
    U64 white_pieces;
    U64 black_pieces;
    U64 all_pieces;
    U64 * boards[BPIECES + 1];
    U64 * bishops[2];
    U64 * rooks[2];
    U64 * pawns[2];
    U64 * queens[2];
    U64 * knights[2];
    U64 * kings[2];

    unsigned char matrix[64];
    
    void clear();

    void flip();
    
    inline int get_sq(int pc) {
        return BSF(*boards[pc]);
    }

    inline int topPiece(bool white) {
        if (white) {
            return white_queens ? WQUEEN : white_rooks ? WROOK : white_bishops ? WBISHOP : white_knights ? WKNIGHT : WPAWN;
        } else {
            return black_queens ? BQUEEN : black_rooks ? BROOK : black_bishops ? BBISHOP : black_knights ? BKNIGHT : BPAWN;
        }
    }

    inline U64 pawnsAndKings() {
        return white_kings | black_kings | white_pawns | black_pawns;
    }

    inline U64 allPawns() {
        return white_pawns | black_pawns;
    }

    inline bool stmHasQueen() {
        return *queens[stack->wtm] != 0;
    }

    inline U64 closedFiles() {
        return FILEFILL(white_pawns) & FILEFILL(black_pawns);
    }

    inline U64 openFiles() {
        return ~(FILEFILL(white_pawns) | FILEFILL(black_pawns));
    }

    inline U64 halfOpenOrOpenFile(bool white) {
        return white ? ~FILEFILL(white_pawns) : ~FILEFILL(black_pawns);
    }

    inline U64 halfOpenFiles(bool white) {
        return halfOpenOrOpenFile(white) ^ openFiles();
    }

    inline U64 all(bool wtm) {
        return wtm ? white_pieces : black_pieces;
    }

    inline U64 getPieces(bool wtm) {
        return wtm ?
                white_rooks | white_knights | white_bishops | white_queens
                :
                black_rooks | black_knights | black_bishops | black_queens;
    }

    inline bool hasPieces(bool wtm) {
        return wtm ?
                white_rooks || white_knights || white_bishops || white_queens
                :
                black_rooks || black_knights || black_bishops || black_queens;
    }

    inline bool hasPieces() {
        return white_rooks || black_rooks || white_knights || black_knights
                || white_bishops || black_bishops || white_queens || black_queens;
    }

    inline bool hasMajors() {
        return white_rooks || black_rooks || white_queens || black_queens;
    }

    inline bool hasPawns() {
        return white_pawns || black_pawns;
    }

    inline bool onlyPawns() {
        return hasPieces() == false;
    }

    bool oppBishopsEG() {
        if (white_rooks || black_rooks
                || white_knights || black_knights
                || white_queens || black_queens
                || white_bishops == 0 || black_bishops == 0
                || gt_1(white_bishops) || gt_1(black_bishops)) {
            return false;
        }
        return bool(white_bishops & WHITE_SQUARES) == bool(black_bishops & BLACK_SQUARES);
    }

    bool isKBBKN(bool w) {
        if (white_pawns || black_pawns
                || white_rooks || black_rooks
                || white_queens || black_queens
                || *knights[w] || *bishops[!w]
                || gt_1(*knights[!w])) {
            return false;
        }
        return bishopPair(w);
    }

    bool isKBPsK(bool us) {
        return *pawns[!us] == 0 && *pawns[us] != 0
                && white_rooks == 0 && black_rooks == 0
                && white_queens == 0 && black_queens == 0
                && white_knights == 0 && black_knights == 0
                && is_1(*bishops[us]) && *bishops[!us] == 0;
    }

    bool isKNPK(bool us) {
        return *pawns[!us] == 0 && is_1(*pawns[us])
                && white_rooks == 0 && black_rooks == 0
                && white_queens == 0 && black_queens == 0
                && white_bishops == 0 && black_bishops == 0
                && is_1(*knights[us]) && *knights[!us] == 0;
    }

    inline int count(int piece) {
        return popCount0(*boards[piece]);
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
        *boards[piece] ^= bit;
        *boards[WPIECES + (piece > WKING)] ^= bit;
        all_pieces ^= bit;
        matrix[sq] = piece;
        //pieces[piece].add(sq);
    }

    /**
     * Add a new piece to the board structure and update hash codes
     * @param piece the piece type (wknight..bking) to add
     * @param sq the piece location (a1..h8)
     */
    inline void addPieceFull(int piece, int sq) {
        HASH_ADD_PIECE(stack->material_hash, piece, count(piece) + BISHOP_IX(piece, sq));
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
        *boards[piece] ^= bit;
        *boards[WPIECES + (piece > WKING)] ^= bit;
        all_pieces ^= bit;
        matrix[sq] = EMPTY;
        //pieces[piece].remove(sq);
    }

    /**
     * Remove a piece from the board representation and hash keys
     * @param piece the piece type to be removed
     * @param sq the square location of the piece (a1..h8)
     */
    inline void removePieceFull(int piece, int sq) {
        removePiece(piece, sq);
        HASH_REMOVE_PIECE(stack->material_hash, piece, count(piece) + BISHOP_IX(piece, sq));
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
        *boards[piece] ^= updateMask;
        *boards[WPIECES + (piece > WKING)] ^= updateMask;
        all_pieces ^= updateMask;
        matrix[ssq] = EMPTY;
        matrix[tsq] = piece;
        //pieces[piece].update(ssq, tsq);
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
        return black_knights & KNIGHT_MOVES[sq]
                || black_pawns & WPAWN_CAPTURES[sq]
                || black_kings & KING_MOVES[sq]
                || (black_bishops | black_queens) & magic::bishop_moves(sq, all_pieces)
                || (black_rooks | black_queens) & magic::rook_moves(sq, all_pieces);
    }

    /**
     * Verify if a square is attacked by white pieces
     * @param sq the square (a1..h8)
     * @return true: attacked, false: not attacked
     */
    inline bool attackedByWhite(int sq) {
        return white_knights & KNIGHT_MOVES[sq]
                || white_pawns & BPAWN_CAPTURES[sq]
                || white_kings & KING_MOVES[sq]
                || (white_bishops | white_queens) & magic::bishop_moves(sq, all_pieces)
                || (white_rooks | white_queens) & magic::rook_moves(sq, all_pieces);
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
        return (KNIGHT_MOVES[sq] & (white_knights | black_knights))
                | (KING_MOVES[sq] & (white_kings | black_kings))
                | (magic::bishop_moves(sq, all_pieces) & (white_bishops | white_queens | black_bishops | black_queens))
                | (magic::rook_moves(sq, all_pieces) & (white_rooks | white_queens | black_rooks | black_queens));
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
    inline bool bishopPair(bool w) {
        int pc = BISHOP[w];
        return (*boards[pc] & WHITE_SQUARES) && (*boards[pc] & BLACK_SQUARES);
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
        return white_pawns & RANK_7;
    }

    /**
     * Return a bitboard with black pawns that can promote
     * @return populated bitboard of promoting pawns
     */
    inline U64 promotingBlackPawns() {
        return black_pawns & RANK_2;
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
        return (KNIGHT_MOVES[sq] & (white_knights | black_knights))
                | (BPAWN_CAPTURES[sq] & white_pawns)
                | (WPAWN_CAPTURES[sq] & black_pawns)
                | (KING_MOVES[sq] & (white_kings | black_kings))
                | (magic::bishop_moves(sq, all_pieces) & (white_bishops | white_queens | black_bishops | black_queens))
                | (magic::rook_moves(sq, all_pieces) & (white_rooks | white_queens | black_rooks | black_queens));
    }

    inline U64 whitePawnAttacks() {
        return (UPLEFT1(white_pawns) | UPRIGHT1(white_pawns));
    }

    inline U64 blackPawnAttacks() {
        return (DOWNLEFT1(black_pawns) | DOWNRIGHT1(black_pawns));
    }

    inline U64 pawnAttacks(bool white) {
        return white ? whitePawnAttacks() : blackPawnAttacks();
    }

    inline U64 pawnAttacks(int sq, bool white) {
        return white ? WPAWN_CAPTURES[sq] : BPAWN_CAPTURES[sq];
    }

    inline bool attackedByWhitePawn(int sq) {
        return BPAWN_CAPTURES[sq] & white_pawns;
    }

    inline bool attackedByBlackPawn(int sq) {
        return WPAWN_CAPTURES[sq] & black_pawns;
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
