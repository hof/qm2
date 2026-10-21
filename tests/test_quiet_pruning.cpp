#include "engine.h"
#include "w17/w17_search.h"
#include <algorithm>
#include <vector>

namespace {
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
class probe_t : public search_t {
public:
    int entry_ply, cancel_call = 0, child_value = 0;
    std::vector<std::string> children;
    explicit probe_t(bool black = false) : search_t(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
        for (const char * text : {"g1f3", "g8f6", "b1c3", "b8c6", "a2a3"}) {
            if (!black && brd.ply == 4) {
                break;
            }
            move_t move{};
            move.set(&brd, text);
            forward(&move, brd.gives_check(&move));
        }
        entry_ply = brd.ply;
        null_enabled = beta_pruning = ffp_enabled = lmr_enabled = false;
        iid_enabled = iir_enabled = main_see_enabled = razoring_enabled = false;
        probcut_enabled = singular_extensions_enabled = upcoming_repetition_enabled = false;
        correction_history_enabled = false;
        tablebase_limit = draw_contempt = 0;
    }
    move_t quiet() {
        move_t move{};
        move.set(&brd, brd.us() ? "f3g1" : "f6g8");
        return move;
    }
    int pvs(int alpha, int beta, int depth, bool cut = false) override {
        if (brd.ply == entry_ply) {
            return search_t::pvs(alpha, beta, depth, cut);
        }
        children.push_back((stack - 1)->current_move.to_string());
        stack->pv_count = 0;
        stack->tt_clocks = {0, 101};
        stop_all = cancel_call && int(children.size()) == cancel_call;
        return -child_value;
    }
};

bool test_boundaries() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        auto s = std::make_unique<probe_t>(black);
        auto move = s->quiet();
        const auto fen = s->brd.to_string();
        const auto key = s->brd.stack->tt_key;
        for (bool improving : {false, true}) {
            s->stack->improving = improving;
            for (int depth = 1; depth < MAX_PLY; ++depth) {
                // Independent budgets for the first three depths are 2/3/6
                // or 4/7/12; deeper budgets must grow and preserve more moves.
                const int limit = (3 + depth * depth) / (improving ? 1 : 2);
                for (int reduced : {0, 6, 7, 8, 9, 20}) {
                    for (int count : {limit - 1, limit, limit + 1}) {
                        s->lmp_enabled = true;
                        s->history_pruning_enabled = false;
                        s->stack->tt_clocks = {0, 101};
                        const bool want = reduced < 9 && count >= limit;
                        const bool got = s->prune_quiet(move, depth, count, reduced, 0, 100, 101, false);
                        ok &= got == want && s->stack->tt_clocks.max == (want ? 98 : 101);
                        ++cases;
                    }
                    for (int offset : {-1, 0, 1}) {
                        s->lmp_enabled = false;
                        s->history_pruning_enabled = true;
                        // Test realizable single-component thresholds; deeper
                        // aggregate behavior is covered through context policies.
                        if (depth <= 7) {
                            s->quiet_history->main[s->brd.us()][move.ssq][move.tsq] = -2048 * depth + offset;
                            const bool want = reduced < 7 && offset < 0;
                            ok &= s->prune_quiet(move, depth, 1, reduced, 0, 100, 101, false) == want;
                            ++cases;
                        }
                    }
                }
                s->quiet_history->main[s->brd.us()][move.ssq][move.tsq] = 0;
            }
        }
        ok &= s->brd.to_string() == fen && s->brd.stack->tt_key == key;
    }
    std::cout << cases << " quiet-pruning boundary cases\n";
    return check(ok, "quiet pruning depth/count/history boundaries failed");
}

bool test_guards() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int guard = 0; guard < 19; ++guard) {
            auto s = std::make_unique<probe_t>(black);
            auto move = s->quiet();
            int count = 10, best = 0, alpha = 100, beta = 101;
            bool dangerous = false;
            s->lmp_enabled = s->history_pruning_enabled = true;
            if (guard == 0) { s->lmp_enabled = s->history_pruning_enabled = false; }
            if (guard == 1) { count = 0; }
            if (guard == 2) { dangerous = true; }
            if (guard == 3) { s->stack->in_check = true; }
            if (guard == 4) { s->stop_all = true; }
            if (guard == 5) { move.capture = WPAWN; }
            if (guard == 6) { move.promotion = WQUEEN; }
            if (guard == 7) { move.castle = 1; }
            if (guard == 8) { beta = 200; }
            if (guard == 9) { best = -score::TABLEBASE + MAX_PLY; }
            if (guard == 10) { alpha = score::TABLEBASE - MAX_PLY; beta = alpha + 1; }
            if (guard == 11) { beta = -score::TABLEBASE + MAX_PLY; alpha = beta - 1; }
            if (guard == 12) { s->iid_active = true; }
            if (guard == 13) { s->verification_mode = verification_mode_t::null_move; }
            if (guard == 14) { s->verification_mode = verification_mode_t::excluded_move; }
            if (guard == 15) { s->stack->excluded_move = move.to_compact(); }
            if (guard == 16) { s->stack->repetition_start = s->brd.ply; }
            if (guard == 17) { s->brd.stack->fifty_count = 99; }
            if (guard == 18) { s->brd.ply = 0; }
            ok &= !s->prune_quiet(move, 1, count, 1, best, alpha, beta, dangerous);
        }
        for (int killer : {-1, 0, 1}) {
            auto s = std::make_unique<probe_t>(black);
            auto move = s->quiet();
            s->lmp_enabled = false;
            s->quiet_history->main[s->brd.us()][move.ssq][move.tsq] = -8192;
            if (killer >= 0) { s->stack->killer[killer] = move; }
            move.score = score::INF;
            ok &= s->prune_quiet(move, 2, 1, 2, 0, 100, 101, false) == (killer < 0);
            s->quiet_history_policy.main = false;
            ok &= !s->prune_quiet(move, 2, 1, 2, 0, 100, 101, false);
        }
    }
    // A pinned pseudo-legal rook move must not supply a prune or clock bound.
    auto pinned = std::make_unique<search_t>("4r1k1/p7/8/8/8/8/4R3/4K3 b - - 0 1");
    move_t move{};
    move.set(&pinned->brd, "a7a6");
    pinned->forward(&move, 0);
    move.set(&pinned->brd, "e2d2");
    pinned->stack->tt_clocks = {0, 101};
    ok &= !pinned->brd.legal(&move)
        && !pinned->prune_quiet(move, 1, 10, 1, 0, 100, 101, false)
        && pinned->stack->tt_clocks.max == 101;
    auto pawns = std::make_unique<search_t>("6k1/p7/8/8/8/8/P7/6K1 b - - 0 1");
    move.set(&pawns->brd, "a7a6"); pawns->forward(&move, 0);
    move.set(&pawns->brd, "g1f1");
    ok &= !pawns->prune_quiet(move, 1, 10, 1, 0, 100, 101, false);
    return check(ok, "quiet pruning helper, tactical, clock, legality or history policy guards failed");
}

bool test_pvs() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int policy = 0; policy < 4; ++policy) {
            for (int depth : {1, 2, 4, 9}) {
                for (bool pv : {false, true}) {
                    for (int cancel : {0, 1, 2}) {
                        auto s = std::make_unique<probe_t>(black);
                        s->lmp_enabled = policy & 1;
                        s->history_pruning_enabled = policy & 2;
                        for (auto & row : s->quiet_history->main[s->brd.us()]) {
                            std::fill(std::begin(row), std::end(row), -8192);
                        }
                        // Two protected early moves make second-child
                        // cancellation reachable even with seeded bad history.
                        s->stack->killer[0] = s->quiet();
                        s->stack->killer[1].set(&s->brd, black ? "c6b8" : "c3b1");
                        s->child_value = pv ? 199 : 0;
                        s->cancel_call = cancel;
                        const auto fen = s->brd.to_string();
                        const auto key = s->brd.stack->tt_key;
                        auto * frame = s->stack;
                        trans_table::clear();
                        const int result = s->pvs(100, pv ? 200 : 101, depth);
                        const auto n = s->children.size();
                        ok &= result == (cancel ? (pv && cancel > 1 ? 199 : 100) : s->child_value) && n >= 1
                            && s->stop_all == bool(cancel) && s->stack == frame
                            && s->brd.to_string() == fen && s->brd.stack->tt_key == key;
                        if (!cancel && !pv && policy && depth <= 2) {
                            ok &= n < 10 && s->stack->tt_clocks.max == 98;
                        }
                        if (!cancel && (pv || !policy || depth == 9)) {
                            ok &= n >= 10;
                        }
                        int value = 0, hint = 0, flag = 0;
                        const bool hit = trans_table::retrieve(key, s->brd.ply, depth, value, hint, flag,
                            s->tt_context(), s->brd.stack->fifty_count);
                        ok &= hit == !bool(cancel);
                        if (hit) { ok &= flag == (pv ? score::EXACT : score::UPPERBOUND) && value == s->child_value; }
                    }
                }
            }
        }
    }
    return check(ok, "quiet pruning production PVS, bounds, cancellation or restoration failed");
}

class domain_probe_t : public search_t {
public:
    std::vector<std::string> children;
    domain_probe_t(const char * fen, const char * last) : search_t(fen) {
        move_t move{};
        move.set(&brd, last);
        if (!brd.valid(&move) || !brd.legal(&move)) { std::abort(); }
        forward(&move, brd.gives_check(&move));
        null_enabled = beta_pruning = ffp_enabled = lmr_enabled = false;
        iid_enabled = iir_enabled = main_see_enabled = razoring_enabled = false;
        probcut_enabled = singular_extensions_enabled = upcoming_repetition_enabled = false;
        correction_history_enabled = false;
        tablebase_limit = draw_contempt = 0;
    }
    int pvs(int alpha, int beta, int depth, bool cut = false) override {
        if (brd.ply == 1) { return search_t::pvs(alpha, beta, depth, cut); }
        children.push_back((stack - 1)->current_move.to_string());
        stack->pv_count = 0;
        stack->tt_clocks = {0, 101};
        return 0;
    }
};
bool test_tactical_domain() {
    struct fixture_t { const char * fen; const char * last; const char * target; };
    // Legal certificates for late quiet checks, both castles, passed pawns,
    // promotions and check evasion after an actual preceding move.
    const fixture_t fixtures[] = {
        {"6k1/p7/8/8/8/8/4R3/4K3 b - - 0 1", "a7a6", "e2g2"},
        {"4k3/p7/8/8/8/8/P7/R3K2R b KQ - 0 1", "a7a6", "e1g1"},
        {"4k3/p7/8/8/8/8/P7/R3K2R b KQ - 0 1", "a7a6", "e1c1"},
        {"4k3/7p/8/P7/8/8/8/R3K3 b - - 0 1", "h7h6", "a5a6"},
        {"4k3/1P5p/8/8/8/8/8/R3K3 b - - 0 1", "h7h6", "b7b8q"},
        {"r5k1/7p/8/8/8/8/8/R3K3 b - - 0 1", "a8e8", "e1d1"},
    };
    bool ok = true;
    for (const auto & fixture : fixtures) {
        auto s = std::make_unique<domain_probe_t>(fixture.fen, fixture.last);
        evaluate(s.get());
        const bool before_ok = ok;
        move_t target{};
        target.set(&s->brd, fixture.target);
        const bool dangerous = s->stack->in_check || s->brd.gives_check(&target)
            || target.capture || target.promotion || target.castle || s->is_advanced_passed_pawn(&target);
        ok &= s->brd.legal(&target) && dangerous;
        s->quiet_history->main[s->brd.us()][target.ssq][target.tsq] = -16384;
        const auto fen = s->brd.to_string();
        trans_table::clear();
        ok &= s->pvs(100, 101, 1) == 0 && s->brd.to_string() == fen
            && std::find(s->children.begin(), s->children.end(), fixture.target) != s->children.end();
        if (before_ok && !ok) {
            std::cerr << "tactical fixture " << fixture.target << " dangerous " << dangerous
                << " legal " << s->brd.legal(&target) << " children " << s->children.size() << '\n';
        }
    }
    return check(ok, "quiet pruning lost a certified tactical move or evasion");
}

bool test_options() {
    bool ok = true;
    for (const char * name : {"LMP", "HistoryPruning"}) {
        for (int enabled : {0, 1}) {
            options::get_option(name)->value = enabled;
            auto s = std::make_unique<probe_t>();
            options::get_option(name)->value = !enabled;
            ok &= (std::string(name) == "LMP" ? s->lmp_enabled : s->history_pruning_enabled) == bool(enabled);
        }
        options::get_option(name)->value = 1;
    }
    auto losers = std::make_unique<w17_search_t>("6k1/p7/8/8/8/8/P7/6K1 w - - 0 1");
    ok &= !losers->lmp_enabled && !losers->history_pruning_enabled;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int mode : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = mode;
        auto s = std::make_unique<probe_t>();
        ok &= !s->lmp_enabled && !s->history_pruning_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    return check(ok, "quiet pruning worker, variant or audit isolation failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    const bool ok = test_boundaries() & test_guards() & test_pvs() & test_tactical_domain() & test_options();
    return ok ? 0 : 1;
}
