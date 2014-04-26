#include "move.h"
#include "board.h"
#include "bbmoves.h"
#include "movegen.h"

/*
 * Loop through all pieces and fill the boardstructure 
 * with moves bitboards
 */


void genCaptures(TBoard * board, TMoveList * list, U64 targets) {
    U64 occ = board->allPieces;
    U64 moves;
    TMove * current = list->last;
    list->current = current;
    if (board->boardFlags->WTM) {
        targets &= board->blackPieces;
        /**
         * WHITE PAWN 
         */
        U64 pawnCaptures = board->whitePawnAttacks() & (targets | BIT(board->boardFlags->epSquare));
        while (pawnCaptures) {
            int tsq = POP(pawnCaptures);
            moves = BPawnCaptures[tsq] & board->whitePawns;
            while (moves) {
                int ssq = POP(moves);
                if (tsq == board->boardFlags->epSquare) {
                    (current++)->setCapture(WPAWN, ssq, tsq, BPAWN);
                } else if (tsq < a8) {
                    (current++)->setCapture(WPAWN, ssq, tsq, board->Matrix[tsq]);
                } else {
                    (current++)->setPromotionCapture(WPAWN, ssq, tsq, WQUEEN, board->Matrix[tsq]);
                    (current++)->setPromotionCapture(WPAWN, ssq, tsq, WKNIGHT, board->Matrix[tsq]);
                    (current++)->setPromotionCapture(WPAWN, ssq, tsq, WROOK, board->Matrix[tsq]);
                    (current++)->setPromotionCapture(WPAWN, ssq, tsq, WBISHOP, board->Matrix[tsq]);
                }
            }
        }

        /**
         * WHITE KNIGHT
         */
        TPiecePlacement * pc = &board->pieces[WKNIGHT];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = KnightMoves[ssq] & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(WKNIGHT, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * WHITE BISHOP 
         */
        pc = &board->pieces[WBISHOP];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicBishopMoves(ssq, occ) & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(WBISHOP, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * WHITE ROOK 
         */
        pc = &board->pieces[WROOK];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicRookMoves(ssq, occ) & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(WROOK, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * WHITE QUEEN 
         */
        pc = &board->pieces[WQUEEN];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicQueenMoves(ssq, occ) & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(WQUEEN, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * WHITE KING 
         */
        pc = &board->pieces[WKING];
        moves = KingMoves[*board->whiteKingPos] & board->blackPieces;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(WKING, *board->whiteKingPos, tsq, board->Matrix[tsq]);
        }
    } else { //BTM
        targets &= board->whitePieces;
        /**
         * BLACK PAWN 
         */
        U64 pawnTargets = targets;
        if (board->boardFlags->epSquare) {
            pawnTargets |= BIT(board->boardFlags->epSquare);
        }
        U64 pawnCaptures = board->blackPawnAttacks() & pawnTargets;
        while (pawnCaptures) {
            int tsq = POP(pawnCaptures);
            moves = WPawnCaptures[tsq] & board->blackPawns;
            while (moves) {
                int ssq = POP(moves);
                if (tsq == board->boardFlags->epSquare && board->boardFlags->epSquare) {
                    (current++)->setCapture(BPAWN, ssq, tsq, WPAWN);
                } else if (tsq > h1) {
                    (current++)->setCapture(BPAWN, ssq, tsq, board->Matrix[tsq]);
                } else {
                    (current++)->setPromotionCapture(BPAWN, ssq, tsq, BQUEEN, board->Matrix[tsq]);
                    (current++)->setPromotionCapture(BPAWN, ssq, tsq, BKNIGHT, board->Matrix[tsq]);
                    (current++)->setPromotionCapture(BPAWN, ssq, tsq, BROOK, board->Matrix[tsq]);
                    (current++)->setPromotionCapture(BPAWN, ssq, tsq, BBISHOP, board->Matrix[tsq]);
                }
            }
        }
        /**
         * BLACK KNIGHT MOVES
         */
        TPiecePlacement * pc = &board->pieces[BKNIGHT];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = KnightMoves[ssq] & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(BKNIGHT, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * BLACK BISHOP MOVES
         */
        pc = &board->pieces[BBISHOP];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicBishopMoves(ssq, occ) & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(BBISHOP, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * BLACK ROOK MOVES
         */
        pc = &board->pieces[BROOK];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicRookMoves(ssq, occ) & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(BROOK, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * BLACK QUEEN MOVES
         */
        pc = &board->pieces[BQUEEN];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicQueenMoves(ssq, occ) & targets;
            while (moves) {
                int tsq = POP(moves);
                (current++)->setCapture(BQUEEN, ssq, tsq, board->Matrix[tsq]);
            }
        }
        /**
         * BLACK KING MOVES
         */
        pc = &board->pieces[BKING];
        moves = KingMoves[*board->blackKingPos] & board->whitePieces;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(BKING, *board->blackKingPos, tsq, board->Matrix[tsq]);
        }
    }
    list->last = current;
}

void genPromotions(TBoard * board, TMoveList * list) {
    TMove * current = list->last;
    list->current = current;
    if (board->boardFlags->WTM) {
        U64 pieces = board->whitePawns & RANK_7;
        while (pieces) {
            int ssq = POP(pieces);
            int tsq = ssq + 8;
            if (board->Matrix[tsq] == EMPTY) {
                (current++)->setPromotion(WPAWN, ssq, tsq, WQUEEN);
                (current++)->setPromotion(WPAWN, ssq, tsq, WKNIGHT);
                (current++)->setPromotion(WPAWN, ssq, tsq, WROOK);
                (current++)->setPromotion(WPAWN, ssq, tsq, WBISHOP);
            }
        }
    } else {
        U64 pieces = board->blackPawns & RANK_2;
        while (pieces) {
            int ssq = POP(pieces);
            int tsq = ssq - 8;
            if (board->Matrix[tsq] == EMPTY) {
                (current++)->setPromotion(BPAWN, ssq, tsq, BQUEEN);
                (current++)->setPromotion(BPAWN, ssq, tsq, BKNIGHT);
                (current++)->setPromotion(BPAWN, ssq, tsq, BROOK);
                (current++)->setPromotion(BPAWN, ssq, tsq, BBISHOP);
            }
        }
    }
    list->last = current;
}

void genCastles(TBoard * board, TMoveList * list) {
    TMove * current = list->last;
    list->current = current;
    if (board->boardFlags->WTM) {
        if (board->castleRight(CASTLE_K)
                && board->Matrix[f1] == EMPTY
                && board->Matrix[g1] == EMPTY) {
            (current++)->setMove(WKING, e1, g1);
        }
        if (board->castleRight(CASTLE_Q)
                && board->Matrix[d1] == EMPTY
                && board->Matrix[c1] == EMPTY
                && board->Matrix[b1] == EMPTY) {
            (current++)->setMove(WKING, e1, c1);
        }
    } else {
        if (board->castleRight(CASTLE_k)
                && board->Matrix[f8] == EMPTY
                && board->Matrix[g8] == EMPTY) {
            (current++)->setMove(BKING, e8, g8);
        }
        if (board->castleRight(CASTLE_q)
                && board->Matrix[d8] == EMPTY
                && board->Matrix[c8] == EMPTY
                && board->Matrix[b8] == EMPTY) {
            (current++)->setMove(BKING, e8, c8);
        }
    }
    list->last = current;
}

void genQuietMoves(TBoard * board, TMoveList * list) {
    U64 occ = board->allPieces;
    U64 targets = ~occ;
    U64 moves;
    TMove * current = list->last;
    list->current = current;
    if (board->boardFlags->WTM) {
        /**
         * WHITE PAWN MOVES
         */
        TPiecePlacement * pc = &board->pieces[WPAWN];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            int tsq = ssq + 8;
            if (board->Matrix[tsq] == EMPTY && tsq < a8) {
                (current++)->setMove(WPAWN, ssq, tsq);
                if (ssq <= h2 && board->Matrix[ssq + 16] == EMPTY) {
                    (current++)->setMove(WPAWN, ssq, ssq + 16);
                }
            }
        }
        /**
         * WHITE KNIGHT MOVES
         */
        pc = &board->pieces[WKNIGHT];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = KnightMoves[ssq] & targets;
            while (moves) {
                (current++)->setMove(WKNIGHT, ssq, POP(moves));
            }
        }
        /**
         * WHITE BISHOP MOVES
         */
        pc = &board->pieces[WBISHOP];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicBishopMoves(ssq, occ) & targets;
            while (moves) {
                (current++)->setMove(WBISHOP, ssq, POP(moves));
            }
        }
        /**
         * WHITE ROOK MOVES
         */
        pc = &board->pieces[WROOK];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicRookMoves(ssq, occ) & targets;
            while (moves) {
                (current++)->setMove(WROOK, ssq, POP(moves));
            }
        }
        /**
         * WHITE QUEEN MOVES
         */
        pc = &board->pieces[WQUEEN];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicQueenMoves(ssq, occ) & targets;
            while (moves) {
                (current++)->setMove(WQUEEN, ssq, POP(moves));
            }
        }
        /**
         * WHITE KING MOVES
         */
        pc = &board->pieces[WKING];
        moves = KingMoves[*board->whiteKingPos] & targets;
        while (moves) {
            (current++)->setMove(WKING, *board->whiteKingPos, POP(moves));
        }
    } else { //BTM

        /**
         * BLACK PAWN MOVES
         */
        TPiecePlacement * pc = &board->pieces[BPAWN];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            int tsq = ssq - 8;
            if (board->Matrix[tsq] == EMPTY && tsq > h1) {
                (current++)->setMove(BPAWN, ssq, tsq);
                if (ssq >= a7 && board->Matrix[ssq - 16] == EMPTY) {
                    (current++)->setMove(BPAWN, ssq, ssq - 16);
                }
            }
        }
        /**
         * BLACK KNIGHT MOVES
         */
        pc = &board->pieces[BKNIGHT];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = KnightMoves[ssq] & targets;
            while (moves) {
                (current++)->setMove(BKNIGHT, ssq, POP(moves));
            }
        }
        /**
         * BLACK BISHOP MOVES
         */
        pc = &board->pieces[BBISHOP];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicBishopMoves(ssq, occ) & targets;
            while (moves) {
                (current++)->setMove(BBISHOP, ssq, POP(moves));
            }
        }
        /**
         * BLACK ROOK MOVES
         */
        pc = &board->pieces[BROOK];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicRookMoves(ssq, occ) & targets;
            while (moves) {
                (current++)->setMove(BROOK, ssq, POP(moves));
            }
        }
        /**
         * BLACK QUEEN MOVES
         */
        pc = &board->pieces[BQUEEN];
        for (int i = 0; i < pc->count; i++) {
            int ssq = pc->squares[i];
            moves = MagicQueenMoves(ssq, occ) & targets;
            while (moves) {
                (current++)->setMove(BQUEEN, ssq, POP(moves));
            }
        }
        /**
         * BLACK KING MOVES
         */
        pc = &board->pieces[BKING];
        moves = KingMoves[*board->blackKingPos] & targets;
        while (moves) {
            (current++)->setMove(BKING, *board->blackKingPos, POP(moves));
        }
    }
    list->last = current;
}

void TMoveList::copy(TMoveList * list) {
    int i = 0;
    first = &_list[0];
    firstX = &_exclude[0];
    TMove * m = list->first;
    while (m != list->last) {
        _list[i].setMove(m);
        _list[i].score = m->score;
        if (m == list->first) {
            first = &_list[i];
        } else if (m == list->pop) {
            pop = &_list[i];
        }
        i++;
        m++;
    }
    last = &_list[i];
    TMove * xm = list->firstX;
    i = 0;
    while (xm != list->lastX) {
        _exclude[i].setMove(xm);
        _exclude[i].score = xm->score;
        if (xm == list->firstX) {
            firstX = &_exclude[i];
        } 
        i++;
        xm++;
    }
    lastX = &_exclude[i];
    minimumScore = list->minimumScore;
    stage = list->stage;
}



