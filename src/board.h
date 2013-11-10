/* 
 * File:   board.h
 * Author: Hermen Reitsma
 *
 * Created on 9 april 2011, 2:52
 * 
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
#define WPAWN    1
#define WKNIGHT  2
#define WBISHOP  3
#define WROOK    4
#define WQUEEN   5
#define WKING    6
#define BPAWN   7
#define BKNIGHT 8
#define BBISHOP 9
#define BROOK   10
#define BQUEEN  11
#define BKING   12

#define WPIECES 13
#define BPIECES 14
#define ALLPIECES   0

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

struct TBoardFlags {
    unsigned char epSquare;
    unsigned char castlingFlags;
    unsigned char fiftyCount;
    unsigned char checkerSq;
    bool WTM; //white to move
    U64 hashCode;
    U64 materialHash;
    U64 pawnHash;
    U64 checkers;

    TScore pct;

    void clear() {
        epSquare = 0;
        castlingFlags = 0;
        fiftyCount = 0;
        WTM = true;
        hashCode = 0;
        materialHash = 0;
        pawnHash = 0;
        pct.clear();
        checkers = 0;
    }

    void copy(TBoardFlags * bFlags) {
        epSquare = bFlags->epSquare;
        castlingFlags = bFlags->castlingFlags;
        fiftyCount = bFlags->fiftyCount;
        WTM = bFlags->WTM;
        hashCode = bFlags->hashCode;
        materialHash = bFlags->materialHash;
        pawnHash = bFlags->pawnHash;
        pct = bFlags->pct;
    }
};

struct TBoard {
    int currentPly;
    int rootPly;

    TBoardFlags _boardFlags[MAX_PLY + 1];
    TBoardFlags * boardFlags;


    U64 whitePawns;
    U64 blackPawns;
    U64 whiteKnights;
    U64 blackKnights;
    U64 whiteBishops;
    U64 blackBishops;
    U64 whiteRooks;
    U64 blackRooks;
    U64 whiteQueens;
    U64 blackQueens;
    U64 whiteKings;
    U64 blackKings;
    U64 whitePieces;
    U64 blackPieces;
    U64 allPieces;
    U64 * boards[BPIECES + 1];

    unsigned char Matrix[64];
    TPiecePlacement pieces[BKING + 1];
    const unsigned char * whiteKingPos;
    const unsigned char * blackKingPos;
    TSCORE_PCT pieceSquareTable;

    void clear();
    void setPieceSquareTable(const TSCORE_PCT & pct);
    void clearPieceSquareTable();

    inline int topPiece(bool white) {
        if (white) {
            return whiteQueens ? WQUEEN : whiteRooks ? WROOK : whiteBishops ? WBISHOP : whiteKnights ? WKNIGHT : WPAWN;
        } else {
            return blackQueens ? BQUEEN : blackRooks ? BROOK : blackBishops ? BBISHOP : blackKnights ? BKNIGHT : BPAWN;
        }
    }

    inline U64 pawnsAndKings() {
        return whiteKings | blackKings | whitePawns | blackPawns;
    }

    inline U64 pawns() {
        return whitePawns | blackPawns;
    }

    inline U64 closedFiles() {
        return FILEFILL(whitePawns) & FILEFILL(blackPawns);
    }

    inline U64 openFiles() {
        return ~(FILEFILL(whitePawns) | FILEFILL(blackPawns));
    }

    inline U64 halfOpenOrOpenFile(bool white) {
        return white ? ~FILEFILL(whitePawns) : ~FILEFILL(blackPawns);
    }

    inline U64 halfOpenFiles(bool white) {
        return halfOpenOrOpenFile(white) ^ openFiles();
    }

    inline U64 getPieces(bool wtm) {
        return wtm ? whiteRooks | whiteKnights | whiteBishops | whiteQueens 
                : blackRooks | blackKnights | blackBishops | blackQueens;
    }
    
    inline U64 queensOrMinorsAndRooks(bool white) {
        return white ? (whiteQueens || (whiteKnights && whiteBishops && whiteRooks))
                : (blackQueens || (whiteKnights && whiteBishops && whiteRooks));
    }

    void fromFen(const char* fen);
    string asFen();

    /**
     * Update the board representation when adding a piece (e.g. a promotion)
     * @param piece the piece to be added
     * @param sq the location square of the piece (a1-h8)
     */
    inline void addPiece(int piece, int sq) {
        U64 bit = BIT(sq);
        *boards[piece] ^= bit;
        *boards[WPIECES + (piece > WKING)] ^= bit;
        allPieces ^= bit;
        Matrix[sq] = piece;
        pieces[piece].add(sq);
    }

    inline void addPieceFull(int piece, int sq) {
        HASH_ADD_PIECE(boardFlags->materialHash, piece, pieces[piece].count + BISHOP_IX(piece, sq));
        HASH_ADD_PIECE(boardFlags->hashCode, piece, sq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_ADD_PIECE(boardFlags->pawnHash, piece, sq);
        }
        addPiece(piece, sq);
        boardFlags->pct.add(pieceSquareTable[piece][sq]);
    }

    /**
     * Update the board representation when removing a piece (e.g. a capture)
     * @param piece the piece to be removed
     * @param sq the square location of the piece (a1-h8)
     */
    inline void removePiece(int piece, int sq) {
        U64 bit = BIT(sq);
        *boards[piece] ^= bit;
        *boards[WPIECES + (piece > WKING)] ^= bit;
        allPieces ^= bit;
        Matrix[sq] = EMPTY;
        pieces[piece].remove(sq);
    }

    inline void removePieceFull(int piece, int sq) {
        boardFlags->pct.sub(pieceSquareTable[piece][sq]);
        removePiece(piece, sq);
        HASH_REMOVE_PIECE(boardFlags->materialHash, piece, pieces[piece].count + BISHOP_IX(piece, sq));
        HASH_REMOVE_PIECE(boardFlags->hashCode, piece, sq);
        if (piece == WPAWN || piece == BPAWN || piece == WKING || piece == BKING) {
            HASH_REMOVE_PIECE(boardFlags->pawnHash, piece, sq);
        }
    }

    /**
     * Update the board representation when moving a piece on the board
     * @param piece the moving piece
     * @param ssq source square, where the piece if moving from
     * @param tsq target square, where the piece is moving to
     */
    inline void movePiece(int piece, int ssq, int tsq) {
        U64 updateMask = BIT(ssq) | BIT(tsq);
        *boards[piece] ^= updateMask;
        *boards[WPIECES + (piece > WKING)] ^= updateMask;
        allPieces ^= updateMask;
        Matrix[ssq] = EMPTY;
        Matrix[tsq] = piece;
        pieces[piece].update(ssq, tsq);
    }

    inline void movePieceFull(int piece, int ssq, int tsq) {
        movePiece(piece, ssq, tsq);
        HASH_MOVE_PIECE(boardFlags->hashCode, piece, ssq, tsq);
        if (piece == WPAWN || piece == BPAWN) {
            HASH_MOVE_PIECE(boardFlags->pawnHash, piece, ssq, tsq);
        }
        boardFlags->pct.add(pieceSquareTable[piece][tsq]);
        boardFlags->pct.sub(pieceSquareTable[piece][ssq]);
    }



    void forward(TMove * move);
    void backward(TMove * move);

    void forward();
    void backward();

    inline bool attackedByBlack(int sq) {
        return KnightMoves[sq] & blackKnights
                || WPawnCaptures[sq] & blackPawns
                || KingMoves[sq] & blackKings
                || MagicBishopMoves(sq, allPieces) & (blackBishops | blackQueens)
                || MagicRookMoves(sq, allPieces) & (blackRooks | blackQueens);
    }

    inline U64 pieceAttacksTo(int sq) {
        return (KnightMoves[sq] & (whiteKnights | blackKnights))
                | (KingMoves[sq] & (whiteKings | blackKings))
                | (MagicBishopMoves(sq, allPieces) & (whiteBishops | whiteQueens | blackBishops | blackQueens))
                | (MagicRookMoves(sq, allPieces) & (whiteRooks | whiteQueens | blackRooks | blackQueens));
    }

    inline bool attackedByWhite(int sq) {
        return KnightMoves[sq] & whiteKnights
                || BPawnCaptures[sq] & whitePawns
                || KingMoves[sq] & whiteKings
                || MagicBishopMoves(sq, allPieces) & (whiteBishops | whiteQueens)
                || MagicRookMoves(sq, allPieces) & (whiteRooks | whiteQueens);
    }

    inline bool attackedByWhitePawn(int sq) {
        return BPawnCaptures[sq] & whitePawns;
    }

    inline bool attackedByBlackPawn(int sq) {
        return WPawnCaptures[sq] & blackPawns;
    }

    inline bool attackedByOpponentPawn(int sq) {
        return boardFlags->WTM ? attackedByBlackPawn(sq) : attackedByWhitePawn(sq);
    }

    inline bool legal() {
        return boardFlags->WTM ? (attackedByWhite(*blackKingPos) == false) :
                (attackedByBlack(*whiteKingPos) == false);
    }
    bool legal(TMove * move);
    bool valid(TMove * move);

    inline bool inCheck() {
        return boardFlags->WTM ? attackedByBlack(*whiteKingPos) :
                attackedByWhite(*blackKingPos);
    }

    int givesCheck(TMove * move);
    bool active(TMove * move);
    bool checksPiece(TMove * move);

    inline bool push7th(TMove * move) {
        return (move->piece == WPAWN && move->tsq >= h7)
                || (move->piece == BPAWN && move->tsq <= h2);
    }

    inline U64 promotingWhitePawns() {
        return whitePawns & RANK_7;
    }

    inline U64 promotingBlackPawns() {
        return blackPawns & RANK_2;
    }

    inline bool promotingPawn() {
        return boardFlags->WTM ? promotingWhitePawns() : promotingBlackPawns();
    }

    inline bool promotingPawns(bool wtm) {
        return wtm ? promotingWhitePawns() : promotingBlackPawns();
    }

    inline U64 attacksTo(int sq) {
        return (KnightMoves[sq] & (whiteKnights | blackKnights))
                | (BPawnCaptures[sq] & whitePawns)
                | (WPawnCaptures[sq] & blackPawns)
                | (KingMoves[sq] & (whiteKings | blackKings))
                | (MagicBishopMoves(sq, allPieces) & (whiteBishops | whiteQueens | blackBishops | blackQueens))
                | (MagicRookMoves(sq, allPieces) & (whiteRooks | whiteQueens | blackRooks | blackQueens));
    }

    inline U64 whitePawnAttacks() {
        return (UPLEFT1(whitePawns) | UPRIGHT1(whitePawns));
    }

    inline U64 blackPawnAttacks() {
        return (DOWNLEFT1(blackPawns) | DOWNRIGHT1(blackPawns));
    }

    U64 getSmallestAttacker(U64 attacks, bool wtm, int &piece);
    int SEE(TMove * capture);

    inline bool castleRight(int flag) {
        return boardFlags->castlingFlags & flag;
    }

    inline int getGamePly() {
        return rootPly + currentPly;
    }

    bool whiteBishopPair() {
        return whiteBishops & BLACK_SQUARES && whiteBishops & WHITE_SQUARES;
    }

    bool blackBishopPair() {
        return blackBishops & BLACK_SQUARES && blackBishops & WHITE_SQUARES;
    }

    bool isDraw();

};


#endif	/* BOARD_H */
