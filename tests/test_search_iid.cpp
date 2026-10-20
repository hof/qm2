#include "engine.h"
#include <vector>

namespace {
class iid_probe_t : public search_t {
public:
    int helpers = 0, helper_depth = 0, child_depth = 0, children = 0;
    bool cancel = false, invalid_hint = false, throw_helper = false;
    std::string hint, first_move;
    explicit iid_probe_t(const char * fen) : search_t(fen) {
        beta_pruning = null_enabled = lmr_enabled = false;
        iir_enabled = false;
        iid_enabled = true;
        tablebase_limit = 0;
        hint = brd.us() ? "g1f3" : "g8f6";
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply == 0 && iid_active) {
            ++helpers;
            ++nodes;
            helper_depth = depth;
            if (!invalid_hint) {
                stack->best_move.set(&brd, hint.c_str());
            } else {
                stack->best_move.clear();
            }
            stack->pv_count = 1;
            stack->current_move = stack->best_move;
            stack->eval_result = 777;
            stack->improving = false;
            // Helper subtrees must not start nested IID.
            prepare_iid_move(alpha, beta, 7);
            if (throw_helper) {
                throw std::runtime_error("helper failure");
            }
            stop_all = cancel;
            return score::MATE - 1;
        }
        if (brd.ply > 0) {
            ++nodes;
            ++children;
            child_depth = depth;
            if (first_move.empty()) {
                first_move = (stack - 1)->current_move.to_string();
            }
            stack->pv_count = 0;
            return 0;
        }
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
}

int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    trans_table::disable();
    options::get_option("OwnBook")->value = 0;
    bool ok = true;
    for (const char * fen : {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"}) {
        for (int depth : {3, 4, 7}) {
            auto probe = std::make_unique<iid_probe_t>(fen);
            const U64 key = probe->brd.stack->tt_key;
            const int value = probe->pvs(-1000, 1000, depth);
            ok &= check(value == 0 && probe->children > 0 && probe->child_depth == depth - 1,
                "IID score replaced full-depth search");
            ok &= check(probe->helpers == (depth >= 4) && (!probe->helpers
                || (probe->helper_depth == depth - 2 && probe->first_move == probe->hint)),
                "IID depth, nesting or first-move hint differs");
            ok &= check(probe->iid_enabled && !probe->brd.ply && probe->stack == probe->root_stack
                && probe->brd.to_string() == fen && probe->brd.stack->tt_key == key,
                "IID did not restore worker/board state");
        }
        for (int exclusion : {0, 1, 2}) {
            auto probe = std::make_unique<iid_probe_t>(fen);
            if (exclusion == 0) {
                probe->iid_enabled = false;
            }
            if (exclusion == 1) {
                probe->stack->tt_move.set(&probe->brd, probe->hint.c_str());
            }
            probe->prepare_iid_move(0, exclusion == 2 ? 1 : 100, 4);
            ok &= check(!probe->helpers, "IID ignored disabled, hash-move or non-PV exclusion");
        }
        for (bool cancel : {false, true}) {
            auto probe = std::make_unique<iid_probe_t>(fen);
            probe->cancel = cancel;
            probe->stack->eval_result = 12;
            probe->stack->improving = true;
            probe->stack->pv_count = 0;
            const U64 key = probe->brd.stack->tt_key;
            probe->prepare_iid_move(-100, 100, 4);
            ok &= check(probe->helpers == 1 && probe->iid_enabled && probe->stop_all == cancel
                && probe->stack->eval_result == 12 && probe->stack->improving && probe->stack->pv_count == 0
                && !probe->stack->current_move.piece && probe->brd.stack->tt_key == key,
                "IID helper frame/cancellation restoration differs");
            ok &= check(bool(probe->stack->tt_move.piece) == !cancel,
                "cancelled IID installed a partial hint");
        }
        auto cancelled = std::make_unique<iid_probe_t>(fen);
        cancelled->cancel = true;
        cancelled->pvs(-1000, 1000, 4);
        ok &= check(cancelled->stop_all && !cancelled->children && cancelled->iid_enabled,
            "search continued after cancelled IID");
        auto exceptional = std::make_unique<iid_probe_t>(fen);
        exceptional->throw_helper = true;
        exceptional->stack->eval_result = 12;
        bool caught = false;
        try { exceptional->prepare_iid_move(-100, 100, 4); }
        catch (const std::runtime_error &) { caught = true; }
        ok &= check(caught && !exceptional->iid_active && exceptional->iid_enabled
            && exceptional->stack->eval_result == 12 && !exceptional->stack->current_move.piece,
            "IID did not restore frame after helper exception");
        auto no_hint = std::make_unique<iid_probe_t>(fen);
        no_hint->invalid_hint = true;
        no_hint->prepare_iid_move(-100, 100, 4);
        ok &= check(!no_hint->stack->tt_move.piece, "IID installed stale hint without helper move");
    }
    trans_table::enable();
    return ok ? 0 : 1;
}
