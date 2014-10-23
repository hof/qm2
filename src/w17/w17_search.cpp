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
 * File: w17_search.cpp
 * Search methods for wild 17 (losers) chess.  
 */

#include "w17_search.h"

int w17_evaluate(w17_search_t * s) {
    return 0;
}

/**
 * Verifies is a position is drawn by 
 * a) fifty quiet moves
 * b) repetition
 * @return true if it's an official draw
 */
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

/**
 * Principle Variation Search (fail-soft)
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth remaining search depth
 * @return score for the current node
 */
int w17_search_t::pvs(int alpha, int beta, int depth) {
    bool us = brd.stack->wtm;
    return w17_pvs(alpha, beta, popcnt(brd.all(!us)), popcnt(brd.all(us)), depth);
}

int w17_search_t::w17_pvs(int alpha, int beta, int max_quiets_us, int max_quiets_them, int depth) {
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
     * Moves loop
     */

    //if no first move, it's checkmate or stalemate so we won
    stack->tt_move.clear();
    move_t * move = move::first(this, 0);
    if (!move) {
        return score::MATE - brd.ply;
    }
    
    //quiet position
    const bool quiet_pos = !stack->in_check && move->capture == 0;
    
    if (quiet_pos) {
        const int eval = w17_evaluate(this);
        const int delta = depth <= 0? 0 : depth * 50;
        if (depth < 8 && max_quiets_us <= 0) {
            //reached quiets limit
            return eval;
        } else if (depth < 8 && eval - delta >= beta) {
            //stand pat
            return eval;
        } else if (depth < 4) {
            //return on a quiet position nearby horizon
            return eval;
        } 
        max_quiets_them = 0;
    }
    
    //horizon in unquiet positions: stop but return "uncertain" score
    if (depth <= 0) {
        return us == WHITE? -2000 : 2000;
    } 
    
    //stop mate search returning an "uncertain" score.
    if (depth < popcnt(brd.bb[ALLPIECES]) && max_quiets_us <= 0 && max_quiets_them <= 0) {
        //mate search horizon
        return us == WHITE? -1800 : 1800;
    }
    
    

    //prepare and do the loop
    int best = -score::INF;
    stack->best_move.clear();
    int searched_moves = 0;
    int score_max = score::MATE - brd.ply - 1;
    bool pv = alpha + 1 < beta;
    do {
        assert(brd.valid(move) && brd.legal(move));
        assert(stack->best_move.equals(move) == false);

        /*
         * Go forward and search next node
         */

        int gives_check = brd.gives_check(move);
        forward(move, gives_check);
        int score;
        if (searched_moves == 0) {
            score = -w17_pvs(-beta, -alpha, max_quiets_them, max_quiets_us - 1, depth - 1);
        } else {
            score = -w17_pvs(-alpha - 1, -alpha, max_quiets_them, max_quiets_us - 1, depth - 1);
            if (pv && score > alpha) {
                //full window research
                score = -w17_pvs(-beta, -alpha, max_quiets_them, max_quiets_us - 1, depth - 1);
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
    return best;
}