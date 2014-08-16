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
 * File: movepicker.cpp
 * Move Picker class to use by the search. The move picker handles move 
 * generation in phases, typically following this order:
 * - Hash moves
 * - Killer Moves
 * - Captures
 * - Quiet Moves
 * All moves returned are legal. If the move picker does not return any move, 
 * it's a (stale)mate.
 */

#include "movepicker.h"
#include "search.h"

void move_picker_t::push(move::list_t * list, move_t * move, int score) {
    for (move_t * m = list->first; m != list->last; m++) {
        if (move->equals(m)) {
            m->score = score;
            return;
        }
    }
    move_t * current = list->last;
    (current++)->set(move);
    move->score = score;
    list->last = current;
}

move_t * move_picker_t::pop(board_t * brd, move::list_t * list) {
    if (list->first == list->last) {
        return NULL;
    }
    int edge = list->minimum_score;
    do {
        move_t * best = list->first;
        for (move_t * move = best + 1; move != list->last; move++) {
            if (move->score > best->score) {
                best = move;
            }
        }
        if (best->score < edge) {
            return NULL;
        }
        if (brd->legal(best)) {
            best->score = move::EXCLUDED;
            return best;
        }
        best->score = move::ILLEGAL;
    } while (true);
    return NULL;
}

move_t * move_picker_t::first(TSearch * s, int depth, int alpha, int beta) {
    move::list_t * list = &s->stack->moveList;
    list->clear();
    list->stage = depth < ONE_PLY ? CAPTURES : HASH;
    return next(s, depth, alpha, beta);
}

move_t * move_picker_t::next(TSearch * s, int depth, int alpha, int beta) {
    U64 mask;
    board_t * brd = s->pos;
    move::list_t * list = &s->stack->moveList;
    /*
     * 1. Pop the best move from the list. If a move is found, a just-in-time 
     * legality check is done and the movepicker returns a valid, legal, move.
     */
    move_t * result = pop(brd, list);
    if (result) {
        return result;
    }

    /*
     * 2. If no move was found, proceed to the next stage and get or generate
     * (some) moves.
     */
    switch (list->stage) {
        case HASH:
            result = &s->stack->tt_move;
            if (result->piece) {
                assert(brd->valid(result));
                assert(brd->legal(result));
                list->stage = MATEKILLER;
                list->last_excluded++->set(result);
                return result;
            }
        case MATEKILLER:
            result = &s->stack->mateKiller;
            if (result->piece
                    && !list->is_excluded(result)
                    && brd->valid(result)
                    && brd->legal(result)) {
                list->stage = CAPTURES;
                list->last_excluded++->set(result);
                return result;
            }
        case CAPTURES:
            mask = brd->bb[ALLPIECES];
            if (s->stack->in_check) {
                mask &= brd->stack->checkers;
            }
            move::gen_captures(brd, list, mask);
            if (list->current != list->last) {
                for (move_t * move = list->current; move != list->last; move++) {
                    if (list->is_excluded(move)) {
                        move->score = move::EXCLUDED;
                    } else {
                        move->score = depth > LOW_DEPTH ? brd->see(move) : brd->mvvlva(move);
                    }
                }
                result = pop(brd, list);
                if (result) {
                    list->stage = PROMOTIONS;
                    return result;
                }
            }
        case PROMOTIONS:
            move::gen_promotions(brd, list);
            if (list->current != list->last) {
                for (move_t * move = list->current; move != list->last; move++) {
                    if (list->is_excluded(move)) {
                        move->score = move::EXCLUDED;
                    } else if ((move->promotion == WQUEEN || move->promotion == BQUEEN)
                            && brd->see(move) >= 0) {
                        move->score = 800;
                    } else {
                        move->score = -move->piece;
                    }
                }
                result = pop(brd, list);
                if (result) {
                    list->stage = KILLER1;
                    return result;
                }
            }
        case KILLER1:
            if (depth >= ONE_PLY) {
                result = &s->stack->killer1;
                if (result->piece
                        && brd->valid(result)
                        && !list->is_excluded(result)
                        && brd->legal(result)) {
                    list->last_excluded++->set(result);
                    list->stage = KILLER2;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            }
        case KILLER2:
            if (depth >= ONE_PLY) {
                result = &s->stack->killer2;
                if (result->piece
                        && brd->valid(result)
                        && !list->is_excluded(result)
                        && brd->legal(result)) {
                    list->last_excluded++->set(result);
                    list->stage = MINORPROMOTIONS;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            }
        case MINORPROMOTIONS: //and captures with see < 0
            if (depth <= LOW_DEPTH) {
                list->minimum_score = -move::INF;
                result = pop(brd, list);
                if (result) {
                    list->stage = CASTLING;
                    return result;
                }
            }
        case CASTLING:
            if (s->stack->in_check == false) {
                move::gen_castles(brd, list);
                for (move_t * move = list->current; move != list->last; move++) {
                    if (list->is_excluded(move)) {
                        move->score = move::EXCLUDED;
                    } else {
                        move->score = 100;
                    }
                }
                result = pop(brd, list);
                if (result) {
                    list->stage = QUIET_MOVES;
                    return result;
                }

            }
        case QUIET_MOVES:
            if (depth >= 0 || s->stack->in_check) {
                list->minimum_score = -move::INF;
                move::gen_quiet_moves(brd, list);
                for (move_t * move = list->current; move != list->last; move++) {
                    if (list->is_excluded(move)) {
                        move->score = move::EXCLUDED;
                    } else {
                        move->score = s->history[move->piece][move->tsq];
                    }
                }
                list->stage = STOP;
                result = pop(brd, list);
                return result;
            }
        case STOP:
        default:
            return NULL;
    }
    return result;
}

int move_picker_t::count_evasions(TSearch * s, move_t * first_move) {
    assert(first_move != NULL);
    move::list_t * list = &s->stack->moveList;
    int result = 1;
    const short MAXLEGALCOUNT = 3;
    move_t * pushback[MAXLEGALCOUNT];

    //get and count legal moves
    while (result < MAXLEGALCOUNT) {
        move_t * m = next(s, 1, -score::INF, score::INF);
        if (m == NULL) {
            break;
        }
        pushback[result++] = m;
    }

    //push moves back on the list
    for (int i = first_move != NULL; i < result; i++) {
        push(list, pushback[i], score::INF - i);
    }
    return result;
}

namespace move {
    move_picker_t _picker;
    
    move_picker_t * picker() {
        return &_picker;
    }
    
    move_t * first(TSearch * s, int depth, int alpha, int beta) {
        return _picker.first(s, depth, alpha, beta);
    }
    
    move_t * next(TSearch * s, int depth, int alpha, int beta) {
        return _picker.next(s, depth, alpha, beta);
    }
};

