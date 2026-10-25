#include "engine.h"
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
        && search.stack == search.root_stack && !search.stack->excluded_move;
}

// Searches the position without one move, as the singular extension does.
int excluded_search(search_t & search, int alpha, int beta, int depth, bool cut_node, uint16_t excluded) {
    search.stack->excluded_move = excluded;
    const int value = search.pvs(alpha, beta, depth, cut_node);
    search.stack->excluded_move = 0;
    return search.stop_all ? alpha : value;
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
            valid &= !stack->excluded_move;
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
        const auto key = position_key(search->brd);
        trans_table::store(key, 0, 0, 8, 12345, excluded, score::EXACT);
        const int alpha = fen == start ? -10001 : -1000, beta = fen == start ? -10000 : 1000;
        const int value = excluded_search(*search, alpha, beta, 1, fen == start, excluded);
        success &= check(value == (fen == queen || fen == start ? 0 : 70)
            && search->valid && !search->children.empty() && !search->children.contains(excluded_text)
            && restored(*search, fen), "excluded search must ignore ordinary TT scores/hints and filter exactly one move");
        if (fen != queen && fen != start) {
            success &= check(search->children.contains("a7a8r") && search->children.contains("a7a8b")
                && search->children.contains("a7a8n"), "excluding a queen promotion must preserve underpromotions");
        }
        tt_hit_t hit;
        success &= check(trans_table::probe(key, 0, 0, hit)
            && hit.score == 12345 && hit.move == excluded && hit.bound == score::EXACT,
            "excluded search must not overwrite the ordinary position's cached score or hint");
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
        const int value = excluded_search(*search, -200, -199, 1, true, excluded);
        success &= check(value == (clock == 100 ? 0 : -200) && restored(*search, fen.c_str()),
            "excluding the only evasion must return fail-low, while actual draw adjudication retains precedence");
    }
    return success;
}

bool test_cancellation() {
    bool success = true;
    for (int limit : {1, 2, 5}) {
        auto search = std::make_unique<search_t>(queen);
        prepare(*search);
        search->game.max_nodes = limit;
        const int value = excluded_search(*search, -1000, 1000, 3, false, compact(*search, "d1d5"));
        success &= check(value == -1000 && search->stop_all && restored(*search, queen),
            "cancelled singular search must unwind moves and never authorize a cutoff");
    }
    return success;
}
}

int main() {
    magic::init();
    trans_table::set_size(4);
    uci::silent(true);
    const bool filter = test_filter_and_cache();
    const bool alternatives = test_no_alternatives_and_draw();
    const bool cancellation = test_cancellation();
    return filter && alternatives && cancellation ? EXIT_SUCCESS : EXIT_FAILURE;
}
