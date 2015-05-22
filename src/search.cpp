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
 * File: search.cpp
 * Search methods for traditional chess. 
 * Maxima uses a fail-soft principle variation search (PVS) with
 * - aspiration windows
 * - null move pruning 
 * - futility pruning
 * - late move reductions (LMR)
 * - check extensions
 * 
 */

#include "search.h"
#include "engine.h"
#include "eval.h"
#include "timeman.h"
#include "eval_material.h"

#define __ENABLE_SEARCH_TRACE

#ifdef ENABLE_SEARCH_TRACE

void trace(search_t * s, move_t * move, int score, int alpha, int beta, int depth, std::string txt) {
    const int f_len = 3;
    const std::string filter[f_len] = { "d7h3", "g2h3", "e8d7" };
    std::string path = "";
    for (int i = 0; i < s->brd.ply; i++) {
        std::string m_str = s->get_stack(i)->current_move.to_string();
        if (i < f_len && filter[i] != m_str) {
            return;
        }
        path += m_str + " ";
    }
    std::cout << path;
    if (move) {
        std::cout << move->to_string() << " ";
    }
    std::cout << "[d" << depth << " ";
    std::cout << score << " (" << alpha << "," << beta << ")] " << txt << std::endl;
}
#endif

#ifndef ENABLE_SEARCH_TRACE 
#define trace(s,m,e,a,b,d,t) /* notn */
#endif

namespace LMR {

    const double cutoff_pct[16] = {
        0.8835, 0.0618, 0.0221, 0.0101, 0.0051, 0.0028, 0.0018, 0.0011,
        0.0008, 0.0007, 0.0007, 0.0007, 0.0007, 0.0007, 0.0007, 0.0006
    };

    uint8_t reduction[32][16];

    bool init_done = false;

    int calc_reduction(int d, int m) {
        const double f = 0.01; //higher number: more reductions
        const double df = 0.25; //higher number: more reductions
        double base_red = MIN(1.8, d * df / 2.0);
        double extra_red = d * df;
        double pct = 1.0 - cutoff_pct[m];
        double mul = MAX(0.0, pct - (1.0 - f)) / f;
        return pct * base_red + mul * extra_red + 0.25;
    }

    void init() {
        if (!init_done) {
            for (int d = 0; d < 32; d++) {
                for (int m = 0; m < 16; m++) {
                    reduction[d][m] = calc_reduction(d, m);
                }
            }
            init_done = true;
        }
    }

    void print() {
        std::cout << "move 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16" << std::endl;
        for (int d = 0; d < 32; d++) {
            std::cout << (d < 10 ? "  d" : " d") << d << " ";
            for (int m = 0; m < 16; m++) {
                std::cout << (int) reduction[d][m] << "  ";
            }
            std::cout << std::endl;
        }
    }

    int reduce(int d, int m) {
        return reduction[MIN(d, 31)][MIN(m, 15)];
    }
}

/**
 * Initialize sort values for root move
 * @param m move
 * @param val initial value
 * @param checks if the move checks the opponent
 * @param see_value static exchange value
 */
void root_move_t::init(move_t * m, bool checks, int see_value) {
    move.set(m);
    gives_check = checks;
    see = see_value;
    nodes = see / 100;
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
 * Initializes the object, preparing to start a new search
 * @param fen string representing the board position
 */
void search_t::init(const char * fen, game_t * g) {
    LMR::init();
    //LMR::print();
    PST::init();
    brd.init(fen);
    game = g ? g : game::instance();
    game->init_tm(brd.us());
    wild = 0;
    book_name = "book.bin";
    king_attack_shelter = options::get_value("KingAttackShelter");
    king_attack_pieces = options::get_value("KingAttackPieces");
    null_adaptive_depth = options::get_value("NullAdaptiveDepth");
    null_adaptive_value = options::get_value("NullAdaptiveValue");
    alpha_pruning = options::get_value("AlphaPruning");
    beta_pruning = options::get_value("BetaPruning");
    null_verify = options::get_value("NullVerify");
    null_enabled = options::get_value("NullMove");
    lmr_enabled = options::get_value("LMR");
    ffp_enabled = options::get_value("FutilityPruning");
    lmp_enabled = options::get_value("LateMovePruning");
    pv_extensions = options::get_value("PVExtensions");
    ponder_move.clear();
    nodes = 0;
    pruned_nodes = 0;
    stop_all = false;
    skip_null = false;
    next_poll = 0;
    sel_depth = 0;
    root_stack = stack = &_stack[0];
    result_score = 0;
    memset(_stack, 0, sizeof (_stack));
    memset(history, 0, sizeof (history));
    stack->eval_result = score::INVALID;
}

/**
 * Lookup current position in polyglot book database (file book.bin)
 */
bool search_t::book_lookup() {
    bool result = false;
    book::open(book_name);
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
        uci::send_pv(0, 1, 1, 1, game->tm.elapsed(),
                stack->best_move.to_string().c_str(), score::EXACT);
    } else if (init_root_moves() > 0) { //do id search
        iterative_deepening();
    }
    uci::send_bestmove(stack->best_move, ponder_move);
}

/**
 * Iterative deepening - call aspiration search iterating over the depth. 
 * For timed searched, the function decides if a new iteration should be started 
 * or not.
 */
void search_t::iterative_deepening() {
    int last_score = -score::INF;
    const int max_time = game->tm.reserved_max();
    const int min_time = game->tm.reserved_min();
    bool timed_search = game->white_time || game->black_time;
    int depth;
    for (depth = 1; depth <= game->max_depth; depth++) {
        int score = aspiration(depth, last_score);
        if (abort(true)) {
            break;
        }
        store_pv();
        if (timed_search && !pondering()) {
            bool score_jump = depth >= 6 && ((ABS(score - last_score) > 20) || score > score::WIN);
            int elapsed = game->tm.elapsed();
            if (root.move_count <= 1 && (depth >= 8 || elapsed > min_time / 8)) {
                //one legal move, still search a bit to get a ponder move
                break;
            } else if (elapsed > max_time / 2) {
                //complex position, maximum (emergency) time control
                break;
            } else if (elapsed > min_time / 2 && !score_jump && root.is_easy()) {
                //easy position, half time control
                break;
            } else if (elapsed > min_time && !score_jump && !root.is_complex()) {
                //neutral position, normal time control
                break;
            } else if (score::mate_in_ply(score) && depth > score::mate_in_ply(score)) {
                //mate in N and search depth > N
                break;
            }
        } else if (depth >= 15 && game->target_score && score >= game->target_score &&
                game->target_move.piece && game->target_move.equals(&stack->best_move)) {
            break;
        }
        last_score = score;
    }
    if (stack->pv_count > 0) {
        uci::send_pv(result_score, MIN(depth, game->max_depth), sel_depth,
                nodes + pruned_nodes, game->tm.elapsed(), pv_to_string().c_str(), score::EXACT);
        if (stack->pv_count > 1) {
            ponder_move.set(&stack->pv_moves[1]);
        }
    }
}

/**
 * Aspiration window search: call search with a small alpha, beta window and 
 * gradually increase the window in case of a fail high or fail low
 * @param depth depth search depth
 * @param last_score score of last iteration
 * @return search result score
 */
int search_t::aspiration(int depth, int last_score) {
    if (depth >= 6 && !score::is_win(last_score)) {
        for (int window = 20; window < 900; window *= 2) {
            int alpha = last_score - window;
            int beta = last_score + window;
            int score = pvs_root(alpha, beta, depth);
            if (stop_all) {
                return score;
            } else if (score > alpha && score < beta) {
                return score;
            } else if (score::is_win(score)) {
                break;
            }
            last_score = score;
        }
    }
    return pvs_root(-score::INF, score::INF, depth);
}

/**
 * Poll to test is the search should be aborted
 */
bool search_t::abort(bool force_poll = false) {
    static const int NODES_BETWEEN_POLLS = 5000;
    bool result = false;
    if (game->max_nodes > 0 && nodes >= game->max_nodes) {
        result = true;
    } else if (stop_all || engine::is_stopped()) {
        result = true;
    } else if (force_poll || --next_poll <= 0) {
        next_poll = NODES_BETWEEN_POLLS;
        result = !pondering() && game->tm.time_is_up() && root_stack->best_move.piece > 0;
    }
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
    return game->ponder && engine::is_ponder();
}

/**
 * Converts principle variation to a string
 * @return pv string
 */
std::string search_t::pv_to_string() {
    std::string result = "";
    board_t b;
    b.init(brd.to_string().c_str());

    //retrieve pv from stack
    for (int i = 0; i < stack->pv_count; i++) {
        result += stack->pv_moves[i].to_string() + " ";
        b.forward(&stack->pv_moves[i]);
    }

    //retrieve extra moves from hash if the pv is short
    if (stack->pv_count < 8) {
        int tt_move = 0, tt_flags, tt_score;
        move_t m;
        for (int i = 0; i < 8; i++) {
            trans_table::retrieve(b.stack->tt_key, 0, 0, tt_score, tt_move, tt_flags);
            if (tt_move == 0) {
                break;
            }
            m.set(tt_move);
            result += m.to_string() + " ";
            b.forward(&m);
        }
    }

    return result;
}

/**
 * Writes principle variation in the hash table. Called after every iteration
 * to make sure pv moves are searched again first
 */
void search_t::store_pv() {
    board_t b;
    b.init(brd.to_string().c_str());
    int tt_move = 0, tt_flags, tt_score;
    move_t m;
    for (int i = 0; i < stack->pv_count; i++) {
        if (i > 0) {
            trans_table::retrieve(b.stack->tt_key, 0, 0, tt_score, tt_move, tt_flags);
            m.set(tt_move);
            if (m.equals(&stack->pv_moves[i]) == false) {
                trans_table::store(b.stack->tt_key, 0, 0, 1, 0, m.to_int(), 0);
            }
        }
        b.forward(&stack->pv_moves[i]);
    }
}

/**
 * Update history sort scores for quiet moves
 * @param move a quiet move that caused a beta cutoff
 * @param depth current search depth
 */
void search_t::update_history(move_t * move) {
    assert(move->capture == EMPTY);
    assert(move->promotion == EMPTY);
    assert(move->castle == EMPTY);
    const int HISTORY_MAX = 2000;
    const int HISTORY_DIV = 64;
    int * record = &history[move->piece][move->tsq];
    *record += (HISTORY_MAX - *record) / HISTORY_DIV;
    assert(*record > 0);
    assert(*record < 2 * HISTORY_MAX);
}

/**
 * Updates killer moves, making sure they're not equal
 * @param move a quiet move that caused a beta cutoff
 */
void search_t::update_killers(move_t * move) {
    assert(move->capture == EMPTY);
    assert(move->promotion == EMPTY);
    assert(move->castle == EMPTY);
    if (stack->killer[0].equals(move)) {
        return;
    }
    //note: address(&stack->killer[1]) can be equal to address(move)
    move_t t;
    t.set(move);
    stack->killer[1].set(&stack->killer[0]);
    stack->killer[0].set(&t);
    assert(stack->killer[0].equals(&stack->killer[1]) == false);
}

/**
 * Initialize moves for the root position
 * @return int amount of legal moves
 */
int search_t::init_root_moves() {
    root.move_count = 0;
    root.moves[0].move.clear();
    root.fifty_count = brd.stack->fifty_count;
    rep_table::store(brd.stack->fifty_count, brd.stack->tt_key);
    int tt_move = 0, tt_flags, tt_score;
    trans_table::retrieve(brd.stack->tt_key, 0, 0, tt_score, tt_move, tt_flags);
    stack->tt_move.set(tt_move);
    for (move_t * move = move::first(this, 1);
            move; move = move::next(this, 1)) {
        root_move_t * rmove = &root.moves[root.move_count++];
        rmove->init(move, brd.gives_check(move), brd.see(move));
    }
    root.in_check = brd.in_check();
    stack->tt_key = brd.stack->tt_key;
    stack->best_move.set(0);
    return root.move_count;
}

/**
 * Algorithm to guess the complexity of the position based on amount
 * of nodes searched
 */
bool root_t::is_complex() {
    const int n0 = moves[0].nodes;
    for (int i = 1; i < move_count; i++) {
        if (moves[i].nodes >= n0) {
            return true;
        }
    }
    return false;
}

/**
 * Algorithm to guess the complexity of the position based on amount
 * of nodes searched
 */
bool root_t::is_easy() {
    const int n0 = moves[0].nodes / 8;
    for (int i = 1; i < move_count; i++) {
        if (moves[i].nodes >= n0) {
            return false;
        }
    }
    return true;
}

/**
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

/**
 * Principle Variation Search (root node)
 * Difference with normal (non-root) search:
 * - Sending new PVs asap to the interface
 * - Sort order based on pv and amount of nodes of the subtrees
 * - No pruning, reductions, extensions and hash table lookup/store
 * - Compatible with all supported wild variants
 */
int search_t::pvs_root(int alpha, int beta, int depth) {

    assert(root.move_count > 0);
    int best = -score::INF;
    root.sort_moves(&stack->best_move);
    //trace_root(alpha, beta, depth);

    /*
     * Moves loop
     */

    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        move_t * move = &rmove->move;
        int nodes_before = nodes;
        int extend = rmove->gives_check;
        int score = 0;
        forward(move, rmove->gives_check);
        if (i > 0) {
            score = -pvs(-alpha - 1, -alpha, depth - 1 + extend);
        }
        if (i == 0 || score > alpha) {
            score = -pvs(-beta, -alpha, depth - 1 + extend);
        }
        backward(move);
        rmove->nodes += nodes - nodes_before;
        if (stop_all) {
            return alpha;
        } else if (score > best) {
            best = score;
            result_score = score;
            rmove->nodes += i;
            stack->best_move.set(move);
            bool exact = score::flags(score, alpha, beta) == score::EXACT;
            if (exact || false == move->equals(&stack->pv_moves[0])) {
                update_pv(&rmove->move);
            }
            uci::send_pv(best, depth, sel_depth, nodes + pruned_nodes, game->tm.elapsed(),
                    pv_to_string().c_str(), score::flags(best, alpha, beta));
            if (!exact) { //adjust asp. window
                trace(this, move, score, alpha, beta, depth, "pvs_root cutoff");
                return score;
            }
            assert(alpha < best);
            alpha = best;
        }
    }
    trace(this, &stack->best_move, best, alpha, beta, depth, "pvs_root result");
    return best;
}

/**
 * Verifies is a position is drawn by 
 * a) lack of material
 * b) fifty quiet moves
 * c) repetition
 * @return true if it's an official draw
 */
bool search_t::is_draw() {
    if (brd.is_draw()) { //draw by no mating material
        return true;
    } else if (brd.stack->fifty_count > 3) {
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
 * Returns dynamic eval margin based on positional score
 * @return 
 */
int search_t::eval_mg() {
    if (stack->in_check) {
        return 0;
    }
    assert(stack->eval_result != score::INVALID);
    const int phase = stack->mt->phase;
    return ABS(stack->pc_score[WKING].get(phase))
            + ABS(stack->passer_score[WHITE].get(phase))
            + ABS(stack->pc_score[BKING].get(phase))
            + ABS(stack->passer_score[BLACK].get(phase));
}

/**
 * Principle Variation Search (fail-soft)
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth remaining search depth
 * @return score for the current node
 */
int search_t::pvs(int alpha, int beta, int depth) {

    assert(alpha < beta);
    assert(alpha >= -score::INF);
    assert(beta <= score::INF);

    stack->pv_count = 0;
    sel_depth = MAX(brd.ply, sel_depth);
    stack->best_move.clear();

    /*
     * If no more depth remaining, return quiescence value
     */

    if (depth < 1) {
        const int score = qsearch(alpha, beta, 0);
        trace(this, NULL, score, alpha, beta, depth, "qsearch result");
        return score;
    }

    /*
     * Stop conditions
     */

    //time 
    nodes++;
    if (abort()) {
        trace(this, NULL, alpha, alpha, beta, depth, "abort");
        return alpha;
    }

    //ceiling
    if (brd.ply >= (MAX_PLY - 1)) {
        trace(this, NULL, evaluate(this), alpha, beta, depth, "ceiling");
        return evaluate(this);
    }

    assert(depth > 0 && depth <= MAX_PLY);
    int alpha1 = alpha;

    //mate distance pruning: if mate(d) in n don't search deeper
    if ((score::MATE - brd.ply) < beta) {
        beta = score::MATE - brd.ply;
        if (alpha >= beta) {
            trace(this, NULL, beta, alpha, beta, depth, "mate distance");
            return beta;
        }
    }
    if ((-score::MATE + brd.ply) > alpha) {
        alpha = -score::MATE + brd.ply;
        if (beta <= alpha) {
            trace(this, NULL, alpha, alpha, beta, depth, "mate distance");
            return alpha;
        }
    }

    //draw by lack of material or fifty quiet moves
    if (is_draw()) {
        trace(this, NULL, draw_score(), alpha, beta, depth, "trivial draw");
        return draw_score();
    }

    /*
     * Transposition table lookup
     */

    const bool pv = alpha + 1 < beta;
    stack->tt_key = brd.stack->tt_key; //needed for testing repetitions
    int tt_move = 0, tt_flag = 0, tt_score;
    if (trans_table::retrieve(stack->tt_key, brd.ply, depth, tt_score, tt_move, tt_flag)) {
        if (pv && tt_flag == score::EXACT) {
            trace(this, NULL, tt_score, alpha, beta, depth, "tt: exact");
            return tt_score;
        } else if (!pv && tt_score >= beta && tt_flag == score::LOWERBOUND) {
            trace(this, NULL, tt_score, alpha, beta, depth, "tt: lowerbound");
            return tt_score;
        } else if (!pv && tt_score <= alpha && tt_flag == score::UPPERBOUND) {
            trace(this, NULL, tt_score, alpha, beta, depth, "tt: upperbound");
            return tt_score;
        }
    }
    stack->tt_move.set(tt_move);

    /*
     * Node pruning
     */

    const bool in_check = stack->in_check;
    const int eval = evaluate(this);
    const bool do_prune_node = !in_check && !skip_null && !pv && alpha < score::WIN && beta > -score::WIN && brd.has_pieces(brd.us());

    //fail low (alpha) pruning: razoring
    const int mg = 150 + eval_mg() + 50 * depth;
    if (do_prune_node && eval + mg < alpha && depth < 4 && alpha_pruning) {
        const int delta = beta - mg;
        int razor_value = qsearch(delta - 1, delta, 0);
        if (razor_value < delta) {
            trace(this, NULL, razor_value, alpha, beta, depth, "alpha pruned");
            return razor_value;
        }
    }

    //fail high (beta) pruning
    if (do_prune_node && eval - mg > beta && depth < 4 && beta_pruning) {
        trace(this, NULL, eval - mg, alpha, beta, depth, "beta pruned");
        return eval - mg;
    }

    //null move pruning
    if (do_prune_node && eval >= beta && depth > 1 && null_enabled) {
        int R = 3;
        if (depth >= 7 && null_adaptive_depth) {
            R += depth / 7;
        }
        if (depth > R && (eval - beta) >= 100 && null_adaptive_value) {
            R += MIN((eval - beta) / 100, 3);
        }
        forward();
        int null_score = -pvs(-beta, -alpha, depth - 1 - R);
        backward();
        if (stop_all) {
            trace(this, NULL, alpha, alpha, beta, depth, "stopped");
            return alpha;
        } else if (null_score >= beta) {
            const int RV = 5;
            if (null_verify && depth > RV /*&& material::is_eg(this)*/) {
                //verification
                skip_null = true;
                int verified_score = pvs(alpha, beta, depth - 1 - RV);
                skip_null = false;
                if (verified_score >= beta) {
                    trace(this, NULL, verified_score, alpha, beta, depth, "null move pruned (verified)");
                    return verified_score;
                }
            } else {
                //no verification
                trace(this, NULL, null_score, alpha, beta, depth, "null move pruned");
                return null_score;
            }
        }
    }

    /*
     * Internal iterative deepening (IID)
     */

    if (depth >= 6 && tt_move == 0) {
        skip_null = pv;
        int R = pv ? 2 : 4;
        int iid_score = pvs(alpha, beta, depth - R);
        if (score::is_mate(iid_score)) {
            trace(this, NULL, iid_score, alpha, beta, depth, "iid mate score");
            return iid_score;
        } else if (stack->best_move.piece) {
            stack->tt_move.set(&stack->best_move);
        }
    }

    /*
     * Moves loop
     */

    //if there is no first move, it's checkmate or stalemate
    move_t * move = move::first(this, depth);
    if (!move) {
        trace(this, NULL, in_check ? -score::MATE + brd.ply : draw_score(), alpha, beta, depth, "(stale)mate");
        return in_check ? -score::MATE + brd.ply : draw_score();
    }

    //prepare and do the loop
    skip_null = false;
    int best = -score::INF;
    int searched_moves = 0;
    const int score_max = score::MATE - brd.ply - 1;
    const bool do_ffp = !pv && depth < 8 && eval + 40 * (depth + 1) <= alpha && ffp_enabled;
    const bool do_lmp = !pv && depth < 4 && eval + 20 * (depth + 1) <= alpha && lmp_enabled;
    stack->best_move.clear();
    do {

        assert(brd.valid(move) && brd.legal(move));
        assert(stack->best_move.equals(move) == false);
        assert(in_searched(move, searched_moves) == false);

        const int gives_check = brd.gives_check(move);
        assert(gives_check == 0 || gives_check == 1 || gives_check == 2);

        /*
         * Move pruning: skip all futile moves
         */

        const bool is_quiet_stage = stack->move_list.stage > QUIET_MOVES && searched_moves > 0;
        const bool is_dangerous = !is_quiet_stage || in_check || gives_check || is_passed_pawn(move);
        const bool do_prune = !is_dangerous && searched_moves > 1 && best > -score::DEEPEST_MATE;

        //prune futile quiet moves (forward futility pruning, FFP)
        if (do_prune && do_ffp) {
            pruned_nodes++;
            continue;
        }

        //prune late quiet moves (late move pruning, LMP)
        if (do_prune && do_lmp && searched_moves >= 4 + (2 * depth)) {
            pruned_nodes++;
            continue;
        }

        /*
         * Move extensions
         */

        int extend = 0;
        if (gives_check > 1) {
            extend = 1;
        } else if (gives_check > 0 && (depth < 4 || pv || brd.see(move) >= 0)) {
            extend = 1;
        } else if (pv && depth < 4 && pv_extensions && brd.is_gain(move)) {
            extend = 1;
        } else if (pv && !in_check && pv_extensions && depth < 4 && !move->promotion && is_passed_pawn(move)) {
            extend = 1;
        }

        /*
         * Move Reductions (Late Move Reductions, LMR) 
         */

        int reduce = 0;
        const bool do_reduce = depth > 1 && is_quiet_stage;
        if (do_reduce && lmr_enabled) {
            reduce = LMR::reduce(depth, searched_moves);
            if (reduce > 1 && is_dangerous) {
                reduce = 1;
            }
            assert((depth - reduce) >= 1);
        }

        /*
         * Go forward and search next node
         */

        forward(move, gives_check);
        int score;
        if (searched_moves == 0) {
            score = -pvs(-beta, -alpha, depth - 1 + extend);
        } else {
            score = -pvs(-alpha - 1, -alpha, depth - 1 - reduce + extend);
            if (score > alpha && reduce > 0) {
                //research without reductions
                score = -pvs(-alpha - 1, -alpha, depth - 1 + extend);
            }
            if (pv && score > alpha) {
                //full window research
                score = -pvs(-beta, -alpha, depth - 1 + extend);
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
                trans_table::store(stack->tt_key, brd.root_ply, brd.ply, depth, score, move->to_int(), score::LOWERBOUND);
                if (!move->capture && !move->promotion && !move->castle) {
                    update_killers(move);
                    update_history(move);
                    for (int i = 0; i < searched_moves; i++) {
                        move_t * m = &stack->searched[i];
                        if (!m->capture && !m->promotion && !m->castle) {
                            history[m->piece][m->tsq] >>= searched_moves;
                        }
                    }
                }
                trace(this, move, score, alpha, beta, depth, "beta cutoff");
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
    } while ((move = move::next(this, depth)));

    /*
     * Store the result in the hash table and return
     */

    assert(!stop_all);
    assert(best > -score::INF);
    assert(best < beta);
    assert(brd.valid(&stack->best_move));
    assert(brd.legal(&stack->best_move));

    int flag = score::flags(best, alpha1, beta);
    trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, best, stack->best_move.to_int(), flag);
    trace(this, &stack->best_move, best, alpha, beta, depth, "pvs result");
    return best;
}

/**
 * Quiescence search with tactical moves only.
 * At depth 0: captures, promotions and quiet checks
 * At depth < 0: captures and promotions
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth depth (0 or lower)
 * @return score
 */
int search_t::qsearch(int alpha, int beta, int depth) {

    assert(depth <= 0);
    assert(alpha < beta);
    assert(alpha >= -score::INF);
    assert(beta <= score::INF);

    /*
     * Stop conditions
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

    //stand-pat: return if eval is already good enough
    const int eval = evaluate(this);
    const bool in_check = stack->in_check;
    if (eval >= beta && !in_check) {
        return eval;
    }

    //get first move; if there's none it's mate, stalemate, or there are no captures/promotions
    stack->tt_move.clear();
    move_t * move = move::first(this, depth);
    if (!move) {
        if (in_check) {
            return -score::MATE + brd.ply;
        }
        return depth == 0 ? draw_score() : eval;
    }
    if (eval > alpha && !in_check) {
        alpha = eval;
    }

    /*
     * Moves loop
     */

    const int fbase = eval + 50;
    const bool is_eg = !in_check && material::is_eg(this);
    do {

        /*
         * Move pruning
         */

        int gives_check = brd.gives_check(move);
        bool dangerous = depth < 0 || move->capture || in_check || gives_check
                || move->promotion || move->castle;

        //prune all quiet moves 
        if (!dangerous) {
            assert(depth == 0);
            pruned_nodes++;
            continue;
        }

        //prune if the capture or promotion can't raise alpha
        bool do_prune = !in_check && !gives_check && !is_eg;
        if (do_prune && fbase + brd.max_gain(move) <= alpha) {
            pruned_nodes++;
            continue;
        }

        if (do_prune && fbase + brd.see(move) <= alpha) {
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

        if (stop_all) {
            trace(this, NULL, score, alpha, beta, depth, "stop");
            return alpha;
        } else if (score > alpha) {
            stack->best_move.set(move);
            if (score >= beta) {
                return score;
            }
            alpha = score;
        }
    } while ((move = move::next(this, depth)));
    return alpha;
}

/**
 * Tests if a move equals one of the killer moves
 */
bool search_t::is_killer(move_t * const move) {
    return !move->capture && (stack->killer[0].equals(move) || stack->killer[1].equals(move));
}

/**
 * Tests if a move is stored in the searched moves list
 * @param move
 * @param searched_moves
 * @return true is listed, false otherwise
 */
bool search_t::in_searched(move_t* move, int searched_moves) {
    for (int i = 0; i < searched_moves; i++) {
        if (stack->searched[i].equals(move)) {
            return true;
        }
    }
    return false;
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

/**
 * Prints the positon debug info and search path upto the point in the tree where 
 * the function was called and exits the program
 * @param alpha lowerbound value 
 * @param beta upperbound value
 */
void search_t::debug_print_search(int alpha, int beta, int depth) {
    std::cout << "print search (" << alpha << ", " << beta << ", "
            << depth << "): " << std::endl;
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
    std::cout << "Hash: " << brd.stack->tt_key << std::endl;
    std::cout << "nodes: " << nodes << std::endl;
    std::cout << "Skip nullmove: " << skip_null << std::endl;
    std::cout << std::endl;
    exit(0);
}

/**
 * Print debug info on the root search
 */
void search_t::trace_root(int alpha, int beta, int depth) {
    std::cout << "\npvs root (alpha, beta, depth) = (" << alpha << ","
            << beta << ", " << depth << ") " << std::endl;
    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        std::cout << rmove->move.to_string()
                << " n: " << rmove->nodes
                << " h: " << history[rmove->move.piece][rmove->move.tsq]
                << ";  \n";
    }
    std::cout << std::endl;
}