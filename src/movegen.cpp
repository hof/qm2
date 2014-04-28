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
    bool us = board->boardFlags->WTM;
    bool them = !us;
    targets &= board->all(them);

    //pawn captures (including en-passant and promotion captures)
    int pc = PAWN[us];
    U64 pieces = *board->pawns[us];
    U64 pawn_caps = board->pawnAttacks(us);
    pawn_caps &= board->boardFlags->epSquare == EMPTY? targets : targets | BIT(board->boardFlags->epSquare);
    while (pawn_caps) {
        int tsq = POP(pawn_caps);
        moves = board->pawnAttacks(tsq, them) & pieces;
        while (moves) {
            int ssq = POP(moves);
            if (tsq == board->boardFlags->epSquare && tsq > 0) {
                (current++)->setCapture(pc, ssq, tsq, PAWN[them]);
            } else if ((BIT(tsq) & RANK[us][8]) == 0) {
                (current++)->setCapture(pc, ssq, tsq, board->Matrix[tsq]);
            } else {
                (current++)->setPromotionCapture(pc, ssq, tsq, QUEEN[us], board->Matrix[tsq]);
                (current++)->setPromotionCapture(pc, ssq, tsq, KNIGHT[us], board->Matrix[tsq]);
                (current++)->setPromotionCapture(pc, ssq, tsq, ROOK[us], board->Matrix[tsq]);
                (current++)->setPromotionCapture(pc, ssq, tsq, BISHOP[us], board->Matrix[tsq]);
            }
        }
    }

    //knight captures
    TPiecePlacement * pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = KnightMoves[ssq] & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->Matrix[tsq]);
        }
    }

    //bishop captures
    pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicBishopMoves(ssq, occ) & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->Matrix[tsq]);
        }
    }

    //rook captures
    pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicRookMoves(ssq, occ) & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->Matrix[tsq]);
        }
    }

    //queen captures
    pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicQueenMoves(ssq, occ) & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->Matrix[tsq]);
        }
    }

    //king captures
    pc++;
    int kpos = *board->kingPos[us];
    moves = KingMoves[kpos] & board->all(them);
    while (moves) {
        int tsq = POP(moves);
        (current++)->setCapture(pc, kpos, tsq, board->Matrix[tsq]);
    }

    list->last = current;
}

void genPromotions(TBoard * board, TMoveList * list) {
    TMove * current = list->last;
    list->current = current;
    bool us = board->boardFlags->WTM;
    U64 pieces = *board->pawns[us] & RANK[us][7];
    if (pieces) {
        int pawn_up = PAWNDIRECTION[us];
        int pc = PAWN[us];
        do {
            int ssq = POP(pieces);
            int tsq = ssq + pawn_up;
            if (board->Matrix[tsq] == EMPTY) {
                (current++)->setPromotion(pc, ssq, tsq, QUEEN[us]);
                (current++)->setPromotion(pc, ssq, tsq, KNIGHT[us]);
                (current++)->setPromotion(pc, ssq, tsq, ROOK[us]);
                (current++)->setPromotion(pc, ssq, tsq, BISHOP[us]);
            }
        } while (pieces);
    }
    list->last = current;
}

void genCastles(TBoard * board, TMoveList * list) { //todo: support chess960
    TMove * current = list->last;
    list->current = current;
    if (board->castleRight(CASTLE_ANY)) {
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
                (

                        current++)->setMove(BKING, e8, c8);
            }
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
        (

                current++)->setMove(pc, kpos, POP(moves));
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



