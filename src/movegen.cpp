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
    bool us = board->boardFlags->WTM;
    int pc = PAWN[us];
    int pawn_up = PAWNDIRECTION[us];
    
    //pawn moves:
    assert((us == WHITE && pc == WPAWN) || (us == BLACK && pc == BPAWN));
    TPiecePlacement * pp = &board->pieces[pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        int tsq = ssq + pawn_up;
        if (board->Matrix[tsq] != EMPTY || (BIT(ssq) & RANK[us][7])) {
            continue;
        }
        (current++)->setMove(pc, ssq, tsq);
        if (BIT(ssq) & RANK[us][2]) {
            tsq += pawn_up;
            if (board->Matrix[tsq]) {
                continue;
            }
            (current++)->setMove(pc, ssq, tsq);
        }
    } 
    //knight moves:
    pp = &board->pieces[++pc];
    assert((us == WHITE && pc == WKNIGHT) || (us == BLACK && pc == BKNIGHT));
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = KnightMoves[ssq] & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, POP(moves));
        }
    }   
    //bishop moves:
    pp = &board->pieces[++pc];
    assert((us == WHITE && pc == WBISHOP) || (us == BLACK && pc == BBISHOP));
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicBishopMoves(ssq, occ) & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, POP(moves));
        }
    }   
    //rook moves:
    pp = &board->pieces[++pc];
    assert((us == WHITE && pc == WROOK) || (us == BLACK && pc == BROOK));
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicRookMoves(ssq, occ) & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, POP(moves));
        }
    } 
    //queen moves:
    pp = &board->pieces[++pc];
    assert((us == WHITE && pc == WQUEEN) || (us == BLACK && pc == BQUEEN));
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicQueenMoves(ssq, occ) & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, POP(moves));
        }
    }  
    //king moves:
    int kpos = *board->kingPos[us];
    pc++;
    assert((us == WHITE && pc == WKING) || (us == BLACK && pc == BKING));
    moves = KingMoves[kpos] & targets;
    while (moves) {
        (current++)->setMove(pc, kpos, POP(moves));
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



