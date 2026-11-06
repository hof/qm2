/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
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
#include <array>
#include <cmath>
#include <limits>
#include "w17_eval.h"
#include "uci_console.h"

/**
 * Verifies is a position is drawn by 
 * a) fifty quiet moves
 * b) repetition
 * @return true if it's an official draw
 */

bool w17_search_t::w17_is_draw() {
    const bool fifty_move = brd.stack->fifty_count >= 100;
    const bool repetition = brd.stack->fifty_count >= 4 && is_repetition();
    if (fifty_move || repetition) {

        // Losing all non-king pieces, checkmate, and stalemate win before a draw.
        if (max_1(brd.all(brd.us())) || max_1(brd.all(brd.them()))) {
            return false;
        }
        move::list_t moves;
        move::gen_captures(&brd, &moves);
        move::gen_promotions(&brd, &moves);
        move::gen_castles(&brd, &moves);
        move::gen_quiet_moves(&brd, &moves);
        for (move_t * move = moves.first; move != moves.last; ++move) {
            if (!brd.legal(move)) {
                continue;
            }
            return true;
        }
        return false;
    }
    return false;
}


U64 w17_search_t::w17_tt_key() const {
    return position_key(brd);
}

int w17_search_t::pvs(int alpha, int beta, int depth, bool) {
    return w17_pvs(alpha, beta, depth);
}

/*
 * Losers search: one plain tree for both sides. Captures are compulsory,
 * also past the horizon: the evaluation comes at the first node without one. Capture nodes are
 * narrow, so depth comes cheap; a winning streak is found once the depth covers it. Streak moves,
 * quiet moves that force the opponent to capture, come first; quiet moves outside the first are
 * searched shallower, free ones most.
 */
int w17_search_t::w17_pvs(int alpha, int beta, int depth) {
    static const auto lmr = [] {
        std::array<std::array<int8_t, 64>, 64> table{};
        for (int d = 1; d < 64; d++) {
            for (int m = 1; m < 64; m++) {
                table[d][m] = int8_t(std::log(d) * std::log(m) / 2);
            }
        }
        return table;
    }();

    stack->pv_count = 0;

    // Every node counts toward a node limit, terminal ones included
    if (abort(false)) {
        return alpha;
    }

    // A side left with only its king has won
    const bool us = brd.us();
    if (max_1(brd.all(us))) {
        return score::MATE - brd.ply;
    }
    if (max_1(brd.all(!us))) {
        return -score::MATE + brd.ply;
    }

    // Draw by repetition or fifty quiet moves
    if (w17_is_draw()) {
        return draw_score();
    }

    if (brd.ply >= sel_depth) {
        sel_depth = brd.ply;
    }

    if (brd.ply >= MAX_PLY) {
        if (!has_legal_move(brd)) {
            return score::MATE - brd.ply;
        }
        return w17_evaluate(this);
    }

    // Mate distance pruning
    beta = MIN(beta, score::MATE - brd.ply);
    alpha = MAX(alpha, -score::MATE + brd.ply);
    if (alpha >= beta) {
        return alpha;
    }

    // Transposition table
    const int alpha1 = alpha;
    stack->tt_key = w17_tt_key();
    tt_hit_t tt;
    const int decisive = score::MATE - MAX_PLY;
    const int tt_depth = MAX(depth, 0);
    if (trans_table::probe(stack->tt_key, brd.ply, brd.stack->fifty_count, tt)
            && tt.bound && brd.stack->fifty_count < 90 && (tt.depth >= tt_depth

                // A proven win or loss holds at any depth
                || (tt.score >= decisive && tt.bound != score::UPPERBOUND)
                || (tt.score <= -decisive && tt.bound != score::LOWERBOUND))) {
        if ((tt.bound == score::LOWERBOUND && tt.score >= beta)
                || (tt.bound == score::UPPERBOUND && tt.score <= alpha)
                || tt.bound == score::EXACT) {
            return tt.score;
        }
    }

    // No legal move: checkmate or stalemate, both a win in losers chess
    stack->tt_move.set_compact(&brd, tt.move);
    move_t * move = move::first(this, MAX(depth, 0));
    if (!move) {
        return score::MATE - brd.ply;
    }

    /*
     * Past the horizon a capture or a check evasion is still forced. A node without one stands
     * pat on the evaluation, unless its side is in the middle of a streak: its last quiet move
     * was answered by forced moves only. Then it may go on with moves that leave the opponent a
     * capture, new or pending, or with a check, but not two checks without a capture between
     * them; a check counts as a check even with a capture pending, since the opponent must evade
     * first. Only a proven win counts. New streaks start inside the nominal depth only.
     */
    const bool quiet_node = move->capture == 0 && stack->in_check == 0;
    const bool horizon = depth <= 0 && move->capture == 0 && !stack->in_check;
    int best = -score::INF;
    if (horizon) {
        best = w17_evaluate(this);
        int last_quiet = 1;
        while (last_quiet <= brd.ply && ((stack - last_quiet)->current_move.capture
                || (stack - last_quiet)->in_check)) {
            last_quiet++;
        }
        if (best >= beta || last_quiet % 2 || last_quiet > brd.ply) {
            return best;
        }
        alpha = MAX(alpha, best);
    }
    const int win = score::MATE - MAX_PLY;
    const bool pv = alpha + 1 < beta;
    stack->best_move.clear();
    int searched_moves = 0;
    do {
        assert(brd.valid(move) && brd.legal(move));
        const bool quiet = move->capture == 0 && move->promotion == 0;
        const int gives_check = brd.gives_check(move);

        /*
         * Every move costs a ply. Streak moves are never reduced; free quiet moves are cut: late
         * ones are pruned near the horizon and reduced hard further up.
         */
        bool forcing = false, new_capture = false;
        if (quiet && quiet_node) {
            if (stack->tt_move.equals(move)) {
                forcing = brd.capture_replies(move, brd.hanging_pieces(), new_capture) > 0;
            } else if (move->score > FORCING_MINIMUM) {
                forcing = true;
                new_capture = forcing_class(move->score) % 2 == 0;
            }
        }
        const bool streak = forcing && new_capture;
        if (horizon && (gives_check ? (stack - 1)->in_check && !(stack - 1)->current_move.capture
                : !forcing)) {
            continue;
        }

        // A streak move must not let the opponent end the streak with a capture we must take back
        if (horizon && !gives_check && !brd.keeps_streak(move)) {
            continue;
        }
        int reduce = 0;
        if (quiet && quiet_node && !streak && !gives_check && depth > 0) {
            if (depth <= 4 && searched_moves >= 2 + depth && best > -decisive) {
                continue;
            }
            if (depth >= 2) {
                reduce = MIN(depth - 1, 1 + !pv + lmr[MIN(depth, 63)][MIN(searched_moves + 1, 63)]);
            }
        }
        forward(move, gives_check);
        const int new_depth = depth - 1;
        int score;
        if (horizon) {

            // Any proven win will do: looking for a shorter one only grows the proof
            score = -w17_pvs(-win, -(win - 1), new_depth);
            backward(move);
            if (stop_all) {
                return alpha;
            }
            if (score < win) {
                continue;
            }
            stack->best_move.set(move);
            update_pv(move);
            trans_table::store(stack->tt_key, brd.root_ply, brd.ply, tt_depth, score, move->to_compact(), score::LOWERBOUND);
            return score;
        } else if (searched_moves == 0 && !reduce) {
            score = -w17_pvs(-beta, -alpha, new_depth);
        } else {
            score = -w17_pvs(-alpha - 1, -alpha, new_depth - reduce);
            if (!stop_all && reduce && score > alpha) {
                score = -w17_pvs(-alpha - 1, -alpha, new_depth);
            }
            if (!stop_all && pv && score > alpha && score < beta) {
                score = -w17_pvs(-beta, -alpha, new_depth);
            }
        }
        backward(move);

        if (stop_all) {
            return alpha;
        }
        if (score > best) {
            best = score;
            stack->best_move.set(move);
            if (score >= beta) {
                trans_table::store(stack->tt_key, brd.root_ply, brd.ply, tt_depth, score, move->to_compact(), score::LOWERBOUND);
                if (quiet && !move->castle) {
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
            if (score > alpha) {
                update_pv(move);
                alpha = score;
            }
        }
        stack->searched[searched_moves++].set(move);
    } while ((move = move::next(this, MAX(depth, 0))));

    assert(best > -score::INF && best < beta);
    assert(!stop_all);
    const int flag = score::flags(best, alpha1, beta);
    trans_table::store(stack->tt_key, brd.root_ply, brd.ply, tt_depth, best,
        stack->best_move.piece ? stack->best_move.to_compact() : 0, flag);
    return best;
}
