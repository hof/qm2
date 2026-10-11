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
#include <limits>
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
    const bool fifty_move = brd.stack->fifty_count >= 100;
    const bool repetition = brd.stack->fifty_count >= 4 && is_repetition();
    if (fifty_move || repetition) {
        // Losing all non-king pieces, checkmate, and stalemate win before a draw.
        if (max_1(brd.all(brd.us())) || max_1(brd.all(brd.them()))) return false;
        move::list_t moves;
        move::gen_captures(&brd, &moves);
        move::gen_promotions(&brd, &moves);
        move::gen_castles(&brd, &moves);
        move::gen_quiet_moves(&brd, &moves);
        for (move_t * move = moves.first; move != moves.last; ++move) {
            if (!brd.legal(move)) continue;
            if (fifty_move) stack->tt_clocks = {100, 101};
            else stack->tt_clocks.max = MIN(stack->tt_clocks.max, 99);
            return true;
        }
        return false;
    }
    stack->tt_clocks.max = MIN(stack->tt_clocks.max, 99);
    return false;
}

U64 w17_search_t::w17_tt_context(int in_mate_search) const {
    assert(in_mate_search >= -1 && in_mate_search <= 1);
    constexpr U64 mode_salts[] = {
        0x243f6a8885a308d3ULL, 0x13198a2e03707344ULL, 0xa4093822299f31d0ULL,
    };
    return tt_context() ^ mode_salts[in_mate_search + 1];
}

int w17_search_t::pvs(int alpha, int beta, int depth) {
    return w17_pvs(alpha, beta, -1, depth);
}

int w17_search_t::w17_pvs(int alpha, int beta, int in_mate_search, int depth) {

    nodes++;
    stack->tt_clocks = {0, 101};
    stack->pv_count = 0;

    //win by having no more pieces
    const bool us = brd.us();
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
    }
    if (brd.ply >= MAX_PLY) {
        return w17_evaluate(this);
    }

    //mate distance pruning: if mate(d) in n don't search deeper
    if ((score::MATE - brd.ply) < beta) {
        beta = score::MATE - brd.ply;
        if (alpha >= beta) {
            return beta;
        }
    }
    
    int alpha1 = alpha;
    if ((-score::MATE + brd.ply) > alpha) {
        alpha = -score::MATE + brd.ply;
        if (beta <= alpha) {
            return alpha;
        }
    }

    /*
     * Transposition table lookup
     */

    stack->tt_key = brd.stack->tt_key;
    // Cache the incoming horizon semantics, before a quiet node changes mode
    // or reduces depth. Descendants compute their own context from that mode.
    const U64 context = w17_tt_context(in_mate_search);
    clock_range_t tt_clocks;
    int tt_move = 0, tt_flag = 0, tt_score;
    if (depth > 0 && trans_table::retrieve(stack->tt_key, brd.ply, depth, tt_score, tt_move, tt_flag, context,
            brd.stack->fifty_count, &tt_clocks)) {
        if ((tt_flag == score::LOWERBOUND && tt_score >= beta)
                || (tt_flag == score::UPPERBOUND && tt_score <= alpha)
                || tt_flag == score::EXACT) {
            stack->tt_clocks.include_child(tt_clocks, 0);
            return tt_score;
        }
    }

    /*
     * First move
     * - if there is no first move, it's checkmate or stalemate and both cases
     * are a win in losers chess
     */

    stack->tt_move.set_compact(&brd, tt_move);
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
            if (in_mate_search == (int) us) {
                //unknown if we can finish the forced mating sequence
                return 1000 + brd.ply;
            }
            return w17_evaluate(this);
        } else if (in_mate_search == (int) them) {
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
            if (!stop_all && pv && score > alpha) {
                //full window research
                const clock_range_t previous_clocks = stack->tt_clocks;
                score = -w17_pvs(-beta, -alpha, in_mate_search, depth - 1 + extend);
                stack->tt_clocks.include_child(previous_clocks, 0);
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
                    trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, score, move->to_compact(), score::LOWERBOUND, context, stack->tt_clocks);
                }

                if (depth > 0 && !move->capture && !move->promotion && !move->castle) {
                    update_killers(move);
                    update_history(move);
                    // History is nonnegative; saturate penalties before the shift becomes invalid.
                    const int history_shift = MIN(searched_moves, std::numeric_limits<int>::digits);
                    for (int i = 0; i < searched_moves; i++) {
                        move_t * m = &stack->searched[i];
                        if (!m->capture && !m->promotion) {
                            history[m->piece][m->tsq] >>= history_shift;
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
        stack->searched[searched_moves++].set(move);
    } while ((move = move::next(this, 0)));

    /*
     * Finalize
     */

    assert(best > -score::INF && best < beta);
    assert(!stop_all);
    assert(brd.valid(&stack->best_move) && brd.legal(&stack->best_move));

    if (depth > 0) {
        int flag = score::flags(best, alpha1, beta); 
        trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, best, stack->best_move.to_compact(), flag, context, stack->tt_clocks);
    }

    return best;
}
