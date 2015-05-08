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
 * File: movegen.cpp
 * Move generators
 */

#include "movegen.h"

namespace move {

    /**
     * Move list constructor
     * @return 
     */
    list_t::list_t() {
        clear();
    }

    /**
     * Clears move list
     */
    void list_t::clear() {
        stage = 0;
        minimum_score = 0;
        current = first = last = &_list[0];
        best = &_list[MAX_MOVES];
    }

    /**
     * Generate Captures. The captures are added to a movelist object.
     * @param board board structure object
     * @param list movelist object
     * @param targets the pieces to capture
     */
    void gen_captures(board_t * board, move::list_t * list) {
        move_t * current = list->last;
        list->current = current;
        const bool us = board->us();
        const bool them = !us;
        const U64 occ = board->bb[ALLPIECES];
        const U64 targets = board->all(them);
        U64 moves;
        int ssq, tsq;
        int pc = PAWN[us];

        //pawn captures (including en-passant and promotion captures):
        U64 pawn_caps = targets;
        if (board->stack->enpassant_sq) {
            pawn_caps |= BIT(board->stack->enpassant_sq);
        }
        pawn_caps &= board->pawn_attacks(us);
        while (pawn_caps) {
            tsq = pop(pawn_caps);
            moves = PAWN_CAPTURES[them][tsq] & board->bb[pc];
            while (moves) {
                ssq = pop(moves);
                if (tsq == board->stack->enpassant_sq && tsq > 0) {
                    (current++)->set(pc, ssq, tsq, PAWN[them]);
                } else if ((BIT(tsq) & RANK[us][8]) == 0) {
                    (current++)->set(pc, ssq, tsq, board->matrix[tsq]);
                } else {
                    (current++)->set(pc, ssq, tsq, board->matrix[tsq], QUEEN[us]);
                    (current++)->set(pc, ssq, tsq, board->matrix[tsq], KNIGHT[us]);
                    (current++)->set(pc, ssq, tsq, board->matrix[tsq], ROOK[us]);
                    (current++)->set(pc, ssq, tsq, board->matrix[tsq], BISHOP[us]);
                }
            }
        }

        //knight captures:
        U64 pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces);
            moves = KNIGHT_MOVES[ssq] & targets;
            while (moves) {
                tsq = pop(moves);
                (current++)->set(pc, ssq, tsq, board->matrix[tsq]);
            }
        }

        //bishop captures:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces); //pp->squares[i];
            moves = magic::bishop_moves(ssq, occ) & targets;
            while (moves) {
                tsq = pop(moves);
                (current++)->set(pc, ssq, tsq, board->matrix[tsq]);
            }
        }

        //rook captures:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces); //pp->squares[i];
            moves = magic::rook_moves(ssq, occ) & targets;
            while (moves) {
                tsq = pop(moves);
                (current++)->set(pc, ssq, tsq, board->matrix[tsq]);
            }
        }

        //queen captures:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces); //pp->squares[i];
            moves = magic::queen_moves(ssq, occ) & targets;
            while (moves) {
                tsq = pop(moves);
                (current++)->set(pc, ssq, tsq, board->matrix[tsq]);
            }
        }

        //king captures:
        int kpos = board->get_sq(++pc);
        moves = KING_MOVES[kpos] & targets;
        while (moves) {
            tsq = pop(moves);
            (current++)->set(pc, kpos, tsq, board->matrix[tsq]);
        }

        list->last = current;
    }

    /**
     * Generate Promotions. The promotions are added to a movelist object.
     * @param board board structure object
     * @param list movelist object
     */
    void gen_promotions(board_t * board, move::list_t * list) {
        move_t * current = list->last;
        list->current = current;
        const bool us = board->us();
        const int pc = PAWN[us];
        U64 pieces = board->bb[pc] & RANK[us][7];
        while (pieces) {
            int ssq = pop(pieces);
            int tsq = ssq + PAWN_DIRECTION[us];
            if (board->matrix[tsq] == EMPTY) {
                (current++)->set(pc, ssq, tsq, 0, QUEEN[us]);
                (current++)->set(pc, ssq, tsq, 0, KNIGHT[us]);
                (current++)->set(pc, ssq, tsq, 0, ROOK[us]);
                (current++)->set(pc, ssq, tsq, 0, BISHOP[us]);
            }
        }
        list->last = current;
    }

    /**
     * Generate castling moves. The castling moves are added to a movelist
     * TODO: support chess960 
     * @param board board structure object
     * @param list movelist object
     */
    void gen_castles(board_t * board, move::list_t * list) {
        move_t * current = list->last;
        list->current = current;
        if (board->has_castle_right(CASTLE_ANY)) {
            if (board->us()) {
                if (board->has_castle_right(CASTLE_K)
                        && board->matrix[f1] == EMPTY
                        && board->matrix[g1] == EMPTY) {
                    (current++)->set(WKING, e1, g1);
                }
                if (board->has_castle_right(CASTLE_Q)
                        && board->matrix[d1] == EMPTY
                        && board->matrix[c1] == EMPTY
                        && board->matrix[b1] == EMPTY) {
                    (current++)->set(WKING, e1, c1);
                }
            } else {
                if (board->has_castle_right(CASTLE_k)
                        && board->matrix[f8] == EMPTY
                        && board->matrix[g8] == EMPTY) {
                    (current++)->set(BKING, e8, g8);
                }
                if (board->has_castle_right(CASTLE_q)
                        && board->matrix[d8] == EMPTY
                        && board->matrix[c8] == EMPTY
                        && board->matrix[b8] == EMPTY) {
                    (current++)->set(BKING, e8, c8);
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
    void gen_quiet_moves(board_t * board, move::list_t * list) {
        move_t * current = list->last;
        list->current = current;
        U64 moves;
        const U64 occ = board->bb[ALLPIECES];
        const U64 targets = ~occ;
        const bool us = board->us();
        const int pawn_up = PAWN_DIRECTION[us];
        int pc = PAWN[us];
        int ssq;
        
        //pawn moves:
        U64 pieces = board->bb[pc] & ~RANK[us][7];
        while (pieces) {
            ssq = pop(pieces);
            int tsq = ssq + pawn_up;
            if (board->matrix[tsq] != EMPTY) {
                continue;
            }
            (current++)->set(pc, ssq, tsq);
            if (BIT(ssq) & RANK[us][2]) {
                tsq += pawn_up;
                if (board->matrix[tsq] == EMPTY) {
                    (current++)->set(pc, ssq, tsq);
                }
            }
        }
        
        //knight moves:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces); 
            moves = KNIGHT_MOVES[ssq] & targets;
            while (moves) {
                (current++)->set(pc, ssq, pop(moves));
            }
        }
        
        //bishop moves:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces); 
            moves = magic::bishop_moves(ssq, occ) & targets;
            while (moves) {
                (current++)->set(pc, ssq, pop(moves));
            }
        }
        
        //rook moves:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces);
            moves = magic::rook_moves(ssq, occ) & targets;
            while (moves) {
                (current++)->set(pc, ssq, pop(moves));
            }
        }
        
        //queen moves:
        pieces = board->bb[++pc];
        while (pieces) {
            ssq = pop(pieces); 
            moves = magic::queen_moves(ssq, occ) & targets;
            while (moves) {
                (current++)->set(pc, ssq, pop(moves));
            }
        }
        
        //king moves:
        int kpos = board->get_sq(++pc);
        moves = KING_MOVES[kpos] & targets;
        while (moves) {
            (current++)->set(pc, kpos, pop(moves));
        }
        
        list->last = current;
    }
}