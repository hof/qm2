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
 * File: search.cpp
 * Search for standard chess: fail-soft principal variation search (PVS) with
 * iterative deepening and aspiration windows.
 * - Transposition table, also in quiescence search, with cached evaluations
 * - Node pruning: reverse futility, razoring, null move with verification,
 *   ProbCut and internal iterative reduction
 * - Singular extensions, including multiple and negative extensions
 * - Move pruning: late move pruning, history pruning, futility and SEE
 * - Late move reductions guided by history
 * - Move ordering: killers, countermoves, main, continuation, pawn and
 *   capture histories, threat and check bonuses
 * - Correction history for the static evaluation
 * - Quiescence search: captures and promotions, all evasions in check
 * - Upcoming-repetition detection
 */

#include "search.h"
#include <bit>
#include <limits>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include "book.h"
#include "uci_console.h"
#include <stdexcept>
#include "timeman.h"
#include "tablebase.h"

namespace {

// Conservative margins in the shared 100-cp pawn scale. Keep the baseline
// independent of history, improving flags and evaluator-specific adjustments.
constexpr int razoring_margin_per_ply = 300;
constexpr int main_see_quiet_margin = 50;
constexpr int main_see_capture_margin = 100;
constexpr int quiescence_futility_margin = 350;
constexpr int lmr_normal_offset = 503 + 1024;
constexpr int lmr_history_limit = 2048;
constexpr int lmr_tt_pv_bonus = 1289;
constexpr int lmr_tt_deep_bonus = 1024;
constexpr int lmr_refutation_bonus = 775;
constexpr int lmr_check_bonus = 1361;
constexpr int lmr_tt_pv_depth_offset = 2057;
constexpr int lmr_depth_offset = 400;

// Pseudo-attacks by cheaper pieces make a static surplus less trustworthy.
// This is the same parent-position threat model used for quiet ordering.
bool lower_value_threat(board_t & board) {
    const bool us = board.us();
    const U64 queens = board.bb[QUEEN[us]];
    const U64 rooks = board.bb[ROOK[us]];
    const U64 minors = board.bb[KNIGHT[us]] | board.bb[BISHOP[us]];
    U64 attacks = board.pawn_attacks(!us);
    if (attacks & (queens | rooks | minors)) {
        return true;
    }
    U64 pieces = board.bb[KNIGHT[!us]];
    while (pieces) {
        attacks |= KNIGHT_MOVES[pop(pieces)];
    }
    pieces = board.bb[BISHOP[!us]];
    while (pieces) {
        attacks |= magic::bishop_moves(pop(pieces), board.all());
    }
    if (attacks & (queens | rooks)) {
        return true;
    }
    pieces = board.bb[ROOK[!us]];
    while (pieces) {
        attacks |= magic::rook_moves(pop(pieces), board.all());
    }
    return attacks & queens;
}

// Same-position helpers may replace the picker, PV, evaluation and repetition
// frame. Restore all of it before the caller resumes, retaining clock limits.
struct search_frame_scope_t {
    search_t & search;
    search_stack_t * frame;
    search_stack_t previous;
    bool null_allowed;

    explicit search_frame_scope_t(search_t & search)
        : search(search), frame(search.stack), previous(*search.stack),
          null_allowed(search.null_allowed) {}

    ~search_frame_scope_t() {
        assert(search.stack == frame);
        *frame = previous;
        search.null_allowed = null_allowed;
    }
};

}

// Most playable positions have a king escape. Fall back to fresh generation
// without disturbing the active move picker or its exclusions.
bool search_t::has_legal_move(board_t & brd, bool in_check) {
    const bool us = brd.us(), them = !us;
    const int from = brd.get_sq(KING[us]);
    if (!in_check) {
        // An off-ray pawn cannot uncover an attack on its king.
        const U64 pawns = brd.bb[PAWN[us]] & ~QUEEN_MOVES[from];
        const U64 pushes = us ? pawns << 8 : pawns >> 8;
        if (pushes & ~brd.bb[ALLPIECES]) {
            return true;
        }
    }
    U64 targets = KING_MOVES[from] & ~brd.all(us);
    const U64 occupied = brd.bb[ALLPIECES] & ~BIT(from);
    const U64 diagonal = brd.bb[BISHOP[them]] | brd.bb[QUEEN[them]];
    const U64 straight = brd.bb[ROOK[them]] | brd.bb[QUEEN[them]];
    while (targets) {
        const int to = pop(targets);
        if ((KNIGHT_MOVES[to] & brd.bb[KNIGHT[them]])
                || (PAWN_CAPTURES[us][to] & brd.bb[PAWN[them]])
                || (KING_MOVES[to] & brd.bb[KING[them]])) {
            continue;
        }
        // Rays from the destination ignore its occupant. Vacate the source
        // so a king escape cannot hide a slider attack behind the old king.
        if ((BISHOP_MOVES[to] & diagonal) && (magic::bishop_moves(to, occupied) & diagonal)) {
            continue;
        }
        if ((ROOK_MOVES[to] & straight) && (magic::rook_moves(to, occupied) & straight)) {
            continue;
        }
        return true;
    }
    move::list_t moves;
    for (auto generate : {move::gen_quiet_moves, move::gen_captures,
            move::gen_promotions, move::gen_castles}) {
        moves.clear();
        generate(&brd, &moves);
        for (move_t * move = moves.first; move != moves.last; ++move) {
            if (brd.legal(move)) {
                return true;
            }
        }
    }
    return false;
}

// Initial root order: SEE in pawns, plus one for a dangerous move.
void root_move_t::init(move_t * m, bool checks, bool dangerous, int see_value) {
    move.set(m);
    gives_check = checks;
    is_dangerous = dangerous;
    see = see_value;
    nodes = see / 100 + dangerous; //initial sort order
}

// Root move sort order: negative when m is the better move.
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

// Raw network scores depend on board/side and the immutable model snapshot,
// not repetition history or the halfmove clock. Scaling is applied separately.
int search_t::neural_static_score(const tt_hit_t * tt, int * deferred_raw_eval) {
    assert(evaluation_network);
    tt_hit_t hit;

    // A supplied result includes misses: do not probe the same node twice.
    if (!tt) {
        trans_table::probe(position_key(brd), brd.ply, brd.stack->fifty_count, hit);
        tt = &hit;
    }
    if (tt->eval != score::INVALID) {
        return tt->eval;
    }
    auto & accumulator = evaluation_accumulators[brd.ply];
    const int value = std::clamp(evaluation_network->evaluate(brd, accumulator,
        std::span<const nnue::accumulator_t>(evaluation_accumulators.data(), brd.ply)),
        -score::TABLEBASE + 1, score::TABLEBASE - 1);
    if (deferred_raw_eval) {
        *deferred_raw_eval = value;
    } else {
        trans_table::store(position_key(brd), brd.root_ply, brd.ply, 0, 0, 0, 0, value);
    }
    return value;
}

void search_t::init(const char * fen, const game_t * g, const search_control_t * control,
        const std::vector<U64> & position_history, int variant) {
    const auto started = std::chrono::steady_clock::now();
    brd.init(fen);
    game = g ? *g : game_t();
    _control = control;
    _position_history = position_history;
    if (_position_history.size() > static_cast<size_t>(brd.stack->fifty_count)) {
        _position_history.erase(_position_history.begin(),
            _position_history.end() - brd.stack->fifty_count);
    }
    game.init_tm(brd.us(), started);
    wild = variant;
    book_name = book::path();
    _own_book = options::get_value("OwnBook") && !options::get_value("UCI_AnalyseMode");
    _book_seeded = false;
    null_allowed = true;
    draw_contempt = options::get_value("DrawContempt");
    evaluation_network.reset();
    if (wild == 0) {
        std::string error;
        if (!nnue::load_default(error)) {
            throw std::runtime_error("NNUE network required: " + error);
        }
        evaluation_network = nnue::snapshot();
    }
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
    stop_all = false;
    next_poll = 0;
    ponder_clock_started = !game.ponder;
    sel_depth = 0;
    root_stack = stack = &_stack[0];
    root_wtm = brd.stack->wtm;
    result_score = 0;
    result_flag = 0;
    result_depth = completed_depth = attempted_depth = 0;
    interrupted = false;
    result_source = result_source_t::none;

    // Plain data apart from move list pointers, which every node resets before use.
    memset(static_cast<void *>(_stack), 0, sizeof (_stack));
    memset(history, 0, sizeof (history));
    memset(countermoves, 0, sizeof(countermoves));
    quiet_history = wild == 0 ? std::make_unique<quiet_history_t>() : nullptr;
    capture_history = wild == 0 ? std::make_unique<capture_history_t>() : nullptr;
    correction_history = wild == 0 ? std::make_unique<correction_history_t>() : nullptr;
    stack->eval_result = score::INVALID;
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = stack->lmr_tt_fail_low = stack->lmr_tt_deep = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->reduction = 1024;
    stack->static_eval = score::INVALID;
    stack->excluded_move = 0;
    stack->move_count = 0;
    stack->multiple_extensions = 0;
    stack->repetition_start = -static_cast<int>(_position_history.size());
}

// Looks up the position in the polyglot book (book.bin).
bool search_t::book_lookup() {
    if (!_own_book || brd.stack->fifty_count >= 90 || is_draw()) {
        return false;
    }
    if (std::find(_position_history.begin(), _position_history.end(),
                  brd.stack->repetition_key()) != _position_history.end()) {
        return false;
    }

    // Let perfect endgame results take precedence over remembered book moves.
    if (wild == 0 && tablebase_limit && popcnt0(brd.all()) <= tablebase_limit) {
        return false;
    }
    book_t reader;
    move::list_t book_moves;
    reader.open(book_name);
    reader.find(&brd, &book_moves);
    if (book_moves.first == book_moves.last) {
        return false;
    }
    std::vector<move_t> candidates;
    std::vector<double> weights;
    stack->in_check = brd.in_check();
    for (move_t * move = move::first(this, 1); move; move = move::next(this, 1)) {
        if (!game.search_moves.empty() && std::find(game.search_moves.begin(),
                game.search_moves.end(), move->to_string()) == game.search_moves.end()) {
            continue;
        }
        if (wild == 0 && !brd.legal(move)) {
            continue;
        }
        double weight = 0;
        for (auto entry = book_moves.first; entry != book_moves.last; ++entry) {
            if (move->equals(entry)) {
                weight += entry->score;
            }
        }
        if (weight > 0) {
            candidates.push_back(*move);
            weights.push_back(weight);
        }
    }
    if (candidates.empty()) {
        return false;
    }
    if (!_book_seeded) {
        _book_random.seed(std::random_device{}());
        _book_seeded = true;
    }
    std::discrete_distribution<size_t> choose(weights.begin(), weights.end());
    const size_t selected = choose(_book_random);
    stack->best_move = candidates[selected];
    stack->pv_moves[0] = stack->best_move;
    stack->pv_count = 1;
    result_score = 0;
    result_source = result_source_t::book;
    return true;
}

// Starts the search.
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
        if (!solved) {
            iterative_deepening();
        }

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
        + " interrupted " + std::to_string(interrupted));
    uci::send_pv(result_score, result_depth, sel_depth, nodes, game.tm.search_elapsed(),
        pv_to_string().c_str(), result_flag, tablebase_hits);

    // Work limits may finish a search, but only protocol control releases its result.
    // Do not use abort(): it includes exhausted node/time budgets and stop_all.
    while ((game.infinite || pondering()) && _control && !_control->stop_requested.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// DTZ guides direct play unless an existing repetition needs search adjudication.
bool search_t::tablebase_lookup() {
    if (wild != 0 || tablebase_limit == 0 || root.move_count == 0
            || popcnt0(brd.all()) > tablebase_limit || abort(true)) {
        return false;
    }

    // A DTZ-minimizing continuation cannot cycle before its next zeroing move.
    // Unique past positions cannot turn that continuation into a threefold.
    // Already repeated positions need history-aware search instead.
    std::vector<U64> history = _position_history;
    history.push_back(brd.stack->repetition_key());
    std::sort(history.begin(), history.end());
    if (std::adjacent_find(history.begin(), history.end()) != history.end()) {
        return false;
    }
    tablebase::root_result_t result;
    if (!tablebase::probe_root(brd, tablebase_limit, result)) {
        return false;
    }
    for (int i = 0; i < root.move_count; ++i) {
        move_t & move = root.moves[i].move;
        if (move.ssq != result.from || move.tsq != result.to
                || move.promotion != result.promotion) {
            continue;
        }
        if (abort(true)) {
            return false;
        }
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

bool search_t::probe_tablebase(int depth, int & value) {
    if (wild != 0 || tablebase_limit == 0 || stack->in_check) {
        return false;
    }
    const int outcome = tablebase::probe_wdl(brd, tablebase_limit, depth, tablebase_probe_depth);
    if (outcome == -3) {
        return false;
    }
    ++tablebase_hits;
    const int magnitude = score::TABLEBASE - brd.ply;
    const bool draw = outcome >= -1 && outcome <= 1;
    value = draw ? draw_score() : outcome > 0 ? magnitude : -magnitude;
    return true;
}

// Scores every legal move from the book.
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
            score = -pvs(-score::INF, score::INF, depth - 1, false);
        }
        backward(move);
        uci::out("book_calc fen " + fen + " move " + move->to_string() + " depth "
            + uci::itoa(game.max_depth) + " score " + uci::itoa(score) + " nodes "
            +  uci::itoa(nodes - nodes_before) + " variant " + uci::itoa(wild));
    }
    uci::out("book_calc ready");
}


// Iterative deepening with aspiration windows. Timed searches decide here
// whether another iteration starts.
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
        uci::send_pv(result_score, depth, sel_depth, nodes, game.tm.search_elapsed(),
            pv_to_string().c_str(), result_flag, tablebase_hits);
        store_pv();
        if (timed_search && !pondering()) {
            bool score_jump = depth >= 6 && ((ABS(score - last_score) > 20) || score > score::WIN);
            int elapsed = game.tm.elapsed();
            bool do_stop = false;
            if (root.move_count <= 1 && elapsed > min_time / 8) {

                // One legal move, still search a bit to get a ponder move
                do_stop = true;
            } else if (elapsed > max_time / 2) {

                // Complex position, maximum (emergency) time control
                do_stop = true;
            } else if (elapsed > min_time / 2 && !score_jump && root.is_easy()) {

                // Easy position, half time control
                do_stop = true;
            } else if (elapsed > min_time && !score_jump && !root.is_complex()) {

                // Neutral position, normal time control
                do_stop = true;
            } else if (score::mate_in_ply(score) && depth > score::mate_in_ply(score)) {

                // Mate in N and search depth > N
                do_stop = true;
            }
            if (do_stop) {
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

// Aspiration windows: search a narrow window around the last score and widen
// it after a fail high or fail low.
int search_t::aspiration(int depth, int last_score) {
    if (depth < 2) {
        return pvs_root(-score::INF, score::INF, depth);
    }

    // Uncertain (large) scores start with a wider window.
    int delta = 12 + last_score * last_score / 30155;
    int alpha = MAX(-score::INF, last_score - delta);
    int beta = MIN(score::INF, last_score + delta);
    int fail_highs = 0;
    for (;;) {

        // A fail high needs confirmation, not the full depth.
        const int value = pvs_root(alpha, beta, MAX(1, depth - fail_highs));
        if (stop_all) {
            return value;
        }
        if (value > alpha && value < beta) {
            return value;
        }
        if (value <= alpha) {
            beta = (alpha + beta) / 2;
            alpha = MAX(-score::INF, value - delta);
            fail_highs = 0;
        } else {
            beta = MIN(score::INF, value + delta);
            fail_highs += !score::is_win(value) && !score::is_win(-value);
        }
        delta = MIN(delta + delta / 3, score::INF);
    }
}

// Checks every stop condition and rearms the poll interval. A node budget
// shortens the interval: each counted node is followed by an abort check, so
// the search still stops at the first check that reaches the budget.
bool search_t::poll() {
    bool result = false;
    if (game.max_nodes > 0 && nodes >= game.max_nodes) {
        result = true;
    } else if (stop_all || (_control && _control->stop_requested.load())) {
        result = true;
    } else {
        result = game.tm.handle_abort(pondering(), ponder_clock_started, next_poll)
            && root_stack->best_move.piece > 0;
        if (game.max_nodes > 0) {
            next_poll = int(std::min<U64>(next_poll, game.max_nodes - nodes));
        }
    }
    stop_all = result;
    return result;
}

// Makes a null move.
void search_t::forward() {
    (stack + 1)->multiple_extensions = stack->multiple_extensions;
    stack->current_move.clear();
    stack++;
    stack->in_check = false;
    stack->eval_result = score::INVALID;
    stack->excluded_move = 0;
    stack->move_count = 0;
    brd.forward();
    if (wild == 0 && evaluation_network) {
        evaluation_network->prepare(brd, nullptr, evaluation_accumulators[brd.ply]);
    }

    // A null move is not part of legal game history.
    stack->repetition_start = brd.ply + 1;
    assert(stack == &_stack[brd.ply]);
}

// Undoes a null move.
void search_t::backward() {
    stack--;
    brd.backward();
    assert(stack == &_stack[brd.ply]);
}

// Makes a move, one ply deeper.
void search_t::forward(move_t * move, bool gives_check) {

    // Count one node per move made; null moves and re-searches of an
    // already made move are not new nodes.
    ++nodes;
    (stack + 1)->multiple_extensions = stack->multiple_extensions;
    stack->current_move.set(move);
    (stack + 1)->repetition_start = stack->repetition_start;
    stack++;
    stack->in_check = gives_check;
    stack->eval_result = score::INVALID;
    stack->excluded_move = 0;
    stack->move_count = 0;
    brd.forward(move);
    trans_table::prefetch(position_key(brd));
    if (wild == 0 && evaluation_network) {
        evaluation_network->prepare(brd, move, evaluation_accumulators[brd.ply]);
    }
    assert(stack == &_stack[brd.ply]);
}

// Undoes a move.
void search_t::backward(move_t * move) {
    stack--;
    brd.backward(move);
    assert(stack == &_stack[brd.ply]);
}

// True while pondering.
bool search_t::pondering() {
    return game.ponder && (!_control || _control->ponder.load());
}

// The principal variation as text.
std::string search_t::pv_to_string() {
    std::string result = "";
    board_t b;
    b.init(brd.to_string().c_str());

    // Retrieve pv from stack
    for (int i = 0; i < stack->pv_count; i++) {
        result += stack->pv_moves[i].to_string() + " ";
        b.forward(&stack->pv_moves[i]);
    }

    // Retrieve extra moves from hash if the pv is short
    if (stack->pv_count < 8) {
        move_t m;
        for (int i = 0; i < 8; i++) {
            tt_hit_t hit;
            trans_table::probe(position_key(b), 0, b.stack->fifty_count, hit);
            const int tt_move = hit.move;
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

// Stores the principal variation in the TT after every iteration, so its
// moves are searched first again.
void search_t::store_pv() {
    board_t b;
    b.init(brd.to_string().c_str());
    move_t m;
    for (int i = 0; i < stack->pv_count; i++) {
        if (i > 0) {
            const U64 key = position_key(b);
            tt_hit_t hit;
            trans_table::probe(key, 0, b.stack->fifty_count, hit);
            m.set_compact(&b, hit.move);
            if (m.equals(&stack->pv_moves[i]) == false) {
                trans_table::store(key, brd.root_ply, 0, 0, 0, stack->pv_moves[i].to_compact(), 0);
            }
        }
        b.forward(&stack->pv_moves[i]);
    }
}

// Updates history scores after a quiet move caused a beta cutoff.
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

std::array<U64, 2> search_t::correction_context_keys() const {
    std::array<U64, 2> keys{};
    if (brd.ply < 2) {
        return keys;
    }

    // Context belongs to this search only. A null move breaks every older pair.
    const auto & last = (stack - 1)->current_move;
    if (!last.piece) {
        return keys;
    }
    const int last_piece = last.promotion ? last.promotion : last.piece;
    for (int distance = 2; distance <= 4 && distance <= brd.ply; ++distance) {
        const auto & older = (stack - distance)->current_move;
        if (!older.piece) {
            break;
        }
        if (distance == 2 || distance == 4) {
            const int piece = older.promotion ? older.promotion : older.piece;
            keys[distance / 2 - 1] = hash::codes[last_piece][last.tsq]
                ^ std::rotl(hash::codes[piece][older.tsq], 1);
        }
    }
    return keys;
}

int search_t::correction_value() const {
    const bool side = brd.us();
    constexpr int mask = correction_history_t::size - 1;
    int value = correction_history->entries[side][brd.stack->pawn_key & mask];
    const U64 piece_keys[] = {brd.stack->nonpawn_key[0], brd.stack->nonpawn_key[1],
        brd.stack->minor_key, brd.stack->major_key};
    for (int i = 0; i < 4; ++i) {
        value += correction_history->pieces[i][side][piece_keys[i] & mask];
    }
    const auto keys = correction_context_keys();
    for (int i = 0; i < 2; ++i) {
        if (keys[i]) {
            value += correction_history->continuation[i][side][keys[i] & mask] / (i + 1);
        }
    }
    return value / correction_history_t::grain;
}

int search_t::corrected_static_eval(int raw) {
    const int correction = correction_value();
    constexpr int limit = score::TABLEBASE - MAX_PLY - 1;
    return std::clamp(raw + correction, -limit, limit);
}

void search_t::update_correction_history(int value, int bound, int depth) {
    constexpr int decisive = score::TABLEBASE - MAX_PLY;
    const auto & move = stack->best_move;
    if (stop_all || stack->in_check
            || stack->excluded_move || depth < 1 || !move.piece
            || move.capture || move.promotion || stack->eval_result == score::INVALID
            || std::abs(value) >= decisive || std::abs(stack->eval_result) >= decisive) {
        return;
    }

    // Descendants can update the same pawn bucket. Compare with a fresh
    // correction of the cached raw evaluation, rather than a stale entry value.
    const int eval = corrected_static_eval(stack->eval_result);
    if ((bound == score::LOWERBOUND && value <= eval)
            || (bound == score::UPPERBOUND && value >= eval)) {
        return;
    }
    const bool side = brd.us();
    const int bonus = correction_history_t::bonus_for(depth, value - eval);
    bool changed = correction_history->update(side, brd.stack->pawn_key, depth, value - eval);
    const U64 piece_keys[] = {brd.stack->nonpawn_key[0], brd.stack->nonpawn_key[1],
        brd.stack->minor_key, brd.stack->major_key};
    for (int i = 0; i < 4; ++i) {
        const bool updated = correction_history_t::update_entry(
            correction_history->pieces[i][side][piece_keys[i] & (correction_history_t::size - 1)], bonus);
        changed |= updated;
    }
    const auto keys = correction_context_keys();
    for (int i = 0; i < 2; ++i) {
        if (keys[i]) {
            const bool updated = correction_history_t::update_entry(
                correction_history->continuation[i][side][keys[i] & (correction_history_t::size - 1)], bonus);
            changed |= updated;
        }
    }
    (void) changed;
}

void search_t::take_history(search_t & previous) {
    if (wild != 0 || previous.wild != 0 || !previous.quiet_history) {
        return;
    }
    quiet_history = std::move(previous.quiet_history);
    capture_history = std::move(previous.capture_history);

    // Learned evaluation errors belong to the immutable model that produced them.
    if (evaluation_network == previous.evaluation_network) {
        correction_history = std::move(previous.correction_history);
    }
    for (int piece = 0; piece <= BKING; ++piece) {
        std::copy(std::begin(previous.countermoves[piece]),
            std::end(previous.countermoves[piece]), std::begin(countermoves[piece]));
    }
}

void search_t::update_capture_cutoff(const move_t & move, int depth, int searched_moves) {
    const int d = std::clamp(depth, 1, 8);
    const int bonus = 32 * d * d;
    const bool noisy = move.capture || move.promotion;
    for (int i = 0; i < searched_moves; ++i) {
        const auto & failed = stack->searched[i];

        // Different sources and promotion choices may share the winner's slot.
        if ((failed.capture || failed.promotion)
                && !(noisy && capture_history_t::same_entry(failed, move))) {
            capture_history->update(failed, -bonus);
        }
    }
    if (noisy) {
        capture_history->update(move, bonus);
    }
}

std::array<const move_t *, 2> search_t::long_continuation_contexts() const {
    std::array<const move_t *, 2> contexts{};

    // Search-stack moves only: never read before the root or across a null pass.
    for (int distance = 1; distance <= 4 && distance <= brd.ply; ++distance) {
        const auto & previous = (stack - distance)->current_move;
        if (!previous.piece) {
            break;
        }
        if (distance == 2 || distance == 4) {
            contexts[distance / 2 - 1] = &previous;
        }
    }
    return contexts;
}

int search_t::move_history_score(const move_t & move) {

    // Read learned history in the parent position. Picker scores also contain
    // material/SEE stage offsets and must never become search history input.
    if (move.capture || move.promotion) {
        const int value = capture_history->score(move);
        return value;
    }
    const auto * previous = brd.ply > 0 ? &(stack - 1)->current_move : nullptr;
    int value = quiet_history->score(brd.us(), move, previous, brd.stack->pawn_key);
    const auto contexts = long_continuation_contexts();
    for (int i = 0; i < 2; ++i) {
        if (contexts[i]) {
            const int score = quiet_history->continuation_score(move, *contexts[i]);
            value += score;
        }
    }
    return value;
}

// Cached fail-highs supply ordering evidence even though no move was searched here.
void search_t::update_tt_history(int compact_move, int depth) {
    if (!brd.ply || stop_all
            || stack->excluded_move) {
        return;
    }
    move_t move{};
    move.set_compact(&brd, compact_move);

    // Hints can survive a score replacement. Validate before learning their indices.
    if (!move.piece || !brd.valid(&move) || !brd.legal(&move)) {
        return;
    }
    const int d = std::clamp(depth, 1, 8);
    const int bonus = 32 * d * d;
    const auto & previous = (stack - 1)->current_move;
    if (!move.capture && !move.promotion && !move.castle) {
        quiet_history->update(brd.us(), move, &previous, brd.stack->pawn_key,
            bonus, nullptr);
        for (const auto * context : long_continuation_contexts()) {
            if (context) {
                quiet_history->update_continuation(move, *context, bonus);
            }
        }
    }

    // An early quiet predecessor allowed this cached refutation. Penalize only
    // its continuation contexts, never moves that were not searched at this node.
    const int ordinal = (stack - 1)->move_count;
    if (!previous.piece || previous.capture || previous.promotion || previous.castle
            || ordinal < 1 || ordinal > 3) {
        return;
    }
    for (int distance = 2; distance <= 5 && distance <= brd.ply; ++distance) {
        const auto & context = (stack - distance)->current_move;
        if (!context.piece) {
            break;
        }
        if ((distance == 2)
                || ((distance == 3 || distance == 5))) {
            quiet_history->update_continuation(previous, context, -bonus);
        }
    }
}

void search_t::update_quiet_cutoff(move_t * move, int depth, int searched_moves) {
    const auto contexts = long_continuation_contexts();
    const auto * previous = brd.ply > 0 ? &(stack - 1)->current_move : nullptr;

    // Retain the provisional depth-weighted gravity rule; no tuning sweep.
    const int d = std::clamp(depth, 1, 8);
    const int bonus = 32 * d * d;
    for (int i = 0; i < searched_moves; ++i) {
        const auto & failed = stack->searched[i];
        if (!failed.capture && !failed.promotion && !failed.castle) {
            quiet_history->update(brd.us(), failed, previous, brd.stack->pawn_key,
                -bonus, move);

            // Piece/to aliases share every continuation slot with the winner.
            if (failed.piece != move->piece || failed.tsq != move->tsq) {
                for (int j = 0; j < 2; ++j) {
                    if (contexts[j]) {
                        quiet_history->update_continuation(failed, *contexts[j], -bonus);
                    }
                }
            }
        }
    }
    quiet_history->update(brd.us(), *move, previous, brd.stack->pawn_key, bonus, nullptr);
    for (int i = 0; i < 2; ++i) {
        if (contexts[i]) {
            quiet_history->update_continuation(*move, *contexts[i], bonus);
        }
    }
}

// Updates the killer moves after a quiet beta cutoff; the two stay different.
void search_t::update_killers(move_t * move) {
    assert(move->capture == EMPTY);
    assert(move->promotion == EMPTY);
    assert(move->castle == EMPTY);
    if (stack->killer[0].equals(move)) {
        return;
    }

    // Note: address(&stack->killer[1]) can be equal to address(move)
    move_t t;
    t.set(move);
    stack->killer[1].set(&stack->killer[0]);
    stack->killer[0].set(&t);
    assert(stack->killer[0].equals(&stack->killer[1]) == false);
}

// Collects the root moves; returns the number of legal moves.
int search_t::init_root_moves() {
    root.move_count = 0;
    root.moves[0].move.clear();
    stack->tt_key = position_key(brd);
    tt_hit_t hit;
    trans_table::probe(stack->tt_key, 0, brd.stack->fifty_count, hit);
    stack->tt_move.set_compact(&brd, hit.move);
    root.in_check = brd.in_check();
    stack->best_move.set(0);
    for (move_t * move = move::first(this, 1);
            move; move = move::next(this, 1)) {
        if (!game.search_moves.empty() && std::find(game.search_moves.begin(),
                game.search_moves.end(), move->to_string()) == game.search_moves.end()) {
            continue;
        }
        if (wild == 0 && !brd.legal(move)) {
            continue;
        }
        int gives_check = brd.gives_check(move);
        const bool is_dangerous = wild != 0 && (root.in_check || gives_check
            || move->capture || move->promotion);
        root_move_t * rmove = &root.moves[root.move_count++];
        rmove->init(move, gives_check, is_dangerous, wild != 0 ? brd.see(move) : 0);
    }
    return root.move_count;
}

// Order standard root moves like an interior node: the move picker with the
// previous best move as hash move. Root work counters move with their moves.
void search_t::order_root_moves(int depth) {
    int count = 0;
    stack->tt_move = stack->best_move;
    for (move_t * move = move::first(this, depth); move && count < root.move_count;
            move = move::next(this, depth)) {
        for (int i = count; i < root.move_count; ++i) {
            if (root.moves[i].move.equals(move)) {
                std::swap(root.moves[i], root.moves[count++]);
                break;
            }
        }
    }
}

// Guesses from the node distribution whether the position is complex.
bool root_t::is_complex() {
    const int64_t n0 = moves[0].nodes;
    for (int i = 1; i < move_count; i++) {
        if (moves[i].nodes >= n0) {
            return true;
        }
    }
    return false;
}

// Guesses from the node distribution whether the position is easy.
bool root_t::is_easy() {
    const int64_t n0 = moves[0].nodes / 8;
    for (int i = 1; i < move_count; i++) {
        if (moves[i].nodes >= n0) {
            return false;
        }
    }
    return true;
}

// Sorts the root moves (insertion sort).
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

// The parent position owns both the history lookup and the node context.
// At the real root, keep three alternatives at nominal depth before reducing.
int search_t::reduction(int depth, int searched_moves, int estimate, bool root_node) {
    if (depth < 3
            || searched_moves < (root_node ? 3 : 1)) {
        return 0;
    }

    // The estimate includes the normal ply; a reduction never extends the move.
    return std::clamp(estimate / 1024 - 1, 0, depth - 2);
}

// The unclamped depth decrement a move would receive at any depth, including the
// normal ply, in 1/1024 plies; pruning uses it to estimate the move's searched depth.
int search_t::reduction_estimate(int depth, int searched_moves, const move_t & move,
        bool pv, bool cut_node, int gives_check) {

    // Immutable shared table: avoid logarithms and initialization in the loop.
    // Saturation keeps unusually deep/wide positions within the sampled range.
    static const auto reductions = [] {
        std::array<int, 256> table{};
        for (int i = 1; i < 256; ++i) {
            table[i] = int(21.3 * std::log(i + 0.25 * std::log(i)));
        }
        return table;
    }();
    const int history = move_history_score(move);
    const bool noisy = move.capture || move.promotion;

    // Captures have one bounded history component; quiets combine several.
    const int history_divisor = noisy ? 4096 : 8192;
    bool refutation = false;
    if (!noisy) {
        const auto compact = move.to_compact();
        refutation = (compact == stack->killer[0].to_compact()
            || compact == stack->killer[1].to_compact());
        if (brd.ply > 0) {
            const auto & previous = (stack - 1)->current_move;
            refutation |= previous.piece && compact == countermoves[previous.piece][previous.tsq];
        }
    }

    // Base reduction plus the normal ply; non-improving nodes reduce one ply more.
    const int base = reductions[std::min(depth, 255)] * reductions[std::min(searched_moves + 1, 255)];
    int reduce = base + lmr_normal_offset + 1024 * (!stack->improving && base > 915);
    reduce += 987 * !pv + 1601 * cut_node
        + 999 * stack->lmr_tt_tactical + 1136 * stack->lmr_tt_fail_low
        + 384 * stack->lmr_alpha_raises;
    reduce -= std::clamp(history * 1024 / history_divisor, -lmr_history_limit, lmr_history_limit);
    reduce -= lmr_tt_pv_bonus * stack->tt_pv + lmr_tt_deep_bonus * stack->lmr_tt_deep
        + lmr_refutation_bonus * refutation + 1024 * noisy
        + lmr_check_bonus * (gives_check != 0) + stack->lmr_correction;
    return reduce;
}

void search_t::update_lmr_history(const move_t & move, int depth, int value, int alpha, int beta) {

    // Learn only from a completed full-depth verification of a reduced trial.
    if (stop_all || stack->in_check
            || move.capture || move.promotion || move.castle || depth < 3
            || (value > alpha && value < beta)) {
        return;
    }
    const int d = std::min(depth, 8);
    const int bonus = (value >= beta ? 16 : -16) * d * d;
    const auto * previous = brd.ply > 0 ? &(stack - 1)->current_move : nullptr;
    bool updated = false;
    if (previous && previous->piece) {
        quiet_history->update_continuation(move, *previous, bonus);
        updated = true;
    }
    for (const auto * context : long_continuation_contexts()) {
        if (context) {
            quiet_history->update_continuation(move, *context, bonus);
            updated = true;
        }
    }
    (void) updated;
}

// No child stack is available here. Resolve legal terminals and draws before
// falling back to static evaluation, including check positions with an evasion.
int search_t::ceiling_score() {
    if (!has_legal_move(brd)) {
        return stack->in_check ? -score::MATE + brd.ply : draw_score();
    }
    return is_draw() ? draw_score() : evaluate(this);
}

// Principal variation search at the root. Unlike pvs it sends new PVs to the
// interface at once, orders moves by PV and subtree size, never prunes or cuts
// off on the TT, and supports all wild variants.
int search_t::pvs_root(int alpha, int beta, int depth) {

    // A reduced aspiration re-search still belongs to the attempted iteration.
    const int report_depth = std::max(depth, attempted_depth);
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = stack->lmr_tt_fail_low = stack->lmr_tt_deep = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->reduction = 1024;
    stack->static_eval = score::INVALID;

    assert(root.move_count > 0);
    int best = -score::INF;
    if (wild != 0) {
        root.sort_moves(&stack->best_move);
    } else if (depth > 1) {
        order_root_moves(depth);
    }
    stack->tt_pv = wild == 0;
    if (stack->tt_pv && depth >= 3 && !stack->in_check) {
        const int eval = evaluate(this);
        stack->static_eval = eval;
        stack->lmr_correction = std::min(2048, std::abs(eval - stack->eval_result) * 1024 / 100);
    }

    /*
     * Moves loop
     */

    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        move_t * move = &rmove->move;
        U64 nodes_before = nodes;
        int score = 0;

        const int estimate = wild == 0
            ? reduction_estimate(depth, i, *move, true, false, rmove->gives_check) : 0;
        const int reduce = wild == 0 ? reduction(depth, i, estimate, true) : 0;

        // Go forward and search one level deeper
        stack->move_count = i + 1;
        forward(move, rmove->gives_check);
        if (i == 0) {
            score = -pvs(-beta, -alpha, depth - 1, false);
        } else {
            int new_depth = depth;
            const int reduced_depth = depth - 1 - reduce;
            stack->reduction = wild == 0 && depth >= 3 && i >= 3 ? estimate : 1024;
            score = -pvs(-alpha - 1, -alpha, reduced_depth, true);
            stack->reduction = 1024;
            if (!stop_all && score > alpha && reduce > 0) {
                const bool deeper = score > best + 32 + 8 * (reduce + 1);
                const bool shallower = score < best + new_depth;
                new_depth += int(deeper) - int(shallower);
                if (new_depth - 1 > reduced_depth && score < score::TABLEBASE - MAX_PLY) {
                    stack->reduction = 1024 * (1 + int(shallower) - int(deeper));
                    score = -pvs(-alpha - 1, -alpha, new_depth - 1, false);
                    stack->reduction = 1024;
                }
            }
            if (!stop_all && score > alpha && score < beta) {
                score = -pvs(-beta, -alpha, new_depth - 1, false);
            }
        }
        backward(move);

        // Handle results
        rmove->add_nodes(nodes - nodes_before);
        if (stop_all) {
            return alpha;
        } else if (score > best) {
            best = score;
            result_score = score;
            result_depth = report_depth;
            result_source = result_source_t::partial;

            // A searched move bounds the root from below. A fail-low move
            // cannot bound unsearched alternatives, so omit that root score.
            result_flag = score <= alpha ? 0 : score::LOWERBOUND;
            rmove->add_nodes(i);
            stack->best_move.set(move);
            bool exact = score::flags(score, alpha, beta) == score::EXACT;
            if (exact || !move->equals(&stack->pv_moves[0])) {
                update_pv(&rmove->move);
            }
            uci::send_pv(best, report_depth, sel_depth, nodes, game.tm.search_elapsed(),
                    pv_to_string().c_str(), result_flag, tablebase_hits);

            // A fail-high bounds the root immediately. A fail-low requires
            // searching every alternative before widening the root window.
            if (score >= beta) {
                return score;
            }
            if (best > alpha) {
                ++stack->lmr_alpha_raises;
                alpha = best;
            }
        }
    }
    return best;
}

// After at least four reversible plies: material, fifty quiet moves or repetition. The actual root
// needs three occurrences; descendants score the first match against game or
// search history as a draw.
bool search_t::is_reversible_draw() {
    if (brd.is_draw()) {
        return true;
    }
    if (brd.stack->fifty_count >= 100) {

        // Checkmate takes precedence. Probe fresh legal evasions, including quiet moves.
        stack->tt_move.clear();
        if (!stack->in_check || has_legal_move(brd)) {
            return true;
        }
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

        // Only the actual root requires an official threefold repetition.
        if (key == repetition_key && (brd.ply > 0 || ++matches == 2)) {
            return true;
        }
    }
    return false;
}

// Return the history distance proving that one legal reversible move can reach
// an earlier position. A descendant already scores the first such match as draw.
int search_t::upcoming_repetition(move_t & witness) {
    witness.clear();
    const int stop_ply = std::max(brd.ply - brd.stack->fifty_count, stack->repetition_start);

    if (wild != 0 || brd.ply >= MAX_PLY - 1 || brd.ply - stop_ply < 3) {
        return 0;
    }

    const U64 key = brd.stack->repetition_key();
    for (int ply = brd.ply - 3; ply >= stop_ply; ply -= 2) {
        const U64 target = ply >= 0 ? brd._stack[ply].repetition_key()
            : _position_history[static_cast<int>(_position_history.size()) + ply];
        move_t move{};

        if (!repetition::find_move(brd, key ^ target, move)) {
            continue;
        }

        // Full resulting identity also checks lost castling rights and expired
        // en passant. Board-only make/unmake leaves the search frame untouched.
        brd.forward(&move);
        const bool matches = brd.stack->repetition_key() == target;
        brd.backward(&move);

        if (matches) {
            witness = move;
            return brd.ply - ply;
        }

    }
    return 0;
}

// Compare real same-side positions only. A missing two-ply evaluation (for
// example, check) may fall back to four plies; an artificial pass is a barrier.
bool search_t::is_improving() const {
    if (stack->in_check || stack->eval_result == score::INVALID) {
        return false;
    }

    // Compare with the same side's last evaluated position, skipping checks.
    // Without one, assume improvement.
    for (int distance = 2; distance <= 4 && distance <= brd.ply; distance += 2) {
        const auto & previous = *(stack - distance);
        if (previous.eval_result != score::INVALID) {
            return stack->eval_result > previous.eval_result;
        }
    }
    return true;
}

// Principal variation search, fail-soft. cut_node marks an expected non-PV
// cutoff, not a proven score bound.
int search_t::pvs(int alpha, int beta, int depth, bool cut_node) {

    // No depth left: dive into quiescence search before any node setup.
    if (depth < 1) {
        return qsearch(alpha, beta, 0);
    }
    assert(!cut_node || alpha + 1 == beta);
    assert(alpha < beta);
    assert(alpha >= -score::INF);
    assert(beta <= score::INF);

    // The parent copies this node's principal variation, even after an early exit.
    stack->pv_count = 0;

    /*
     * Stop conditions
     */

    // Time
    if (abort()) {
        return alpha;
    }

    // Ceiling
    if (brd.ply >= (MAX_PLY - 1)) {
        return ceiling_score();
    }

    assert(depth > 0 && depth <= MAX_PLY);
    int alpha1 = alpha;

    // Mate distance pruning: a mate in n needs no deeper search
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

    // Draw by lack of material or fifty quiet moves
    if (is_draw()) {
        return draw_score();
    }
    int tablebase_score;
    if (!stack->excluded_move && tablebase_limit && probe_tablebase(depth, tablebase_score)) {
        return tablebase_score;
    }

    const bool pv = alpha + 1 < beta;
    if (raise_repetition_alpha(alpha) && alpha >= beta) {
        return alpha;
    }

    /*
     * Node setup
     */

    stack->improving = false;

    // A singular search keeps the main-line flag of the node it re-searches.
    if (!stack->excluded_move) {
        stack->tt_pv = false;
    }
    stack->lmr_tt_tactical = stack->lmr_tt_fail_low = stack->lmr_tt_deep = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->reduction = 1024;
    stack->static_eval = score::INVALID;
    stack->best_move.clear();
    sel_depth = std::max(brd.ply, sel_depth);

    /*
     * Transposition table lookup
     */

    const auto excluded_move = stack->excluded_move;
    const bool normal_search = !excluded_move;
    stack->tt_key = position_key(brd);
    tt_hit_t tt;
    const bool tt_hit = normal_search && trans_table::probe(stack->tt_key, brd.ply,
        brd.stack->fifty_count, tt);
    const int tt_move = tt.move, tt_flag = tt.bound, tt_score = tt.score;
    const int decisive_tt = score::TABLEBASE - MAX_PLY;

    // Mate and tablebase bounds are trusted at any depth.
    const bool proven = tt_flag && ((tt_score >= decisive_tt && (tt_flag & score::LOWERBOUND)
        && tt_score >= beta) || (tt_score <= -decisive_tt && (tt_flag & score::UPPERBOUND)
        && tt_score <= alpha));
    const bool usable_tt_score = tt_hit && tt_flag
        && (proven || tt.depth >= depth + int(tt_score >= beta));
    if (normal_search) {
        stack->tt_pv = (pv || tt.pv);
        stack->lmr_tt_deep = tt_hit && tt_flag && tt.depth >= depth;

        // A main-line node whose cached score already failed low is reduced more.
        stack->lmr_tt_fail_low = stack->tt_pv && tt_hit && tt_flag && tt_score <= alpha;
    }

    // Decode and check the hash move once. Only a key collision stores a move
    // that is invalid here; such an entry supplies neither a move nor a score.
    // Legality is tested when the move is made.
    stack->tt_move.set_compact(&brd, tt_move);
    if (stack->tt_move.piece && !brd.valid(&stack->tt_move)) {
        stack->tt_move.clear();
    }
    const bool tt_move_valid = !tt_move || stack->tt_move.piece;
    if (!pv && usable_tt_score && tt_move_valid && brd.stack->fifty_count < 90) {

        // A compatible exact score resolves either side of the window.
        if (tt_flag == score::EXACT) {
            if (tt_score >= beta
                    && std::abs(tt_score) < score::TABLEBASE - MAX_PLY) {
                update_tt_history(tt_move, depth);
            }
            return tt_score;
        } else if (tt_score >= beta && tt_flag == score::LOWERBOUND) {
            if (std::abs(tt_score) < score::TABLEBASE - MAX_PLY) {
                update_tt_history(tt_move, depth);
            }
            return tt_score;
        } else if (tt_score <= alpha && tt_flag == score::UPPERBOUND) {
            return tt_score;
        }
    }

    /*
     * Node pruning
     */

    const bool in_check = stack->in_check;
    const int eval = evaluate(this);
    stack->static_eval = eval;

    // A cached bound beyond the static evaluation refines it for static cutoffs.
    int refined_eval = eval;
    if (tt_hit && (tt_flag == score::EXACT || (tt_flag == score::UPPERBOUND && tt_score < eval)
            || (tt_flag == score::LOWERBOUND && tt_score > eval))) {
        refined_eval = tt_score;
    }
    stack->improving = is_improving();

    // Hindsight: a reduced move whose static evals disagree with the reduction adjusts depth.
    const auto & parent = *(stack - 1);
    if (brd.ply > 0 && !pv && !in_check && normal_search && parent.static_eval != score::INVALID) {
        if (parent.reduction >= 1419 && eval + parent.static_eval < 0) {
            ++depth;
        }
        if (depth >= 2 && parent.reduction >= 2494 && eval + parent.static_eval > 128) {
            --depth;
        }
    }
    if (normal_search) {
        auto & hint = stack->tt_move;
        stack->lmr_tt_tactical = hint.piece && (hint.capture || hint.promotion);
        if (!in_check && stack->eval_result != score::INVALID) {
            stack->lmr_correction = std::min(2048, std::abs(eval - stack->eval_result) * 1024 / 100);
        }
    }
    const int decisive_score = score::TABLEBASE - MAX_PLY;
    const auto & hint = stack->tt_move;

    // Razoring: a low static score triggers a quiescence verification, never a
    // cutoff by itself. The node's frame must survive the rejected probe.
    if (!pv && brd.ply && !in_check && !excluded_move
            && std::abs(alpha) < decisive_score && std::abs(beta) < decisive_score
            && std::abs(eval) < decisive_score && eval < alpha - razoring_margin_per_ply * depth) {
        int razor_score;
        {
            search_frame_scope_t scope(*this);
            razor_score = qsearch(alpha, beta, 0);
        }
        if (stop_all) {
            return alpha;
        }
        if (razor_score <= alpha && std::abs(razor_score) < decisive_score) {
            return razor_score;
        }
    }

    // Reverse futility pruning: the static evaluation supplies a selective
    // fail-high premise. A quiet hash move deserves a search, and its bound is
    // never used as a static evaluation.
    if (!pv && wild == 0 && depth < 9 && brd.ply && !in_check && !excluded_move
            && std::abs(beta) < decisive_score && std::abs(refined_eval) < decisive_score
            && refined_eval >= beta && !(hint.piece && !hint.capture && !hint.promotion)) {

        // Correction magnitude expresses uncertainty in the raw evaluator. A
        // threatened valuable piece must not strengthen the improving discount.
        const int correction = std::abs(stack->static_eval - int(stack->eval_result));
        const int margin = 100 * depth
            - 100 * int(stack->improving && !lower_value_threat(brd)) + correction / 2;
        if (refined_eval - margin >= beta) {
            return beta + (refined_eval - beta) / 3;
        }
    }

    // Null move pruning: skip it when the cache predicts that it fails or that a capture wins
    const bool tt_refutes_null = tt_hit && ((tt_flag == score::UPPERBOUND && tt_score < beta)
        || (tt_flag == score::LOWERBOUND && hint.piece && brd.max_gain(&hint) > 2 * board::PVAL[WPAWN]));
    const bool do_prune_node = !excluded_move && !in_check && !pv && !tt_refutes_null
        && std::abs(beta) < decisive_score && brd.has_pieces(brd.us());
    const bool previous_null = brd.ply > 0 && !(stack - 1)->current_move.piece;
    const bool null_policy = brd.ply > 0 && cut_node && depth > 2 && normal_search
        && eval + 132 * int(stack->improving) >= beta + 8 * depth;
    const bool do_null = null_allowed && !previous_null && null_policy;

    if (do_prune_node && do_null) {

        // Stronger static evidence and deeper nodes permit a shallower null
        // search. A tactical hash move is ordering evidence, never a score bound.
        const bool tactical_hint = hint.piece && (hint.capture || hint.promotion);
        const int surplus = std::clamp((eval - beta) / 174, 0, 4);
        const int null_depth = std::max(0, depth - 4 - depth / 3 - surplus - int(tactical_hint));
        forward();
        int null_score = -pvs(-beta, -beta + 1, null_depth, !cut_node);
        backward();
        if (stop_all) {
            return alpha;
        } else if (null_score >= beta && std::abs(null_score) < decisive_score) {
            if (depth >= 12) {

                // Confirm the cutoff with real moves and no further null moves;
                // even a null probe reduced to qsearch needs it.
                int verified_score;
                {
                    search_frame_scope_t scope(*this);
                    null_allowed = false;
                    verified_score = pvs(beta - 1, beta, std::max(1, null_depth), cut_node);
                }
                if (stop_all) {
                    return alpha;
                }
                if (verified_score >= beta) {
                    return verified_score;
                }
            } else {

                // No verification
                return null_score;
            }
        }
    }

    // Internal iterative reduction: without a hash move, deep move ordering is
    // less reliable. Reduce the whole node once; everything downstream,
    // including TT storage, uses the searched depth.
    if (brd.ply && depth >= 6 && (pv || cut_node) && !in_check
            && !excluded_move && !hint.piece) {
        --depth;
    }

    // ProbCut: a tactical move that survives qsearch and a reduced main search
    // justifies a selective fail-high. Neither test is a proof at full depth.
    if (!pv && brd.ply && depth >= 5 && !in_check && !excluded_move
            && brd.ply < MAX_PLY - 2 && std::abs(alpha) < decisive_score
            && std::abs(beta) < decisive_score && std::abs(eval) < decisive_score) {
        constexpr int margin = 250, improving_margin = 50;
        constexpr int eval_divisor = 300, adaptive_offset = 100, adaptive_divisor = 200;
        const int threshold = beta + margin - improving_margin * int(stack->improving);
        tt_hit_t cached;

        // A deep enough cached score below the threshold makes the cut unlikely.
        const bool unlikely = threshold >= decisive_score
            || (trans_table::probe(stack->tt_key, brd.ply, brd.stack->fifty_count, cached)
                && cached.bound && cached.depth >= depth - 3 && cached.score < threshold);
        if (!unlikely) {
            const int cached_move = cached.move;
            search_frame_scope_t frame(*this);
            move::list_t candidates;
            move::gen_captures(&brd, &candidates);
            move::gen_promotions(&brd, &candidates);
            for (auto * candidate = candidates.first; candidate != candidates.last; ++candidate) {
                candidate->score = 16 * brd.mvvlva(candidate) + move_history_score(*candidate)
                    + (candidate->to_compact() == cached_move ? 1000000 : 0);
            }
            const int base_depth = std::clamp(depth - 3 - (eval - beta) / eval_divisor, 0, depth - 1);
            const int see_threshold = (threshold - eval) * 3 / 4;
            while (candidates.first != candidates.last) {
                auto * best_candidate = std::max_element(candidates.first, candidates.last,
                    [](const move_t & a, const move_t & b) { return a.score < b.score; });
                move_t candidate = *best_candidate;
                *best_candidate = *--candidates.last;
                if (!brd.see(&candidate, see_threshold) || !brd.legal(&candidate)) {
                    continue;
                }
                int result, searched_depth = base_depth, search_beta = threshold;
                {

                    // Undo even if a child throws; frame restoration alone cannot undo
                    // a made move. The qsearch screen and main search share this child.
                    forward(&candidate, brd.gives_check(&candidate));
                    struct undo_t {
                        search_t & search;
                        move_t & move;
                        ~undo_t() { search.backward(&move); }
                    } undo{*this, candidate};
                    result = -qsearch(-threshold, -threshold + 1, 0);
                    if (!stop_all && result >= threshold && std::abs(result) < decisive_score) {
                        searched_depth = std::max(0, base_depth
                            - std::clamp((result - threshold - adaptive_offset) / adaptive_divisor, 0, 3));

                        // Shallower verification requires a correspondingly higher bar.
                        search_beta = std::min(threshold + (base_depth - searched_depth) * adaptive_divisor,
                            decisive_score - 1);
                        if (searched_depth > 0) {
                            result = -pvs(-search_beta, -search_beta + 1, searched_depth, !cut_node);
                            if (!stop_all && result < search_beta && search_beta > threshold) {
                                searched_depth = base_depth;
                                search_beta = threshold;
                                result = -pvs(-threshold, -threshold + 1, searched_depth, !cut_node);
                            }
                        }
                    }
                }
                if (stop_all) {
                    return alpha;
                }
                if (result >= search_beta && std::abs(result) < decisive_score) {

                    // Cache only the depth actually verified, before removing the margin.
                    // Never label a ProbCut result as a full-depth bound.
                    trans_table::store(stack->tt_key, brd.root_ply, brd.ply, searched_depth + 1,
                        result, candidate.to_compact(), score::LOWERBOUND);
                    return result - (search_beta - beta);
                }
            }
        }
    }

    /*
     * Moves loop
     */

    // Singular extension: only a compatible lower or exact hash score can nominate
    // an only move. The alternatives are searched in the same position without
    // it; failing low is a heuristic reason for extra depth, not a proven bound.
    uint16_t singular_move = 0;
    int singular_extension = 0;
    const int root_depth = attempted_depth ? attempted_depth : depth;
    if (brd.ply && depth >= 7 && brd.ply < 2 * root_depth
            && brd.ply < MAX_PLY - 2 && !in_check && !excluded_move
            && std::abs(alpha) < decisive_score && std::abs(beta) < decisive_score && hint.piece) {
        tt_hit_t hit;
        const bool found = trans_table::probe(stack->tt_key, brd.ply, brd.stack->fifty_count, hit);
        const int threshold = hit.score - depth;
        if (found && hit.depth >= depth - 3 && hit.move == hint.to_compact()
                && (hit.bound == score::LOWERBOUND || hit.bound == score::EXACT)
                && std::abs(hit.score) < decisive_score && std::abs(threshold) < decisive_score) {
            const bool quiet = !hint.capture && !hint.promotion;
            int alternative;
            {
                search_frame_scope_t scope(*this);
                stack->excluded_move = uint16_t(hit.move);
                alternative = pvs(threshold - 1, threshold, (depth - 1) / 2, cut_node);
            }
            if (stop_all) {
                return alpha;
            }
            if (alternative < threshold) {
                singular_move = uint16_t(hit.move);
                singular_extension = 1;

                // Extra depth belongs to non-PV forced lines. A line budget prevents
                // repeated multiple extensions from sustaining the remaining depth.
                if (!pv && stack->multiple_extensions < 12 && alternative < threshold - 16) {
                    singular_extension = 2 + int(quiet && alternative < threshold - 64);
                }
            } else if (!pv && alternative >= beta && std::abs(alternative) < decisive_score) {

                // Reduced alternative work supplies a selective lower bound, without
                // inventing a full-depth TT entry or training the unsearched TT move.
                return alternative;
            } else if (hit.score >= beta) {
                singular_move = uint16_t(hit.move);
                singular_extension = -3 + int(pv);
            } else if (cut_node) {
                singular_move = uint16_t(hit.move);
                singular_extension = -2;
            }
        }
    }

    // Without a first move it is checkmate or stalemate
    move_t * move = move::first(this, depth);
    if (!move) {
        if (excluded_move) {
            return alpha;
        }
        return in_check ? -score::MATE + brd.ply : draw_score();
    }

    // Prepare and run the loop
    int best = -score::INF;
    int searched_moves = 0;

    // Late-move pruning and reductions count every legal move considered, pruned ones included.
    int considered_moves = 0;
    const int score_max = score::MATE - brd.ply - 1;

    // A quiet's remaining reduction terms are nonnegative. Even its maximum
    // history, refutation and check bonuses cannot search deeper than this.
    const int minimum_estimate = lmr_normal_offset - lmr_history_limit - lmr_refutation_bonus - lmr_check_bonus
            - lmr_tt_pv_bonus * stack->tt_pv - lmr_tt_deep_bonus * stack->lmr_tt_deep
            - stack->lmr_correction;
    const bool quiet_lmp_certain = std::max(0, depth
        - (minimum_estimate + lmr_tt_pv_depth_offset * stack->tt_pv - lmr_depth_offset) / 1024) < 9;
    const int move_limit = (3 + depth * depth) / (2 - int(stack->improving));
    stack->best_move.clear();
    do {
        if (excluded_move && move->to_compact() == excluded_move) {
            continue;
        }

        // Legality first: illegal moves are neither counted nor made.
        if (!brd.legal(move)) {
            continue;
        }
        ++considered_moves;

        assert(brd.valid(move));
        assert(!stack->best_move.equals(move));
        assert(in_searched(move, searched_moves) == false);

        const bool nominated = singular_move && move->to_compact() == singular_move;
        const bool singular = nominated && singular_extension > 0;
        const bool quiet_pruning_allowed = !singular
            && brd.ply && searched_moves && !in_check && !stop_all
            && !move->capture && !move->promotion && best > -decisive_score;

        // The existing LMP predicate is certain: do not compute unused check
        // and history facts. Legality and the considered ordinal remain exact.
        if (quiet_pruning_allowed && quiet_lmp_certain && considered_moves >= move_limit) {
            continue;
        }
        const int gives_check = brd.gives_check(move);
        assert(gives_check == 0 || gives_check == 1 || gives_check == 2);
        const int estimate = singular ? 0
            : reduction_estimate(depth, considered_moves - 1, *move, pv, cut_node, gives_check);
        const int reduce = singular ? 0 : reduction(depth, searched_moves, estimate);
        const int lmr_depth = std::max(0, depth - (estimate + lmr_tt_pv_depth_offset * stack->tt_pv - lmr_depth_offset) / 1024);

        // Late move and history pruning: a searched legal alternative protects
        // terminals and losing or mating lines. The move limit grows quadratically
        // with depth, larger when improving, and counts every legal move considered.
        if (quiet_pruning_allowed) {
            const bool lmp_prune = lmr_depth < 9 && considered_moves >= move_limit;
            const bool history_prune = !lmp_prune && lmr_depth < 7
                && !move->equals(&stack->killer[0]) && !move->equals(&stack->killer[1])
                && move_history_score(*move) < -2048 * depth;
            if (lmp_prune || history_prune) {
                continue;
            }
        }

        // Futility pruning: the move's optimistic gain cannot reach alpha. Only
        // parent-position history counts, never picker bonuses; captures get a
        // material allowance, including the en-passant victim.
        if (!singular && wild == 0 && brd.ply && searched_moves && !in_check
                && !gives_check && !stop_all && best > -decisive_score
                && std::abs(alpha) < decisive_score && std::abs(beta) < decisive_score
                && !move->promotion && lmr_depth < (move->capture ? 8 : 6)) {
            const int margin = (move->capture ? 200 : 100) * (1 + lmr_depth)
                + move_history_score(*move) / 64 + (move->capture ? brd.max_gain(move) : 0);
            if (eval + margin <= alpha) {
                continue;
            }
        }

        // SEE pruning: SEE only models exchanges on one square, so a legal
        // alternative is searched first, and deeper nodes allow larger sacrifices.
        if (brd.ply && searched_moves && best > -decisive_score
                && !brd.see(move, move->capture ? -main_see_capture_margin * depth
                    : -main_see_quiet_margin * depth * depth)) {
            continue;
        }

        // Move extensions and reductions

        const int extend = nominated ? singular_extension : 0;


        // Go forward and search the next node

        forward(move, gives_check);
        stack->multiple_extensions += extend >= 2;
        (stack - 1)->move_count = considered_moves;
        int score;
        bool lmr_researched = false;
        if (searched_moves == 0) {
            score = -pvs(-beta, -alpha, depth - 1 + extend, !pv && !cut_node);
        } else {
            int new_depth = depth + extend;
            const int reduced_depth = new_depth - 1 - reduce;
            stack->reduction = depth >= 3 && !singular ? estimate : 1024;
            score = -pvs(-alpha - 1, -alpha, reduced_depth, reduce > 0 || !cut_node);
            stack->reduction = 1024;
            if (!stop_all && score > alpha && reduce > 0) {

                // Adjust the full-depth re-search by how far the reduced result beat the best score.
                lmr_researched = true;
                const bool deeper = score > best + 32 + 8 * (reduce + 1);
                const bool shallower = score < best + new_depth;
                new_depth += int(deeper) - int(shallower);
                if (new_depth - 1 > reduced_depth && score < score::TABLEBASE - MAX_PLY) {
                    stack->reduction = 1024 * (1 + int(shallower) - int(deeper));
                    score = -pvs(-alpha - 1, -alpha, new_depth - 1, !cut_node);
                    stack->reduction = 1024;
                }
            }
            if (!stop_all && pv && score > alpha && score < beta) {
                score = -pvs(-beta, -alpha, new_depth - 1, false);
            }
        }
        backward(move);
        if (lmr_researched) {
            update_lmr_history(*move, depth, score, alpha, beta);
        }

        // Handle results: update the best value or cut off

        if (stop_all) {
            return alpha;
        } else if (score > best) {
            stack->best_move.set(move);
            if (score >= beta) {
                if (normal_search) {
                    trans_table::store(stack->tt_key, brd.root_ply, brd.ply, depth, score, move->to_compact(), score::LOWERBOUND,
                        score::INVALID, stack->tt_pv);
                }
                update_correction_history(score, score::LOWERBOUND, depth);
                if (normal_search) {
                    update_capture_cutoff(*move, depth, searched_moves);
                }
                if (normal_search && !move->capture && !move->promotion && !move->castle) {
                    if (brd.ply > 0 && (stack - 1)->current_move.piece) {
                        const auto & previous = (stack - 1)->current_move;
                        countermoves[previous.piece][previous.tsq] = move->to_compact();
                    }
                    update_quiet_cutoff(move, depth, searched_moves);

                    // A returned move can alias killer[1]; update killers last.
                    update_killers(move);

                }
                return score;
            }
            best = score;
            if (best > alpha) {
                update_pv(move);
                ++stack->lmr_alpha_raises;
                alpha = best;
            }
            if (best >= score_max) {
                break;
            }
        }
        stack->searched[searched_moves++].set(move);
    } while ((move = move::next(this, depth)));

    /*
     * Store the result
     */

    assert(!stop_all);
    if (best == -score::INF) {
        if (excluded_move) {
            return alpha;
        }
        return in_check ? -score::MATE + brd.ply : draw_score();
    }
    assert(best > -score::INF);
    assert(best < beta);
    assert(brd.valid(&stack->best_move));
    assert(brd.legal(&stack->best_move));

    int flag = score::flags(best, alpha1, beta);
    update_correction_history(best, flag, depth);
    if (normal_search) {
        trans_table::store(stack->tt_key, brd.root_ply, brd.ply, depth, best, stack->best_move.to_compact(), flag,
            score::INVALID, stack->tt_pv);
    }
    return best;
}

// Quiescence search: captures and promotions at every depth, all legal evasions
// in check.
int search_t::qsearch(int alpha, int beta, int depth) {

    // The parent copies this node's principal variation, even after an early exit.
    stack->pv_count = 0;

    assert(depth <= 0);
    assert(!stack->excluded_move);
    assert(alpha < beta);
    assert(alpha >= -score::INF);
    assert(beta <= score::INF);

    if (abort()) {
        return alpha;
    }

    if (brd.ply >= (MAX_PLY - 1)) {
        return ceiling_score();
    }

    // Mate distance pruning: a mate in n needs no deeper search

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

    if (is_draw()) {
        return draw_score();
    }

    int tablebase_score;

    if (tablebase_limit && probe_tablebase(depth, tablebase_score)) {
        return tablebase_score;
    }

    const bool legal_witness = raise_repetition_alpha(alpha);

    if (legal_witness && alpha >= beta) {
        return alpha;
    }

    stack->best_move.clear();
    stack->tt_move.clear();
    sel_depth = std::max(brd.ply, sel_depth);

    const bool in_check = stack->in_check;
    const U64 key = position_key(brd);
    stack->tt_key = key;
    tt_hit_t tt;

    const bool usable = trans_table::probe(key, brd.ply, brd.stack->fifty_count, tt) && tt.bound;
    const int tt_score = tt.score, tt_flag = tt.bound;

    if (usable && alpha + 1 == beta && brd.stack->fifty_count < 90 && (tt_flag == score::EXACT
            || (tt_flag == score::LOWERBOUND && tt_score >= beta)
            || (tt_flag == score::UPPERBOUND && tt_score <= alpha))) {
        return tt_score;
    }

    int deferred_raw_eval = score::INVALID;

    // Forced inline: GCC otherwise splits the store into a call at every exit.
    const auto finish = [&](int value, int flag) __attribute__((always_inline)) {

        trans_table::store(key, brd.root_ply, brd.ply, 0, value,
            stack->best_move.to_compact(), flag, deferred_raw_eval, false, tt.slot);

        return value;
    };

    const int eval = evaluate(this, &tt, &deferred_raw_eval);

    if (eval >= beta && !in_check) {
        return finish(eval, score::LOWERBOUND);
    }

    stack->tt_move.set_compact(&brd, tt.move);
    move_t * move = move::first(this, depth);

    if (!move) {

        if (in_check) {
            return finish(-score::MATE + brd.ply, score::EXACT);
        }

        // Match the normal fail-low path even when the picker is empty.
        // Merely having an illegal tactical candidate must not change the bound.
        return !legal_witness && !has_legal_move(brd, in_check) ? finish(draw_score(), score::EXACT)
            : finish(eval, score::UPPERBOUND);
    }

    if (eval > alpha && !in_check) {
        alpha = eval;
    }

    // Keep the best searched score independently of the window: a fail-low
    // still carries a useful upper bound below alpha.
    int best_score = in_check ? -score::INF : eval;
    int searched_moves = 0;
    const int decisive = score::TABLEBASE - MAX_PLY;
    const int futility = eval + quiescence_futility_margin;
    do {

        if (in_check && searched_moves > 0 && best_score > -decisive
                && !brd.see(move, 0)) {
            continue;
        }

        // Futility: a losing tactical move cannot lift a hopeless stand pat.
        int gives_check = -1;

        if (!in_check && best_score > -decisive
                && futility <= alpha && std::abs(futility) < decisive
                && !((stack - 1)->current_move.piece
                    && move->tsq == (stack - 1)->current_move.tsq)) {
            gives_check = brd.gives_check(move);

            if (!gives_check && !brd.see(move, 1)) {
                best_score = std::max(best_score, futility);
                continue;
            }

        }

        if (!brd.legal(move)) {
            continue;
        }

        if (gives_check < 0) {
            gives_check = brd.gives_check(move);
        }

        // Publish before recursion so descendants see the same raw-eval entry.
        // A node without children combines this value with its result store.
        if (deferred_raw_eval != score::INVALID) {
            trans_table::store(key, brd.root_ply, brd.ply, 0, 0, 0, 0, deferred_raw_eval, false, tt.slot);
            deferred_raw_eval = score::INVALID;
        }
        forward(move, gives_check);
        ++searched_moves;
        int score = -qsearch(-beta, -alpha, depth - 1);
        backward(move);

        if (stop_all) {
            return alpha;
        }

        best_score = std::max(best_score, score);

        if (score > alpha) {
            stack->best_move.set(move);

            if (score >= beta) {
                return finish(score, score::LOWERBOUND);
            }

            alpha = score;
        }

    } while ((move = move::next(this, depth)));

    if (searched_moves == 0 && (in_check || (!legal_witness && !has_legal_move(brd, in_check)))) {
        return finish(in_check ? -score::MATE + brd.ply : draw_score(), score::EXACT);
    }

    return finish(best_score, score::UPPERBOUND);
}

// True if the move is one of the two killer moves.
bool search_t::is_killer(move_t * const move) {
    return !move->capture && (stack->killer[0].equals(move) || stack->killer[1].equals(move));
}

// True if the move is among the moves already searched.
bool search_t::in_searched(move_t* move, int searched_moves) {
    for (int i = 0; i < searched_moves; i++) {
        if (stack->searched[i].equals(move)) {
            return true;
        }
    }
    return false;
}
