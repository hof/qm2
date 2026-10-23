#include "engine.h"
#include <vector>

namespace {
constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool check(bool success, const char * message) {
    if (!success) {
        std::cerr << message << '\n';
    }
    return success;
}

void play(search_t & search, const char * text) {
    move_t move;
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::abort();
    }
    search.forward(&move, search.brd.gives_check(&move));
    search.nodes = 0; // fixture moves are setup, not search
}

bool test_trend() {
    auto search = std::make_unique<search_t>(start);
    bool success = check(!search->is_improving() && !search->stack->improving,
        "a fresh root must have no evaluation trend");
    search->stack->eval_result = 10;
    play(*search, "g1f3");
    search->stack->eval_result = -1000;
    play(*search, "g8f6");
    search->stack->eval_result = 11;
    success &= check(search->is_improving(), "trend must compare the same side, not the previous ply");
    search->stack->eval_result = 10;
    success &= check(!search->is_improving(), "equal evaluations must not count as improvement");
    search->stack->eval_result = 9;
    success &= check(!search->is_improving(), "a lower evaluation must not count as improvement");
    play(*search, "b1c3");
    play(*search, "b8c6");
    search->stack->eval_result = 20;
    (search->stack - 2)->eval_result = 30;
    success &= check(!search->is_improving(), "the nearest valid same-side evaluation must take priority");
    (search->stack - 2)->eval_result = score::INVALID;
    success &= check(search->is_improving(), "a missing two-ply evaluation must permit the four-ply fallback");
    search->stack->in_check = true;
    success &= check(!search->is_improving(), "checked nodes must not have a static-evaluation trend");
    search->stack->in_check = false;
    search->root_stack->eval_result = score::INVALID;
    success &= check(search->is_improving(), "unknown same-side history must assume improvement");
    search->root_stack->eval_result = 10;
    search->stack->eval_result = score::INVALID;
    success &= check(!search->is_improving(), "missing current evaluation must not claim improvement");
    search->stack->improving = true;
    search->forward();
    success &= check(!search->stack->improving && !search->is_improving(),
        "a null child must start without stale evaluation history");
    search->backward();
    success &= check(search->stack->improving, "undo must retain the parent's trend");
    search->reset_stack();
    success &= check(!search->stack->improving && !search->is_improving(),
        "resetting the search root must discard the previous path's trend");
    return success;
}

class context_probe_t : public search_t {
public:
    struct visit_t { int alpha, beta, depth; bool cut_node, parent_improving; };
    std::vector<visit_t> visits;
    int entry_ply = 0;
    bool force_research = false, refute_reduction = false, cancel = false;
    explicit context_probe_t() : search_t(start) {
        null_enabled = beta_pruning = ffp_enabled = false;
        tablebase_limit = 0;
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply == entry_ply + 1) {
            visits.push_back({alpha, beta, depth, cut_node, (stack - 1)->improving});
            stack->pv_count = 0;
            if (cancel) {
                stop_all = true;
            }
            if (force_research && (visits.size() == 2 || visits.size() == 3)) {
                return -10;
            }
            if (refute_reduction && visits.size() == 4) {
                return -1;
            }
            return 0;
        }
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};

bool test_propagation() {
    bool success = true;
    for (bool cut_node : {false, true}) {
        for (bool reductions : {false, true}) {
            auto search = std::make_unique<context_probe_t>();
            search->lmr_enabled = reductions;
            search->refute_reduction = reductions;
            search->search_t::pvs(0, 1, 3, cut_node);
            success &= check(search->visits.size() >= 5, "context fixture must search multiple legal moves");
            for (size_t i = 0; i < search->visits.size(); ++i) {
                const auto & visit = search->visits[i];
                const bool reduced = visit.depth == 1;
                success &= check(visit.cut_node == (reduced || !cut_node)
                    && visit.beta == visit.alpha + 1,
                    "non-PV first/scout/reduced/verification context propagation differs");
            }
            if (reductions) {
                success &= check(search->visits[3].depth == 1 && search->visits[3].cut_node
                    && search->visits[4].depth == 2 && search->visits[4].cut_node == !cut_node,
                    "a reduced fail-high must restore full depth and its expected node type");
            }
            success &= check(search->brd.to_string() == start && search->stack == search->root_stack
                && !search->brd.ply && !search->stop_all, "context propagation must restore board and stack");
        }
    }
    for (bool root : {false, true}) {
        auto search = std::make_unique<context_probe_t>();
        search->force_research = true;
        if (root) {
            search->init_root_moves();
            search->pvs_root(-100, 100, 1);
        } else {
            search->search_t::pvs(-100, 100, 1, false);
        }
        success &= check(search->visits.size() > 3 && !search->visits[0].cut_node
            && search->visits[1].cut_node && !search->visits[2].cut_node
            && search->visits[0].beta - search->visits[0].alpha > 1
            && search->visits[1].beta - search->visits[1].alpha == 1
            && search->visits[2].beta - search->visits[2].alpha > 1,
            "PV/root first child, scout and open-window re-search contexts differ");
    }
    return success;
}

bool test_trend_lifetime() {
    bool success = true;
    for (bool cancel : {false, true}) {
        auto search = std::make_unique<context_probe_t>();
        search->root_stack->eval_result = -30000;
        play(*search, "g1f3");
        play(*search, "g8f6");
        search->entry_ply = search->brd.ply;
        search->cancel = cancel;
        const auto fen = search->brd.to_string();
        search->search_t::pvs(-10000, 10000, 1, false);
        success &= check(!search->visits.empty() && search->stack->improving
            && search->visits[0].parent_improving && search->stop_all == cancel
            && search->brd.to_string() == fen && search->brd.ply == 2,
            "real PVS must establish and retain its parent trend across child completion/cancellation");
        search->stop_all = false;
        // A spent node budget cancels the next entry before evaluation.
        search->nodes = std::max<U64>(search->nodes, 1);
        search->game.max_nodes = search->nodes;
        search->search_t::pvs(0, 1, 1, true);
        success &= check(search->stop_all && !search->stack->improving,
            "an entry cancelled before evaluation must clear an old trend");
    }
    return success;
}
}

int main() {
    magic::init();
    uci::silent(true);
    trans_table::disable();
    const bool trend = test_trend();
    const bool propagation = test_propagation();
    const bool lifetime = test_trend_lifetime();
    return trend && propagation && lifetime ? EXIT_SUCCESS : EXIT_FAILURE;
}
