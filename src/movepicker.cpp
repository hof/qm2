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
 * - TT move
 * - Good noisy moves
 * - Killers and countermove
 * - Positive-history quiets
 * - Bad noisy moves
 * - Remaining quiets
 * Standard chess returns pseudo-legal candidates; callers own king safety.
 * No candidates does not by itself establish a legal terminal position.
 */

#include "movepicker.h"
#include "search.h"
#include <algorithm> 
#include <limits>

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
        if (list->stage == stop && s->wild == 0) {
            // The previously selected maximum bounds this unchanged quiet list.
            // Stop at its first occurrence to preserve swap-removal tie order.
            const int maximum_score = list->best->score;
            if (best->score != maximum_score) {
                for (move_t * move = list->first + 1; move != list->last; ++move) {
                    if (*move > *best) {
                        best = move;
                        if (best->score == maximum_score) {
                            break;
                        }
                    }
                }
            }
        } else {
            for (move_t * move = list->first + 1; move != list->last; move++) {
                if (*move > *best) {
                    best = move;
                }
            }
        }

        //return if move score is too low
        if (best->score < list->minimum_score) {
            return NULL;
        }

        //filter negative see if still in "good captures" phase
        if (list->minimum_score == 0 && best->capture) {
            const int see = s->brd.see(best);
            if (see < 0) {
                best->score = see;
                continue;
            }
        }

        //legality check just before returning the move
        move_t * result = list->best;
        if (result != best) {
            result->set(best);
        }
        if (best != --list->last) {
            best->set(list->last);
        }
        if (!s->stack->tt_move.equals(result)
                && !s->is_killer(result)
                && (s->wild != 17 || s->brd.legal(result))) {
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
    if (s->wild == 0 && s->evasions_enabled && s->stack->in_check) {
        list->evasion_targets = move::evasion_targets(s->brd);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ++s->nnue_audit.counters.evasion_nodes;
        s->nnue_audit.counters.double_check_nodes += list->evasion_targets == 0;
#endif
    }
    list->stage = hash_move;
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
    if (s->wild == 0) {
        return next_standard(s, depth);
    }

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
    board_t * brd = &s->brd;
    const bool do_quiets = depth >= 0 || s->stack->in_check;
    switch (list->stage) {
        case hash_move:
            result = &s->stack->tt_move;
            if (result->piece && brd->valid(result)
                    && (s->wild != 17 || brd->legal(result))) {
                list->stage = captures;
                return result;
            }
        case captures:
            move::gen_captures(brd, list);
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
                        list->stage = promotions;
                        return result;
                    }
                } else {
                    list->minimum_score = -move::INF;
                    result = pop(s, list);
                    if (result || (s->stack->tt_move.piece && s->stack->tt_move.capture)) {
                        list->stage = stop;
                        return result;
                    }
                }
            }
        case promotions:
            move::gen_promotions(brd, list);
            if (list->current != list->last) {
                for (move_t * move = list->current; move != list->last; move++) {
                    if (s->wild == 17) {
                        move->score = 10 - move->promotion;
                    } else if (depth <= 0 || brd->see(move, 0)) {
                        move->score = move->promotion;
                    } else {
                        move->score = -100 + move->promotion;
                    }
                }
                result = pop(s, list);
                if (result) {
                    list->stage = killer_1;
                    return result;
                }
            }
        case killer_1:
            if (do_quiets) {
                result = &s->stack->killer[0];
                if (result->piece
                        && !s->stack->tt_move.equals(result)
                        && brd->valid(result)
                        && (s->wild != 17 || brd->legal(result))) {
                    list->stage = killer_2;
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    return result;
                }
            }
        case killer_2:
            if (do_quiets) {
                result = &s->stack->killer[1];
                if (result->piece
                        && !s->stack->tt_move.equals(result)
                        && brd->valid(result)
                        && (s->wild != 17 || brd->legal(result))) {
                    assert(s->stack->killer[0].equals(result) == false);
                    assert(result->capture == EMPTY && result->promotion == EMPTY);
                    list->stage = minor_promotions;
                    return result;
                }
            }
        case minor_promotions: //and captures with see < 0
            list->minimum_score = -move::INF;
            result = pop(s, list);
            if (result) {
                list->stage = castling;
                return result;
            }
        case castling:
            if (s->stack->in_check == false) {
                move::gen_castles(brd, list);
                for (move_t * move = list->current; move != list->last; move++) {
                    move->score = 100;
                }
                result = pop(s, list);
                if (result) {
                    list->stage = quiet_moves;
                    return result;
                }
            }
        case quiet_moves:
            if (do_quiets) {
                list->minimum_score = -move::INF;
                move::gen_quiet_moves(brd, list);
                for (move_t * move = list->current; move != list->last; move++) {
                    move->score = s->history[move->piece][move->tsq];
                }
                list->best->score = std::numeric_limits<int>::max();
                list->stage = stop;
                result = pop(s, list);
                return result;
            }
        case stop:
        default:
            return NULL;
    }
    return result;
}

// Standard chess returns candidates; only search decides king safety.
move_t * move_picker_t::pop_standard(search_t * s, int phase) {
    auto * list = &s->stack->move_list;
    while (list->first != list->last) {
        move_t * best = nullptr;
        const int maximum = phase == bad_quiets ? list->best->score
            : std::numeric_limits<int>::max();
        for (auto * move = list->first; move != list->last; ++move) {
            const bool noisy = move->capture || move->promotion;
            const bool eligible = phase == promotions ? noisy && move->score >= 0
                : phase == good_quiets ? !noisy && move->score > 0
                : phase == bad_noisy ? noisy : !noisy;
            if (eligible && (!best || move->score > best->score)) {
                best = move;
                if (phase == bad_quiets && best->score == maximum) {
                    break;
                }
            }
        }
        if (!best) {
            return nullptr;
        }
        if (phase == promotions && !s->brd.see(best, 0)) {
            best->score = s->brd.see(best);
            continue;
        }
        *list->best = *best;
        *best = *--list->last;
        const auto code = list->best->to_compact();
        bool duplicate = false;
        for (auto hint : list->returned_hints) {
            duplicate |= hint == code;
        }
        if (!duplicate) {
            return list->best;
        }
    }
    return nullptr;
}

move_t * move_picker_t::next_standard(search_t * s, int depth) {
    auto * list = &s->stack->move_list;
    auto * board = &s->brd;
    const bool do_quiets = depth >= 0 || s->stack->in_check;
    for (;;) {
        move_t * result = nullptr;
        switch (list->stage) {
            case hash_move:
                list->stage = captures;
                result = &s->stack->tt_move;
                if (result->piece && board->valid(result)
                        && move::allows_evasion(*result, list->evasion_targets)) {
                    list->returned_hints[0] = result->to_compact();
                    return result;
                }
                break;
            case captures:
                move::gen_captures(board, list);
                move::gen_promotions(board, list);
                for (auto * move = list->first; move != list->last; ++move) {
                    move->score = board->mvvlva(move);
                }
                list->stage = promotions;
                break;
            case promotions:
                result = pop_standard(s, promotions);
                if (result) {
                    return result;
                }
                list->stage = killer_1;
                break;
            case killer_1:
            case killer_2: {
                const int index = list->stage == killer_1 ? 0 : 1;
                list->stage = index == 0 ? killer_2 : countermove;
                result = &s->stack->killer[index];
                if (s->killers_enabled && do_quiets && result->piece && !result->capture && !result->promotion
                        && !result->castle && board->valid(result)
                        && move::allows_evasion(*result, list->evasion_targets)
                        && result->to_compact() != list->returned_hints[0]
                        && (index == 0 || result->to_compact() != list->returned_hints[1])) {
                    list->returned_hints[index + 1] = result->to_compact();
                    return result;
                }
                break;
            }
            case countermove:
                list->stage = quiet_init;
                if (s->countermoves_enabled && do_quiets && board->ply > 0) {
                    const auto & previous = (s->stack - 1)->current_move;
                    if (previous.piece) {
                        list->counter_candidate.set_compact(board,
                            s->countermoves[previous.piece][previous.tsq]);
                        result = &list->counter_candidate;
                        if (result->piece && !result->capture && !result->promotion
                                && !result->castle && board->valid(result)
                                && move::allows_evasion(*result, list->evasion_targets)
                                && result->to_compact() != list->returned_hints[0]
                                && result->to_compact() != list->returned_hints[1]
                                && result->to_compact() != list->returned_hints[2]) {
                            list->returned_hints[3] = result->to_compact();
                            return result;
                        }
                    }
                }
                break;
            case quiet_init: {
                auto * begin = list->last;
                move::gen_castles(board, list);
                if (do_quiets) {
                    move::gen_quiet_moves(board, list);
                }
                for (auto * move = begin; move != list->last; ++move) {
                    move->score = s->quiet_score(*move);
                }
                list->stage = good_quiets;
                break;
            }
            case good_quiets:
                result = pop_standard(s, good_quiets);
                if (result) {
                    return result;
                }
                list->stage = bad_noisy;
                break;
            case bad_noisy:
                result = pop_standard(s, bad_noisy);
                if (result) {
                    return result;
                }
                // Only quiets remain. The preceding maximum bounds subsequent
                // selections, preserving the cheap all-equal-history path.
                list->best->score = std::numeric_limits<int>::max();
                list->stage = bad_quiets;
                break;
            case bad_quiets:
                result = pop_standard(s, bad_quiets);
                if (result) {
                    return result;
                }
                list->stage = stop;
                break;
            default:
                return nullptr;
        }
    }
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

