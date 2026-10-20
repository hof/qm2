#include "engine.h"
#include <vector>

namespace {
constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
void play(search_t & search, const char * text) {
    move_t move{};
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::abort();
    }
    search.forward(&move, search.brd.gives_check(&move));
}
class probe_t : public search_t {
public:
    std::vector<int> depths;
    int helper_calls = 0, cancel_call = 0, child_score = 0;
    explicit probe_t(bool black = true) : search_t(black ? start :
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1") {
        play(*this, black ? "g1f3" : "g8f6");
        beta_pruning = null_enabled = lmr_enabled = ffp_enabled = correction_history_enabled = false;
        tablebase_limit = draw_contempt = 0;
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (iid_active) {
            ++helper_calls;
            stack->best_move.set(&brd, brd.us() ? "g1f3" : "g8f6");
            return 20;
        }
        if (brd.ply > 1) {
            depths.push_back(depth);
            stack->tt_clocks = {0, 98};
            stack->pv_count = 0;
            stop_all = cancel_call && int(depths.size()) == cancel_call;
            return child_score;
        }
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};
bool test_policy() {
    auto s = std::make_unique<probe_t>();
    bool ok = true;
    for (bool enabled : {false, true}) {
        s->iir_enabled = enabled;
        for (bool pv : {false, true}) {
            for (bool cut : {false, true}) {
                for (int depth = 0; depth <= MAX_PLY; ++depth) {
                    ok &= s->internal_search_depth(depth, pv, cut)
                        == depth - int(enabled && depth >= 6 && (pv || cut));
                }
            }
        }
    }
    s->iir_enabled = true;
    for (int exclusion = 0; exclusion < 7; ++exclusion) {
        auto p = std::make_unique<probe_t>();
        if (exclusion == 0) {
            p->stack->in_check = true;
        } else if (exclusion == 1) {
            p->iid_active = true;
        } else if (exclusion == 2) {
            p->verification_mode = verification_mode_t::null_move;
        } else if (exclusion == 3) {
            p->verification_mode = verification_mode_t::excluded_move;
        } else if (exclusion == 4) {
            p->stack->excluded_move = 1;
        } else if (exclusion == 5) {
            p->brd.stack->fifty_count = 99;
        } else {
            p->stack->tt_move.set(&p->brd, "g8f6");
        }
        ok &= p->internal_search_depth(6, true, false) == 6;
    }
    // Pseudo-legal self-check hints cannot suppress IIR.
    auto pinned = std::make_unique<search_t>("4k3/4r3/8/8/8/8/8/K3R3 b - - 0 1");
    play(*pinned, "e7e6");
    play(*pinned, "a1b1");
    pinned->stack->tt_move.set(&pinned->brd, "e6d6");
    ok &= pinned->brd.valid(&pinned->stack->tt_move)
        && !pinned->brd.legal(&pinned->stack->tt_move)
        && pinned->internal_search_depth(6, false, true) == 5;
    auto root = std::make_unique<search_t>(start);
    ok &= root->internal_search_depth(6, true, false) == 6;
    return check(ok, "IIR depth range, hint or context policy failed");
}
bool test_search_case(bool black, int child_score, bool iir, bool iid,
        int window, bool hint, int depth, int cancel) {
    auto p = std::make_unique<probe_t>(black);
    p->child_score = child_score;
    p->iir_enabled = iir;
    p->iid_enabled = iid;
    p->cancel_call = cancel;
    const bool pv = window == 0, cut = window == 1;
    const int beta = pv ? 100 : 1;
    const auto fen = p->brd.to_string();
    const auto key = p->brd.stack->tt_key;
    auto * frame = p->stack;
    move_t move{};
    move.set(&p->brd, black ? "g8f6" : "g1f3");
    p->stack->killer[0] = move;
    bool valid = true;
    trans_table::clear();
    if (hint) {
        trans_table::store(key, 0, 1, 1, 0, move.to_compact(), score::UPPERBOUND,
            p->tt_context());
    }
    const int result = p->pvs(-100, pv ? beta : -99, depth, cut);
    const int searched = depth - int(iir && (pv || cut) && depth >= 6 && !hint);
    const bool stopped = cancel && int(p->depths.size()) >= cancel;
    const int stopped_alpha = cancel > 1 && !child_score ? 0 : -100;
    valid &= p->stop_all == stopped
        && result == (stopped ? stopped_alpha : -child_score);
    valid &= !p->depths.empty();
    for (int child : p->depths) {
        valid &= child == searched - 1;
    }
    valid &= p->helper_calls == int(!iir && iid && pv && !hint);
    valid &= p->brd.to_string() == fen && p->brd.stack->tt_key == key
        && p->stack == frame && p->brd.ply == 1 && !p->iid_active
        && p->stack->tt_clocks.max < 98;
    int value = 0, stored_move = 0, flag = 0;
    const bool hit = trans_table::retrieve(key, 1, searched, value,
        stored_move, flag, p->tt_context(), p->brd.stack->fifty_count);
    if (!stopped) {
        valid &= hit && value == result
            && flag == score::flags(result, -100, pv ? beta : -99);
        if (searched != depth) {
            valid &= !trans_table::retrieve(key, 1, depth, value, stored_move,
                flag, p->tt_context(), p->brd.stack->fifty_count);
        }
    } else {
        valid &= !hit;
    }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    const auto & c = p->nnue_audit.counters;
    valid &= c.iir_reductions == uint64_t(searched != depth)
        && c.iir_pv == uint64_t(searched != depth && pv)
        && c.iir_cut == uint64_t(searched != depth && cut)
        && c.iir_cancelled == uint64_t(searched != depth && stopped)
        && c.iir_cutoffs == uint64_t(searched != depth && !stopped && !pv && !child_score);
#endif
    if (!valid) {
        std::cerr << "first failed case black/child/iir/iid/window/hint/depth/cancel "
            << black << '/' << child_score << '/' << iir << '/' << iid << '/'
            << window << '/' << hint << '/' << depth << '/' << cancel
            << " result " << result << " children " << p->depths.size()
            << " helper " << p->helper_calls << " clock " << int(p->stack->tt_clocks.max)
            << " TT " << hit << '/' << value << '/' << flag << '\n';
    }
    return valid;
}
bool test_search() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        for (int child_score : {0, 200}) {
            for (bool iir : {false, true}) {
                for (bool iid : {false, true}) {
                    for (int window = 0; window < 3; ++window) {
                        for (bool hint : {false, true}) {
                            for (int depth : {5, 6, 8}) {
                                for (int cancel : {0, 1, 2}) {
                                    ok &= test_search_case(black, child_score, iir, iid,
                                        window, hint, depth, cancel);
                                    ++cases;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::cout << cases << " controlled IIR/IID PVS cases\n";
    return check(ok, "IIR/IID child depth, TT bound/depth, cancellation or restoration failed");
}
bool test_worker_and_terminals() {
    bool ok = true;
    for (bool enabled : {false, true}) {
        options::get_option("IIR")->value = enabled;
        auto worker = std::make_unique<probe_t>();
        options::get_option("IIR")->value = !enabled;
        ok &= worker->iir_enabled == enabled;
        auto royal = std::make_unique<search_t>(start, nullptr, nullptr, std::vector<U64>{}, 17);
        ok &= !royal->iir_enabled;
    }
    options::get_option("IIR")->value = 1;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto worker = std::make_unique<probe_t>();
        ok &= !worker->iir_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    for (const char * last : {"g5g6", "g5g7"}) {
        auto p = std::make_unique<search_t>("7k/8/5K2/6Q1/8/8/8/8 w - - 0 1");
        play(*p, last);
        p->tablebase_limit = p->draw_contempt = 0;
        p->beta_pruning = p->null_enabled = false;
        trans_table::clear();
        const auto fen = p->brd.to_string();
        const int result = p->pvs(-score::INF, score::INF, 6);
        ok &= result == (last == std::string("g5g6") ? 0 : -score::MATE + 1)
            && p->brd.to_string() == fen && p->brd.ply == 1;
    }
    return check(ok, "IIR worker, variant, audit or legal terminal contract failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    options::get_option("OwnBook")->value = 0;
    bool ok = test_policy();
    ok &= test_search();
    ok &= test_worker_and_terminals();
    return ok ? 0 : 1;
}
