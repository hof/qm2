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
        QS_DELTA = 100,
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
void root_move_t::init(move_t * m, bool checks, int see_value) {
    nodes = 0;
    move.set(m);
    gives_check = checks;
    see = see_value;
}

/**
 * Compare two root moves for sorting
 * @param m move
 * @param best_move best move found so far
 * @return int < 0 -> m is better; >= 0 m is not better
 */
int root_move_t::compare(root_move_t * m, move_t * best_move) {
    if (this->move.equals(best_move)) {
        return 1;
    }
    if (m->move.equals(best_move)) {
        return -1;
    }
    return nodes - m->nodes;
}

/**
 * Initialize to start a new search
 * @param fen string representing the board position
 */
void search_t::init(const char * fen, game_t * g) {
    brd.init(fen);
    game = g ? g : game::instance();
    game->init_tm(brd.stack->wtm);
    memset(history, 0, sizeof (history));
    init_pst();
    ponder_move.clear();
    nodes = 0;
    pruned_nodes = 0;
    stop_all = false;
    skip_null = false;
    next_poll = NODES_BETWEEN_POLLS;
    sel_depth = 0;
    root_stack = stack = &_stack[0];
    stack->eval_result = score::INVALID;
    stack->best_move.clear();
    result_score = 0;
}

/**
 * Lookup current position in book
 */
bool search_t::book_lookup() {
    bool result = false;
    book::open("book.bin");
    move::list_t * bmoves = &stack->move_list;
    int total_score = book::find(&brd, bmoves);
    if (total_score > 0) {
        int rnd = (rand() % total_score) + 1;
        int score = 0;
        for (move_t * bmove = bmoves->first; bmove != bmoves->last; bmove++) {
            score += bmove->score;
            if (score >= rnd) {
                result = true;
                stack->best_move.set(bmove);
                break;
            }
        }
    }
    book::close();
    return result;
}

/**
 * Starts the searching
 */
void search_t::go() {
    assert(stack->best_move.piece == 0 && ponder_move.piece == 0);
    if (book_lookup()) { //book hit
        uci::send_pv(result_score, 0, 0, 0, game->tm.elapsed(),
                stack->best_move.to_string().c_str(), score::EXACT);
    } else if (init_root_moves() > 0) { //do iid search
        iterative_deepening();
    }
    uci::send_bestmove(stack->best_move, ponder_move);
}

void search_t::iterative_deepening() {
    bool is_easy = root.move_count <= 1 || (root.moves[0].see > 0 && root.moves[1].see <= 0);
    int last_score = -score::INF;
    move_t easy_move;
    easy_move.set(&root.moves[0].move);
    int max_time = game->tm.reserved();
    bool timed_search = game->white_time || game->black_time;
    int depth;
    for (depth = 1; depth <= game->max_depth; depth++) {
        int score = aspiration(depth, last_score);
        if (abort(true)) {
            break;
        }
        is_easy &= stack->best_move.equals(&easy_move) && score + 50 < last_score;
        int elapsed = game->tm.elapsed();
        if (timed_search && !pondering() && root.move_count <= 1 && elapsed > max_time / 8) {
            break;
        }
        if (timed_search && !pondering() && is_easy && elapsed > max_time / 4) {
            break;
        }
        if (timed_search && !pondering() && elapsed > max_time / 2) {
            break;
        }
        if (timed_search && score::mate_in_ply(score) && depth > score::mate_in_ply(score)) {
            break;
        }
        if (depth >= 6 && game->target_score && score >= game->target_score &&
                game->target_move.piece && game->target_move.equals(&stack->best_move)) {
            break;
        }
        last_score = score;
        root.sort_moves(&stack->best_move);
    }
    uci::send_pv(result_score, MIN(depth, game->max_depth), sel_depth,
            nodes + pruned_nodes, game->tm.elapsed(), pv_to_string().c_str(), score::EXACT);
    assert(stack->pv_count > 0);
    if (stack->pv_count > 1) {
        ponder_move.set(&stack->pv_moves[1]);
    }
}

int search_t::aspiration(int depth, int last_score) {
    if (depth >= 6 && !score::is_mate(last_score)) {
        for (int window = 40; window < 900; window *= 2) {
            int alpha = last_score - window;
            int beta = last_score + window;
            int score = pvs_root(alpha, beta, depth);
            if (stop_all) {
                return score;
            }
            if (score > alpha && score < beta) {
                return score;
            }
            if (score::is_mate(score)) {
                break;
            }
        }
    }
    return pvs_root(-score::INF, score::INF, depth);
}

/**
 * Poll to test is the search should be aborted
 */
bool search_t::abort(bool force_poll = false) {
    bool result = false;
    if (game->max_nodes > 0 && nodes >= game->max_nodes) {
        result = true;
    } else if (stop_all || engine::is_stopped()) {
        result = true;
    } else if (force_poll || --next_poll <= 0) {
        result = game->tm.time_is_up() && (!game->ponder || !engine::is_ponder());
    }
    next_poll = NODES_BETWEEN_POLLS;
    stop_all = result;
    return result;
}

/**
 * Do a null move
 */
void search_t::forward() {

    skip_null = true;
    stack->current_move.clear();
    stack++;
    stack->in_check = false;
    stack->eval_result = score::INVALID;
    brd.forward();
    assert(stack == &_stack[brd.ply]);
}

/**
 * Undo a null move
 */
void search_t::backward() {

    stack--;
    brd.backward();
    skip_null = false;
    assert(stack == &_stack[brd.ply]);
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
    assert(stack == &_stack[brd.ply]);
}

/**
 * Undo a move
 * @param move
 */
void search_t::backward(move_t * move) {

    stack--;
    brd.backward(move);
    assert(stack == &_stack[brd.ply]);
}

/**
 * Test if the search is pondering
 * @return bool true if pondering 
 */
bool search_t::pondering() {

    return game->ponder || engine::is_ponder();
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
    root.move_count = 0;
    root.moves[0].move.clear();
    root.fifty_count = brd.stack->fifty_count;
    rep_table::store(brd.stack->fifty_count, brd.stack->hash_code);
    int tt_move, tt_flags, tt_score;
    trans_table::retrieve(brd.stack->hash_code, 0, 0, tt_score, tt_move, tt_flags);
    for (move_t * move = move::first(this, 1);
            move; move = move::next(this, 1)) {
        root_move_t * rmove = &root.moves[root.move_count++];
        rmove->init(move, brd.gives_check(move), brd.see(move));
        if (rmove->gives_check) {
            rmove->checker_sq = (brd.stack + 1)->checker_sq;
            rmove->checkers = (brd.stack + 1)->checkers;
        }
    }
    root.in_check = brd.in_check();
    stack->hash_code = brd.stack->hash_code;
    stack->eval_result = evaluate(this);
    stack->best_move.set(&root.moves[0].move);
    return root.move_count;
}

/*
 * Sort algorithm for root moves (simple insertion sort)
 */
void root_t::sort_moves(move_t * best_move) {
    for (int j = 1; j < move_count; j++) {
        root_move_t rmove = moves[j];
        int i = j - 1;
        while (i >= 0 && rmove.compare(&moves[i], best_move) > 0) {
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
 * - Sending new PVs asap to the interface
 * - Sort order based on pv and amount of nodes of the subtrees
 * - No pruning, reductions, extensions and hash table lookup/store
 */
int search_t::pvs_root(int alpha, int beta, int depth) {

    assert(root.move_count > 0);
    int new_depth = depth - 1;
    int best = -score::INF;

    /*
    std::cout << "\npvs root (alpha, beta, depth) = (" << alpha << "," << beta << ", " << depth << ") " << std::endl;
    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        std::cout << rmove->move.to_string()
                << " n: " << rmove->nodes
                << ";  \n";
    }
    std::cout << std::endl;
     */

    /*
     * Moves loop
     */
    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        int nodes_before = nodes;
        forward(&rmove->move, rmove->gives_check);
        if (rmove->gives_check) {
            brd.stack->checker_sq = rmove->checker_sq;
            brd.stack->checkers = rmove->checkers;
        }
        int score;
        if (i > 0) {
            score = -pvs(-alpha - 1, -alpha, new_depth);
        }
        if (i == 0 || score > alpha) {
            score = -pvs(-beta, -alpha, new_depth);
        }
        backward(&rmove->move);
        rmove->nodes += nodes - nodes_before;
        if (stop_all) {
            return alpha;
        }
        if (score > best) {
            best = score;
            result_score = score;
            rmove->nodes += i;
            stack->best_move.set(&rmove->move);
            bool exact = score::flags(score, alpha, beta) == score::EXACT;
            if (exact || false == rmove->move.equals(&stack->pv_moves[0])) {
                update_pv(&rmove->move);
            }
            uci::send_pv(best, depth, sel_depth, nodes + pruned_nodes, game->tm.elapsed(),
                    pv_to_string().c_str(), score::flags(best, alpha, beta));
            if (!exact) { //adjust asp. window
                return score;
            }
            assert(alpha < best);
            alpha = best;
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
        if (brd.min_gain(move) >= 0 || brd.see(move) >= 0) {
            return 1;
        }
        return 0;
    }
    return 0;
}

bool search_t::is_draw() {
    if (brd.stack->fifty_count == 0 && brd.is_draw()) { //draw by no mating material
        return true;
    } else if (brd.stack->fifty_count > 3) {
        if (brd.stack->fifty_count >= (100 + stack->in_check)) {
            return true; //50 reversible moves
        }
        int stop_ply = brd.ply - brd.stack->fifty_count;
        for (int ply = brd.ply - 4; ply >= stop_ply; ply -= 2) { //draw by repetition
            if (ply >= 0 && get_stack(ply)->hash_code == brd.stack->hash_code) {
                return true;
            } else if (ply < 0 && rep_table::retrieve(root.fifty_count + ply) == brd.stack->hash_code) {
                return true;
            }
        }
    }
    assert(brd.stack->fifty_count > 0 || brd.is_draw() == false);
    return false;
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
        return qsearch(alpha, beta, 0);
    }

    /*
     * 2. Stop conditions
     */

    //time 
    nodes++;
    if (abort()) {
        return alpha;
    }

    //ceiling
    if (brd.ply > sel_depth) {
        sel_depth = brd.ply;
        if (brd.ply >= (MAX_PLY - 1)) {
            return evaluate(this);
        }
    }

    assert(depth >= 1 && depth <= MAX_PLY);

    //mate distance pruning (if mate(d) in n: don't search deeper)
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

    //draw by lack of material or fifty quiet moves
    if (is_draw()) {
        return draw_score();
    }

    /*
     * 4. Transposition table lookup
     */
    int tt_move = 0, tt_flag, tt_score;
    if (trans_table::retrieve(brd.stack->hash_code, brd.ply, depth, tt_score, tt_move, tt_flag)) {
        if ((tt_flag == score::LOWERBOUND && tt_score >= beta)
                || (tt_flag == score::UPPERBOUND && tt_score <= alpha)
                || tt_flag == score::EXACT) {
            return tt_score;
        }
    }
    stack->tt_move.set(tt_move);
    assert(tt_move == 0 || brd.valid(&stack->tt_move));

    /*
     * 5. Pruning: Fail-high and Nullmove
     */
    int eval = evaluate(this);
    bool in_check = stack->in_check;

    //a) Fail-high pruning (return if static evaluation score is already much better than beta)
    if (!in_check
            && depth <= 4
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
            && depth > 1
            && eval >= beta
            && beta > -score::DEEPEST_MATE
            && brd.has_pieces(brd.stack->wtm)) {
        int rdepth = depth - 4;
        rdepth = MAX(rdepth, 0);
        forward();
        int null_score = -pvs(-beta, -alpha, rdepth);
        backward();
        if (null_score >= beta) {
            if (null_score > score::DEEPEST_MATE) {
                return beta; // not return unproven mate scores
            }
            trans_table::store(brd.stack->hash_code, brd.root_ply, brd.ply, depth, null_score, 0, score::LOWERBOUND);
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
    bool pv = alpha + 1 < beta;
    if (pv && depth > 3 && tt_move == 0) {
        skip_null = true;
        stack->best_move.clear();
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
    move_t * first_move = move::first(this, depth);
    if (!first_move) { //no legal move: it's checkmate or stalemate
        return in_check ? -score::MATE + brd.ply : draw_score();
    }
    int gives_check = brd.gives_check(first_move);
    int extend = extend_move(first_move, gives_check);
    stack->best_move.set(first_move);
    forward(first_move, gives_check);
    int best = -pvs(-beta, -alpha, depth - 1 + extend);
    backward(first_move);
    if (best > alpha) {
        if (best >= beta) {
            trans_table::store(brd.stack->hash_code, brd.root_ply, brd.ply, depth, best, first_move->to_int(), score::LOWERBOUND);
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
    int max_reduce = MAX(0, depth - 2);
    while (move_t * move = move::next(this, depth)) {
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
                && depth <= 4
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
            if (brd.min_gain(move) < 0 && brd.see(move) < 0) {
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
        int score = -pvs(-alpha - 1, -alpha, depth - 1 - reduce + extend);
        if (score > alpha && reduce > 0) {
            //research without reductions
            score = -pvs(-alpha - 1, -alpha, depth - 1 + extend);
        }
        if (pv && score > alpha) {
            //full window research
            score = -pvs(-beta, -alpha, depth - 1 + extend);
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
                trans_table::store(brd.stack->hash_code, brd.root_ply, brd.ply, depth, score, move->to_int(), score::LOWERBOUND);
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
    trans_table::store(brd.stack->hash_code, brd.root_ply, brd.ply, depth, best, stack->best_move.to_int(), flag);

    return best;
}

/*
 * Quiescence search
 */
int search_t::qsearch(int alpha, int beta, int depth) {

    /*
     * 1. Stop conditions
     */

    //time 
    nodes++;
    if (abort()) {
        return alpha;
    }

    //ceiling
    if (brd.ply >= (MAX_PLY - 1)) {
        return evaluate(this);
    }

    assert(depth <= 0);

    //mate distance pruning (if mate(d) in n: don't search deeper)
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

    //draw by lack of material or fifty quiet moves
    if (is_draw()) {
        return draw_score();
    }

    const int eval = evaluate(this);
    const bool in_check = stack->in_check;

    //stand-pat: return if eval is "good enough"
    if (eval >= beta && !in_check) {
        return eval;
    }

    //get first move; if there's none it's mate, stalemate, or there are no captures/promotions
    move_t * move = move::first(this, depth);
    if (!move) {
        if (in_check) {
            return -score::MATE + brd.ply;
        } else if (depth == 0) {
            return draw_score();
        } else {
            return eval;
        }
    }

    /*
     * 2. Moves loop
     */

    //prepare moves loop
    if (eval > alpha && !in_check) {
        alpha = eval;
    }
    stack->hash_code = brd.stack->hash_code;
    const int delta = depth ? FUTILITY_MARGIN : QS_DELTA;

    //do the loop 
    do {
        
        /*
         * 3. Move pruning
         */
        
        int gives_check = brd.gives_check(move);
        bool dangerous = in_check || move->capture || move->promotion || move->castle
                || is_dangerous_check(move, gives_check);

        //prune all quiet moves
        if (!dangerous) {
            pruned_nodes++;
            continue;
        }
        
        //delta pruning
        bool skip_prune = in_check || gives_check;
        if (!skip_prune && eval + delta + brd.max_gain(move) <= alpha) {
            pruned_nodes++;
            continue;
        }
        
        //SEE pruning
        if (!skip_prune && brd.min_gain(move) < 0 && brd.see(move) < 0) {
            pruned_nodes++;
            continue;
        }
        
        /*
         * Go forward and search next node
         */
 
        forward(move, gives_check);
        int score = -qsearch(-beta, -alpha, depth - 1);
        backward(move);
        
        /*
         * Handle results: beta cutoff or improved alpha
         */
        if (score >= beta) {
            stack->best_move.set(move);
            return score;
        } else if (score > alpha) {
            stack->best_move.set(move);
            alpha = score;
        }
    } while ((move = move::next(this, depth)));

    return alpha;
}

bool search_t::is_dangerous_check(move_t * const move, const int gives_check) {
    if (gives_check == 0) {
        return false;
    } else if (gives_check > 1) {
        return true;
    } else {
        assert(gives_check == 1);
        return brd.min_gain(move) >= 0 || brd.see(move) >= 0;
    }
}

void search_t::debug_print_search(int alpha, int beta) {
    std::cout << "print search (" << alpha << ", " << beta << "): " << std::endl;

    board_t brd2;
    memcpy(&brd2, &brd, sizeof (board_t));
    while (brd2.ply > 0) {
        move_t * move = &get_stack(brd2.ply - 1)->current_move;
        move->piece ? brd2.backward(move) : brd2.backward();
    }

    std::cout << "ROOT FEN: " << brd2.to_string() << std::endl;
    std::cout << "Path ";
    for (int i = 0; i < brd.ply; i++) {

        std::cout << " " << get_stack(i)->current_move.to_string();
    }
    std::cout << "\nFEN: " << brd.to_string() << std::endl;
    std::cout << "Hash: " << brd.stack->hash_code << std::endl;
    std::cout << "nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << skip_null << std::endl;

    std::cout << std::endl;
    exit(0);
}

/*
 * Helper function to reset the search stack. 
 * This is used for self-playing games, where  
 * MAX_PLY is not sufficient to hold all moves of a chess game-> 
 * Note: in UCI mode this is not an issue, because each position
 * starts with a new stack. 
 */
void search_t::reset_stack() {
    brd.ply = 0;
    stack = root_stack;
    stack->eval_result = score::INVALID;
    brd._stack[0].copy(brd.stack);
    brd.stack = &brd._stack[0];
    nodes = 0;
    pruned_nodes = 0;
    stack->pv_count = 0;
}