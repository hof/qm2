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
 * - check and pv extensions
 * 
 */

#include "search.h"
#include "engine.h"
#include "eval.h"
#include "timeman.h"
#include "eval_material.h"

/**
 * Initialize sort values for root move
 * @param m move
 * @param val initial value
 * @param checks if the move checks the opponent
 * @param see_value static exchange value
 */
void root_move_t::init(move_t * m, bool checks, bool dangerous, int see_value) {
    move.set(m);
    gives_check = checks;
    is_dangerous = dangerous;
    see = see_value;
    nodes = see / 100 + dangerous; //initial sort order
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
    PST::init();
    brd.init(fen);
    game = g ? g : game::instance();
    game->init_tm(brd.us());
    wild = 0;
    book_name = "book.bin";
    king_attack_shelter = options::get_value("KingAttackShelter");
    king_attack_pieces = options::get_value("KingAttackPieces");
    beta_pruning = options::get_value("BetaPruning");
    null_verify = options::get_value("NullVerify");
    null_enabled = options::get_value("NullMove");
    lmr_enabled = options::get_value("LMR");
    ffp_enabled = options::get_value("FutilityPruning");
    draw_contempt = options::get_value("DrawContempt");
    ponder_move.clear();
    nodes = 0;
    pruned_nodes = 0;
    stop_all = false;
    next_poll = 0;
    sel_depth = 0;
    root_stack = stack = &_stack[0];
    root_wtm = brd.stack->wtm;
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
 * Get a score for every legal move
 */
void search_t::book_calc() {
    const int count = init_root_moves();
    std::string fen = brd.to_string();
    uci::out("book_calc start");
    for (int i = 0; i < count; i++) {
        root_move_t * rmove = &root.moves[i];
        move_t * move = &rmove->move;
        forward(move, rmove->gives_check);
        int score = 0;
        U64 nodes_before = nodes;
        for (int depth = 1; depth <= game->max_depth; depth++) {
            score = -pvs(-score::INF, score::INF, depth - 1);
        }
        backward(move);
        uci::out("book_calc fen " + fen + " move " + move->to_string() + " depth " 
            + uci::itoa(game->max_depth) + " score " + uci::itoa(score) + " nodes " 
            +  uci::itoa(nodes - nodes_before) + " variant " + uci::itoa(wild));
    }
    uci::out("book_calc ready");
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
        if (timed_search) {
            bool score_jump = depth >= 6 && ((ABS(score - last_score) > 20) || score > score::WIN);
            int elapsed = game->tm.elapsed();
            bool do_stop = false;
            if (root.move_count <= 1 && elapsed > min_time / 8) {
                //one legal move, still search a bit to get a ponder move
                do_stop = true;
            } else if (elapsed > max_time / 2) {
                //complex position, maximum (emergency) time control
                do_stop = true;
            } else if (elapsed > min_time / 2 && !score_jump && root.is_easy()) {
                //easy position, half time control
                do_stop = true;
            } else if (elapsed > min_time && !score_jump && !root.is_complex()) {
                //neutral position, normal time control
                do_stop = true;
            } else if (score::mate_in_ply(score) && depth > score::mate_in_ply(score)) {
                //mate in N and search depth > N
                do_stop = true;
            }
            //abort search or prepare pondering to abort early
            if (do_stop && pondering()) {
                game->tm.set_max(0);
            } else if (do_stop) {
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
    if (brd.stack->fifty_count < 100) {
        rep_table::store(brd.stack->fifty_count, brd.stack->tt_key);
    } else {
        //it's a draw
    }
    int tt_move = 0, tt_flags, tt_score;
    trans_table::retrieve(brd.stack->tt_key, 0, 0, tt_score, tt_move, tt_flags);
    stack->tt_move.set(tt_move);
    root.in_check = brd.in_check();
    stack->tt_key = brd.stack->tt_key;
    stack->best_move.set(0);
    for (move_t * move = move::first(this, 1);
            move; move = move::next(this, 1)) {
        int gives_check = brd.gives_check(move);
        bool is_dangerous = root.in_check || gives_check || move->capture || move->promotion || is_passed_pawn(move);
        root_move_t * rmove = &root.moves[root.move_count++];
        rmove->init(move, gives_check, is_dangerous, brd.see(move));
    }
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
 * Move extensions
 * @param move the move
 * @param depth remaining search depth
 * @param pv pv node
 * @param gives_check if the move checks
 * @return 1 for extending, 0 otherwise
 */
int search_t::extension(move_t * move, int gives_check) {
    if (gives_check > 1) {
        return 1;
    } else if (gives_check == 1 && brd.see(move) >= 0) {
        return 1;
    }
    return 0;
}

/**
 * Determine reduction for the current move
 * @param depth remaining search depth
 * @param searched_moves how many moves have been searched in the current node
 * @param is_dangerous if the move is dangerous, e.g. a capture
 * @return amount of plies to reduce
 */
int search_t::reduction(int depth, int searched_moves, bool is_dangerous) {
    if (is_dangerous || searched_moves < 3 || !lmr_enabled) {
        return 0;
    }
    if (searched_moves >= 6) {
        return depth / 3;
    }
    return 1;
}

/**
 * Principle Variation Search (root node)
 * Difference with normal (non-root) search:
 * - Sending new PVs asap to the interface
 * - Sort order based on pv and amount of nodes of the subtrees
 * - No pruning and hash table lookup/store
 * - Compatible with all supported wild variants
 */
int search_t::pvs_root(int alpha, int beta, int depth) {

    assert(root.move_count > 0);
    int best = -score::INF;
    root.sort_moves(&stack->best_move);

    /*
     * Moves loop
     */

    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        move_t * move = &rmove->move;
        int nodes_before = nodes;
        int score = 0;

        //go forward and search one level deeper
        forward(move, rmove->gives_check);
        if (i == 0) {
            score = -pvs(-beta, -alpha, depth - 1);
        } else {
            score = -pvs(-alpha - 1, -alpha, depth - 1);
            if (score > alpha) { //open window research w/o reductions
                score = -pvs(-beta, -alpha, depth - 1);
            }
        }
        backward(move);

        //handle results
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
                return score;
            }
            assert(alpha < best);
            alpha = best;
        }
    }
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
        return score;
    }

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

    assert(depth > 0 && depth <= MAX_PLY);
    int alpha1 = alpha;

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

    //draw by lack of material or fifty quiet moves
    if (is_draw()) {
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
            return tt_score;
        } else if (!pv && tt_score >= beta && tt_flag == score::LOWERBOUND) {
            return tt_score;
        } else if (!pv && tt_score <= alpha && tt_flag == score::UPPERBOUND) {
            return tt_score;
        }
    }
    stack->tt_move.set(tt_move);

    /*
     * Node pruning
     */

    const bool in_check = stack->in_check;
    const int eval = evaluate(this);
    const bool do_prune_node = eval >= beta && !in_check && !pv && !score::is_mate(beta) && brd.has_pieces(brd.us());

    // beta pruning
    if (do_prune_node && depth < 4 && beta_pruning) {
        int bp_score = eval - 50 * depth;
        if (bp_score >= beta) {
            return bp_score;
        }
    }

    //null move pruning
    if (do_prune_node && null_enabled) {
        int R = 3;
        forward();
        int null_score = -pvs(-beta, -alpha, depth - 1 - R);
        backward();
        if (stop_all) {
            return alpha;
        } else if (null_score >= beta) {
            const int RV = 5;
            if (null_verify && depth > RV && material::is_eg(this)) {
                //verification
                int verified_score = pvs(alpha, beta, depth - 1 - RV);
                if (verified_score >= beta) {
                    return verified_score;
                }
            } else {
                //no verification
                return null_score;
            }
        }
    }

    /*
     * Internal iterative deepening (IID)
     */

    if (pv && depth > 2 && tt_move == 0) {
        int iid_score = pvs(alpha, beta, depth - 2);
        if (score::is_mate(iid_score)) {
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
        return in_check ? -score::MATE + brd.ply : draw_score();
    }

    //set futility pruning delta value
    bool do_ffp = false;
    int delta = score::INVALID;
    if (depth <= 8 && !in_check && !score::is_mate(alpha) && !material::is_eg(this) && ffp_enabled) {
        int ffp_score = eval + 40 * depth;
        if (ffp_score <= alpha) {
            do_ffp = true;
            delta = ffp_score + 50;
        }
    }

    //prepare and do the loop
    int best = -score::INF;
    int searched_moves = 0;
    const int score_max = score::MATE - brd.ply - 1;
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

        const bool is_dangerous = in_check || gives_check || move->capture || move->promotion || move->castle || is_advanced_passed_pawn(move);

        bool pruned = false;
        if (do_ffp && searched_moves > 0) {
            pruned = !is_dangerous;
            pruned |= gives_check == 0 && (move->capture || move->promotion) && brd.max_gain(move) + delta <= alpha;
            if (pruned) {
                pruned_nodes++;
                continue;
            }
        }


        /*
         * Move extensions and reductions
         */

        int extend = extension(move, gives_check);
        int reduce = reduction(depth, searched_moves, is_dangerous);

        /*
         * Go forward and search next node
         */

        forward(move, gives_check);
        int score;
        if (searched_moves == 0) {
            score = -pvs(-beta, -alpha, depth - 1 + extend);
        } else {
            score = -pvs(-alpha - 1, -alpha, depth - 1 - reduce + extend);
            if (score > alpha && (pv || reduce > 0)) { //open window research without reductions
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

    const int delta = 50;
    const int fbase = eval + delta;
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
        bool pruned = false;
        if (do_prune) {
            pruned = fbase + brd.max_gain(move) <= alpha;
            pruned |= fbase + brd.see(move) <= alpha;
            if (pruned) {
                pruned_nodes++;
                continue;
            }
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
    std::cout << std::endl;
    exit(0);
}