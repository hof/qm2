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

/*
 * At a horizon node in our streak, with moves scored by the move picker: may we go on with the move? A quiet move must leave the
 * opponent a capture and must not let it end the streak with a capture we must take back; a check
 * is fine, but not two checks without a capture between them.
 */
bool w17_search_t::streak_try(const move_t * move, const int gives_check) {
    if (gives_check) {
        return !((stack - 1)->in_check && !(stack - 1)->current_move.capture);
    }
    return !move->capture && !move->promotion && move->score > FORCING_MINIMUM && brd.keeps_streak(move);
}

namespace {
    constexpr uint32_t DFPN_INF = 1u << 30;
    constexpr U64 DFPN_CHECK_SALT = 0x9e3779b97f4a7c15ULL;
    constexpr size_t DFPN_SIZE = size_t(1) << 20;

    uint32_t dfpn_add(const uint32_t a, const uint32_t b) {
        return uint32_t(std::min<U64>(U64(a) + b, DFPN_INF - 1));
    }

    // A child's threshold may pass its sibling's number by a quarter, so df-pn switches less often
    uint32_t dfpn_beyond(const uint32_t second) {
        return second >= DFPN_INF - 1 ? DFPN_INF : uint32_t(std::min<U64>(U64(second) + 1 + second / 4, DFPN_INF));
    }
}

/*
 * The moves of a streak node by the rules of the horizon proof in w17_pvs: our quiet nodes go on
 * only with streak moves, capture and check nodes are forced, a defender without a capture or
 * check escapes. Returns the number of moves, or -1 when the node is decided already, with
 * proven telling who won: a side left with its king only or without a legal move has won, a
 * draw or the ply limit is no proof.
 */
int w17_search_t::streak_children(dfpn_child_t * children, const bool or_node, bool & proven) {
    const bool us = brd.us();
    if (max_1(brd.all(us)) || max_1(brd.all(!us))) {
        proven = max_1(brd.all(us)) == or_node;
        return -1;
    }
    proven = false;
    if (w17_is_draw() || brd.ply >= MAX_PLY - 1) {
        return -1;
    }
    stack->tt_move.clear();
    move_t * move = move::first(this, 0);
    if (!move) {
        proven = or_node;
        return -1;
    }
    const bool quiet_node = move->capture == 0 && !stack->in_check;
    if (quiet_node && !or_node) {
        return -1;
    }
    int count = 0;
    do {
        const int gives_check = brd.gives_check(move);
        if (quiet_node && !streak_try(move, gives_check)) {
            continue;
        }
        dfpn_child_t & child = children[count++];
        child.move.set(move);
        child.check = gives_check;

        /*
         * Only a position right after a capture or pawn move goes into the table: nothing after it
         * can repeat what came before, so its result does not depend on the path to it.
         */
        brd.forward(move);
        child.key = brd.stack->fifty_count ? 0
            : position_key(brd) ^ (stack->in_check && !move->capture ? DFPN_CHECK_SALT : 0);
        brd.backward(move);

        // Fewer capture replies, easier to prove; a capture we must take back, likelier to escape
        child.pn = or_node && quiet_node && !gives_check ? std::max(1, forcing_class(move->score) / 2) : 1;
        child.dn = !or_node && move->capture ? 2 - (move->score & 1) : 1;
        child.length = 0;
    } while ((move = move::next(this, 0)));
    if (count == 0) {
        return -1;
    }
    return count;
}

/*
 * Depth-first proof-number search of a streak, from the streaking side (OR) against the
 * defender (AND). It always works on the try that is cheapest to prove or refute, counted in open
 * defender replies, instead of finishing one try at a time.
 */
dfpn_result_t w17_search_t::dfpn(const uint32_t thpn, const uint32_t thdn, const bool or_node) {
    if (abort(false)) {
        return {1, 1, 0, {}};
    }
    if (brd.ply >= sel_depth) {
        sel_depth = brd.ply;
    }
    dfpn_child_t children[move::MAX_MOVES];
    bool won;
    const int count = streak_children(children, or_node, won);
    if (count < 0) {
        return won ? dfpn_result_t{0, DFPN_INF, 0, {}} : dfpn_result_t{DFPN_INF, 0, 0, {}};
    }
    for (int i = 0; i < count; i++) {
        const dfpn_entry_t & entry = dfpn_table[children[i].key & (DFPN_SIZE - 1)];
        if (children[i].key && entry.key == children[i].key) {
            children[i].pn = entry.pn;
            children[i].dn = entry.dn;
            children[i].length = entry.length;
        }
    }
    const U64 key = brd.stack->fifty_count ? 0
        : position_key(brd) ^ ((stack - 1)->in_check && !(stack - 1)->current_move.capture ? DFPN_CHECK_SALT : 0);
    while (true) {
        uint32_t pn = or_node ? DFPN_INF : 0, dn = or_node ? 0 : DFPN_INF, second = DFPN_INF;
        int best = 0;
        for (int i = 0; i < count; i++) {
            const uint32_t own = or_node ? children[i].pn : children[i].dn;
            const uint32_t other = or_node ? children[i].dn : children[i].pn;
            uint32_t & small = or_node ? pn : dn;
            uint32_t & sum = or_node ? dn : pn;
            sum = dfpn_add(sum, other);
            if (own < small) {
                second = small;
                small = own;
                best = i;
            } else if (own < second) {
                second = own;
            }
        }
        if (or_node && pn == 0) {
            dn = DFPN_INF;
        }
        if (!or_node && dn == 0) {
            pn = DFPN_INF;
        }
        if (pn == 0 || dn == 0 || pn >= thpn || dn >= thdn) {
            int length = 0;
            if (pn == 0) {
                for (int i = 0; i < count; i++) {
                    if (or_node && children[i].pn == 0 && children[i].length < children[best].length) {
                        best = i;
                    }
                    if (!or_node) {
                        length = std::max(length, children[i].length);
                    }
                }
                length = 1 + (or_node ? children[best].length : length);
            }
            if (key) {
                dfpn_table[key & (DFPN_SIZE - 1)] = {key, pn, dn, length};
            }
            return {pn, dn, length, children[best].move};
        }
        dfpn_child_t & child = children[best];
        uint32_t child_pn, child_dn;
        if (or_node) {
            child_pn = std::min(thpn, dfpn_beyond(second));
            child_dn = thdn >= DFPN_INF ? DFPN_INF : uint32_t(std::min<U64>(U64(thdn) - dn + child.dn, DFPN_INF));
        } else {
            child_dn = std::min(thdn, dfpn_beyond(second));
            child_pn = thpn >= DFPN_INF ? DFPN_INF : uint32_t(std::min<U64>(U64(thpn) - pn + child.pn, DFPN_INF));
        }
        forward(&child.move, child.check);
        const dfpn_result_t result = dfpn(child_pn, child_dn, !or_node);
        backward(&child.move);
        if (stop_all) {
            return {pn, dn, 0, {}};
        }
        child.pn = result.pn;
        child.dn = result.dn;
        child.length = result.length;
    }
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

        /*
         * The streak itself goes to df-pn: only a proven win counts, and any proven win will do,
         * so df-pn works on the cheapest try instead of finishing one try at a time.
         */
        if (dfpn_table.empty()) {
            dfpn_table.resize(DFPN_SIZE);
        }
        const dfpn_result_t result = dfpn(DFPN_INF, DFPN_INF, true);
        if (stop_all) {
            return alpha;
        }
        if (result.pn == 0) {
            const int score = score::MATE - (brd.ply + result.length);
            move_t proof_move = result.move;
            stack->best_move.set(&proof_move);
            stack->pv_moves[0].set(&proof_move);
            stack->pv_count = 1;
            trans_table::store(stack->tt_key, brd.root_ply, brd.ply, tt_depth, score, proof_move.to_compact(), score::LOWERBOUND);
            return score;
        }
        trans_table::store(stack->tt_key, brd.root_ply, brd.ply, tt_depth, best, 0, score::flags(best, alpha1, beta));
        return best;
    }
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
        if (searched_moves == 0 && !reduce) {
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
