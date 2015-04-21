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
#include <algorithm> 

/**
 * Pops the move with highest score above a minimum from the move list
 * @param s search object
 * @param list move list
 * @return best move on the list or null
 */
move_t * move_picker_t::pop(search_t * s, move::list_t * list) {
    while (list->first != list->last) {
        
        //pick move with the highest score
        
        move_t * best = list->first;
        for (move_t * move = list->first+1; move != list->last; move++) {
            if (*move > *best) {
                best = move;
            }
        }
        
        //return if move score is too low
        if (best->score < list->minimum_score) {
            return NULL;
        }

        //filter negative see if still in "good captures" phase
        if (list->minimum_score == 0 && best->capture && s->brd.min_gain(best) < 0) {
            assert(s->wild != 17);
            int see = s->brd.see(best);
            if (see < 0) {
                best->score = see;
                continue;
            }
        }

        //legality check just before returning the move
        move_t * result = list->best;
        result->set(best);
        best->set(--list->last);
        if (!s->stack->tt_move.equals(result)
                && !s->is_killer(result)
                && s->brd.legal(result)) {
            return result;
        }

    };
    return NULL;
}

/**
 * Gets the first move from the list, by clearing the search list and calling
 * "next"
 * @param s search object
 * @param depth remaining search depth
 * @return first move or null
 */
move_t * move_picker_t::first(search_t * s, int depth) {
    move::list_t * list = &s->stack->move_list;
    list->clear();
    list->stage = HASH;
    return next(s, depth);
}

/**
 * Gets the next available move. Generates moves "on the fly" depending on 
 * the current stage
 * @param s search object
 * @param depth remaining search depth
 * @return 
 */
move_t * move_picker_t::next(search_t * s, int depth) {

    /*
     * 1. Pop the best move from the list. If a move is found, a just-in-time 
     * legality check is done and the movepicker returns a valid, legal, move.
     */
    move::list_t * list = &s->stack->move_list;
    move_t * result = pop(s, list);
    if (result) {
        return result;
    }

    /*
     * 2. If no move was found, proceed to the next stage and get or generate
     * (some) moves.
     */
    U64 mask;
    board_t * brd = &s->brd;
    bool do_quiets = depth >= 0 || s->stack->in_check;
    switch (list->stage) {
        case HASH:
            result = &s->stack->tt_move;
            if (result->piece) {
                assert(brd->valid(result));
                assert(brd->legal(result));
                list->stage = CAPTURES;
                return result;
            }
        case CAPTURES:
            mask = brd->bb[ALLPIECES];
            move::gen_captures(brd, list, mask);
            if (list->current != list->last) {
                for (move_t * move = list->current; move != list->last; move++) {
                    if (s->wild == 17) {
                        move->score = s->brd.is_attacked(move->tsq, move->capture <= WKING);
                    } else {
                        move->score = brd->mvvlva(move);
                    }
                }
                if (s->wild != 17) {
                    result = pop(s, list);
                    if (result) {
                        list->stage = PROMOTIONS;
                        return result;
                    }
                } else {
                    list->minimum_score = -move::INF;
                    result = pop(s, list);
                    if (result || (s->stack->tt_move.piece && s->stack->tt_move.capture)) {
                        list->stage = STOP;
                        return result;
                    }
                }
            }
        case PROMOTIONS:
            move::gen_promotions(brd, list);
            if (list->current != list->last) {
                for (move_t * move = list->current; move != list->last; move++) {
                    if (s->wild == 17) {
                        move->score = 10 - move->promotion;
                    } else if (depth <= 0 || brd->see(move) >= 0) {
                        move->score = move->promotion;
                    } else {
                        move->score = -100 + move->promotion;
                    }
                }
                result = pop(s, list);
                if (result) {
                    list->stage = MATEKILLER;
                    return result;
                }
            }
        case MATEKILLER:
            if (do_quiets) {
                result = &s->stack->killer[0];
                if (result->piece
                        && !s->stack->tt_move.equals(result)
                        && brd->valid(result)
                        && brd->legal(result)) {
                    list->stage = KILLER1;
                    return result;
                }
            }
        case KILLER1:
            if (do_quiets) {
                result = &s->stack->killer[1];
                if (result->piece
                        && !s->stack->tt_move.equals(result)
                        && !s->stack->killer[0].equals(result)
                        && brd->valid(result)
                        && brd->legal(result)) {
                    list->stage = KILLER2;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            }
        case KILLER2:
            if (do_quiets) {
                result = &s->stack->killer[2];
                if (result->piece
                        && !s->stack->tt_move.equals(result)
                        && !s->stack->killer[0].equals(result)
                        && !s->stack->killer[1].equals(result)
                        && brd->valid(result)
                        && brd->legal(result)) {
                    list->stage = MINORPROMOTIONS;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            }
        case MINORPROMOTIONS: //and captures with see < 0
            list->minimum_score = -move::INF;
            result = pop(s, list);
            if (result) {
                list->stage = CASTLING;
                return result;
            }
        case CASTLING:
            if (s->stack->in_check == false) {
                move::gen_castles(brd, list);
                for (move_t * move = list->current; move != list->last; move++) {
                    move->score = 100;
                }
                result = pop(s, list);
                if (result) {
                    list->stage = QUIET_MOVES;
                    return result;
                }
            }
        case QUIET_MOVES:
            if (do_quiets) {
                list->minimum_score = -move::INF;
                move::gen_quiet_moves(brd, list);
                for (move_t * move = list->current; move != list->last; move++) {
                    move->score = s->history[move->piece][move->tsq];
                }
                list->stage = STOP;
                result = pop(s, list);
                return result;
            }
        case STOP:
        default:
            return NULL;
    }
    return result;
}

namespace move {
    move_picker_t _picker;

    move_picker_t * picker() {
        return &_picker;
    }

    move_t * first(search_t * s, int depth) {
        return _picker.first(s, depth);
    }

    move_t * next(search_t * s, int depth) {
        return _picker.next(s, depth);
    }
};

