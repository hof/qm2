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

#define a1 0
#define b1 1
#define c1 2
#define d1 3
#define e1 4
#define f1 5
#define g1 6
#define h1 7
#define a2 8
#define b2 9
#define c2 10
#define d2 11
#define e2 12
#define f2 13
#define g2 14
#define h2 15
#define a3 16
#define b3 17
#define c3 18
#define d3 19
#define e3 20
#define f3 21
#define g3 22
#define h3 23
#define a4 24
#define b4 25
#define c4 26
#define d4 27
#define e4 28
#define f4 29
#define g4 30
#define h4 31
#define a5 32
#define b5 33
#define c5 34
#define d5 35
#define e5 36
#define f5 37
#define g5 38
#define h5 39
#define a6 40
#define b6 41
#define c6 42
#define d6 43
#define e6 44
#define f6 45
#define g6 46
#define h6 47
#define a7 48
#define b7 49
#define c7 50
#define d7 51
#define e7 52
#define f7 53
#define g7 54
#define h7 55
#define a8 56
#define b8 57
#define c8 58
#define d8 59
#define e8 60
#define f8 61
#define g8 62
#define h8 63

#define EMPTY   0
#define WPAWN   1
#define WKNIGHT 2
#define WBISHOP 3
#define WROOK   4
#define WQUEEN  5
#define WKING   6
#define BPAWN   7
#define BKNIGHT 8
#define BBISHOP 9
#define BROOK   10
#define BQUEEN  11
#define BKING   12
#define WPIECES 13
#define BPIECES 14
#define ALLPIECES 0

const uint8_t PAWN[2] = {BPAWN, WPAWN}; //index by color: white(1) or black(0)
const uint8_t QUEEN[2] = {BQUEEN, WQUEEN};
const uint8_t ROOK[2] = {BROOK, WROOK};
const uint8_t BISHOP[2] = {BBISHOP, WBISHOP};
const uint8_t KNIGHT[2] = {BKNIGHT, WKNIGHT};

#define WHITEPIECE(pc) ((pc)<=WKING)

#define BLACK 0
#define WHITE 1

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

//piece placement structure, containing square locations of pieces

struct TPiecePlacement {
    unsigned char count;
    unsigned char squares[MAX_PIECECOUNT];

    inline void add(int sq) {
        squares[count++] = sq;
        assert(count <= MAX_PIECECOUNT);
    }

    inline void remove(int sq) {
        count--;
        assert(count >= 0);
        for (int i = 0; i < count; i++) {
            if (squares[i] == sq) {
                squares[i] = squares[count];
                return;
            }
        }
    }

    inline void update(int ssq, int tsq) {
        for (int i = 0; i < count; i++) {
            if (squares[i] == ssq) {
                squares[i] = tsq;
                return;
            }
        }
    }
};

//Structure with metadata, e.g. side to move, kept on a stack

struct TBoardStack {
    uint8_t enpassant_sq; //enpassant square
    uint8_t castling_flags;
    uint8_t fifty_count;
    uint8_t checker_sq;
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
    TPiecePlacement pieces[BKING + 1];
    const uint8_t * white_king_sq;
    const uint8_t * black_king_sq;
    const uint8_t * king_sq[2];

    void clear();

    void flip();
    
    int test();

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
    
    inline bool onlyPawns() {
        return hasPieces() == false;
    }
    
    inline bool bothBishops(bool w) {
        return gt_1(*bishops[w]) 
                && (*bishops[w] & WHITE_SQUARES) != 0 
                && (*bishops[w] & BLACK_SQUARES) != 0; 
    }
    
    inline bool isKBBKN(bool w) {
        if (white_pawns || black_pawns 
                || white_rooks || black_rooks 
                || white_queens || black_queens
                || *knights[w] || *bishops[!w]
                || gt_1(*knights[!w])) {
            return false;
        }
        return bothBishops(w); 
    }

    inline U64 queensOrMinorsAndRooks(bool wtm) {
        return wtm ? (white_queens || (white_knights && white_bishops && white_rooks))
                : (black_queens || (white_knights && white_bishops && white_rooks));
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
        pieces[piece].add(sq);
    }

    /**
     * Add a new piece to the board structure and update hash codes
     * @param piece the piece type (wknight..bking) to add
     * @param sq the piece location (a1..h8)
     */
    inline void addPieceFull(int piece, int sq) {
        HASH_ADD_PIECE(stack->material_hash, piece, pieces[piece].count + BISHOP_IX(piece, sq));
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
        pieces[piece].remove(sq);
    }

    /**
     * Remove a piece from the board representation and hash keys
     * @param piece the piece type to be removed
     * @param sq the square location of the piece (a1..h8)
     */
    inline void removePieceFull(int piece, int sq) {
        removePiece(piece, sq);
        HASH_REMOVE_PIECE(stack->material_hash, piece, pieces[piece].count + BISHOP_IX(piece, sq));
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
        pieces[piece].update(ssq, tsq);
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
        return KNIGHT_MOVES[sq] & black_knights
                || WPAWN_CAPTURES[sq] & black_pawns
                || KING_MOVES[sq] & black_kings
                || MagicBishopMoves(sq, all_pieces) & (black_bishops | black_queens)
                || MagicRookMoves(sq, all_pieces) & (black_rooks | black_queens);
    }

    /**
     * Verify if a square is attacked by white pieces
     * @param sq the square (a1..h8)
     * @return true: attacked, false: not attacked
     */
    inline bool attackedByWhite(int sq) {
        return KNIGHT_MOVES[sq] & white_knights
                || BPAWN_CAPTURES[sq] & white_pawns
                || KING_MOVES[sq] & white_kings
                || MagicBishopMoves(sq, all_pieces) & (white_bishops | white_queens)
                || MagicRookMoves(sq, all_pieces) & (white_rooks | white_queens);
    }

    /**
     * Get a bitboard of all attacking pieces (not pawns) to a square
     * @param sq the square (a1..h8) to investigate 
     * @return bitboard populated with pieces attacking the square
     */
    inline U64 pieceAttacksTo(int sq) {
        return (KNIGHT_MOVES[sq] & (white_knights | black_knights))
                | (KING_MOVES[sq] & (white_kings | black_kings))
                | (MagicBishopMoves(sq, all_pieces) & (white_bishops | white_queens | black_bishops | black_queens))
                | (MagicRookMoves(sq, all_pieces) & (white_rooks | white_queens | black_rooks | black_queens));
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
    bool bishopPair(bool white) {
        return white ? whiteBishopPair() : blackBishopPair();
    }

    /**
     * Test is white has the bishop pair
     * @return true: white has the bishop pair, false otherwise
     */
    bool whiteBishopPair() {
        return (white_bishops & BLACK_SQUARES) && (white_bishops & WHITE_SQUARES);
    }

    /**
     * Test if black has the bishop pair
     * @return true: black has the bishop pair, false otherwise
     */
    bool blackBishopPair() {
        return (black_bishops & BLACK_SQUARES) && (black_bishops & WHITE_SQUARES);
    }

    bool isDraw(); //verify if the position on the board is a trivial draw

    /**
     * Test if the actual position is legal, e.g. no king can be captured
     * @return true: legal, false: not legal
     */
    inline bool legal() {
        return stack->wtm ? (attackedByWhite(*black_king_sq) == false) :
                (attackedByBlack(*white_king_sq) == false);
    }

    bool legal(TMove * move); //test if a move is legal in the actual position

    bool valid(TMove * move); //test if a move is valid in the actual position

    /**
     * Test if the king is in check
     * @return true: in check, false: not in check
     */
    inline bool inCheck() {
        return stack->wtm ? attackedByBlack(*white_king_sq) :
                attackedByWhite(*black_king_sq);
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
                | (MagicBishopMoves(sq, all_pieces) & (white_bishops | white_queens | black_bishops | black_queens))
                | (MagicRookMoves(sq, all_pieces) & (white_rooks | white_queens | black_rooks | black_queens));
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
