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
#include <limits>
#include <algorithm>
#include <chrono>
#include <thread>
#include "book.h"
#include "uci_console.h"
#include "eval.h"
#include "timeman.h"
#include "eval_material.h"
#include "eval_endgame.h"
#include "tablebase.h"

namespace {

constexpr U64 game_history_salt = 0x9e3779b97f4a7c15ULL;
constexpr U64 search_history_salt = 0xd1b54a32d192ed03ULL;

U64 mix_context(U64 key) {
    key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
    key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
    return key ^ (key >> 31);
}

}

// Most playable positions have a king escape. Fall back to fresh generation
// without disturbing the active move picker or its exclusions.
bool search_t::has_legal_move(board_t & brd) {
    const int king = KING[brd.us()];
    const int from = brd.get_sq(king);
    U64 targets = KING_MOVES[from] & ~brd.all(brd.us());
    while (targets) {
        const int to = pop(targets);
        move_t move{};
        move.set(king, from, to, brd.matrix[to]);
        if (brd.legal(&move)) return true;
    }
    move::list_t moves;
    for (auto generate : {move::gen_quiet_moves, move::gen_captures,
            move::gen_promotions, move::gen_castles}) {
        moves.clear();
        generate(&brd, &moves);
        for (move_t * move = moves.first; move != moves.last; ++move) {
            if (brd.legal(move)) return true;
        }
    }
    return false;
}

// A multiset fingerprint preserves occurrence counts without requiring the
// same move order. Game history and search history have different draw policies.
void search_t::init_tt_history() {
    stack->tt_history = 0;
    for (U64 key : _position_history) {
        stack->tt_history += mix_context(key ^ game_history_salt);
    }
}

U64 search_t::tt_context() const {
    const U64 node_kind = brd.ply == 0 ? game_history_salt
        : brd.ply < stack->repetition_start ? 0 : search_history_salt;
    const int white_contempt = root_wtm ? draw_contempt : -draw_contempt;
    return stack->tt_history ^ node_kind
        ^ mix_context(static_cast<U64>(white_contempt) ^ 0xa0761d6478bd642fULL)
        ^ (wild == 0 && evaluation_network
            ? mix_context(evaluation_network->fingerprint() ^ 0x48414c464b503032ULL) : 0);
}

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
    return (nodes > m->nodes) - (nodes < m->nodes);
}

// Keep the signed initial SEE ordering bias, then accumulate unsigned visited
// work without narrowing or overflowing the root ordering/complexity counter.
void root_move_t::add_nodes(U64 count) {
    constexpr int64_t limit = std::numeric_limits<int64_t>::max();
    const U64 capacity = static_cast<U64>(limit) - static_cast<U64>(nodes);
    if (count > capacity) {
        nodes = limit;
    } else if (count <= static_cast<U64>(limit)) {
        nodes += static_cast<int64_t>(count);
    } else {
        // A negative initial bias can leave room for a count above INT64_MAX.
        nodes = static_cast<int64_t>(static_cast<U64>(nodes) + count);
    }
}

/**
 * Initializes the object, preparing to start a new search
 * @param fen string representing the board position
 */
void search_t::init(const char * fen, const game_t * g, const search_control_t * control,
        const std::vector<U64> & position_history) {
    const auto started = std::chrono::steady_clock::now();
    PST::init();
    brd.init(fen);
    game = g ? *g : game_t();
    _control = control;
    _position_history = position_history;
    if (_position_history.size() > static_cast<size_t>(brd.stack->fifty_count)) {
        _position_history.erase(_position_history.begin(),
            _position_history.end() - brd.stack->fifty_count);
    }
    game.init_tm(brd.us(), started);
    wild = 0;
    book_name = book::path();
    _own_book = options::get_value("OwnBook") && !options::get_value("UCI_AnalyseMode");
    _experience_depth = options::get_value("ExperienceDepth");
    _experience = experience::snapshot();
    _book_seeded = false;
    king_attack_shelter = options::get_value("KingAttackShelter");
    king_attack_pieces = options::get_value("KingAttackPieces");
    beta_pruning = options::get_value("BetaPruning");
    null_verify = options::get_value("NullVerify");
    null_enabled = options::get_value("NullMove");
    lmr_enabled = options::get_value("LMR");
    ffp_enabled = options::get_value("FutilityPruning");
    draw_contempt = options::get_value("DrawContempt");
    evaluation_network = options::get_value("UseNNUE") ? nnue::snapshot() : nullptr;
    evaluation_accumulators.clear();
    if (evaluation_network) {
        evaluation_accumulators.resize(MAX_PLY + 1);
        evaluation_network->prepare(brd, nullptr, evaluation_accumulators[0]);
    }
    ponder_move.clear();
    tablebase_hits = 0;
    tablebase_limit = MIN(options::get_value("SyzygyProbeLimit"), tablebase::max_pieces());
    tablebase_probe_depth = options::get_value("SyzygyProbeDepth");
    nodes = 0;
    pruned_nodes = 0;
    stop_all = false;
    next_poll = 0;
    sel_depth = 0;
    root_stack = stack = &_stack[0];
    root_wtm = brd.stack->wtm;
    result_score = 0;
    result_flag = 0;
    result_depth = completed_depth = attempted_depth = 0;
    interrupted = false;
    result_source = result_source_t::none;
    memset(_stack, 0, sizeof (_stack));
    memset(history, 0, sizeof (history));
    stack->eval_result = score::INVALID;
    stack->repetition_start = -static_cast<int>(_position_history.size());
    init_tt_history();
    stack->tt_clocks = {0, 101};
}

/**
 * Lookup current position in polyglot book database (file book.bin)
 */
bool search_t::book_lookup() {
    if (!_own_book || brd.stack->fifty_count >= 90 || is_draw()) return false;
    if (std::find(_position_history.begin(), _position_history.end(),
                  brd.stack->repetition_key()) != _position_history.end()) return false;
    // Let perfect endgame results take precedence over remembered book moves.
    if (wild == 0 && tablebase_limit && popcnt0(brd.all()) <= tablebase_limit) return false;
    book_t reader;
    move::list_t book_moves;
    reader.open(book_name);
    reader.find(&brd, &book_moves);
    if (book_moves.first == book_moves.last && (wild != 0 || !_experience)) return false;
    const U64 key = book_t::polyglot_key(&brd);
    double mean_weight = 100;
    if (book_moves.first != book_moves.last) {
        U64 total = 0;
        for (auto entry = book_moves.first; entry != book_moves.last; ++entry) total += entry->score;
        mean_weight = double(total) / (book_moves.last - book_moves.first);
    }
    std::vector<move_t> candidates;
    std::vector<double> weights;
    bool has_reference = false, has_analysis = false;
    struct candidate_t {
        move_t move;
        double weight;
        const experience::entry_t * learned;
        int analysis_score;
    };
    std::vector<candidate_t> legal;
    const int no_analysis = std::numeric_limits<int>::min();
    int best_analysis = no_analysis;
    stack->in_check = brd.in_check();
    for (move_t * move = move::first(this, 1); move; move = move::next(this, 1)) {
        if (!game.search_moves.empty() && std::find(game.search_moves.begin(),
                game.search_moves.end(), move->to_string()) == game.search_moves.end()) continue;
        double weight = 0;
        for (auto entry = book_moves.first; entry != book_moves.last; ++entry)
            if (move->equals(entry)) weight += entry->score;
        has_reference |= weight > 0;
        const unsigned promotion = move->promotion ? (move->promotion - 1) % 6 : 0;
        const unsigned code = move->ssq | (move->tsq << 6) | (promotion << 12);
        const auto * learned = wild == 0 && _experience ? _experience->find(key, code) : nullptr;
        int analysis_score = no_analysis;
        if (learned && learned->depth >= _experience_depth
                && learned->clock == unsigned(brd.stack->fifty_count)) {
            analysis_score = learned->score_type ? (learned->score > 0 ? 30000 : -30000)
                : std::clamp(int(learned->score), -29000, 29000);
            best_analysis = std::max(best_analysis, analysis_score);
        }
        legal.push_back({*move, weight, learned, analysis_score});
    }
    for (const auto & candidate : legal) {
        double weight = candidate.weight;
        if (candidate.learned) {
            const auto & learned = *candidate.learned;
            const double games = double(learned.wins) + learned.draws + learned.losses;
            // Analysed mistakes remain observations, but do not qualify as recommendations.
            const bool analysed = candidate.analysis_score != no_analysis
                && candidate.analysis_score >= best_analysis - 50;
            has_analysis |= analysed;
            if (!weight && (games > 0 || analysed)) {
                weight = mean_weight * (analysed ? 1 : 0.125 + 0.875 * games / (games + 8));
            }
            weight *= learned.factor();
        }
        if (weight > 0) {
            candidates.push_back(candidate.move);
            weights.push_back(weight);
        }
    }
    if (candidates.empty()) return false;
    if (!_book_seeded) {
        _book_random.seed(std::random_device{}());
        _book_seeded = true;
    }
    // Sparse unanalysed experience competes with doing a fresh search.
    if (!has_reference && !has_analysis) weights.push_back(mean_weight);
    std::discrete_distribution<size_t> choose(weights.begin(), weights.end());
    const size_t selected = choose(_book_random);
    if (selected == candidates.size()) return false;
    stack->best_move = candidates[selected];
    stack->pv_moves[0] = stack->best_move;
    stack->pv_count = 1;
    result_score = 0; // Experience scores are evidence, never transposition bounds.
    result_source = result_source_t::book;
    return true;
}

/**
 * Starts the searching
 */
void search_t::go() {
    assert(stack->best_move.piece == 0 && ponder_move.piece == 0);
    // Royal losers terminal rules apply before book selection or root moves.
    bool solved = false;
    if (wild == 17 && (max_1(brd.all(brd.us())) || max_1(brd.all(brd.them())))) {
        result_score = max_1(brd.all(brd.us())) ? score::MATE : -score::MATE;
        solved = true;
    } else if (wild == 17 && !has_legal_move(brd)) {
        result_score = score::MATE; // Both checkmate and stalemate win.
        solved = true;
    }
    if (solved) {
        result_source = result_source_t::terminal;
        result_flag = score::EXACT;
    } else if ((solved = book_lookup())) {
        // Book selection has no searched depth, nodes or evaluated score.
    } else if (init_root_moves() > 0) {
        solved = tablebase_lookup();
        if (!solved) iterative_deepening();
        // A tiny node budget or early stop can interrupt the first root move.
        // Still return a legal move from the permitted root set.
        if (!stack->best_move.piece) {
            stack->best_move = root.moves[0].move;
            stack->pv_moves[0] = stack->best_move;
            stack->pv_count = 1;
            result_source = result_source_t::fallback;
        }
    } else {
        result_source = result_source_t::terminal;
        result_score = root.in_check ? -score::MATE : draw_score();
        result_flag = score::EXACT;
    }
    const char * source = "none";
    switch (result_source) {
        case result_source_t::partial: source = "partial"; break;
        case result_source_t::complete: source = "complete"; break;
        case result_source_t::fallback: source = "fallback"; break;
        case result_source_t::book: source = "book"; break;
        case result_source_t::terminal: source = "terminal"; break;
        case result_source_t::tablebase: source = "tablebase"; break;
        case result_source_t::none: break;
    }
    uci::out(std::string("info string search_result ") + source
        + " completed_depth " + std::to_string(completed_depth)
        + " attempted_depth " + std::to_string(attempted_depth)
        + " interrupted " + std::to_string(interrupted)
        + " pruned_moves " + std::to_string(pruned_nodes));
    uci::send_pv(result_score, result_depth, sel_depth, nodes, game.tm.elapsed(),
        pv_to_string().c_str(), result_flag, tablebase_hits);
    // Work limits may finish a search, but only protocol control releases its result.
    // Do not use abort(): it includes exhausted node/time budgets and stop_all.
    while ((game.infinite || pondering()) && _control && !_control->stop_requested.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// Root DTZ results lack repetition history. Use the shortcut only when no
// reversible game history is known; otherwise retain normal search.
bool search_t::tablebase_lookup() {
    if (wild != 0 || tablebase_limit == 0 || root.move_count == 0
            || !_position_history.empty() || abort(true)) return false;
    tablebase::root_result_t result;
    if (!tablebase::probe_root(brd, tablebase_limit, result)) return false;
    for (int i = 0; i < root.move_count; ++i) {
        move_t & move = root.moves[i].move;
        if (move.ssq != result.from || move.tsq != result.to
                || move.promotion != result.promotion) continue;
        if (abort(true)) return false;
        stack->best_move = move;
        stack->pv_moves[0] = move;
        stack->pv_count = 1;
        result_score = result.outcome == 0 ? draw_score() : result.outcome * score::TABLEBASE;
        ++tablebase_hits;
        result_source = result_source_t::tablebase;
        result_flag = score::EXACT;
        return true;
    }
    return false;
}

bool search_t::probe_tablebase(int alpha, int beta, int depth, int & value) {
    if (wild != 0 || tablebase_limit == 0 || stack->in_check) return false;
    const int outcome = tablebase::probe_wdl(brd, tablebase_limit, depth, tablebase_probe_depth);
    if (outcome == -3) return false;
    ++tablebase_hits;
    const int magnitude = score::TABLEBASE - brd.ply;
    const bool draw = outcome >= -1 && outcome <= 1;
    value = draw ? draw_score() : outcome > 0 ? magnitude : -magnitude;
    if (!draw && !(outcome > 0 ? value >= beta : value <= alpha)) return false;
    stack->tt_clocks.include_child({0, 0}, 0);
    return true;
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
        for (int depth = 1; depth <= game.max_depth; depth++) {
            score = -pvs(-score::INF, score::INF, depth - 1);
        }
        backward(move);
        uci::out("book_calc fen " + fen + " move " + move->to_string() + " depth " 
            + uci::itoa(game.max_depth) + " score " + uci::itoa(score) + " nodes "
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
    const int max_time = game.tm.reserved_max();
    const int min_time = game.tm.reserved_min();
    bool timed_search = game.white_time || game.black_time;
    int depth;
    for (depth = 1; depth <= game.max_depth; depth++) {
        attempted_depth = depth;
        int score = aspiration(depth, last_score);
        if (abort(true)) {
            interrupted = true;
            break;
        }
        completed_depth = result_depth = depth;
        result_score = score;
        result_flag = score::EXACT;
        result_source = result_source_t::complete;
        uci::send_pv(result_score, depth, sel_depth, nodes, game.tm.elapsed(),
            pv_to_string().c_str(), result_flag, tablebase_hits);
        store_pv();
        if (timed_search) {
            bool score_jump = depth >= 6 && ((ABS(score - last_score) > 20) || score > score::WIN);
            int elapsed = game.tm.elapsed();
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
                game.tm.set_max(0);
            } else if (do_stop) {
                break;
            }
        } else if (depth >= 15 && game.target_score && score >= game.target_score &&
                game.target_move.piece && game.target_move.equals(&stack->best_move)) {
            break;
        }
        last_score = score;
    }
    if (stack->pv_count > 0) {
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
    if (game.max_nodes > 0 && nodes >= game.max_nodes) {
        result = true;
    } else if (stop_all || (_control && _control->stop_requested.load())) {
        result = true;
    } else if (force_poll || --next_poll <= 0) {
        next_poll = NODES_BETWEEN_POLLS;
        result = !pondering() && game.tm.time_is_up() && root_stack->best_move.piece > 0;
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
    if (wild == 0 && evaluation_network)
        evaluation_network->prepare(brd, nullptr, evaluation_accumulators[brd.ply]);
    // A null move is not part of legal game history.
    stack->repetition_start = brd.ply + 1;
    stack->tt_history = 0;
    stack->tt_clocks = {0, 101};
    assert(stack == &_stack[brd.ply]);
}

/**
 * Undo a null move
 */
void search_t::backward() {
    (stack - 1)->tt_clocks.include_child(stack->tt_clocks, 0);
    stack--;
    brd.backward();
    assert(stack->tt_clocks.contains(brd.stack->fifty_count));
    assert(stack == &_stack[brd.ply]);
}

/**
 * Make a move, going one ply deeper in the search
 * @param move 
 * @param gives_check
 */
void search_t::forward(move_t * move, bool gives_check) {
    stack->current_move.set(move);
    (stack + 1)->repetition_start = stack->repetition_start;
    (stack + 1)->tt_history = stack->tt_history;
    stack++;
    stack->in_check = gives_check;
    stack->tt_clocks = {0, 101};
    stack->eval_result = score::INVALID;
    brd.forward(move);
    if (wild == 0 && evaluation_network)
        evaluation_network->prepare(brd, move, evaluation_accumulators[brd.ply]);
    if (brd.stack->fifty_count == 0) {
        stack->tt_history = 0;
    } else if (brd.ply - 1 >= stack->repetition_start) {
        stack->tt_history += mix_context((brd.stack - 1)->repetition_key()
            ^ (brd.ply == 1 ? game_history_salt : search_history_salt));
    }
    assert(stack == &_stack[brd.ply]);
}

/**
 * Undo a move
 * @param move
 */
void search_t::backward(move_t * move) {
    // Pawn moves and captures reset the clock, so their result cannot depend
    // on the parent clock. Quiet moves advance it by one; null moves do not.
    if (brd.stack->fifty_count != 0) {
        (stack - 1)->tt_clocks.include_child(stack->tt_clocks, 1);
    }
    stack--;
    brd.backward(move);
    assert(stack->tt_clocks.contains(brd.stack->fifty_count));
    assert(stack == &_stack[brd.ply]);
}

/**
 * Test if the search is pondering
 * @return bool true if pondering 
 */
bool search_t::pondering() {
    return game.ponder && (!_control || _control->ponder.load());
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
            m.set_compact(&b, tt_move);
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
            m.set_compact(&b, tt_move);
            if (m.equals(&stack->pv_moves[i]) == false) {
                trans_table::store(b.stack->tt_key, 0, 0, 1, 0, stack->pv_moves[i].to_compact(), 0);
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
    int tt_move = 0, tt_flags, tt_score;
    trans_table::retrieve(brd.stack->tt_key, 0, 0, tt_score, tt_move, tt_flags);
    stack->tt_move.set_compact(&brd, tt_move);
    root.in_check = brd.in_check();
    stack->tt_key = brd.stack->tt_key;
    stack->best_move.set(0);
    for (move_t * move = move::first(this, 1);
            move; move = move::next(this, 1)) {
        if (!game.search_moves.empty() && std::find(game.search_moves.begin(),
                game.search_moves.end(), move->to_string()) == game.search_moves.end()) continue;
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
    const int64_t n0 = moves[0].nodes;
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
    const int64_t n0 = moves[0].nodes / 8;
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

// No child stack is available here. Resolve legal terminals and draws before
// falling back to static evaluation, including check positions with an evasion.
int search_t::ceiling_score() {
    if (!has_legal_move(brd)) {
        return stack->in_check ? -score::MATE + brd.ply : draw_score();
    }
    return is_draw() ? draw_score() : evaluate(this);
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
        U64 nodes_before = nodes;
        int score = 0;

        //go forward and search one level deeper
        forward(move, rmove->gives_check);
        if (i == 0) {
            score = -pvs(-beta, -alpha, depth - 1);
        } else {
            score = -pvs(-alpha - 1, -alpha, depth - 1);
            if (!stop_all && score > alpha) { //open window research w/o reductions
                score = -pvs(-beta, -alpha, depth - 1);
            }
        }
        backward(move);

        //handle results
        rmove->add_nodes(nodes - nodes_before);
        if (stop_all) {
            return alpha;
        } else if (score > best) {
            best = score;
            result_score = score;
            result_depth = depth;
            result_source = result_source_t::partial;
            // A searched move bounds the root from below. A fail-low move
            // cannot bound unsearched alternatives, so omit that root score.
            result_flag = score <= alpha ? 0 : score::LOWERBOUND;
            rmove->add_nodes(i);
            stack->best_move.set(move);
            bool exact = score::flags(score, alpha, beta) == score::EXACT;
            if (exact || false == move->equals(&stack->pv_moves[0])) {
                update_pv(&rmove->move);
            }
            uci::send_pv(best, depth, sel_depth, nodes, game.tm.elapsed(),
                    pv_to_string().c_str(), result_flag, tablebase_hits);
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
 * Game history requires three occurrences; cycles strictly inside search
 * are cut off after one earlier match as a search heuristic.
 * @return true if search should score this node as drawn
 */
bool search_t::is_draw() {
    if (brd.is_draw() || (is_1(brd.bb[WPAWN] | brd.bb[BPAWN]) && eg::probe_kpk(brd) == 0)) {
        return true;
    }
    if (brd.stack->fifty_count >= 100) {
        // Checkmate takes precedence. Probe fresh legal evasions, including quiet moves.
        stack->tt_move.clear();
        if (!stack->in_check || move::first(this, 1) != nullptr) {
            stack->tt_clocks = {100, 101};
            return true;
        }
        return false;
    }
    stack->tt_clocks.max = MIN(stack->tt_clocks.max, 99);
    if (brd.stack->fifty_count < 4) {
        return false;
    }
    return is_repetition();
}

bool search_t::is_repetition() const {
    const int stop_ply = MAX(brd.ply - brd.stack->fifty_count, stack->repetition_start);
    const U64 repetition_key = brd.stack->repetition_key();
    int matches = 0;
    for (int ply = brd.ply - 4; ply >= stop_ply; ply -= 2) {
        const U64 key = ply >= 0 ? brd._stack[ply].repetition_key()
            : _position_history[static_cast<int>(_position_history.size()) + ply];
        // The root belongs to game history, not to an internal search cycle.
        if (key == repetition_key && (ply > 0 || ++matches == 2)) {
            return true;
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
    stack->tt_clocks = {0, 101};

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
        return ceiling_score();
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
    int tablebase_score;
    if (tablebase_limit && probe_tablebase(alpha, beta, depth, tablebase_score)) {
        return tablebase_score;
    }

    /*
     * Transposition table lookup
     */

    const bool pv = alpha + 1 < beta;
    stack->tt_key = brd.stack->tt_key;
    const U64 context = tt_context();
    clock_range_t tt_clocks;
    int tt_move = 0, tt_flag = 0, tt_score;
    if (trans_table::retrieve(stack->tt_key, brd.ply, depth, tt_score, tt_move, tt_flag, context,
            brd.stack->fifty_count, &tt_clocks)) {
        if (pv && tt_flag == score::EXACT) {
            stack->tt_clocks.include_child(tt_clocks, 0);
            return tt_score;
        } else if (!pv && tt_score >= beta && tt_flag == score::LOWERBOUND) {
            stack->tt_clocks.include_child(tt_clocks, 0);
            return tt_score;
        } else if (!pv && tt_score <= alpha && tt_flag == score::UPPERBOUND) {
            stack->tt_clocks.include_child(tt_clocks, 0);
            return tt_score;
        }
    }
    stack->tt_move.set_compact(&brd, tt_move);

    /*
     * Node pruning
     */

    const bool in_check = stack->in_check;
    const int eval = evaluate(this);
    const int decisive_score = score::TABLEBASE - MAX_PLY;
    const bool do_prune_node = eval >= beta && !in_check && !pv
        && std::abs(beta) < decisive_score && brd.has_pieces(brd.us())
        && brd.stack->fifty_count < 99;
    const bool previous_null = brd.ply > 0 && !(stack - 1)->current_move.piece;
    const bool do_null = null_enabled && !previous_null;

    // A static/null cutoff requires legal play: passing in stalemate would
    // turn an exact draw into a heuristic fail-high. Probe only when pruning
    // can run; the ordinary move loop handles the remaining terminal nodes.
    const bool beta_cutoff = depth < 4 && beta_pruning && eval - 50 * depth >= beta;
    // At clock 99 a reversible move draws unless it mates; static evaluation and
    // an artificial pass can still claim a win. Search legal moves there.
    // Propagate this restriction so cached parents cannot bypass the guard.
    if (do_prune_node && (beta_cutoff || do_null)) {
        stack->tt_clocks.max = MIN(stack->tt_clocks.max, 98);
    }
    if (do_prune_node && (beta_cutoff || do_null) && !has_legal_move(brd)) {
        return draw_score();
    }

    // beta pruning
    if (do_prune_node && beta_cutoff) {
        return eval - 50 * depth;
    }

    //null move pruning
    if (do_prune_node && do_null) {
        int R = 3;
        forward();
        int null_score = -pvs(-beta, -alpha, depth - 1 - R);
        backward();
        if (stop_all) {
            return alpha;
        } else if (null_score >= beta && std::abs(null_score) < decisive_score) {
            const int RV = 5;
            if (null_verify && depth > RV && material::is_eg(this)) {
                //verification
                const clock_range_t previous_clocks = stack->tt_clocks;
                // Exclude artificial passes throughout the verification subtree.
                null_enabled = false;
                int verified_score = pvs(alpha, beta, depth - 1 - RV);
                null_enabled = true;
                stack->tt_clocks.include_child(previous_clocks, 0);
                if (stop_all) {
                    return alpha;
                }
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
        const clock_range_t previous_clocks = stack->tt_clocks;
        int iid_score = pvs(alpha, beta, depth - 2);
        stack->tt_clocks.include_child(previous_clocks, 0);
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
            if (!stop_all && score > alpha && (pv || reduce > 0)) { //open window research without reductions
                const clock_range_t previous_clocks = stack->tt_clocks;
                score = -pvs(-beta, -alpha, depth - 1 + extend);
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
                trans_table::store(stack->tt_key, brd.root_ply, brd.ply, depth, score, move->to_compact(), score::LOWERBOUND, context, stack->tt_clocks);
                if (!move->capture && !move->promotion && !move->castle) {
                    update_killers(move);
                    update_history(move);
                    // History is nonnegative; saturate penalties before the shift becomes invalid.
                    const int history_shift = MIN(searched_moves, std::numeric_limits<int>::digits);
                    for (int i = 0; i < searched_moves; i++) {
                        move_t * m = &stack->searched[i];
                        if (!m->capture && !m->promotion && !m->castle) {
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
    trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, best, stack->best_move.to_compact(), flag, context, stack->tt_clocks);
    return best;
}

/**
 * Quiescence search with tactical moves only.
 * At depth 0: captures, promotions, quiet checks and castling
 * At depth < 0: captures, promotions and castling
 * All legal evasions are searched in check. Castling retains the existing
 * bounded quiet extension (each side can castle at most once); delta/SEE
 * pruning still applies to nonchecking castles.
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth depth (0 or lower)
 * @return score
 */

int search_t::qsearch(int alpha, int beta, int depth) {
    stack->tt_clocks = {0, 101};

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
        return ceiling_score();
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
    int tablebase_score;
    if (tablebase_limit && probe_tablebase(alpha, beta, depth, tablebase_score)) {
        return tablebase_score;
    }

    //stand-pat: return if eval is already good enough
    const int eval = evaluate(this);
    const bool in_check = stack->in_check;
    if (eval >= beta && !in_check) {
        return has_legal_move(brd) ? eval : draw_score();
    }

    //get first move; if there's none it's mate, stalemate, or there are no captures/promotions
    stack->tt_move.clear();
    move_t * move = move::first(this, depth);
    if (!move) {
        if (in_check) {
            return -score::MATE + brd.ply;
        }
        return depth == 0 || !has_legal_move(brd) ? draw_score() : eval;
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
 * MAX_PLY is not sufficient to hold all moves of a chess game.
 * Note: in UCI mode this is not an issue, because each position
 * starts with a new stack.
 */
void search_t::reset_stack() {
    for (int ply = 0; ply < brd.ply; ++ply) {
        _position_history.push_back(brd._stack[ply].repetition_key());
    }
    if (_position_history.size() > static_cast<size_t>(brd.stack->fifty_count)) {
        _position_history.erase(_position_history.begin(),
            _position_history.end() - brd.stack->fifty_count);
    }
    brd.ply = 0;
    stack = root_stack;
    stack->eval_result = score::INVALID;
    stack->repetition_start = -static_cast<int>(_position_history.size());
    init_tt_history();
    stack->tt_clocks = {0, 101};
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
