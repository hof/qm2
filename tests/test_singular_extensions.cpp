#include "engine.h"
#include <cstring>
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
    int calls = 0, delta = -1, cancel = 0, helper_depth = 0, expected_threshold = 93, node_ply = 1;
    bool valid = true, raise_exception = false;
    std::vector<int> child_depths;
    std::vector<uint16_t> child_moves;
    explicit probe_t(bool black = true, bool checking = false) : search_t(checking
            ? (black ? "r6k/8/8/8/8/8/P7/K7 w - - 0 1"
                : "k7/p7/8/8/8/8/8/R6K b - - 0 1")
            : (black ? start : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1")) {
        play(*this, checking ? (black ? "a2a3" : "a7a6") : (black ? "g1f3" : "g8f6"));
        stack->tt_move.set(&brd, checking ? (black ? "a8a3" : "a1a6") : (black ? "g8f6" : "g1f3"));
        beta_pruning = null_enabled = lmr_enabled = ffp_enabled = false;
        correction_history_enabled = upcoming_repetition_enabled = false;
        iid_enabled = iir_enabled = main_see_enabled = razoring_enabled = false;
        tablebase_limit = draw_contempt = 0;
        attempted_depth = 8;
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (verification_mode == verification_mode_t::excluded_move && brd.ply == node_ply) {
            ++calls;
            helper_depth = depth;
            valid &= !null_enabled && stack->excluded_move == stack->tt_move.to_compact()
                && beta == alpha + 1 && beta == expected_threshold;
            // Recursive consumers must remain disabled inside verification.
            valid &= singular_extension(-1000, 1000, 8, false).move == 0;
            nodes += 3;
            stack->move_list.stage = 99;
            stack->pv_count = 0;
            stack->tt_clocks = {0, 80};
            stack->eval_result = 456;
            stack->improving = !stack->improving;
            stack->repetition_start = 99;
            if (cancel == 1) {
                stop_all = true;
            }
            if (raise_exception) {
                throw std::runtime_error("singular verification fixture");
            }
            return beta + delta;
        }
        if (brd.ply > node_ply) {
            child_depths.push_back(depth);
            child_moves.push_back((stack - 1)->current_move.to_compact());
            stack->pv_count = 0;
            stack->tt_clocks = {0, 98};
            if (cancel == 2) {
                stop_all = true;
            }
            return -200;
        }
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
    void seed(int depth = 4, int flag = score::LOWERBOUND, int value = 100,
            U64 context = 0, clock_range_t clocks = {0, 101},
            tt_mode_t mode = tt_mode_t::main_search) {
        trans_table::clear();
        trans_table::store(brd.stack->tt_key, 0, brd.ply, depth, value,
            stack->tt_move.to_compact(), flag, context ? context : tt_context(), clocks, mode);
    }
};
bool test_policy_and_cache() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int depth = 0; depth <= MAX_PLY; ++depth) {
            auto p = std::make_unique<probe_t>(black);
            p->seed(MAX_PLY);
            p->expected_threshold = 100 - depth;
            const auto result = p->singular_extension(-1000, 1000, depth, false).move;
            ok &= bool(result) == (depth >= 7) && p->calls == int(depth >= 7)
                && (!result || p->helper_depth == (depth - 1) / 2) && p->valid;
        }
        for (int flag : {score::UPPERBOUND, score::LOWERBOUND, score::EXACT}) {
            for (int stored : {3, 4, 6, 7}) {
                auto p = std::make_unique<probe_t>(black);
                p->seed(stored, flag);
                const bool expected = flag != score::UPPERBOUND && stored >= 4;
                ok &= bool(p->singular_extension(-1000, 1000, 7, true).move) == expected;
            }
        }
        for (int exclusion = 0; exclusion < 17; ++exclusion) {
            auto p = std::make_unique<probe_t>(black);
            p->seed();
            if (exclusion == 0) {
                p->singular_extensions_enabled = false;
            } else if (exclusion == 1) {
                p->stack->in_check = true;
            } else if (exclusion == 2) {
                p->iid_active = true;
            } else if (exclusion == 3) {
                p->verification_mode = verification_mode_t::null_move;
            } else if (exclusion == 4) {
                p->verification_mode = verification_mode_t::excluded_move;
            } else if (exclusion == 5) {
                p->stack->excluded_move = p->stack->tt_move.to_compact();
            } else if (exclusion == 6) {
                p->brd.stack->fifty_count = 99;
            } else if (exclusion == 7) {
                p->seed(4, score::LOWERBOUND, 100, p->tt_context() ^ 1);
            } else if (exclusion == 8) {
                p->seed(4, score::LOWERBOUND, 100, 0, {2, 98});
            } else if (exclusion == 9) {
                p->seed(4, score::LOWERBOUND, 100, 0, {0, 101}, tt_mode_t::quiescence_entry);
            } else if (exclusion == 10) {
                p->seed(4, score::LOWERBOUND, score::TABLEBASE - MAX_PLY);
            } else if (exclusion == 11) {
                p->seed(4, score::LOWERBOUND, -score::TABLEBASE + MAX_PLY + 1);
            } else if (exclusion == 12) {
                p->stack->tt_move.clear();
            } else if (exclusion == 13) {
                p->stack->tt_move.set(&p->brd, black ? "b8c6" : "b1c3");
            } else if (exclusion == 14) {
                p->attempted_depth = 0;
                p->brd.ply = MAX_PLY - 2;
            } else if (exclusion == 15) {
                p->attempted_depth = 0;
                p->brd.ply = 14;
            } else {
                trans_table::disable();
            }
            ok &= p->singular_extension(-1000, 1000, 7, false).move == 0 && !p->calls;
            trans_table::enable();
        }
        for (int sign : {-1, 1}) {
            auto p = std::make_unique<probe_t>(black);
            p->seed();
            const int bound = sign * (score::TABLEBASE - MAX_PLY);
            ok &= p->singular_extension(bound, bound + 1, 7, false).move == 0;
        }
    }
    auto root = std::make_unique<search_t>(start);
    root->stack->tt_move.set(&root->brd, "g1f3");
    ok &= root->singular_extension(-1000, 1000, 7, false).move == 0;
    // A pinned pseudo-legal hint cannot nominate an extension.
    auto pinned = std::make_unique<search_t>("4k3/4r3/8/8/8/8/8/K3R3 b - - 0 1");
    play(*pinned, "e7e6");
    play(*pinned, "a1b1");
    pinned->stack->tt_move.set(&pinned->brd, "e6d6");
    ok &= pinned->brd.valid(&pinned->stack->tt_move)
        && !pinned->brd.legal(&pinned->stack->tt_move)
        && pinned->singular_extension(-1000, 1000, 7, false).move == 0;
    return check(ok, "singular policy, compatible TT bounds/depth/mode/context/clocks or illegal hint failed");
}
bool test_helper_scope() {
    bool ok = true;
    for (int delta : {-1, 0, 1}) {
        for (int outcome = 0; outcome < 3; ++outcome) {
            auto p = std::make_unique<probe_t>();
            p->delta = delta;
            p->cancel = outcome == 1;
            p->raise_exception = outcome == 2;
            p->null_enabled = true;
            p->seed(4, score::LOWERBOUND, 100, 0, {0, 90});
            const auto saved = *p->stack;
            const auto fen = p->brd.to_string();
            const auto key = p->brd.stack->tt_key;
            bool threw = false;
            uint16_t result = 0;
            try {
                result = p->singular_extension(-1000, 1000, 7, false).move;
            } catch (const std::runtime_error &) {
                threw = true;
            }
            auto expected = saved;
            expected.tt_clocks.max = 80;
            ok &= std::memcmp(&expected, p->stack, sizeof(expected)) == 0
                && p->brd.to_string() == fen && p->brd.stack->tt_key == key
                && p->brd.ply == 1 && p->stack == p->root_stack + 1
                && p->null_enabled && !p->iid_active
                && p->verification_mode == verification_mode_t::none
                && p->valid && p->calls == 1 && p->nodes == 3
                && threw == (outcome == 2)
                && bool(result) == (outcome == 0 && delta < 0);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
            const auto & c = p->nnue_audit.counters;
            ok &= c.singular_attempts == 1
                && c.singular_extensions == unsigned(outcome == 0 && delta < 0)
                && c.singular_rejected == unsigned(outcome == 0 && delta >= 0)
                && c.singular_cancelled == unsigned(outcome == 1);
#endif
        }
    }
    return check(ok, "singular helper thresholds, cancellation/exception or complete frame restoration failed");
}
bool test_pvs() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        for (bool enabled : {false, true}) {
            for (bool cut : {false, true}) {
                for (int delta : {-1, 0, 1}) {
                    for (int cancel : {0, 1, 2}) {
                        auto p = std::make_unique<probe_t>(black);
                        p->singular_extensions_enabled = enabled;
                        p->delta = delta;
                        p->cancel = cancel;
                        p->seed();
                        const auto candidate = p->stack->tt_move.to_compact();
                        const auto fen = p->brd.to_string();
                        const auto key = p->brd.stack->tt_key;
                        const int value = p->pvs(-1000, cut ? -999 : 1000, 7, cut);
                        const bool helper_stop = enabled && cancel == 1;
                        const bool stopped = helper_stop || cancel == 2;
                        ok &= value == (stopped ? -1000 : 200) && p->stop_all == stopped
                            && p->valid && p->calls == int(enabled)
                            && p->brd.to_string() == fen && p->brd.stack->tt_key == key
                            && p->stack == p->root_stack + 1 && !p->stack->excluded_move
                            && p->verification_mode == verification_mode_t::none;
                        ok &= p->child_depths.empty() == helper_stop;
                        for (size_t i = 0; i < p->child_depths.size(); ++i) {
                            ok &= p->child_depths[i] == 6 + int(enabled && delta < 0
                                && p->child_moves[i] == candidate);
                        }
                        int score = 0, move = 0, flag = 0;
                        const bool hit = trans_table::retrieve(key, 1, 7, score, move,
                            flag, p->tt_context(), p->brd.stack->fifty_count);
                        ok &= hit == !stopped;
                        if (hit) {
                            ok &= score == value && flag == (cut ? score::LOWERBOUND : score::EXACT);
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    std::cout << cases << " controlled singular PVS cases\n";
    return check(ok, "singular PVS chosen child depth, bound, cancellation or restoration failed");
}
bool test_repetition_floor() {
    auto p = std::make_unique<probe_t>();
    play(*p, "g8f6");
    play(*p, "f3g1");
    p->node_ply = 3;
    p->upcoming_repetition_enabled = true;
    p->stack->tt_move.set(&p->brd, "f6h5");
    p->seed();
    const auto fen = p->brd.to_string();
    const int value = p->pvs(-1000, 1000, 7);
    return check(value == 200 && !p->calls && !p->child_depths.empty()
        && p->brd.to_string() == fen && p->stack == p->root_stack + 3,
        "upcoming repetition floor must suppress singular verification");
}
bool test_checking_candidate() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int delta : {-1, 0}) {
            auto p = std::make_unique<probe_t>(black, true);
            p->delta = delta;
            p->seed();
            const auto candidate = p->stack->tt_move.to_compact();
            ok &= p->brd.gives_check(&p->stack->tt_move) != 0;
            p->pvs(-1000, 1000, 7);
            bool found = false;
            for (size_t i = 0; i < p->child_depths.size(); ++i) {
                if (p->child_moves[i] == candidate) {
                    found = true;
                    ok &= p->child_depths[i] == 6 + int(delta < 0);
                }
            }
            ok &= found && p->valid;
        }
    }
    return check(ok, "a checking candidate must receive only its singular allocation");
}
bool test_real_verification() {
    bool ok = true;
    // No check, only h3h4 is legal. Excluding it must fail low without mate.
    auto p = std::make_unique<search_t>("8/8/8/8/8/1qk4P/8/K7 b - - 0 1");
    play(*p, "c3c2");
    p->stack->tt_move.set(&p->brd, "h3h4");
    p->tablebase_limit = p->draw_contempt = 0;
    const auto candidate = p->stack->tt_move.to_compact();
    const auto fen = p->brd.to_string();
    trans_table::clear();
    trans_table::store(p->brd.stack->tt_key, 0, 1, 4, 100, candidate,
        score::LOWERBOUND, p->tt_context());
    ok &= !p->stack->in_check && p->singular_extension(-1000, 1000, 7, false).move == candidate
        && p->brd.to_string() == fen && p->stack == p->root_stack + 1
        && p->verification_mode == verification_mode_t::none && !p->stack->excluded_move;
    return check(ok, "real only-legal-move exclusion, terminal handling or restoration failed");
}
bool test_real_cancellation() {
    bool ok = true;
    for (int limit : {1, 2, 3, 8}) {
        auto p = std::make_unique<search_t>(start);
        play(*p, "g1f3");
        p->stack->tt_move.set(&p->brd, "g8f6");
        p->tablebase_limit = p->draw_contempt = 0;
        p->game.max_nodes = limit;
        const auto fen = p->brd.to_string();
        const auto key = p->brd.stack->tt_key;
        const auto history = std::make_unique<quiet_history_t>(*p->quiet_history);
        const auto captures = std::make_unique<capture_history_t>(*p->capture_history);
        trans_table::clear();
        trans_table::store(key, 0, 1, 4, 100, p->stack->tt_move.to_compact(),
            score::LOWERBOUND, p->tt_context());
        ok &= p->singular_extension(-1000, 1000, 7, false).move == 0 && p->stop_all
            && p->brd.to_string() == fen && p->brd.stack->tt_key == key
            && p->stack == p->root_stack + 1 && p->brd.ply == 1
            && !p->stack->excluded_move && p->verification_mode == verification_mode_t::none
            && std::memcmp(history.get(), p->quiet_history.get(), sizeof(*history)) == 0
            && std::memcmp(captures.get(), p->capture_history.get(), sizeof(*captures)) == 0;
        int score = 0, move = 0, flag = 0;
        ok &= trans_table::retrieve(key, 1, 4, score, move, flag, p->tt_context(), 1)
            && score == 100 && flag == score::LOWERBOUND
            && !trans_table::retrieve(key, 1, 7, score, move, flag, p->tt_context(), 1);
    }
    return check(ok, "real singular cancellation, TT/history isolation or board restoration failed");
}
bool test_workers() {
    bool ok = options::get_value("SingularExtensions") == 1
        && std::string(options::get_uci_option("SingularExtensions")) == "type check default true";
    for (bool enabled : {false, true}) {
        options::get_option("SingularExtensions")->value = enabled;
        auto p = std::make_unique<search_t>(start);
        options::get_option("SingularExtensions")->value = !enabled;
        ok &= p->singular_extensions_enabled == enabled;
        auto royal = std::make_unique<search_t>(start, nullptr, nullptr, std::vector<U64>{}, 17);
        ok &= !royal->singular_extensions_enabled;
    }
    options::get_option("SingularExtensions")->value = 1;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto p = std::make_unique<search_t>(start);
        ok &= !p->singular_extensions_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    return check(ok, "singular worker snapshot, public control or variant/audit isolation failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    options::get_option("SingularPolicy")->value = 0;
    const bool ok = test_policy_and_cache() & test_helper_scope() & test_pvs()
        & test_repetition_floor() & test_checking_candidate() & test_real_verification() & test_real_cancellation() & test_workers();
    return ok ? 0 : 1;
}
