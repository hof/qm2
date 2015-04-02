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
 * File: w17_search.cpp
 * Search methods for wild 17 (losers) chess.  
 */

#include "w17_search.h"
#include "w17_eval.h"
#include "uci_console.h"

/**
 * Verifies is a position is drawn by 
 * a) fifty quiet moves
 * b) repetition
 * @return true if it's an official draw
 */

namespace {
    const bool DO_EXTEND = false;
}

bool w17_search_t::w17_is_draw() {
    if (brd.stack->fifty_count > 3) {
        if (brd.stack->fifty_count >= (100 + stack->in_check)) {
            return true; //50 reversible moves
        }
        int stop_ply = brd.ply - brd.stack->fifty_count;
        for (int ply = brd.ply - 4; ply >= stop_ply; ply -= 2) { //draw by repetition
            if (ply >= 0 && get_stack(ply)->tt_key == brd.stack->tt_key) {
                return true;
            } else if (ply < 0 && rep_table::retrieve(root.fifty_count + ply) == brd.stack->tt_key) {
                return true;
            }
        }
    }
    return false;
}

int w17_search_t::pvs(int alpha, int beta, int depth) {
    return w17_pvs(alpha, beta, -1, depth);
}

int w17_search_t::w17_pvs(int alpha, int beta, int in_mate_search, int depth) {

    nodes++;
    stack->pv_count = 0;

    //win by having no more pieces
    bool us = brd.stack->wtm;
    if (max_1(brd.all(us))) {
        return score::MATE - brd.ply;
    }

    //loss by opponent having no more pieces
    bool them = !us;
    if (max_1(brd.all(them))) {
        return -score::MATE + brd.ply;
    }

    //draw by repetition or fifty quiet moves
    if (w17_is_draw()) {
        return draw_score();
    }

    /*
     * Stop conditions
     */

    //time 
    if (abort(false)) {
        return alpha;
    }

    //ceiling
    if (brd.ply >= sel_depth) {
        sel_depth = brd.ply;
        if (brd.ply >= MAX_PLY) {
            return w17_evaluate(this);
        }
    }

    //mate distance pruning: if mate(d) in n don't search deeper
    if ((score::MATE - brd.ply) < beta) {
        beta = score::MATE - brd.ply;
        if (alpha >= beta) {
            return beta;
        }
    }
    if ((-score::MATE + brd.ply) > alpha) {
        alpha = -score::MATE + brd.ply;
        if (beta <= alpha) {
            return alpha;
        }
    }

    /*
     * Transposition table lookup
     */

    stack->tt_key = brd.stack->tt_key; //needed for testing repetitions
    int tt_move = 0, tt_flag = 0, tt_score;
    if (depth > 0 && trans_table::retrieve(stack->tt_key, brd.ply, depth, tt_score, tt_move, tt_flag)) {
        if ((tt_flag == score::LOWERBOUND && tt_score >= beta)
                || (tt_flag == score::UPPERBOUND && tt_score <= alpha)
                || tt_flag == score::EXACT) {
            return tt_score;
        }
    }

    /*
     * First move
     * - if there is no first move, it's checkmate or stalemate and both cases
     * are a win in losers chess
     */

    stack->tt_move.set(tt_move);
    move_t * move = move::first(this, 0);
    if (!move) {
        return score::MATE - brd.ply;
    }

    /*
     * Node pruning
     * - special attention to those rare quiet positions: either stop the search 
     *   altogether (horizon) or at least stop searching for a forced mate sequence 
     *   by the opponent
     * - if the horizon position is not quiet, return an "uncertain score"
     */

    const bool quiet_pos = move->capture == 0 && stack->in_check == 0;
    const bool pv = alpha + 1 < beta;
    if (quiet_pos) {
        if (depth <= 0) {
            if (in_mate_search == (int)us) {
                //unknown if we can finish the forced mating sequence
                return 1000 + brd.ply;
            }
            return w17_evaluate(this);
        } else if (in_mate_search == (int)them) {
            return w17_evaluate(this);
        } else if (in_mate_search == -1 && depth < brd.max_mate_depth_us()) {
            in_mate_search = us;
        } else if (in_mate_search == -1) {
            depth -= 1;
        }
    }

    /*
     * Moves loop
     */

    int best = -score::INF;
    stack->best_move.clear();
    int searched_moves = 0;
    int score_max = score::MATE - brd.ply - 1;
    do {
        assert(brd.valid(move) && brd.legal(move));
        assert(stack->best_move.equals(move) == false);

        /*
         * Go forward and search next node
         */

        int gives_check = brd.gives_check(move);
        bool extend = DO_EXTEND && pv && move->capture > 0 && in_mate_search != -1;
        forward(move, gives_check);
        int score;
        if (searched_moves == 0) {
            score = -w17_pvs(-beta, -alpha, in_mate_search, depth - 1 + extend);
        } else {
            score = -w17_pvs(-alpha - 1, -alpha, in_mate_search, depth - 1 + extend);
            if (pv && score > alpha) {
                //full window research
                score = -w17_pvs(-beta, -alpha, in_mate_search, depth - 1 + extend);
            }
        }
        backward(move);

        /*
         * Handle results: update the best value / do a beta cutoff
         */

        if (stop_all) {
            return alpha;
        } else if (score > best) {
            stack->best_move.set(move);
            if (score >= beta) {

                if (depth > 0) {
                    trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, score, move->to_int(), score::LOWERBOUND);
                }

                if (depth > 0 && !move->capture && !move->promotion) {
                    update_killers(move, score);
                    update_history(move, depth);
                    for (move_t * cur = stack->move_list.first;
                            searched_moves && cur != stack->move_list.last; cur++) {
                        if (cur->score == move::EXCLUDED && cur != move) {
                            update_history(cur, -depth);
                        }
                    }
                }
                return score;
            }
            best = score;
            if (best > alpha) {
                update_pv(move);
                alpha = best;
            }
            if (best >= score_max) {
                break;
            }
        }
        searched_moves++;
    } while ((move = move::next(this, 0)));

    /*
     * Finalize
     */

    assert(best > -score::INF && best < beta);
    assert(!stop_all);
    assert(brd.valid(&stack->best_move) && brd.legal(&stack->best_move));

    if (depth > 0) {
        int flag = score::flags(best, alpha, beta); //obs: not using "old-alpha" to keep long pv
        trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, best, stack->best_move.to_int(), flag);
    }

    return best;
}
