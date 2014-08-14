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
#include "move.h"

void TMovePicker::push(TSearch * searchData, move_t * move, int score) {
    move::list_t * list = &searchData->stack->moveList;
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

inline move_t * TMovePicker::popBest(board_t * pos, move::list_t * list) {
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
        if (pos->legal(best)) {
            best->score = move::EXCLUDED;
            return best;
        }
        best->score = move::ILLEGAL;
    } while (true);
    return NULL;
}

move_t * TMovePicker::pickFirstMove(TSearch * searchData, int depth, int alpha, int beta) {
    move::list_t * moveList = &searchData->stack->moveList;
    moveList->clear();
    moveList->stage = depth < ONE_PLY ? CAPTURES : MATEKILLER;
    searchData->stack->captureMask = searchData->pos->bb[ALLPIECES];
    return pickNextMove(searchData, depth, alpha, beta);
}

move_t * TMovePicker::pickNextMove(TSearch * searchData, int depth, int alpha, int beta) {
    U64 mask;
    move::list_t * moveList = &searchData->stack->moveList;
    board_t * pos = searchData->pos;

    /*
     * 1. If there are moves present in the moveList, this means
     * they are already scored by the Movepicker. They are also valid (semilegal) 
     * and we can return the best one. 
     * A last-minute legality check is performed so the movepicker 
     * always returns fully legal moves.
     */
    move_t * result = popBest(pos, moveList);

    /*
     * 2. Proceed to the next stage if no move was found
     */
    if (!result) {
        switch (moveList->stage) {
            case MATEKILLER:
                result = &searchData->stack->mateKiller;
                if (result->piece
                        && !moveList->is_excluded(result)
                        && pos->valid(result)
                        && pos->legal(result)) {
                    moveList->stage = CAPTURES;
                    moveList->last_excluded++->set(result);
                    return result;
                }
            case CAPTURES:
                mask = pos->bb[ALLPIECES];
                if (searchData->stack->in_check) {
                    mask &= pos->stack->checkers;
                }
                move::gen_captures(pos, moveList, mask);
                if (moveList->current != moveList->last) {
                    for (move_t * move = moveList->current; move != moveList->last; move++) {
                        if (moveList->is_excluded(move)) {
                            move->score = move::EXCLUDED;
                        } else {
                            move->score = depth > LOW_DEPTH ? pos->see(move) : MVVLVA(move);
                        }
                    }
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = PROMOTIONS;
                        return result;
                    }
                }
            case PROMOTIONS:
                move::gen_promotions(pos, moveList);
                if (moveList->current != moveList->last) {
                    for (move_t * move = moveList->current; move != moveList->last; move++) {
                        if (moveList->is_excluded(move)) {
                            move->score = move::EXCLUDED;
                        } else if ((move->promotion == WQUEEN || move->promotion == BQUEEN)
                                && pos->see(move) >= 0) {
                            move->score = 800;
                        } else {
                            move->score = -move->piece;
                        }
                    }
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = KILLER1;
                        return result;
                    }
                }
            case KILLER1:
                if (depth >= ONE_PLY) {
                    result = &searchData->stack->killer1;
                    if (result->piece
                            && pos->valid(result)
                            && !moveList->is_excluded(result)
                            && pos->legal(result)) {
                        moveList->last_excluded++->set(result);
                        moveList->stage = KILLER2;
                        assert(result->capture == EMPTY && result->promotion == EMPTY);
                        return result;
                    }
                }
            case KILLER2:
                if (depth >= ONE_PLY) {
                    result = &searchData->stack->killer2;
                    if (result->piece
                            && pos->valid(result)
                            && !moveList->is_excluded(result)
                            && pos->legal(result)) {
                        moveList->last_excluded++->set(result);
                        moveList->stage = MINORPROMOTIONS;
                        assert(result->capture == EMPTY && result->promotion == EMPTY);
                        return result;
                    }
                }
            case MINORPROMOTIONS: //and captures with see < 0
                if (depth <= LOW_DEPTH) {
                    moveList->minimum_score = -move::INF;
                    result = popBest(pos, moveList);
                    if (result) {
                        moveList->stage = CASTLING;
                        return result;
                    }
                }
            case CASTLING:
                if (searchData->stack->in_check == false) {
                    move::gen_castles(pos, moveList);
                    if (moveList->current != moveList->last) {
                        for (move_t * move = moveList->current; move != moveList->last; move++) {
                            if (moveList->is_excluded(move)) {
                                move->score = move::EXCLUDED;
                            } else {
                                move->score = 100;
                            }
                        }
                        result = popBest(pos, moveList);
                        if (result) {
                            moveList->stage = QUIET_MOVES;
                            return result;
                        }
                    }
                }
            case QUIET_MOVES:
                if (depth >= 0 || searchData->stack->in_check) {
                    moveList->minimum_score = -move::INF;
                    move::gen_quiet_moves(pos, moveList);
                    for (move_t * move = moveList->current; move != moveList->last; move++) {
                        if (moveList->is_excluded(move)) {
                            move->score = move::EXCLUDED;
                        } else {
                            move->score = searchData->history[move->piece][move->tsq];
                        }
                    }
                    moveList->stage = STOP;
                    result = popBest(pos, moveList);
                    return result;
                }
            case STOP:
            default:
                return NULL;
        }
    }
    return result;
}

short TMovePicker::countEvasions(TSearch * sd, move_t * firstMove) {
    assert(firstMove != NULL);
    int result = 1;
    const short MAXLEGALCOUNT = 3;
    move_t * pushback[MAXLEGALCOUNT];

    //get and count legal moves
    while (result < MAXLEGALCOUNT) {
        move_t * m = pickNextMove(sd, 1, -score::INF, score::INF);
        if (m == NULL) {
            break;
        }
        pushback[result++] = m;
    }

    //push moves back on the list
    for (int i = firstMove != NULL; i < result; i++) {
        push(sd, pushback[i], score::INF - i);
    }

    return result;
}

