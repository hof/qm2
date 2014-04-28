#include "move.h"
#include "board.h"
#include "bbmoves.h"
#include "movegen.h"

/**
 * Generate Captures. The captures are added to a movelist object.
 * @param board board structure object
 * @param list movelist object
 * @param targets the pieces to capture
 */
void genCaptures(TBoard * board, TMoveList * list, U64 targets) {
    TMove * current = list->last;
    list->current = current;
    U64 moves;
    U64 occ = board->all_pieces;
    bool us = board->stack->wtm;
    bool them = !us;
    int pc = PAWN[us];
    targets &= board->all(them);

    //pawn captures (including en-passant and promotion captures):
    U64 pawn_caps = targets;
    if (board->stack->enpassant_sq) {
        pawn_caps |= BIT(board->stack->enpassant_sq);
    }
    pawn_caps &= board->pawnAttacks(us);
    while (pawn_caps) {
        int tsq = POP(pawn_caps);
        moves = board->pawnAttacks(tsq, them) & *board->pawns[us];
        while (moves) {
            int ssq = POP(moves);
            if (tsq == board->stack->enpassant_sq && tsq > 0) {
                (current++)->setCapture(pc, ssq, tsq, PAWN[them]);
            } else if ((BIT(tsq) & RANK[us][8]) == 0) {
                (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
            } else {
                (current++)->setPromotionCapture(pc, ssq, tsq, QUEEN[us], board->matrix[tsq]);
                (current++)->setPromotionCapture(pc, ssq, tsq, KNIGHT[us], board->matrix[tsq]);
                (current++)->setPromotionCapture(pc, ssq, tsq, ROOK[us], board->matrix[tsq]);
                (current++)->setPromotionCapture(pc, ssq, tsq, BISHOP[us], board->matrix[tsq]);
            }
        }
    }
    //knight captures:
    TPiecePlacement * pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = KnightMoves[ssq] & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //bishop captures:
    pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicBishopMoves(ssq, occ) & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //rook captures:
    pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicRookMoves(ssq, occ) & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //queen captures:
    pp = &board->pieces[++pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        moves = MagicQueenMoves(ssq, occ) & targets;
        while (moves) {
            int tsq = POP(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //king captures:
    pc++;
    int kpos = *board->king_sq[us];
    moves = KingMoves[kpos] & board->all(them);
    while (moves) {
        int tsq = POP(moves);
        (current++)->setCapture(pc, kpos, tsq, board->matrix[tsq]);
    }

    list->last = current;
}

/**
 * Generate Promotions. The promotions are added to a movelist object.
 * @param board board structure object
 * @param list movelist object
 */
void genPromotions(TBoard * board, TMoveList * list) {
    TMove * current = list->last;
    list->current = current;
    bool us = board->stack->wtm;
    U64 pieces = *board->pawns[us] & RANK[us][7];
    if (pieces) {
        int pawn_up = PAWNDIRECTION[us];
        int pc = PAWN[us];
        do {
            int ssq = POP(pieces);
            int tsq = ssq + pawn_up;
            if (board->matrix[tsq] == EMPTY) {
                (current++)->setPromotion(pc, ssq, tsq, QUEEN[us]);
                (current++)->setPromotion(pc, ssq, tsq, KNIGHT[us]);
                (current++)->setPromotion(pc, ssq, tsq, ROOK[us]);
                (current++)->setPromotion(pc, ssq, tsq, BISHOP[us]);
            }
        } while (pieces);
    }
    list->last = current;
}

/**
 * Generate castling moves. The castling moves are added to a movelist
 * TODO: support chess960 
 * @param board board structure object
 * @param list movelist object
 */
void genCastles(TBoard * board, TMoveList * list) { 
    TMove * current = list->last;
    list->current = current;
    if (board->castleRight(CASTLE_ANY)) {
        if (board->stack->wtm) {
            if (board->castleRight(CASTLE_K)
                    && board->matrix[f1] == EMPTY
                    && board->matrix[g1] == EMPTY) {
                (current++)->setMove(WKING, e1, g1);
            }
            if (board->castleRight(CASTLE_Q)
                    && board->matrix[d1] == EMPTY
                    && board->matrix[c1] == EMPTY
                    && board->matrix[b1] == EMPTY) {
                (current++)->setMove(WKING, e1, c1);
            }
        } else {
            if (board->castleRight(CASTLE_k)
                    && board->matrix[f8] == EMPTY
                    && board->matrix[g8] == EMPTY) {
                (current++)->setMove(BKING, e8, g8);
            }
            if (board->castleRight(CASTLE_q)
                    && board->matrix[d8] == EMPTY
                    && board->matrix[c8] == EMPTY
                    && board->matrix[b8] == EMPTY) {
                (

                        current++)->setMove(BKING, e8, c8);
            }
        }
    }
    list->last = current;
}

/**
 * Generate quiet moves, e.g.no captures, promotions or castling moves. Add the
 * moves to a movelist.
 * @param board board structure object
 * @param list movelist object
 */
void genQuietMoves(TBoard * board, TMoveList * list) {
    U64 occ = board->all_pieces;
    U64 targets = ~occ;
    U64 moves;
    TMove * current = list->last;
    list->current = current;
    bool us = board->stack->wtm;
    int pc = PAWN[us];
    int pawn_up = PAWNDIRECTION[us];

    //pawn moves:
    assert((us == WHITE && pc == WPAWN) || (us == BLACK && pc == BPAWN));
    TPiecePlacement * pp = &board->pieces[pc];
    for (int i = 0; i < pp->count; i++) {
        int ssq = pp->squares[i];
        int tsq = ssq + pawn_up;
        if (board->matrix[tsq] != EMPTY || (BIT(ssq) & RANK[us][7])) {
            continue;
        }
        (current++)->setMove(pc, ssq, tsq);
        if (BIT(ssq) & RANK[us][2]) {
            tsq += pawn_up;
            if (board->matrix[tsq]) {
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
    int kpos = *board->king_sq[us];
    pc++;
    assert((us == WHITE && pc == WKING) || (us == BLACK && pc == BKING));
    moves = KingMoves[kpos] & targets;
    while (moves) {
        (

                current++)->setMove(pc, kpos, POP(moves));
    }
    list->last = current;
}

/**
 * Copy a movelist object from another movelist object
 * @param list the movelist object to copy
 */
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