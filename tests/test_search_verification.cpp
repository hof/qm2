#include "engine.h"
#include <cstring>
#include <set>

namespace {
constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr auto queen = "7k/8/8/3q4/8/8/8/K2R4 w - - 0 1";

bool check(bool success, const char * message) {
    if (!success) {
        std::cerr << message << '\n';
    }
    return success;
}
uint16_t compact(search_t & search, const char * text) {
    move_t move{};
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::abort();
    }
    return move.to_compact();
}
void prepare(search_t & search) {
    search.stack->in_check = search.brd.in_check();
    search.tablebase_limit = search.draw_contempt = 0;
    search.q_tt_enabled = true;
}
bool restored(search_t & search, const char * fen) {
    return search.brd.to_string() == fen && !search.brd.ply
        && search.stack == search.root_stack && !search.stack->excluded_move
        && search.verification_mode == verification_mode_t::none;
}

class scope_probe_t : public search_t {
public:
    bool cancel = false, fail = false, valid = true;
    int calls = 0;
    using search_t::search_t;
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        ++calls;
        valid &= !null_enabled && verification_mode != verification_mode_t::none
            && alpha == 0 && beta == 1 && depth == 2 && cut_node;
        nodes += 2;
        stack->tt_key = 999;
        stack->tt_history = 777;
        stack->repetition_start = 55;
        stack->eval_result = 123;
        stack->improving = false;
        stack->best_move.set(WKING, a1, a2);
        stack->current_move = stack->best_move;
        stack->killer[0] = stack->best_move;
        stack->pv_count = 1;
        stack->move_list.stage = 99;
        stack->move_list.first = stack->searched;
        stack->excluded_move = 1234;
        stack->tt_clocks = {0, 80};
        stop_all = cancel;
        if (fail) {
            throw std::runtime_error("verification fixture");
        }
        return 50;
    }
};

bool test_scope() {
    bool success = true;
    for (auto mode : {verification_mode_t::null_move, verification_mode_t::excluded_move}) {
        for (int outcome = 0; outcome < 3; ++outcome) {
            auto search = std::make_unique<scope_probe_t>(start);
            prepare(*search);
            search->null_enabled = outcome != 1;
            search->stack->improving = true;
            search->stack->tt_clocks = {0, 90};
            search->stack->move_list.evasion_targets = BIT(e2);
            const auto before = *search->stack;
            const bool previous_null = search->null_enabled;
            search->cancel = outcome == 1;
            search->fail = outcome == 2;
            const auto excluded = mode == verification_mode_t::excluded_move ? compact(*search, "g1f3") : 0;
            bool caught = false;
            int value = -1;
            try {
                value = search->verification_search(0, 1, 2, true, mode, excluded);
            } catch (const std::runtime_error &) {
                caught = true;
            }
            const auto & after = *search->stack;
            success &= check(search->calls == 1 && search->valid && caught == search->fail
                && (caught || value == (search->cancel ? 0 : 50)) && search->nodes == 2
                && search->stop_all == search->cancel && search->null_enabled == previous_null
                && restored(*search, start),
                "verification must restore worker policy and retain work/cancellation on return or exception");
            success &= check(after.tt_key == before.tt_key && after.tt_history == before.tt_history
                && after.repetition_start == before.repetition_start && after.eval_result == before.eval_result
                && after.improving == before.improving && after.best_move.piece == before.best_move.piece
                && after.current_move.piece == before.current_move.piece && after.killer[0].piece == before.killer[0].piece
                && after.pv_count == before.pv_count && after.move_list.stage == before.move_list.stage
                && after.move_list.first == before.move_list.first
                && after.move_list.evasion_targets == before.move_list.evasion_targets
                && after.tt_clocks.min == 0 && after.tt_clocks.max == 80,
                "verification must restore picker/PV/evaluation/history state and merge clock restrictions");
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
            const auto & counts = search->nnue_audit.counters;
            success &= check(counts.verification_calls == 1 && counts.verification_visits == 2
                && counts.excluded_calls == (mode == verification_mode_t::excluded_move)
                && counts.verification_cancelled == search->cancel,
                "verification audit work must survive cancellation and local exceptions");
#endif
        }
    }
    return success;
}

// A controlled one-ply leaf contract isolates move filtering from evaluation.
class filtered_probe_t : public search_t {
public:
    std::set<std::string> children;
    bool valid = true;
    using search_t::search_t;
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply) {
            const auto & move = (stack - 1)->current_move;
            children.insert((stack - 1)->current_move.to_string());
            valid &= !stack->excluded_move && !null_enabled
                && verification_mode == verification_mode_t::excluded_move;
            ++nodes;
            stack->pv_count = 0;
            return move.promotion == WROOK ? -70 : move.capture == BQUEEN ? -50 : 0;
        }
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};

bool test_filter_and_cache() {
    bool success = true;
    for (const char * fen : {queen, start, "7k/P7/8/8/8/8/8/K7 w - - 0 1"}) {
        trans_table::clear();
        auto search = std::make_unique<filtered_probe_t>(fen);
        prepare(*search);
        const char * excluded_text = fen == queen ? "d1d5" : fen == start ? "g1f3" : "a7a8q";
        const auto excluded = compact(*search, excluded_text);
        const auto key = search->brd.stack->tt_key, context = search->tt_context();
        trans_table::store(key, 0, 0, 8, 12345, excluded, score::EXACT, context);
        const auto history = std::make_unique<quiet_history_t>(*search->quiet_history);
        const int alpha = fen == start ? -10001 : -1000, beta = fen == start ? -10000 : 1000;
        const int value = search->verification_search(alpha, beta, 1, fen == start,
            verification_mode_t::excluded_move, excluded);
        success &= check(value == (fen == queen || fen == start ? 0 : 70)
            && search->valid && !search->children.empty() && !search->children.contains(excluded_text)
            && restored(*search, fen), "excluded search must ignore ordinary TT scores/hints and filter exactly one move");
        if (fen != queen && fen != start) {
            success &= check(search->children.contains("a7a8r") && search->children.contains("a7a8b")
                && search->children.contains("a7a8n"), "excluding a queen promotion must preserve underpromotions");
        }
        int score = 0, move = 0, flag = 0;
        success &= check(trans_table::retrieve(key, 0, 8, score, move, flag, context)
            && score == 12345 && move == excluded && flag == score::EXACT,
            "excluded search must not overwrite the ordinary position's cached score or hint");
        success &= check(std::memcmp(history.get(), search->quiet_history.get(), sizeof(quiet_history_t)) == 0,
            "verification cutoffs must not train ordinary quiet history");
    }
    return success;
}

bool test_no_alternatives_and_draw() {
    bool success = true;
    for (int clock : {0, 99, 100}) {
        const auto fen = std::string("R6k/8/5K2/8/8/8/8/8 b - - ") + std::to_string(clock) + " 1";
        auto search = std::make_unique<search_t>(fen.c_str());
        prepare(*search);
        const auto excluded = compact(*search, "h8h7");
        const int value = search->verification_search(-200, -199, 1, true,
            verification_mode_t::excluded_move, excluded);
        success &= check(value == (clock == 100 ? 0 : -200) && restored(*search, fen.c_str()),
            "excluding the only evasion must return fail-low, while actual draw adjudication retains precedence");
    }
    return success;
}

bool test_real_subtree_isolation() {
    bool success = true;
    for (int depth : {0, 2}) {
        trans_table::clear();
        auto search = std::make_unique<search_t>(queen);
        prepare(*search);
        const auto history = std::make_unique<quiet_history_t>(*search->quiet_history);
        const auto key = search->brd.stack->tt_key, context = search->tt_context();
        trans_table::store(key, 0, 0, 8, 12345, compact(*search, "d1d5"), score::EXACT, context,
            {0, 101}, depth ? tt_mode_t::main_search : tt_mode_t::quiescence_entry);
        const int value = search->verification_search(12344, 12345, depth, true, verification_mode_t::null_move);
        success &= check(value != 12345 && search->nodes > 0 && restored(*search, queen)
            && std::memcmp(history.get(), search->quiet_history.get(), sizeof(quiet_history_t)) == 0,
            "real verification subtrees must skip normal main/q TT cutoffs and history training");
        int score = 0, move = 0, flag = 0;
        success &= check(trans_table::retrieve(key, 0, 8, score, move, flag, context, 0, nullptr,
            depth ? tt_mode_t::main_search : tt_mode_t::quiescence_entry) && score == 12345,
            "real verification subtrees must leave existing main/q cache entries intact");
        trans_table::clear();
        auto cold = std::make_unique<search_t>(queen);
        prepare(*cold);
        cold->verification_search(-score::INF, score::INF, depth, false, verification_mode_t::null_move);
        success &= check(!trans_table::retrieve(key, 0, 1, score, move, flag, context)
            && !trans_table::retrieve(key, 0, 1, score, move, flag, context, 0, nullptr,
                tt_mode_t::quiescence_entry), "verification must not create ordinary main/q cache entries");
    }
    for (int limit : {1, 2, 5}) {
        auto search = std::make_unique<search_t>(queen);
        prepare(*search);
        search->game.max_nodes = limit;
        const int value = search->verification_search(-1000, 1000, 3, false,
            verification_mode_t::excluded_move, compact(*search, "d1d5"));
        success &= check(value == -1000 && search->stop_all && restored(*search, queen),
            "cancelled real verification must unwind moves and never authorize a cutoff");
    }
    return success;
}
}

int main() {
    magic::init();
    trans_table::set_size(4);
    uci::silent(true);
    const bool scope = test_scope();
    const bool filter = test_filter_and_cache();
    const bool alternatives = test_no_alternatives_and_draw();
    const bool isolation = test_real_subtree_isolation();
    return scope && filter && alternatives && isolation ? EXIT_SUCCESS : EXIT_FAILURE;
}
