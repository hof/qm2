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
 * File: movegen.cpp
 * Move generators
 */

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
void genCaptures(board_t * board, TMoveList * list, U64 targets) {
    TMove * current = list->last;
    list->current = current;
    U64 moves;
    U64 occ = board->bb[ALLPIECES];
    bool us = board->stack->wtm;
    bool them = !us;
    int pc = PAWN[us];
    targets &= board->all(them);

    //pawn captures (including en-passant and promotion captures):
    U64 pawn_caps = targets;
    if (board->stack->enpassant_sq) {
        pawn_caps |= BIT(board->stack->enpassant_sq);
    }
    pawn_caps &= board->pawn_attacks(us);
    while (pawn_caps) {
        int tsq = pop(pawn_caps);
        moves = board->pawn_attacks(tsq, them) & board->bb[PAWN[us]];
        while (moves) {
            int ssq = pop(moves);
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
    U64 pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);
        moves = KNIGHT_MOVES[ssq] & targets;
        while (moves) {
            int tsq = pop(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //bishop captures:
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = magic::bishop_moves(ssq, occ) & targets;
        while (moves) {
            int tsq = pop(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //rook captures:
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = magic::rook_moves(ssq, occ) & targets;
        while (moves) {
            int tsq = pop(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //queen captures:
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = magic::queen_moves(ssq, occ) & targets;
        while (moves) {
            int tsq = pop(moves);
            (current++)->setCapture(pc, ssq, tsq, board->matrix[tsq]);
        }
    }
    //king captures:
    int kpos = board->get_sq(++pc);
    moves = KING_MOVES[kpos] & board->all(them);
    while (moves) {
        int tsq = pop(moves);
        (current++)->setCapture(pc, kpos, tsq, board->matrix[tsq]);
    }

    list->last = current;
}

/**
 * Generate Promotions. The promotions are added to a movelist object.
 * @param board board structure object
 * @param list movelist object
 */
void genPromotions(board_t * board, TMoveList * list) {
    TMove * current = list->last;
    list->current = current;
    bool us = board->stack->wtm;
    U64 pieces = board->bb[PAWN[us]] & RANK[us][7];
    if (pieces) {
        int pawn_up = PAWNDIRECTION[us];
        int pc = PAWN[us];
        do {
            int ssq = pop(pieces);
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
void genCastles(board_t * board, TMoveList * list) { 
    TMove * current = list->last;
    list->current = current;
    if (board->has_castle_right(CASTLE_ANY)) {
        if (board->stack->wtm) {
            if (board->has_castle_right(CASTLE_K)
                    && board->matrix[f1] == EMPTY
                    && board->matrix[g1] == EMPTY) {
                (current++)->setMove(WKING, e1, g1);
            }
            if (board->has_castle_right(CASTLE_Q)
                    && board->matrix[d1] == EMPTY
                    && board->matrix[c1] == EMPTY
                    && board->matrix[b1] == EMPTY) {
                (current++)->setMove(WKING, e1, c1);
            }
        } else {
            if (board->has_castle_right(CASTLE_k)
                    && board->matrix[f8] == EMPTY
                    && board->matrix[g8] == EMPTY) {
                (current++)->setMove(BKING, e8, g8);
            }
            if (board->has_castle_right(CASTLE_q)
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
void genQuietMoves(board_t * board, TMoveList * list) {
    U64 occ = board->bb[ALLPIECES];
    U64 targets = ~occ;
    U64 moves;
    TMove * current = list->last;
    list->current = current;
    bool us = board->stack->wtm;
    int pc = PAWN[us];
    int pawn_up = PAWNDIRECTION[us];

    //pawn moves:
    U64 pieces = board->bb[pc];
    while (pieces) {
        int ssq = pop(pieces);
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
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = KNIGHT_MOVES[ssq] & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, pop(moves));
        }
    }
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = magic::bishop_moves(ssq, occ) & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, pop(moves));
        }
    }
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = magic::rook_moves(ssq, occ) & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, pop(moves));
        }
    }
    pieces = board->bb[++pc];
    while (pieces) {
        int ssq = pop(pieces);//pp->squares[i];
        moves = magic::queen_moves(ssq, occ) & targets;
        while (moves) {
            (current++)->setMove(pc, ssq, pop(moves));
        }
    }
    //king moves:
    int kpos = board->get_sq(++pc);
    moves = KING_MOVES[kpos] & targets;
    while (moves) {
        (current++)->setMove(pc, kpos, pop(moves));
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