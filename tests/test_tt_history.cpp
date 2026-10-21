#include "engine.h"
#include "w17/w17_search.h"
#include <cstring>

namespace {
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
class probe_t : public search_t {
public:
    int children = 0;
    bool ordinals_valid = true;
    explicit probe_t(bool black = false) : search_t(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
        for (const char * text : {"g1f3", "g8f6", "e2e3", "e7e6", "b1c3", "b8c6"}) {
            if (black && brd.ply == 5) {
                break;
            }
            move_t move{};
            move.set(&brd, text);
            forward(&move, brd.gives_check(&move));
        }
        (stack - 1)->move_count = 1;
        null_enabled = beta_pruning = ffp_enabled = lmr_enabled = false;
        iid_enabled = iir_enabled = main_see_enabled = razoring_enabled = false;
        probcut_enabled = singular_extensions_enabled = false;
        correction_history_enabled = upcoming_repetition_enabled = false;
        // Keep this mechanism probe independent of quiet-pruning policy.
        lmp_enabled = history_pruning_enabled = false;
        tablebase_limit = draw_contempt = 0;
    }
    int pvs(int, int, int, bool = false) override {
        ++children;
        ordinals_valid &= (stack - 1)->move_count == children;
        return 0;
    }
    move_t hint() {
        move_t move{};
        move.set(&brd, brd.us() ? "f1e2" : "f8e7");
        return move;
    }
    void seed(int depth, int value, int flag, int compact, U64 context,
            clock_range_t clocks = {0, 98}, tt_mode_t mode = tt_mode_t::main_search) {
        trans_table::clear();
        trans_table::store(brd.stack->tt_key, 0, brd.ply, depth, value,
            compact, flag, context, clocks, mode);
    }
};

bool test_updates() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int depth : {1, 4, 20}) {
            for (int ordinal : {0, 1, 3, 4}) {
                for (int mask = 0; mask < 16; ++mask) {
                    auto s = std::make_unique<probe_t>(black);
                    s->quiet_history_policy = {bool(mask & 1), bool(mask & 2), bool(mask & 4)};
                    s->long_continuation_enabled = mask & 8;
                    (s->stack - 1)->move_count = ordinal;
                    auto move = s->hint();
                    auto expected = std::make_unique<quiet_history_t>();
                    const auto & previous = (s->stack - 1)->current_move;
                    const int d = std::clamp(depth, 1, 8), bonus = 32 * d * d;
                    expected->update(s->brd.us(), move, &previous, s->brd.stack->pawn_key,
                        bonus, nullptr, s->quiet_history_policy);
                    for (const auto * context : s->long_continuation_contexts()) {
                        if (context) {
                            expected->update_continuation(move, *context, bonus);
                        }
                    }
                    if (ordinal == 1 || ordinal == 3) {
                        for (int distance : {2, 3, 5}) {
                            if ((distance == 2 && (mask & 2)) || (distance != 2 && (mask & 8))) {
                                expected->update_continuation(previous, (s->stack - distance)->current_move, -bonus);
                            }
                        }
                    }
                    s->seed(depth, 75, score::LOWERBOUND, move.to_compact(), s->tt_context());
                    const auto fen = s->brd.to_string();
                    const auto key = s->brd.stack->tt_key;
                    const auto frame = *s->stack;
                    const auto parent = *(s->stack - 1);
                    const int value = s->search_t::pvs(74, 75, depth);
                    auto after = *s->stack;
                    after.tt_key = frame.tt_key;
                    after.tt_clocks = frame.tt_clocks;
                    ok &= value == 75 && s->nodes == 1 && s->children == 0
                        && std::memcmp(expected.get(), s->quiet_history.get(), sizeof(quiet_history_t)) == 0
                        && s->brd.to_string() == fen && s->brd.stack->tt_key == key
                        && s->stack == &s->_stack[black ? 5 : 6]
                        && std::memcmp(&frame, &after, sizeof(frame)) == 0
                        && std::memcmp(&parent, s->stack - 1, sizeof(parent)) == 0
                        && s->stack->tt_clocks.max == 98;
                }
            }
        }
    }
    return check(ok, "TT cutoff reward, predecessor penalty, policy or state restoration failed");
}

bool test_cutoff_contract() {
    bool ok = true;
    for (int flag : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
        for (int value : {74, 75, 76}) {
            for (bool pv : {false, true}) {
                auto s = std::make_unique<probe_t>();
                const auto move = s->hint();
                s->seed(4, value, flag, move.to_compact(), s->tt_context());
                const int result = s->search_t::pvs(pv ? 70 : 74, 75, 4);
                const bool cutoff = flag == score::EXACT || (!pv
                    && ((flag == score::LOWERBOUND && value >= 75)
                        || (flag == score::UPPERBOUND && value <= 74)));
                const bool learns = cutoff && !pv && value >= 75;
                ok &= bool(s->quiet_history->main[s->brd.us()][move.ssq][move.tsq]) == learns
                    && s->ordinals_valid
                    && (!cutoff || (result == value && s->nodes == 1 && s->children == 0));
            }
        }
    }
    // Compatible scores alone are insufficient: helper/null/root/clock/decisive
    // contexts and missing, invalid or pinned hints must not teach history.
    for (int guard = 0; guard < 15; ++guard) {
        auto s = std::make_unique<probe_t>();
        auto move = s->hint();
        int compact = move.to_compact(), value = 75, depth = 4;
        U64 context = s->tt_context();
        clock_range_t clocks{0, 98};
        auto mode = tt_mode_t::main_search;
        if (guard == 0) { s->tt_history_enabled = false; }
        if (guard == 1) { s->iid_active = true; }
        if (guard == 2) { s->verification_mode = verification_mode_t::null_move; }
        if (guard == 3) {
            s->verification_mode = verification_mode_t::excluded_move;
            s->stack->excluded_move = compact;
        }
        if (guard == 4) { s->stack->repetition_start = 1; context = s->tt_context(); }
        if (guard == 5) { s->stop_all = true; }
        if (guard == 6) { compact = 0; }
        if (guard == 7) { move.set(&s->brd, "a1a8"); compact = move.to_compact(); }
        if (guard == 8) { depth = 3; }
        if (guard == 9) { context ^= 1; }
        if (guard == 10) { clocks = {80, 98}; }
        if (guard == 11) { mode = tt_mode_t::quiescence_entry; }
        if (guard == 12) { value = score::TABLEBASE; }
        if (guard == 13) { s->brd.stack->fifty_count = 99; clocks = {0, 101}; context = s->tt_context(); }
        if (guard == 14) { s->stack->repetition_start = 1; s->forward(); context = s->tt_context(); }
        s->seed(depth, value, score::EXACT, compact, context, clocks, mode);
        s->search_t::pvs(74, 75, 4);
        const auto empty = std::make_unique<quiet_history_t>();
        ok &= std::memcmp(empty.get(), s->quiet_history.get(), sizeof(quiet_history_t)) == 0;
    }
    return check(ok, "TT bound/window, depth/context/clock/mode or learning guard failed");
}

bool test_move_classes() {
    bool ok = true;
    struct fixture_t { const char * fen; const char * preceding; const char * hint; bool reward; };
    const fixture_t fixtures[] = {
        // Pinned rook: pseudo-legal alone is insufficient.
        {"k3r3/7p/8/8/8/8/4R3/4K3 b - - 0 1", "h7h6", "e2f2", false},
        {"k3r3/7p/8/8/8/8/4R3/4K3 b - - 0 1", "h7h6", "e2e8", false},
        {"7k/P6p/8/8/8/8/8/4K3 b - - 0 1", "h7h6", "a7a8q", false},
        {"7k/7p/8/8/8/8/8/4K2R b K - 0 1", "h7h6", "e1g1", false},
        {"4r2k/8/8/8/8/8/8/R3K3 b - - 0 1", "e8e2", "e1d1", true}
    };
    for (const auto & fixture : fixtures) {
        auto s = std::make_unique<search_t>(fixture.fen);
        move_t previous{}, move{};
        previous.set(&s->brd, fixture.preceding);
        s->forward(&previous, s->brd.gives_check(&previous));
        move.set(&s->brd, fixture.hint);
        auto expected = std::make_unique<quiet_history_t>();
        if (fixture.reward) {
            expected->update(s->brd.us(), move, &previous, s->brd.stack->pawn_key, 512);
        }
        trans_table::clear();
        trans_table::store(s->brd.stack->tt_key, 0, 1, 4, 75, move.to_compact(),
            score::LOWERBOUND, s->tt_context(), {0, 98});
        ok &= s->pvs(74, 75, 4) == 75 && s->nodes == 1
            && std::memcmp(expected.get(), s->quiet_history.get(), sizeof(quiet_history_t)) == 0;
    }
    // An actual root TT reuse has no preceding search move and must not learn.
    auto root = std::make_unique<search_t>(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    move_t move{};
    move.set(&root->brd, "g1f3");
    trans_table::clear();
    trans_table::store(root->brd.stack->tt_key, 0, 0, 4, 75, move.to_compact(),
        score::LOWERBOUND, root->tt_context(), {0, 98});
    ok &= root->pvs(74, 75, 4) == 75
        && root->quiet_history->main[WHITE][g1][f3] == 0;
    return check(ok, "TT move legality, capture/promotion/castling, checked evasion or root guard failed");
}

bool test_worker() {
    bool ok = true;
    for (int enabled : {0, 1}) {
        options::get_option("TTHistory")->value = enabled;
        auto s = std::make_unique<probe_t>();
        options::get_option("TTHistory")->value = !enabled;
        ok &= s->tt_history_enabled == bool(enabled);
        const auto move = s->hint();
        s->seed(4, 75, score::LOWERBOUND, move.to_compact(), s->tt_context());
        ok &= s->search_t::pvs(74, 75, 4) == 75
            && bool(s->quiet_history->main[s->brd.us()][move.ssq][move.tsq]) == bool(enabled);
    }
    options::get_option("TTHistory")->value = 1;
    auto losers = std::make_unique<w17_search_t>(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    ok &= !losers->tt_history_enabled && !losers->quiet_history;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto s = std::make_unique<probe_t>();
        ok &= !s->tt_history_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    return check(ok, "TT history worker snapshot or variant/audit isolation failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    return test_updates() && test_cutoff_contract() && test_move_classes() && test_worker() ? 0 : 1;
}
