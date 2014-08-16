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
 * File: search.cpp
 * Search methods for traditional chess. 
 * Maxima uses a fail-soft principle variation search (PVS) with
 * - aspiration windows
 * - null move pruning 
 * - futility pruning
 * - late move reductions (LMR) and late move pruning (LMP)
 * 
 */

#include "search.h"
#include "engine.h"
#include "evaluate.h"
#include "timeman.h"

namespace {
    enum search_constants_t {
        FUTILITY_MARGIN = 50,
        QS_DELTA = 200,
        NODES_BETWEEN_POLLS = 5000
    };
};

/**
 * Initialize sort values for root move
 * @param m move
 * @param val initial value
 * @param checks if the move checks the opponent
 * @param see_value static exchange value
 */
void root_move_t::init(move_t * m, int val, bool checks, int see_value) {
    nodes = 0;
    pv = 0;
    value = -score::INF;
    initial_value = val;
    move.set(m);
    gives_check = checks;
    see = see_value;
}

/**
 * Compare two root moves for sorting
 * @param m move
 * @return int < 0 -> m is better; >= 0 m is not better
 */
int root_move_t::compare(root_move_t * m) {
    int result = pv - m->pv;
    if (result) {
        return result;
    }
    result = value - m->value;
    if (result) {
        return result;
    }
    result = nodes - m->nodes;
    if (result) {
        return result;
    }
    result = initial_value - m->initial_value;
    if (result) {
        return result;
    }
    result = see - m->see;
    return result;
}

/**
 * Constructor
 * @param fen string representing the board position
 */
search_t::search_t(const char * fen) {
    brd.create(fen);
    memset(history, 0, sizeof (history));
    InitPST();
    nodes = 0;
    pruned_nodes = 0;
    learn = 1.0;
    stop_all = false;
    ponder = false;
    skip_null = false;
    next_poll = NODES_BETWEEN_POLLS;
    max_nodes = 0;
    sel_depth = 0;
    learn = 1.0;
    root_stack = stack = &_stack[0];
    stack->eval_result = score::INVALID;
}

/**
 * Poll to test is the search should be aborted
 */
void search_t::poll() {
    next_poll = NODES_BETWEEN_POLLS;
    stop_all = (time_man::time_is_up() && (!ponder || engine::is_ponder() == false))
            || engine::is_stopped()
            || (max_nodes > 0 && nodes > max_nodes);
}

/**
 * Do a null move
 */
void search_t::forward() {
    skip_null = true;
    stack->current_move.set(0);
    stack++;
    stack->in_check = false;
    stack->eval_result = score::INVALID;
    brd.forward();
    assert(stack == &_stack[brd.current_ply]);
}

/**
 * Undo a null move
 */
void search_t::backward() { 
    stack--;
    brd.backward();
    skip_null = false;
    assert(stack == &_stack[brd.current_ply]);
}

/**
 * Make a move, going one ply deeper in the search
 * @param move 
 * @param gives_check
 */
void search_t::forward(move_t * move, bool gives_check) {
    stack->current_move.set(move);
    stack++;
    stack->in_check = gives_check;
    stack->eval_result = score::INVALID;
    brd.forward(move);
    assert(stack == &_stack[brd.current_ply]);
}

/**
 * Undo a move
 * @param move
 */
void search_t::backward(move_t * move) {
    stack--;
    brd.backward(move);
    assert(stack == &_stack[brd.current_ply]);
}

/**
 * Test if the search is pondering
 * @return bool true if pondering 
 */
bool search_t::pondering() {
    return ponder || engine::is_ponder();
}

/**
 * Converts principle variation to a string
 * @return pv string
 */
std::string search_t::pv_to_string() {
    std::string result = "";
    for (int i = 0; i < stack->pv_count; i++) {
        result += stack->pv_moves[i].to_string() + " ";
    }
    return result;
}

/**
 * Update history sort scores for quiet moves
 * @param move a quiet move
 * @param depth current search depth
 */
void search_t::update_history(move_t * move, int depth) {
    const int HISTORY_MAX = 5000;
    int pc = move->piece;
    int tsq = move->tsq;
    history[pc][tsq] += depth * ABS(depth);
    if (ABS(history[pc][tsq]) > HISTORY_MAX) {
        for (int pc = WPAWN; pc <= BKING; pc++) {
            for (int sq = a1; sq <= h8; sq++) {
                history[pc][sq] >>= 1;
            }
        }
    }
}

/**
 * Initialize moves for the root position
 * @return int amount of legal moves
 */
int search_t::init_root_moves() {
    int result = 0;
    root.move_count = 0;
    root.fifty_count = brd.stack->fifty_count;
    rep_table::store(brd.stack->fifty_count, brd.stack->hash_code);

    int trans_move = 0;
    int trans_flags = 0;
    int score = 0;
    trans_table::retrieve(brd.stack->hash_code, 0, 0, score, trans_move, trans_flags);

    for (move_t * move = move::first(this, 1, -score::INF, score::INF);
            move; move = move::next(this, 1, -score::INF, score::INF)) {
        root_move_t * rmove = &root.moves[root.move_count++];
        int move_score = 1000 - root.move_count;
        if (move->to_int() == trans_move) {
            move_score += 10000;
        }
        rmove->init(move, move_score, brd.gives_check(move), brd.see(move));
        if (rmove->gives_check) {
            rmove->checker_sq = (brd.stack + 1)->checker_sq;
            rmove->checkers = (brd.stack + 1)->checkers;
        }
    }
    root.in_check = brd.in_check();
    stack->hash_code = brd.stack->hash_code;
    stack->eval_result = score::INVALID;
    result = root.move_count;
    return result;
}

/*
 * Sort algorithm for root moves (simple insertion sort)
 */
void root_t::sort_moves() {
    for (int j = 1; j < move_count; j++) {
        root_move_t rmove = moves[j];
        int i = j - 1;
        while (i >= 0 && rmove.compare(&moves[i]) > 0) {
            moves[i + 1] = moves[i];
            i--;
        }
        moves[i + 1] = rmove;
    }
}

/* 
 * Copy root moves from another list
 */
void root_t::match_moves(move::list_t * list) {
    if (list->first == list->last) {
        return;
    }
    for (int j = 0; j < move_count;) {
        bool match = false;
        root_move_t rmove = moves[j];
        std::cout << rmove.move.to_string() << " ";
        ;
        for (move_t * m = list->first; m != list->last; m++) {
            if (rmove.move.equals(m)) {
                match = true;
                break;
            }
        }
        if (match == false) {
            moves[j] = moves[move_count - 1];
            move_count--;
            continue;
        }
        j++;
    }
}

/**
 * Principle Variation Search (root node)
 * Difference with normal (non-root) search:
 * - Sending new PVs to the interface
 * - Sort order based on amount of nodes of the subtrees
 * - No pruning, reductions, extensions and hash table lookup/store
 */
int search_t::pvs_root(int alpha, int beta, int depth) {
    /*
     * Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    assert(root.move_count > 0);
    int nodes_before = nodes;
    root_move_t * rmove = &root.moves[0];
    forward(&rmove->move, rmove->gives_check);
    if (rmove->gives_check) {
        brd.stack->checker_sq = rmove->checker_sq;
        brd.stack->checkers = rmove->checkers;
    }
    int new_depth = depth - 1;
    int best = -pvs(-beta, -alpha, new_depth);
    backward(&rmove->move);
    int pv_score = 1000 * depth;
    rmove->nodes += nodes - nodes_before;
    rmove->pv = pv_score;
    rmove->value = best;
    stack->best_move.set(&rmove->move);
    if (!rmove->move.equals(&stack->pv_moves[0])) {
        stack->pv_moves[0].set(&rmove->move);
        stack->pv_count = 1;
    }
    if (stop_all) {
        return alpha;
    }
    if (best < alpha) {
        return best; //return to adjust the aspiration window
    }
    if (best >= beta) {
        uci::send_pv(best, depth, sel_depth, nodes + pruned_nodes, time_man::elapsed(),
                pv_to_string().c_str(), score::flags(best, alpha, beta));
        return best;
    }
    if (best > alpha) {
        update_pv(&rmove->move);
        uci::send_pv(best, depth, sel_depth, nodes + pruned_nodes, time_man::elapsed(),
                pv_to_string().c_str(), score::flags(best, alpha, beta));
        alpha = best;
    }

    /*
     * Search remaining moves with a zero width window
     */
    for (int i = 1; i < root.move_count; i++) {
        rmove = &root.moves[i];
        nodes_before = nodes;
        forward(&rmove->move, rmove->gives_check);
        if (rmove->gives_check) {
            brd.stack->checker_sq = rmove->checker_sq;
            brd.stack->checkers = rmove->checkers;
        }
        int score = -pvs(-alpha - 1, -alpha, new_depth);
        if (score > alpha && stop_all == false) {
            score = -pvs(-beta, -alpha, new_depth);
        }
        backward(&rmove->move);
        rmove->nodes += nodes - nodes_before;
        rmove->value = score;
        if (stop_all) {
            return alpha;
        }
        if (score > best) {
            rmove->pv = pv_score + i;
            stack->best_move.set(&rmove->move);
            if (score >= beta) {
                if (!rmove->move.equals(&stack->pv_moves[0])) {
                    stack->pv_moves[0].set(&rmove->move);
                    stack->pv_count = 1;
                }
                return score;
            }
            best = score;
            if (score > alpha) {
                update_pv(&rmove->move);
                uci::send_pv(best, depth, sel_depth, nodes + pruned_nodes,
                        time_man::elapsed(), pv_to_string().c_str(),
                        score::flags(best, alpha, beta));
                alpha = score;
            }
        } else {
            rmove->pv = rmove->pv / 32;
        }
    }
    return best;
}

/**
 * Move extensions
 */
int search_t::extend_move(move_t * move, int gives_check) {
    if (gives_check > 0) {
        if (gives_check > 1) { //double check or exposed check
            return 1;
        }
        if (move->capture) {
            return 1;
        }
        if (move->piece == WPAWN || move->piece == BPAWN) {
            return 1;
        }
        if (brd.see(move) >= 0) {
            return 1;
        }
        return 0;
    }
    return 0;
}

/**
 * Principle Variation Search (fail-soft)
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth remaining search depth
 * @return score for the current node
 */
int search_t::pvs(int alpha, int beta, int depth) {

    stack->pv_count = 0;

    /*
     * 1. If no more depth remaining, return quiescence value
     */
    if (depth < 1) {
        sel_depth = MAX(sel_depth, brd.current_ply);
        return qsearch(alpha, beta, depth);
    }

    //time check
    nodes++;
    if (next_poll <= 0) {
        poll();
    }
    if (stop_all || brd.current_ply >= (MAX_PLY - 1)) {
        return alpha;
    }
    assert(depth >= ONE_PLY && depth < 256);

    /* 
     * 2. Mate distance pruning: 
     * if we already know we will win/loose by mate in n, 
     * it is not needed to search deeper than n
     */
    if ((score::MATE - brd.current_ply) < beta) {
        beta = score::MATE - brd.current_ply;
        if (alpha >= (score::MATE - brd.current_ply)) {
            return score::MATE - brd.current_ply;
        }
    }
    if ((-score::MATE + brd.current_ply) > alpha) {
        alpha = -score::MATE + brd.current_ply;
        if (beta <= (-score::MATE + brd.current_ply)) {
            return -score::MATE + brd.current_ply;
        }
    }

    /*
     * 3. Return obvious draws 
     */
    if (brd.is_draw()) { //draw by no mating material
        return draw_score();
    }
    if (brd.stack->fifty_count > 3) {
        if (brd.stack->fifty_count >= (100 + stack->in_check)) {
            return draw_score(); //draw by 50 reversible moves
        }
        int stopPly = brd.current_ply - brd.stack->fifty_count;
        for (int ply = brd.current_ply - 4; ply >= stopPly; ply -= 2) { //draw by repetition
            if (ply >= 0 && get_stack(ply)->hash_code == brd.stack->hash_code) {
                return draw_score();
            } else if (ply < 0 && rep_table::retrieve(root.fifty_count + ply) == brd.stack->hash_code) {
                return draw_score();
            }
        }
    }

    /*
     * 4. Transposition table lookup
     */
    int tmove = 0;
    int tflag = 0;
    int tscore = 0;
    if (trans_table::retrieve(brd.stack->hash_code, brd.current_ply, depth, tscore, tmove, tflag)) {
        if ((tflag == score::LOWERBOUND && tscore >= beta)
                || (tflag == score::UPPERBOUND && tscore <= alpha)
                || tflag == score::EXACT) {
            return tscore;
        }
    }
    stack->tt_move.set(tmove);

    /*
     * 5. Pruning: Fail-high and Nullmove
     */
    int eval = evaluate(this);
    int new_depth = depth - 1;
    bool in_check = stack->in_check;
    assert(new_depth >= 0);

    //a) Fail-high pruning (return if static evaluation score is already much better than beta)
    if (!in_check
            && new_depth <= 3
            && (eval - FUTILITY_MARGIN * depth) >= beta
            && beta > -score::DEEPEST_MATE
            && brd.has_pieces(brd.stack->wtm)) {
        return eval - FUTILITY_MARGIN * depth;
    }

    //b) Null move pruning
    stack->hash_code = brd.stack->hash_code;
    bool mate_threat = false;
    if (!skip_null
            && !in_check
            && new_depth > 0
            && eval >= beta
            && beta > -score::DEEPEST_MATE
            && brd.has_pieces(brd.stack->wtm)) {
        int rdepth = new_depth - 3;
        rdepth = MAX(rdepth, 0);
        forward();
        int null_score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (null_score >= beta) {
            if (null_score > score::DEEPEST_MATE) {
                return beta; // not return unproven mate scores
            }
            trans_table::store(brd.stack->hash_code, brd.root_ply, brd.current_ply, depth, null_score, 0, score::LOWERBOUND);
            return null_score;
        } else {
            mate_threat = null_score < -score::DEEPEST_MATE;
            if (mate_threat) {
                move_t * threat = &(stack + 1)->best_move;
                if (!threat->capture && !threat->promotion) {
                    update_history(threat, rdepth);
                }
            }
        }
    }



    /*
     * IID 
     */
    bool pv = is_pv(alpha, beta);
    if (pv && depth > 3 && tmove == 0) {
        skip_null = true;
        stack->best_move.set(0);
        int iid_depth = depth - 2 - depth / 4;
        iid_depth = MAX(1, iid_depth);
        /*int iid_score = */pvs(alpha, beta, iid_depth);
        stack->tt_move.set(&stack->best_move);
    }
    skip_null = false;

    /*
     * 7. Principle variation search (PVS). 
     * Generate the first move from pv, hash or internal iterative deepening,
     * all handled by the movepicker instance. 
     * If no move is returned, the position is either MATE or STALEMATE, 
     * otherwise search the first move with full alpha beta window.
     */
    move_t * first_move = move::first(this, depth, alpha, beta);
    if (!first_move) { //no legal move: it's checkmate or stalemate
        return in_check ? -score::MATE + brd.current_ply : draw_score();
    }
    int gives_check = brd.gives_check(first_move);
    int extend = extend_move(first_move, gives_check);
    stack->best_move.set(first_move);
    forward(first_move, gives_check);
    int best = -pvs(-beta, -alpha, new_depth + extend);
    backward(first_move);
    if (best > alpha) {
        if (best >= beta) {
            trans_table::store(brd.stack->hash_code, brd.root_ply, brd.current_ply, depth, best, first_move->to_int(), score::LOWERBOUND);
            if (!first_move->capture && !first_move->promotion) {
                if (best < score::DEEPEST_MATE) {
                    update_killers(first_move);
                } else {
                    stack->killer[0].set(first_move);
                }
                update_history(first_move, depth);
            }
            return best;
        }
        update_pv(first_move);
        alpha = best;
    }

    /*
     * 10. Search remaining moves with a zero width window (beta == alpha+1), 
     * in the following order:
     * - Captures and queen promotions with a positive see value 
     * - Killer moves
     * - Castling
     * - Non-captures with a positive static score
     * - All remaining moves
     */
    int searched_moves = 1;
    int max_reduce = MAX(0, new_depth - 1);
    while (move_t * move = move::next(this, depth, alpha, beta)) {
        assert(stack->best_move.equals(move) == false);
        assert(first_move->equals(move) == false);
        gives_check = brd.gives_check(move);
        bool skip_prune = stack->move_list.stage < QUIET_MOVES
                || in_check || gives_check > 0 || move->capture || move->promotion
                || move->castle || is_passed_pawn(move) || beta < -score::DEEPEST_MATE;

        /*
         * 11. forward futility pruning at low depths
         * (moves without potential are skipped)
         */
        if (!skip_prune
                && !pv
                && (eval < alpha || best >= alpha)
                && new_depth <= 3
                ) {
            int mc_max = 2 + ((depth * depth) / 4);
            if (searched_moves > mc_max) {
                pruned_nodes++;
                continue;
            }
            if (eval + FUTILITY_MARGIN * depth <= alpha) {
                pruned_nodes++;
                continue;
            }
            if (brd.see(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }

        /*
         * 12. Late move Reductions (LMR) 
         */
        extend = extend_move(move, gives_check);
        int reduce = 0;

        if (!skip_prune && max_reduce > 0 && searched_moves >= 3) {
            reduce = searched_moves < 6 ? 1 : depth / 3;
            reduce = MIN(reduce, max_reduce);
        }
        assert(reduce == 0 || extend == 0);

        /*
         * 13. Go forward and search next node
         */
        forward(move, gives_check);
        int score = -pvs(-alpha - 1, -alpha, new_depth - reduce + extend);
        if (score > alpha && reduce > 0) {
            //research without reductions
            score = -pvs(-alpha - 1, -alpha, new_depth + extend);
        }
        if (pv && score > alpha) {
            //full window research
            score = -pvs(-beta, -alpha, new_depth + extend);
        }
        backward(move);

        /*
         * 14. Update the best value for this node and return is the score is 
         * above beta (beta cutoff)
         */
        if (score > best) {
            stack->best_move.set(move);
            if (score >= beta) {
                // Beta Cutoff, hash the results, update killers and history table
                trans_table::store(brd.stack->hash_code, brd.root_ply, brd.current_ply, depth, score, move->to_int(), score::LOWERBOUND);
                if (!move->capture && !move->promotion) {
                    if (best < score::DEEPEST_MATE) {
                        update_killers(move);
                    } else {
                        stack->killer[0].set(move);
                    }
                    update_history(move, depth);
                    for (move_t * cur = stack->move_list.first;
                            cur != stack->move_list.last; cur++) {
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
        }
        searched_moves++;
    }

    /*
     * 15. Store the result in the hash table and return
     */
    int flag = score::flags(best, alpha, beta);
    trans_table::store(brd.stack->hash_code, brd.root_ply, brd.current_ply, depth, best, stack->best_move.to_int(), flag);
    return best;
}

/*
 * Quiescence search
 */
int search_t::qsearch(int alpha, int beta, int depth) {

    //time check
    nodes++;
    if (--next_poll <= 0) {
        poll();
    }
    if (stop_all || brd.current_ply >= (MAX_PLY - 1)) {
        return alpha;
    }

    //return obvious draws
    if (brd.stack->fifty_count > 3) { //draw by repetition or fifty quiet moves
        if (brd.stack->fifty_count >= (100 + stack->in_check)) {
            return draw_score();
        }
        int stopPly = brd.current_ply - brd.stack->fifty_count;
        for (int ply = brd.current_ply - 4; ply >= stopPly; ply -= 2) {
            if (ply >= 0 && get_stack(ply)->hash_code == brd.stack->hash_code) {
                return draw_score();
            } else if (ply < 0 && rep_table::retrieve(root.fifty_count + ply) == brd.stack->hash_code) {
                return draw_score();
            }
        }
    }
    if (brd.is_draw()) { //draw by no mating material
        return draw_score();
    }

    //mate distance pruning
    if ((score::MATE - brd.current_ply) < beta) {
        beta = score::MATE - brd.current_ply;
        if (alpha >= (score::MATE - brd.current_ply)) {
            return score::MATE - brd.current_ply;
        }
    }
    if ((-score::MATE + brd.current_ply) > alpha) {
        alpha = -score::MATE + brd.current_ply;
        if (beta <= (-score::MATE + brd.current_ply)) {
            return -score::MATE + brd.current_ply;
        }
    }

    int eval = evaluate(this); //always do an eval - it's incremental

    //if not in check, generate captures, promotions and (upto some plies ) quiet checks
    if (eval >= beta && !stack->in_check) { //return evaluation score is it's already above beta (stand-pat idea)
        return eval;
    }

    move_t * move = move::first(this, depth, alpha, beta);
    if (!move) { //return evaluation score if there are no quiescence moves
        return stack->in_check ? -score::MATE + brd.current_ply : eval;
    }
    if (!stack->in_check) {
        alpha = MAX(eval, alpha);
    }
    stack->hash_code = brd.stack->hash_code;
    bool pv = is_pv(alpha, beta);
    do { //loop through quiescence moves
        int gives_check = brd.gives_check(move);
        if (!stack->in_check && !move->capture && !move->promotion && !move->castle &&
                (gives_check == 0 || (gives_check == 1 && brd.see(move) < 0))) {
            pruned_nodes++;
            continue;
        }

        //pruning (delta futility and negative see), skipped for checks
        if (!stack->in_check && gives_check == 0 && !pv) {

            //1. delta futility pruning: the captured piece + max. positional gain should raise alpha
            int gain = PIECE_VALUE[move->capture];
            if (move->promotion) {
                if (move->promotion != WQUEEN && move->promotion != BQUEEN) {
                    pruned_nodes++;
                    continue;
                }
                gain += PIECE_VALUE[move->promotion] - VPAWN;
            }
            int delta = depth >= 0 ? QS_DELTA : FUTILITY_MARGIN;
            if (eval + gain + delta < alpha) {
                pruned_nodes++;
                continue;
            }

            //2. prune moves when see is negative
            if (brd.see(move) < 0) {
                pruned_nodes++;
                continue;
            }
        }
        forward(move, gives_check);
        int score = -qsearch(-beta, -alpha, depth - 1);
        backward(move);
        if (score >= beta) {
            stack->best_move.set(move);
            return score;
        } else if (score > alpha) {
            stack->best_move.set(move);
            alpha = score;
        }
    } while ((move = move::next(this, depth, alpha, beta)));
    return alpha;
}

void search_t::debug_print_search(int alpha, int beta) {
    std::cout << "print search (" << alpha << ", " << beta << "): " << std::endl;

    board_t brd2;
    memcpy(&brd2, &brd, sizeof (board_t));
    while (brd2.current_ply > 0) {
        move_t * move = &get_stack(brd2.current_ply - 1)->current_move;
        move->piece ? brd2.backward(move) : brd2.backward();
    }

    std::cout << "ROOT FEN: " << brd2.to_string() << std::endl;
    std::cout << "Path ";
    for (int i = 0; i < brd.current_ply; i++) {
        std::cout << " " << get_stack(i)->current_move.to_string();
    }
    std::cout << "\nFEN: " << brd.to_string() << std::endl;
    std::cout << "Hash: " << brd.stack->hash_code << std::endl;
    std::cout << "nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << this->skip_null << std::endl;

    std::cout << std::endl;
    exit(0);
}

/*
 * Helper function to reset the search stack. 
 * This is used for self-playing games, where  
 * MAX_PLY is not sufficient to hold all moves of a chess game. 
 * Note: in UCI mode this is not an issue, because each position
 * starts with a new stack. 
 */
void search_t::reset_stack() {
    brd.current_ply = 0;
    stack = root_stack;
    stack->eval_result = score::INVALID;
    brd._stack[0].copy(this->brd.stack);
    brd.stack = &this->brd._stack[0];
    nodes = 0;
    pruned_nodes = 0;
    stack->pv_count = 0;
}