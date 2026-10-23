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
#include <bit>
#include "repetition.h"
#include <limits>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include "book.h"
#include "uci_console.h"
#include "eval.h"
#include "progress.h"
#include <stdexcept>
#include "timeman.h"
#include "tablebase.h"

namespace {

constexpr U64 game_history_salt = 0x9e3779b97f4a7c15ULL;
constexpr U64 search_history_salt = 0xd1b54a32d192ed03ULL;
// Conservative margins in the shared 100-cp pawn scale. Keep the baseline
// independent of history, improving flags and evaluator-specific adjustments.
constexpr int razoring_margin_per_ply = 300;
constexpr int main_see_quiet_margin = 50;
constexpr int main_see_capture_margin = 100;
constexpr int quiescence_futility_margin = 350;

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

U64 mix_context(U64 key) {
    key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
    key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
    return key ^ (key >> 31);
}

// Same-position helpers may replace the picker, PV, evaluation and repetition
// frame. Restore all of it before the caller resumes, retaining clock limits.
struct search_frame_scope_t {
    search_t & search;
    search_stack_t * frame;
    search_stack_t previous;
    bool null_enabled;

    explicit search_frame_scope_t(search_t & search)
        : search(search), frame(search.stack), previous(*search.stack),
          null_enabled(search.null_enabled) {}

    ~search_frame_scope_t() {
        assert(search.stack == frame);
        const auto clocks = frame->tt_clocks;
        *frame = previous;
        frame->tt_clocks.include_child(clocks, 0);
        search.null_enabled = null_enabled;
    }
};

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
        if (brd.legal(&move)) {
            return true;
        }
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
    // Share score bounds within a clock bucket, retaining move hints across
    // buckets. Existing propagated clock ranges still guard terminal draws.
    const int clock_bucket = wild == 0 ? rule50_bucket(brd.stack->fifty_count) : 0;
    return stack->tt_history ^ node_kind
        ^ (clock_bucket ? mix_context(U64(clock_bucket) ^ 0x72756c6535303031ULL) : 0)
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
// Raw network scores depend on board/side and the immutable model snapshot,
// not repetition history or the halfmove clock. Scaling is applied separately.
int search_t::neural_static_score() {
    assert(evaluation_network);
    constexpr size_t entries = 65536;
    if (_neural_cache.empty()) {
        _neural_cache.resize(entries);
    }
    auto & cached = _neural_cache[brd.stack->tt_key & (entries - 1)];
    if (cached.key == brd.stack->tt_key && cached.value != score::INVALID) {
        return cached.value;
    }
    int tt_eval;
    if (tt_eval_enabled) {
        if (trans_table::retrieve_eval(brd.stack->tt_key, evaluation_network->fingerprint(), tt_eval)) {
            cached = {brd.stack->tt_key, tt_eval};
            return tt_eval;
        }
    }
    auto & accumulator = evaluation_accumulators[brd.ply];
    const int value = std::clamp(evaluation_network->evaluate(brd, accumulator,
        std::span<const nnue::accumulator_t>(evaluation_accumulators.data(), brd.ply)),
        -score::TABLEBASE + 1, score::TABLEBASE - 1);
    if (tt_eval_enabled) {
        trans_table::store_eval(brd.stack->tt_key, evaluation_network->fingerprint(), value, brd.root_ply);
    }
    cached = {brd.stack->tt_key, value};
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
    _experience_depth = options::get_value("ExperienceDepth");
    _experience = experience::snapshot();
    _book_seeded = false;
    beta_pruning = options::get_value("BetaPruning");
    null_verify = options::get_value("NullVerify");
    null_enabled = options::get_value("NullMove");
    lmr_enabled = options::get_value("LMR");
    lmr_history_enabled = wild == 0 && options::get_value("LMRHistory");
    aspiration_enabled = options::get_value("Aspiration");
    lmp_enabled = wild == 0 && options::get_value("LMP");
    history_pruning_enabled = wild == 0 && options::get_value("HistoryPruning");
    tt_history_enabled = wild == 0 && options::get_value("TTHistory");
    probcut_enabled = wild == 0 && options::get_value("ProbCut");
    singular_extensions_enabled = wild == 0 && options::get_value("SingularExtensions");
    razoring_enabled = wild == 0 && options::get_value("Razoring");
    main_see_enabled = wild == 0 && options::get_value("MainSEE");
    iir_enabled = wild == 0 && options::get_value("IIR");
    q_tt_enabled = wild == 0 && options::get_value("QsearchTT");
    quiet_history_policy = {options::get_value("MainHistory") != 0,
        options::get_value("ContinuationHistory") != 0, options::get_value("PawnHistory") != 0};
    capture_history_enabled = wild == 0 && options::get_value("CaptureHistory");
    long_continuation_enabled = wild == 0 && options::get_value("LongContinuationHistory");
    threat_ordering_enabled = wild == 0 && options::get_value("ThreatOrdering");
    check_ordering_enabled = wild == 0 && options::get_value("CheckOrdering");
    tt_eval_enabled = wild == 0 && options::get_value("TTEval");
    correction_history_enabled = wild == 0 && options::get_value("CorrectionHistory");
    piece_correction_enabled = wild == 0 && options::get_value("PieceCorrection");
    continuation_correction_enabled = wild == 0 && options::get_value("ContinuationCorrection");
    countermoves_enabled = options::get_value("Countermoves");
    killers_enabled = options::get_value("Killers");
    upcoming_repetition_enabled = wild == 0 && options::get_value("UpcomingRepetition");
    evasions_enabled = wild == 0 && options::get_value("CheckEvasions");
    ffp_enabled = options::get_value("FutilityPruning");
    draw_contempt = options::get_value("DrawContempt");
    evaluation_network.reset();
    if (wild == 0) {
        std::string error;
        if (!nnue::load_default(error)) {
            throw std::runtime_error("NNUE network required: " + error);
        }
        evaluation_network = nnue::snapshot();
    }
    _neural_cache.clear();
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
    memset(countermoves, 0, sizeof(countermoves));
    quiet_history = wild == 0 ? std::make_unique<quiet_history_t>() : nullptr;
    capture_history = wild == 0 ? std::make_unique<capture_history_t>() : nullptr;
    correction_history = wild == 0 ? std::make_unique<correction_history_t>() : nullptr;
    stack->eval_result = score::INVALID;
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->excluded_move = 0;
    stack->move_count = 0;
    stack->multiple_extensions = 0;
    stack->repetition_start = -static_cast<int>(_position_history.size());
    init_tt_history();
    stack->tt_clocks = {0, 101};
}

/**
 * Lookup current position in polyglot book database (file book.bin)
 */
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
    if (book_moves.first == book_moves.last && (wild != 0 || !_experience)) {
        return false;
    }
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
                game.search_moves.end(), move->to_string()) == game.search_moves.end()) {
            continue;
        }
        if (wild == 0 && !brd.legal(move)) {
            continue;
        }
        double weight = 0;
        for (auto entry = book_moves.first; entry != book_moves.last; ++entry)
            if (move->equals(entry)) {
                weight += entry->score;
            }
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
    if (candidates.empty()) {
        return false;
    }
    if (!_book_seeded) {
        _book_random.seed(std::random_device{}());
        _book_seeded = true;
    }
    // Sparse unanalysed experience competes with doing a fresh search.
    if (!has_reference && !has_analysis) {
        weights.push_back(mean_weight);
    }
    std::discrete_distribution<size_t> choose(weights.begin(), weights.end());
    const size_t selected = choose(_book_random);
    if (selected == candidates.size()) {
        return false;
    }
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
        + " interrupted " + std::to_string(interrupted)
        + " pruned_moves " + std::to_string(pruned_nodes));
    uci::send_pv(result_score, result_depth, sel_depth, nodes, game.tm.elapsed(),
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
    // At a zero clock the WDL outcome is solved, including full-window nodes.
    // Re-searching a proven win with static evaluation would retract its
    // reserved-range score when the window widens.
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
            score = -pvs(-score::INF, score::INF, depth - 1, false);
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
    if (!aspiration_enabled || depth < 2) {
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
        const int remaining = game.tm.remaining();
        // Tighten polling near the deadline without taxing the ordinary hot path.
        next_poll = remaining <= 50 ? 64 : remaining <= 250 ? 512 : NODES_BETWEEN_POLLS;
        result = !pondering() && game.tm.time_is_up() && root_stack->best_move.piece > 0;
    }
    stop_all = result;
    return result;
}

/**
 * Do a null move
 */
void search_t::forward() {
    (stack + 1)->multiple_extensions = stack->multiple_extensions;
    stack->current_move.clear();
    stack++;
    stack->in_check = false;
    stack->eval_result = score::INVALID;
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->excluded_move = 0;
    stack->move_count = 0;
    brd.forward();
    if (wild == 0 && evaluation_network) {
        evaluation_network->prepare(brd, nullptr, evaluation_accumulators[brd.ply]);
    }
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
    // Count one node per move made; null moves and re-searches of an
    // already made move are not new nodes.
    ++nodes;
    (stack + 1)->multiple_extensions = stack->multiple_extensions;
    stack->current_move.set(move);
    (stack + 1)->repetition_start = stack->repetition_start;
    (stack + 1)->tt_history = stack->tt_history;
    stack++;
    stack->in_check = gives_check;
    stack->tt_clocks = {0, 101};
    stack->eval_result = score::INVALID;
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->excluded_move = 0;
    stack->move_count = 0;
    brd.forward(move);
    if (wild == 0 && evaluation_network) {
        evaluation_network->prepare(brd, move, evaluation_accumulators[brd.ply]);
    }
    if (brd.stack->fifty_count == 0) {
        stack->tt_history = 0;
    } else if (brd.ply - 1 >= stack->repetition_start) {
        stack->tt_history += mix_context((brd.stack - 1)->repetition_key()
            ^ (brd.ply == 1 ? game_history_salt : search_history_salt));
    }
    assert(stack == &_stack[brd.ply]);
}

// Search accepts a candidate only after making it. Castling additionally
// requires safe origin/transit squares, which the final position cannot prove.
bool search_t::try_forward(move_t * move, bool gives_check) {
    if (move->castle && !brd.legal(move)) {
        return false;
    }
    const move_t previous = stack->current_move;
    forward(move, gives_check);
    if (brd.legal()) {
        return true;
    }
    // A rejected candidate has no searched result or clock constraints to merge.
    brd.backward(move);
    --stack;
    stack->current_move = previous;
    --nodes;
    return false;
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

std::array<U64, 2> search_t::correction_context_keys() const {
    std::array<U64, 2> keys{};
    if (!continuation_correction_enabled || brd.ply < 2) {
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
    if (piece_correction_enabled) {
        const U64 keys[] = {brd.stack->nonpawn_key[0], brd.stack->nonpawn_key[1],
            brd.stack->minor_key, brd.stack->major_key};
        for (int i = 0; i < 4; ++i) {
            value += correction_history->pieces[i][side][keys[i] & mask];
        }
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
    if (!correction_history_enabled) {
        return raw;
    }
    const int correction = correction_value();
    constexpr int limit = score::TABLEBASE - MAX_PLY - 1;
    return std::clamp(raw + correction, -limit, limit);
}

void search_t::update_correction_history(int value, int bound, int depth) {
    constexpr int decisive = score::TABLEBASE - MAX_PLY;
    const auto & move = stack->best_move;
    if (!correction_history_enabled || stop_all || stack->in_check
            || stack->excluded_move || depth < 1 || !move.piece
            || move.capture || move.promotion || stack->eval_result == score::INVALID
            || std::abs(value) >= decisive || std::abs(stack->eval_result) >= decisive
            || brd.stack->fifty_count >= 90) {
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
    if (piece_correction_enabled) {
        const U64 keys[] = {brd.stack->nonpawn_key[0], brd.stack->nonpawn_key[1],
            brd.stack->minor_key, brd.stack->major_key};
        for (int i = 0; i < 4; ++i) {
            const bool updated = correction_history_t::update_entry(
                correction_history->pieces[i][side][keys[i] & (correction_history_t::size - 1)], bonus);
            changed |= updated;
        }
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
    if (!capture_history_enabled) {
        return;
    }
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
    if (!long_continuation_enabled) {
        return contexts;
    }
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
        if (!capture_history_enabled) {
            return 0;
        }
        const int value = capture_history->score(move);
        return value;
    }
    const auto * previous = brd.ply > 0 ? &(stack - 1)->current_move : nullptr;
    int value = quiet_history->score(brd.us(), move, previous, brd.stack->pawn_key, quiet_history_policy);
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
    if (!tt_history_enabled || !brd.ply || stop_all
            || stack->excluded_move || brd.stack->fifty_count >= 99) {
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
            bonus, nullptr, quiet_history_policy);
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
        if ((distance == 2 && quiet_history_policy.continuation)
                || ((distance == 3 || distance == 5) && long_continuation_enabled)) {
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
                -bonus, move, quiet_history_policy);
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
    quiet_history->update(brd.us(), *move, previous, brd.stack->pawn_key, bonus, nullptr, quiet_history_policy);
    for (int i = 0; i < 2; ++i) {
        if (contexts[i]) {
            quiet_history->update_continuation(*move, *contexts[i], bonus);
        }
    }
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

// The parent position owns both the history lookup and the node context.
// At the real root, keep two alternatives at nominal depth before reducing.
int search_t::reduction(int depth, int searched_moves, int estimate, bool root_node) {
    if (depth < 3 || !lmr_enabled
            || searched_moves < (root_node ? 2 : 1)) {
        return 0;
    }
    return std::clamp(estimate, 0, depth - 2);
}

// The unclamped reduction a move would receive at any depth; pruning uses it
// to estimate the move's searched depth.
int search_t::reduction_estimate(int depth, int searched_moves, const move_t & move,
        bool pv, bool cut_node, int gives_check) {
    if (!lmr_enabled) {
        return 0;
    }
    // Immutable shared table: avoid logarithms and initialization in the loop.
    // Saturation keeps unusually deep/wide positions within the sampled range.
    static const auto base_reductions = [] {
        std::array<std::array<int, 64>, 64> table{};
        for (int d = 1; d < 64; ++d) {
            for (int m = 1; m < 64; ++m) {
                table[d][m] = int(1.0 + std::log(d) * std::log(m) / 3.0);
            }
        }
        return table;
    }();
    const int history = move_history_score(move);
    const bool noisy = move.capture || move.promotion;
    // Captures have one bounded history component; quiets combine several.
    const int history_divisor = noisy ? 4096 : 8192;
    int reduce = base_reductions[std::min(depth, 63)][std::min(searched_moves + 1, 63)]
        + int(cut_node) + int(!stack->improving) - int(pv)
        - std::clamp(history / history_divisor, -2, 2);
    bool refutation = false;
    if (!noisy) {
        const auto compact = move.to_compact();
        refutation = killers_enabled && (compact == stack->killer[0].to_compact()
            || compact == stack->killer[1].to_compact());
        if (countermoves_enabled && brd.ply > 0) {
            const auto & previous = (stack - 1)->current_move;
            refutation |= previous.piece && compact == countermoves[previous.piece][previous.tsq];
        }
    }
    reduce += int(cut_node) + int(stack->lmr_tt_tactical)
        + std::min(stack->lmr_alpha_raises / 2, 2);
    reduce -= int(stack->tt_pv) + int(gives_check != 0) + int(stack->in_check)
        + int(noisy) + int(refutation) + stack->lmr_correction;
    return reduce;
}

void search_t::update_lmr_history(const move_t & move, int depth, int value, int alpha, int beta) {
    // Learn only from a completed full-depth verification of a reduced trial.
    if (!lmr_enabled || !lmr_history_enabled || stop_all || stack->in_check
            || move.capture || move.promotion || move.castle || depth < 3
            || (value > alpha && value < beta)) {
        return;
    }
    const int d = std::min(depth, 8);
    const int bonus = (value >= beta ? 16 : -16) * d * d;
    const auto * previous = brd.ply > 0 ? &(stack - 1)->current_move : nullptr;
    bool updated = false;
    if (quiet_history_policy.continuation && previous && previous->piece) {
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

/**
 * Principle Variation Search (root node)
 * Difference with normal (non-root) search:
 * - Sending new PVs asap to the interface
 * - Sort order based on pv and amount of nodes of the subtrees
 * - No pruning and hash table lookup/store
 * - Compatible with all supported wild variants
 */
int search_t::pvs_root(int alpha, int beta, int depth) {
    // A reduced aspiration re-search still belongs to the attempted iteration.
    const int report_depth = std::max(depth, attempted_depth);
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;

    assert(root.move_count > 0);
    int best = -score::INF;
    if (wild != 0) {
        root.sort_moves(&stack->best_move);
    } else if (depth > 1) {
        order_root_moves(depth);
    }
    stack->tt_pv = wild == 0 && lmr_enabled;
    if (stack->tt_pv && depth >= 3 && !stack->in_check) {
        const int eval = evaluate(this);
        stack->lmr_correction = std::min(2, std::abs(eval - stack->eval_result) / 100);
    }

    /*
     * Moves loop
     */

    for (int i = 0; i < root.move_count; i++) {
        root_move_t * rmove = &root.moves[i];
        move_t * move = &rmove->move;
        U64 nodes_before = nodes;
        int score = 0;

        const int reduce = wild == 0
            ? reduction(depth, i,
                reduction_estimate(depth, i, *move, true, false, rmove->gives_check), true) : 0;
        //go forward and search one level deeper
        stack->move_count = i + 1;
        forward(move, rmove->gives_check);
        if (i == 0) {
            score = -pvs(-beta, -alpha, depth - 1, false);
        } else {
            score = -pvs(-alpha - 1, -alpha, depth - 1 - reduce, true);
            if (!stop_all && score > alpha) { //open window research w/o reductions
                const auto previous_clocks = stack->tt_clocks;
                score = -pvs(-beta, -alpha, depth - 1, false);
                if (lmr_enabled) {
                    stack->tt_clocks.include_child(previous_clocks, 0);
                }
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
            result_depth = report_depth;
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
            uci::send_pv(best, report_depth, sel_depth, nodes, game.tm.elapsed(),
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

/**
 * Verifies is a position is drawn by 
 * a) lack of material
 * b) fifty quiet moves
 * c) repetition
 * The actual root requires three occurrences; descendants score the first
 * match against either game or search history as a draw heuristic.
 * @return true if search should score this node as drawn
 */
bool search_t::is_draw() {
    if (brd.is_draw()) {
        return true;
    }
    if (brd.stack->fifty_count >= 100) {
        // Checkmate takes precedence. Probe fresh legal evasions, including quiet moves.
        stack->tt_move.clear();
        if (!stack->in_check || has_legal_move(brd)) {
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

bool search_t::raise_repetition_alpha(int & alpha, int beta, move_t & witness) {
    if (!upcoming_repetition_enabled || brd.ply == 0
            || stack->excluded_move || alpha >= draw_score()) {
        return false;
    }
    const int distance = upcoming_repetition(witness);
    if (!distance) {
        return false;
    }
    stack->tt_clocks.min = std::max(int(stack->tt_clocks.min), distance);
    alpha = draw_score();
    if (alpha >= beta) {
        stack->best_move = witness;
        stack->pv_moves[0] = witness;
        stack->pv_count = 1;
    }
    return true;
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

// A tactical move that survives qsearch and a reduced main search can justify
// a selective fail-high. Neither test is a proof at the requested main depth.
bool search_t::probcut(int alpha, int beta, int depth, int eval, bool cut_node, int & value) {
    constexpr int margin = 250, improving_margin = 50;
    constexpr int eval_divisor = 300, adaptive_offset = 100, adaptive_divisor = 200;
    const int decisive = score::TABLEBASE - MAX_PLY;
    const int threshold = beta + margin - improving_margin * int(stack->improving);
    if (!probcut_enabled || !brd.ply || depth < 5 || alpha + 1 != beta
            || stack->in_check || stack->excluded_move
            || brd.ply >= MAX_PLY - 2 || brd.stack->fifty_count >= 99
            || std::abs(alpha) >= decisive || std::abs(beta) >= decisive
            || std::abs(eval) >= decisive || threshold >= decisive) {
        return false;
    }
    int cached = 0, hint = 0, bound = 0;
    clock_range_t clocks;
    const U64 key = brd.stack->tt_key, context = tt_context();
    // Only a compatible upper/exact bound can refute the raised threshold.
    // A low lower bound says nothing about whether a better result exists.
    if (trans_table::retrieve(key, brd.ply, depth - 3, cached, hint, bound,
            context, brd.stack->fifty_count, &clocks)
            && (bound == score::UPPERBOUND || bound == score::EXACT) && cached < threshold) {
        return false;
    }
    search_frame_scope_t frame(*this);
    move::list_t candidates;
    move::gen_captures(&brd, &candidates);
    move::gen_promotions(&brd, &candidates);
    for (auto * move = candidates.first; move != candidates.last; ++move) {
        move->score = 16 * brd.mvvlva(move) + move_history_score(*move)
            + (move->to_compact() == hint ? 1000000 : 0);
    }
    const int base_depth = std::clamp(depth - 3 - (eval - beta) / eval_divisor, 0, depth - 1);
    const int see_threshold = (threshold - eval) * 3 / 4;
    while (candidates.first != candidates.last) {
        auto * best = std::max_element(candidates.first, candidates.last,
            [](const move_t & a, const move_t & b) { return a.score < b.score; });
        move_t move = *best;
        *best = *--candidates.last;
        if (!brd.see(&move, see_threshold) || !brd.legal(&move)) {
            continue;
        }
        int result, searched_depth = base_depth, search_beta = threshold;
        {
            // Undo even if a child throws; frame restoration alone cannot undo
            // a made move. The qsearch screen and main search share this child.
            forward(&move, brd.gives_check(&move));
            struct undo_t {
                search_t & search;
                move_t & move;
                ~undo_t() { search.backward(&move); }
            } undo{*this, move};
            result = -qsearch(-threshold, -threshold + 1, 0);
            if (!stop_all && result >= threshold && std::abs(result) < decisive) {
                searched_depth = std::max(0, base_depth
                    - std::clamp((result - threshold - adaptive_offset) / adaptive_divisor, 0, 3));
                // Shallower verification requires a correspondingly higher bar.
                search_beta = std::min(threshold + (base_depth - searched_depth) * adaptive_divisor,
                    decisive - 1);
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
            value = alpha;
            return false;
        }
        if (result >= search_beta && std::abs(result) < decisive) {
            stack->tt_clocks.max = std::min<uint8_t>(stack->tt_clocks.max, 98);
            // Cache only the depth actually verified, before removing the margin
            // for the caller. Never label a ProbCut result as a full-depth bound.
            trans_table::store(key, brd.root_ply, brd.ply, searched_depth + 1,
                result, move.to_compact(), score::LOWERBOUND, context, stack->tt_clocks);
            value = result - (search_beta - beta);
            return true;
        }
    }
    return false;
}

// A low static score only triggers a tactical verification, never a cutoff by
// itself. The full main-search frame must survive a rejected same-position probe.
bool search_t::razor_node(int alpha, int beta, int depth, int eval, int & value) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    if (!razoring_enabled || !brd.ply || depth < 1
            || alpha + 1 != beta || stack->in_check || stack->excluded_move
            || brd.stack->fifty_count >= 99 || std::abs(alpha) >= decisive
            || std::abs(beta) >= decisive || std::abs(eval) >= decisive
            || eval >= alpha - razoring_margin_per_ply * depth) {
        return false;
    }
    {
        search_frame_scope_t scope(*this);
        value = qsearch(alpha, beta, 0);
    }
    if (stop_all) {
        value = alpha;
        return false;
    }
    if (value <= alpha && std::abs(value) < decisive) {
        // Parents must not reuse this selective result at the clock-99 boundary.
        stack->tt_clocks.max = std::min<uint8_t>(stack->tt_clocks.max, 98);
        return true;
    }
    return false;
}

// Static evaluation supplies a selective fail-high premise, not a legal pass.
bool search_t::reverse_futility(int beta, int depth, int eval, int & value) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    if (wild != 0 || !beta_pruning || depth < 1 || depth >= 9
            || !brd.ply || stack->in_check || stop_all
            || stack->excluded_move
            || brd.stack->fifty_count >= 99
            || std::abs(beta) >= decisive
            || std::abs(eval) >= decisive || eval < beta) {
        return false;
    }
    // A quiet refutation deserves a search. Never trust an unvalidated hash
    // hint as tactical evidence or use its bound as a static evaluation.
    auto & hint = stack->tt_move;
    const bool legal_hint = hint.piece && brd.valid(&hint) && brd.legal(&hint);
    if (legal_hint && !hint.capture && !hint.promotion) {
        return false;
    }
    // Correction magnitude expresses uncertainty in the raw evaluator. A
    // threatened valuable piece must not strengthen the improving discount.
    const int correction = std::abs(eval - int(stack->eval_result));
    const int margin = 100 * depth
        - 100 * int(stack->improving && !lower_value_threat(brd)) + correction / 2;
    if (eval - margin < beta) {
        return false;
    }
    // A null-like static premise is invalid in stalemate. Keep its clock
    // restriction even when this node's result is propagated to a TT parent.
    stack->tt_clocks.max = std::min(int(stack->tt_clocks.max), 98);
    value = has_legal_move(brd) ? beta + (eval - beta) / 3 : draw_score();
    return true;
}

bool search_t::prune_futility(move_t & move, int reduced_depth, int searched_moves,
        int best, int alpha, int beta, int gives_check, int eval) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    if (wild != 0 || !ffp_enabled || !brd.ply || !searched_moves
            || stack->in_check || gives_check || stop_all
            || brd.stack->fifty_count >= 99
            || best <= -decisive || std::abs(alpha) >= decisive || std::abs(beta) >= decisive
            || move.promotion) {
        return false;
    }
    const bool capture = move.capture;
    reduced_depth = std::max(0, reduced_depth);
    if (reduced_depth >= (capture ? 8 : 6)) {
        return false;
    }
    // Use parent-position learned history, never picker stage/material bonuses.
    // Captures need a separate material allowance, including the EP victim.
    const int margin = (capture ? 200 : 100) * (1 + reduced_depth)
        + move_history_score(move) / 64 + (capture ? brd.max_gain(&move) : 0);
    if (eval + margin > alpha || !brd.legal(&move)) {
        return false;
    }
    stack->tt_clocks.max = std::min(int(stack->tt_clocks.max), 98);
    ++pruned_nodes;
    return true;
}

// A searched legal alternative protects terminals and losing/mating lines.
bool search_t::prune_quiet(move_t & move, int depth, int searched_moves,
        int reduced_depth, int best) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    if ((!lmp_enabled && !history_pruning_enabled) || !brd.ply || depth < 1
            || !searched_moves || stack->in_check || stop_all
            || move.capture || move.promotion || best <= -decisive
            || brd.stack->fifty_count >= 99) {
        return false;
    }
    // Quadratic depth growth, with a larger budget for improving positions.
    // Count only completed legal searches, never picker candidates or prunes.
    const int move_limit = (3 + depth * depth) / (2 - int(stack->improving));
    const bool lmp_candidate = lmp_enabled && reduced_depth < 9;
    const bool history_candidate = history_pruning_enabled && reduced_depth < 7
        && !move.equals(&stack->killer[0]) && !move.equals(&stack->killer[1]);
    const bool lmp_prune = lmp_candidate && searched_moves >= move_limit;
    const bool history_prune = !lmp_prune && history_candidate && move_history_score(move) < -2048 * depth;
    // An illegal pseudo-legal candidate supplies no pruning evidence.
    if (!(lmp_prune || history_prune) || !brd.legal(&move)) {
        return false;
    }
    stack->tt_clocks.max = std::min(int(stack->tt_clocks.max), 98);
    ++pruned_nodes;
    return true;
}

// SEE only models exchanges on one square. Search a legal alternative first,
// protect the clock-99 draw boundary, and allow larger sacrifices at greater depth.
bool search_t::prune_main_see(move_t & move, int depth, int searched_moves, int best) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    if (!main_see_enabled || !brd.ply || depth < 1 || !searched_moves
            || brd.stack->fifty_count >= 99
            || best <= -decisive) {
        return false;
    }
    const int threshold = move.capture ? -main_see_capture_margin * depth
        : -main_see_quiet_margin * depth * depth;
    if (brd.see(&move, threshold)) {
        return false;
    }
    // The picker may return self-check candidates. They cannot contribute to
    // pruning evidence, nor replace the already searched legal alternative.
    if (!brd.legal(&move)) {
        return false;
    }
    stack->tt_clocks.max = std::min(int(stack->tt_clocks.max), 98);
    ++pruned_nodes;
    return true;
}

// Missing move hints make deep ordering less reliable. Reduce the whole node
// once; all downstream consumers, including TT storage, use the searched depth.
int search_t::internal_search_depth(int depth, bool pv, bool cut_node) {
    if (!iir_enabled || !brd.ply || depth < 6 || (!pv && !cut_node)
            || stack->in_check
            || stack->excluded_move || brd.stack->fifty_count >= 99) {
        return depth;
    }
    if (stack->tt_move.piece && brd.valid(&stack->tt_move) && brd.legal(&stack->tt_move)) {
        return depth;
    }
    return depth - 1;
}

// Only a compatible lower/exact main-search score can nominate an only move.
// Verification owns an isolated same-position frame; retain a compact move,
// never a pointer into its picker. A failed-low alternative is a heuristic
// reason to allocate extra depth, not a proven bound on the original position.
search_t::singular_result_t search_t::singular_extension(int alpha, int beta, int depth, bool cut_node) {
    const int decisive = score::TABLEBASE - MAX_PLY;
    const int root_depth = attempted_depth ? attempted_depth : depth;
    if (!singular_extensions_enabled || !brd.ply || depth < 7
            || brd.ply >= 2 * root_depth || brd.ply >= MAX_PLY - 2
            || stack->in_check || stack->excluded_move
            || brd.stack->fifty_count >= 99
            || std::abs(alpha) >= decisive || std::abs(beta) >= decisive
            || !stack->tt_move.piece || !brd.valid(&stack->tt_move)
            || !brd.legal(&stack->tt_move)) {
        return {};
    }
    int value = 0, move = 0, flag = 0;
    clock_range_t clocks;
    if (!trans_table::retrieve(brd.stack->tt_key, brd.ply, depth - 3,
            value, move, flag, tt_context(), brd.stack->fifty_count, &clocks)
            || move != stack->tt_move.to_compact()
            || (flag != score::LOWERBOUND && flag != score::EXACT)
            || std::abs(value) >= decisive) {
        return {};
    }
    const int threshold = value - depth;
    if (std::abs(threshold) >= decisive) {
        return {};
    }
    stack->tt_clocks.include_child(clocks, 0);
    stack->tt_clocks.max = MIN(stack->tt_clocks.max, 98);
    const bool pv = beta > alpha + 1;
    const bool quiet = !stack->tt_move.capture && !stack->tt_move.promotion;
    const auto candidate = uint16_t(move);
    const int alternative = singular_search(threshold - 1, threshold,
        (depth - 1) / 2, cut_node, candidate);
    if (stop_all) {
        return {};
    }
    singular_result_t result;
    if (alternative < threshold) {
        result = {candidate, 1};
        // Extra depth belongs to non-PV forced lines. A line budget prevents
        // repeated multiple extensions from sustaining the remaining depth.
        if (!pv && stack->multiple_extensions < 12
                && alternative < threshold - 16) {
            result.extension = 2 + int(quiet && alternative < threshold - 64);
        }
    } else {
        // Reuse the alternative's fail-soft lower bound only above our beta.
        // Decisive scores from reduced work must not masquerade as proven mates.
        if (!pv && alternative >= beta && std::abs(alternative) < decisive) {
            result.cutoff = true;
            result.value = alternative;
        } else if (value >= beta) {
            result = {candidate, -3 + int(pv)};
        } else if (cut_node) {
            result = {candidate, -2};
        }
    }
    return result;
}

// Stronger static evidence and deeper nodes permit a shallower artificial
// branch. The legal tactical hint is ordering evidence, never a score bound.
int search_t::null_search_depth(int depth, int eval, int beta) {
    auto hint = stack->tt_move;
    const bool tactical = hint.piece && (hint.capture || hint.promotion)
        && brd.valid(&hint) && brd.legal(&hint);
    const int surplus = std::clamp((eval - beta) / 174, 0, 4);
    return std::max(0, depth - 4 - depth / 3 - surplus - int(tactical));
}

// Search this position again without one move. Only this frame skips the
// TT and node pruning; its children are ordinary searches.
int search_t::singular_search(int alpha, int beta, int depth, bool cut_node,
        uint16_t excluded_move) {
    assert(wild == 0 && excluded_move && depth > 0 && brd.ply < MAX_PLY - 1);
    assert(alpha < beta && (!cut_node || alpha + 1 == beta));
    int value;
    {
        search_frame_scope_t scope(*this);
        stack->excluded_move = excluded_move;
        value = pvs(alpha, beta, depth, cut_node);
    }
    return stop_all ? alpha : value;
}

// Confirm a null-move cutoff with real moves and no further null moves.
int search_t::null_verification(int alpha, int beta, int depth, bool cut_node) {
    assert(wild == 0 && alpha < beta && (!cut_node || alpha + 1 == beta));
    int value;
    {
        search_frame_scope_t scope(*this);
        null_enabled = false;
        value = pvs(alpha, beta, depth, cut_node);
    }
    return stop_all ? alpha : value;
}

/**
 * Principle Variation Search (fail-soft)
 * @param alpha lowerbound value
 * @param beta upperbound value
 * @param depth remaining search depth
 * @param cut_node expected non-PV cutoff, not a proven score bound
 * @return score for the current node
 */
int search_t::pvs(int alpha, int beta, int depth, bool cut_node) {
    stack->tt_clocks = {0, 101};
    stack->improving = false;
    // A singular search keeps the main-line flag of the node it re-searches.
    if (!stack->excluded_move) {
        stack->tt_pv = false;
    }
    stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    assert(!cut_node || alpha + 1 == beta);

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
    if (!stack->excluded_move && tablebase_limit && probe_tablebase(depth, tablebase_score)) {
        return tablebase_score;
    }

    const bool pv = alpha + 1 < beta;
    move_t repetition_move{};
    const bool repetition_floor = raise_repetition_alpha(alpha, beta, repetition_move);
    if (repetition_floor && alpha >= beta) {
        return alpha;
    }

    /*
     * Transposition table lookup
     */
    const auto excluded_move = stack->excluded_move;
    const bool normal_search = !excluded_move;
    stack->tt_key = brd.stack->tt_key;
    const U64 context = normal_search ? tt_context() : 0;
    clock_range_t tt_clocks;
    int tt_move = 0, tt_flag = 0, tt_score;
    bool cached_pv = false;
    const bool usable_tt_score = normal_search && trans_table::retrieve(stack->tt_key, brd.ply,
        depth, tt_score, tt_move, tt_flag, context, brd.stack->fifty_count, &tt_clocks,
        tt_mode_t::main_search, &cached_pv);
    if (normal_search) {
        stack->tt_pv = lmr_enabled && (pv || cached_pv);
    }
    // A cached score below a legal draw escape cannot resolve this node.
    // Retain its move hint, but continue searching for something better.
    if (!pv && usable_tt_score && (!repetition_floor || tt_score >= draw_score())) {
        // A compatible exact score resolves either side of the window.
        if (tt_flag == score::EXACT) {
            if (!repetition_floor && tt_score >= beta
                    && std::abs(tt_score) < score::TABLEBASE - MAX_PLY) {
                update_tt_history(tt_move, depth);
            }
            stack->tt_clocks.include_child(tt_clocks, 0);
            return tt_score;
        } else if (tt_score >= beta && tt_flag == score::LOWERBOUND) {
            if (!repetition_floor && std::abs(tt_score) < score::TABLEBASE - MAX_PLY) {
                update_tt_history(tt_move, depth);
            }
            stack->tt_clocks.include_child(tt_clocks, 0);
            return tt_score;
        } else if (tt_score <= alpha && tt_flag == score::UPPERBOUND) {
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
    stack->improving = is_improving();
    if (lmr_enabled && normal_search) {
        auto & hint = stack->tt_move;
        stack->lmr_tt_tactical = hint.piece && (hint.capture || hint.promotion)
            && brd.valid(&hint) && brd.legal(&hint);
        if (!in_check && stack->eval_result != score::INVALID) {
            stack->lmr_correction = std::min(2, std::abs(eval - stack->eval_result) / 100);
        }
    }
    int razor_score = alpha;
    if (!pv && !repetition_floor && razor_node(alpha, beta, depth, eval, razor_score)) {
        return razor_score;
    }
    if (stop_all) {
        return alpha;
    }
    const int decisive_score = score::TABLEBASE - MAX_PLY;
    int futility_score = beta;
    if (!pv && !repetition_floor && reverse_futility(beta, depth, eval, futility_score)) {
        return futility_score;
    }
    const bool do_prune_node = !excluded_move && eval >= beta && !in_check && !pv
        && std::abs(beta) < decisive_score && brd.has_pieces(brd.us())
        && brd.stack->fifty_count < 99;
    const bool previous_null = brd.ply > 0 && !(stack - 1)->current_move.piece;
    const bool null_policy = brd.ply > 0 && cut_node && depth > 2 && normal_search
        && !repetition_floor && eval + 132 * int(stack->improving) >= beta + 8 * depth;
    const bool do_null = null_enabled && !previous_null && null_policy;

    // A null cutoff requires legal play: passing in stalemate would turn an
    // exact draw into a heuristic fail-high. At clock 99 a reversible move draws unless it mates; static evaluation and
    // an artificial pass can still claim a win. Search legal moves there.
    // Propagate this restriction so cached parents cannot bypass the guard.
    if (do_prune_node && do_null) {
        stack->tt_clocks.max = MIN(stack->tt_clocks.max, 98);
    }
    if (do_prune_node && do_null && !has_legal_move(brd)) {
        return draw_score();
    }

    //null move pruning
    if (do_prune_node && do_null) {
        const int null_depth = null_search_depth(depth, eval, beta);
        forward();
        int null_score = -pvs(-beta, -beta + 1, null_depth, !cut_node);
        backward();
        if (stop_all) {
            return alpha;
        } else if (null_score >= beta && std::abs(null_score) < decisive_score) {
            if (null_verify && depth >= 12) {
                // Even a null probe reduced to qsearch must be confirmed by real moves.
                const int verification_depth = std::max(1, null_depth);
                int verified_score = null_verification(beta - 1, beta,
                    verification_depth, cut_node);
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

    depth = internal_search_depth(depth, pv, cut_node);

    int probcut_score = alpha;
    if (!pv && !repetition_floor && probcut(alpha, beta, depth, eval, cut_node, probcut_score)) {
        return probcut_score;
    }
    if (stop_all) {
        return alpha;
    }

    /*
     * Moves loop
     */

    const auto singular_result = repetition_floor ? singular_result_t{}
        : singular_extension(alpha, beta, depth, cut_node);
    if (stop_all) {
        return alpha;
    }

    if (singular_result.cutoff) {
        // Reduced alternative work supplies a selective lower bound, without
        // inventing a full-depth TT entry or training the unsearched TT move.
        return singular_result.value;
    }

    //if there is no first move, it's checkmate or stalemate
    move_t * move = move::first(this, depth);
    if (!move) {
        if (excluded_move) {
            return alpha;
        }
        return in_check ? -score::MATE + brd.ply : draw_score();
    }

    //prepare and do the loop
    int best = repetition_floor ? draw_score() : -score::INF;
    int searched_moves = 0;
    const int score_max = score::MATE - brd.ply - 1;
    if (repetition_floor) {
        stack->best_move = repetition_move;
        stack->pv_moves[0] = repetition_move;
        stack->pv_count = 1;
    } else {
        stack->best_move.clear();
    }
    do {
        if (excluded_move && move->to_compact() == excluded_move) {
            continue;
        }

        assert(brd.valid(move));
        assert(!stack->best_move.equals(move) || (repetition_floor && repetition_move.equals(move)));
        assert(in_searched(move, searched_moves) == false);

        const int gives_check = brd.gives_check(move);
        assert(gives_check == 0 || gives_check == 1 || gives_check == 2);

        /*
         * Move pruning: skip all futile moves
         */


        const bool nominated = singular_result.move && move->to_compact() == singular_result.move;
        const bool singular = nominated && singular_result.extension > 0;
        const int estimate = singular ? 0
            : reduction_estimate(depth, searched_moves, *move, pv, cut_node, gives_check);
        const int reduce = singular ? 0 : reduction(depth, searched_moves, estimate);
        const int lmr_depth = depth - estimate;
        if (!repetition_floor && !singular && prune_quiet(*move, depth, searched_moves,
                lmr_depth, best)) {
            continue;
        }

        if (!repetition_floor && !singular && prune_futility(*move,
                lmr_depth, searched_moves, best, alpha, beta, gives_check, eval)) {
            continue;
        }

        if (prune_main_see(*move, depth, searched_moves, best)) {
            continue;
        }

        /*
         * Move extensions and reductions
         */

        const int extend = nominated ? singular_result.extension : 0;


        /*
         * Go forward and search next node
         */

        if (!try_forward(move, gives_check)) {
            continue;
        }
        stack->multiple_extensions += extend >= 2;
        (stack - 1)->move_count = searched_moves + 1;
        int score;
        bool lmr_researched = false;
        if (searched_moves == 0) {
            score = -pvs(-beta, -alpha, depth - 1 + extend, !pv && !cut_node);
        } else {
            int new_depth = depth + extend;
            const int reduced_depth = new_depth - 1 - reduce;
            score = -pvs(-alpha - 1, -alpha, reduced_depth, reduce > 0 || !cut_node);
            if (!stop_all && score > alpha && reduce > 0) {
                // Adjust the full-depth re-search by how far the reduced result beat the best score.
                lmr_researched = true;
                new_depth += int(score > best + 32 + 8 * (reduce + 1)) - int(score < best + new_depth);
                if (new_depth - 1 > reduced_depth && score < score::TABLEBASE - MAX_PLY) {
                    score = -pvs(-alpha - 1, -alpha, new_depth - 1, !cut_node);
                }
            }
            if (!stop_all && pv && score > alpha && score < beta) {
                score = -pvs(-beta, -alpha, new_depth - 1, false);
            }
        }
        backward(move);
        if (lmr_researched && !repetition_floor) {
            update_lmr_history(*move, depth, score, alpha, beta);
        }

        /*
         * Handle results: update the best value / do a beta cutoff
         */

        if (stop_all) {
            return alpha;
        } else if (score > best) {
            stack->best_move.set(move);
            if (score >= beta) {
                if (normal_search) {
                    trans_table::store(stack->tt_key, brd.root_ply, brd.ply, depth, score, move->to_compact(), score::LOWERBOUND, context, stack->tt_clocks,
                        tt_mode_t::main_search, stack->tt_pv);
                }
                if (!repetition_floor) {
                    update_correction_history(score, score::LOWERBOUND, depth);
                }
                if (normal_search) {
                    update_capture_cutoff(*move, depth, searched_moves);
                }
                if (normal_search && !move->capture && !move->promotion && !move->castle) {
                    if (countermoves_enabled && brd.ply > 0 && (stack - 1)->current_move.piece) {
                        const auto & previous = (stack - 1)->current_move;
                        countermoves[previous.piece][previous.tsq] = move->to_compact();
                    }
                    update_quiet_cutoff(move, depth, searched_moves);
                    // A returned move can alias killer[1]; update killers last.
                    if (killers_enabled) {
                        update_killers(move);
                    }
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
     * Store the result in the hash table and return
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
    if (!repetition_floor) {
        update_correction_history(best, flag, depth);
    }
    if (normal_search) {
        trans_table::store(brd.stack->tt_key, brd.root_ply, brd.ply, depth, best, stack->best_move.to_compact(), flag, context, stack->tt_clocks,
            tt_mode_t::main_search, stack->tt_pv);
    }
    return best;
}

/**
 * Quiescence search with tactical moves only.
 * Captures and promotions at every depth; all legal evasions in check.
 */

int search_t::qsearch(int alpha, int beta, int depth) {
    stack->tt_clocks = {0, 101};
    stack->pv_count = 0;
    stack->best_move.clear();
    stack->tt_move.clear();
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;

    assert(depth <= 0);
    assert(!stack->excluded_move);
    assert(alpha < beta);
    assert(alpha >= -score::INF);
    assert(beta <= score::INF);

    /*
     * Stop conditions
     */

    //time
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
    if (tablebase_limit && probe_tablebase(depth, tablebase_score)) {
        return tablebase_score;
    }

    move_t repetition_move{};
    const bool repetition_floor = raise_repetition_alpha(alpha, beta, repetition_move);
    if (repetition_floor && alpha >= beta) {
        return alpha;
    }

    const bool in_check = stack->in_check;
    const auto mode = depth == 0 ? tt_mode_t::quiescence_entry : tt_mode_t::quiescence_tactical;
    const U64 key = brd.stack->tt_key;
    const bool use_q_tt = q_tt_enabled;
    const U64 context = use_q_tt ? tt_context() : 0;
    stack->tt_key = key;
    if (use_q_tt) {
        int tt_score = 0, tt_move = 0, tt_flag = 0;
        clock_range_t clocks;
        const bool usable = trans_table::retrieve(key, brd.ply, 1, tt_score, tt_move,
            tt_flag, context, brd.stack->fifty_count, &clocks, mode);
        if (usable && (!repetition_floor || tt_score >= draw_score())
                && alpha + 1 == beta && (tt_flag == score::EXACT
                || (tt_flag == score::LOWERBOUND && tt_score >= beta)
                || (tt_flag == score::UPPERBOUND && tt_score <= alpha))) {
            stack->tt_clocks.include_child(clocks, 0);
            return tt_score;
        }
        stack->tt_move.set_compact(&brd, tt_move);
        const auto & hint = stack->tt_move;
        // Any valid hint is searched first, even a quiet move.
        if (hint.piece && !brd.valid(&hint)) {
            stack->tt_move.clear();
        }
    }
    // Only completed results reach this helper. A q depth marker is never a
    // main-search depth; the packed mode gates score reuse and replacement.
    const auto finish = [&](int value, int flag) {
        if (repetition_floor && value == draw_score() && !stack->best_move.piece) {
            stack->best_move = repetition_move;
            stack->pv_moves[0] = repetition_move;
            stack->pv_count = 1;
        }
        if (use_q_tt) {
            trans_table::store(key, brd.root_ply, brd.ply, 1, value,
                stack->best_move.to_compact(), flag, context, stack->tt_clocks, mode);
        }
        return value;
    };

    //stand-pat: return if eval is already good enough
    const int eval = evaluate(this);
    if (eval >= beta && !in_check) {
        return has_legal_move(brd) ? finish(eval, score::LOWERBOUND)
            : finish(draw_score(), score::EXACT);
    }

    //get first move; if there's none it's mate, stalemate, or there are no captures/promotions
    move_t * move = move::first(this, depth);
    if (!move) {
        if (in_check) {
            return finish(-score::MATE + brd.ply, score::EXACT);
        }
        // Match the normal fail-low path even when the picker is empty.
        // Merely having an illegal tactical candidate must not change the bound.
        return !has_legal_move(brd) ? finish(draw_score(), score::EXACT)
            : finish(repetition_floor ? std::max(eval, draw_score()) : eval, score::UPPERBOUND);
    }
    if (eval > alpha && !in_check) {
        alpha = eval;
    }

    /*
     * Moves loop
     */

    // Keep the best searched score independently of the window: a fail-low
    // still carries a useful upper bound below alpha. A legal repetition
    // escape remains a floor even when the static score is lower.
    int best_score = in_check ? -score::INF : eval;
    if (repetition_floor) {
        best_score = std::max(best_score, draw_score());
    }
    int searched_moves = 0;
    const int decisive = score::TABLEBASE - MAX_PLY;
    const int futility = eval + quiescence_futility_margin;
    do {

        /*
         * Move pruning
         */

        int gives_check = brd.gives_check(move);

        // After one evasion, skip evasions that lose material.
        if (ffp_enabled && in_check && searched_moves > 0 && best_score > -decisive
                && !brd.see(move, 0)) {
            continue;
        }

        // Futility: a losing tactical move cannot lift a hopeless stand pat.
        const bool recapture = (stack - 1)->current_move.piece
            && move->tsq == (stack - 1)->current_move.tsq;
        const bool use_futility = ffp_enabled;
        if (use_futility && !in_check && !gives_check && !recapture && best_score > -decisive
                && futility <= alpha && std::abs(futility) < decisive && !brd.see(move, 1)) {
            ++pruned_nodes;
            best_score = std::max(best_score, futility);
            continue;
        }

        /*
         * Go forward and search next node
         */

        if (!try_forward(move, gives_check)) {
            continue;
        }
        ++searched_moves;
        int score = -qsearch(-beta, -alpha, depth - 1);
        backward(move);

        /*
         * Handle results: beta cutoff or improved alpha
         */

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
    if (searched_moves == 0 && (in_check || !has_legal_move(brd))) {
        return finish(in_check ? -score::MATE + brd.ply : draw_score(), score::EXACT);
    }
    return finish(best_score, score::UPPERBOUND);
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
    stack->improving = false;
    stack->tt_pv = stack->lmr_tt_tactical = false;
    stack->lmr_correction = stack->lmr_alpha_raises = 0;
    stack->excluded_move = 0;
    stack->move_count = 0;
    stack->multiple_extensions = 0;
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
