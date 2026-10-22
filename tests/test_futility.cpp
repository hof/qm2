#include "engine.h"
#include "w17/w17_search.h"
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
    int entry_ply, child_value = 0, cancel_call = 0;
    std::vector<std::string> children;
    explicit probe_t(bool black = false) : search_t(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") {
        for (const char * text : {"g1f3", "g8f6", "b1c3"}) {
            if (!black && brd.ply == 2) {
                break;
            }
            move_t move{};
            move.set(&brd, text);
            forward(&move, brd.gives_check(&move));
        }
        entry_ply = brd.ply;
        null_enabled = lmr_enabled = lmp_enabled = history_pruning_enabled = false;
        iid_enabled = iir_enabled = razoring_enabled = main_see_enabled = false;
        probcut_enabled = singular_extensions_enabled = upcoming_repetition_enabled = false;
        correction_history_enabled = false;
        tablebase_limit = draw_contempt = 0;
        evaluate(this);
    }
    move_t quiet() {
        move_t move{};
        move.set(&brd, brd.us() ? "a2a3" : "a7a6");
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

bool test_reverse() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        auto s = std::make_unique<probe_t>(black);
        const auto fen = s->brd.to_string();
        const auto key = s->brd.stack->tt_key;
        for (bool improving : {false, true}) {
            for (int correction : {-200, 0, 200}) {
                s->stack->improving = improving;
                s->stack->eval_result = 1000 - correction;
                for (int depth = 1; depth <= 10; ++depth) {
                    const int margin = std::max(25, 100 * depth - 100 * int(improving)
                        + std::abs(correction) / 2);
                    for (int offset : {-1, 0, 1}) {
                        const int beta = 1000 - margin + offset;
                        int value = 9999;
                        s->stack->tt_clocks = {0, 101};
                        const bool want = depth < 9 && offset <= 0;
                        const bool got = s->reverse_futility(beta, depth, 1000, 1000, value);
                        ok &= got == want && (!got || value == beta + (1000 - beta) / 3)
                            && s->stack->tt_clocks.max == (want ? 98 : 101);
                        ++cases;
                    }
                }
            }
        }
        s->stack->eval_result = 1000;
        s->stack->improving = false;
        for (int guard = 0; guard < 12; ++guard) {
            const auto frame = *s->stack;
            const auto clock = s->brd.stack->fifty_count;
            s->broader_futility_enabled = s->beta_pruning = true;
            if (guard == 0) { s->broader_futility_enabled = false; }
            if (guard == 1) { s->beta_pruning = false; }
            if (guard == 2) { s->stack->in_check = true; }
            if (guard == 3) { s->stop_all = true; }
            if (guard == 4) { s->iid_active = true; }
            if (guard == 5) { s->verification_mode = verification_mode_t::null_move; }
            if (guard == 6) { s->verification_mode = verification_mode_t::excluded_move; }
            if (guard == 7) { s->stack->excluded_move = 1; }
            if (guard == 8) { s->stack->repetition_start = 1; }
            if (guard == 9) { s->brd.stack->fifty_count = 99; }
            if (guard == 10) { s->stack->tt_move = s->quiet(); }
            int value = 0;
            ok &= !s->reverse_futility(guard == 11 ? -score::TABLEBASE : 0, 4, 1000, 1000, value);
            *s->stack = frame;
            s->brd.stack->fifty_count = clock;
            s->stop_all = s->iid_active = false;
            s->verification_mode = verification_mode_t::none;
        }
        s->broader_futility_enabled = s->beta_pruning = true;
        int value = 0;
        ok &= !s->reverse_futility(0, 4, score::TABLEBASE, score::TABLEBASE, value);
        // A malformed hint has no policy authority and must not reach legality.
        s->stack->tt_move.set(WPAWN, a1, a8);
        ok &= s->reverse_futility(0, 4, 1000, 1000, value);
        ok &= s->brd.to_string() == fen && s->brd.stack->tt_key == key;
    }
    // A pawn attacking our rook suppresses the improving discount. A legal
    // tactical TT hint is allowed; replacing it with a quiet hint vetoes RFP.
    auto threatened = std::make_unique<search_t>("7k/7p/8/3p4/8/2p5/3R4/K7 b - - 0 1");
    move_t waiting{};
    waiting.set(&threatened->brd, "h7h6");
    threatened->forward(&waiting, false);
    threatened->stack->eval_result = 1000;
    threatened->stack->improving = true;
    threatened->stack->tt_move.set(&threatened->brd, "d2d5");
    int threatened_value = 0;
    ok &= threatened->brd.valid(&threatened->stack->tt_move)
        && threatened->brd.legal(&threatened->stack->tt_move)
        && !threatened->reverse_futility(601, 4, 1000, 1000, threatened_value)
        && threatened->reverse_futility(600, 4, 1000, 1000, threatened_value);
    threatened->stack->tt_move.set(&threatened->brd, "d2e2");
    ok &= !threatened->reverse_futility(0, 4, 1000, 1000, threatened_value);
    // No synthetic legal move is required for these terminal certificates.
    for (const char * fen : {"k7/8/1QK5/8/8/8/8/8 b - - 0 1",
                            "k7/1Q6/2K5/8/8/8/8/8 b - - 0 1",
                            "7k/8/8/8/8/8/6P1/6K1 w - - 0 1"}) {
        auto s = std::make_unique<search_t>(fen);
        int value = 0;
        ok &= !s->reverse_futility(0, 4, 1000, 1000, value); // Root is always protected.
    }
    auto terminal = std::make_unique<search_t>("knR5/8/8/1Q1K4/8/8/8/8 w - - 0 1");
    move_t last{};
    last.set(&terminal->brd, "b5b6");
    ok &= terminal->brd.legal(&last);
    terminal->forward(&last, terminal->brd.gives_check(&last));
    int terminal_value = 123;
    terminal->stack->eval_result = 1000;
    ok &= !terminal->stack->in_check && terminal->brd.has_pieces(terminal->brd.us())
        && terminal->reverse_futility(0, 4, 1000, 1000, terminal_value) && terminal_value == terminal->draw_score();
    // Test both king-escape and fresh-generation legal certificates. In the
    // second position only rook moves are available to the side being pruned.
    for (const auto & fixture : std::vector<std::pair<const char *, const char *>>{
            {"6k1/8/8/8/8/8/8/KR6 b - - 0 1", "g8h8"},
            {"8/8/8/8/2k5/p7/P1P5/KB5R b - - 0 1", "c4c3"}}) {
        auto s = std::make_unique<search_t>(fixture.first);
        move_t previous{};
        previous.set(&s->brd, fixture.second);
        ok &= s->brd.legal(&previous);
        s->forward(&previous, s->brd.gives_check(&previous));
        const auto fen = s->brd.to_string();
        const int eval = evaluate(s.get());
        ok &= eval >= 101 && s->pvs(0, 1, 1) == 1 + (eval - 1) / 3
            && s->nodes == 1 && s->brd.to_string() == fen && !s->stop_all;
    }
    std::cout << cases << " reverse-futility threshold cases\n";
    return check(ok, "reverse futility thresholds or guards failed");
}

bool test_forward() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        auto s = std::make_unique<probe_t>(black);
        auto move = s->quiet();
        const auto fen = s->brd.to_string();
        const int eval = evaluate(s.get());
        for (int history : {-4096, 0, 4096}) {
            s->quiet_history->main[s->brd.us()][move.ssq][move.tsq] = history;
            for (int depth : {0, 1, 2, 5, 6, 8, 12}) {
                const int margin = 100 * (1 + depth) + history / 64;
                for (int offset : {-1, 0, 1}) {
                    const int alpha = eval + margin + offset;
                    s->stack->tt_clocks = {0, 101};
                    const bool want = depth < 6 && offset >= 0;
                    ok &= s->prune_futility(move, depth, 1, 0, alpha, alpha + 1, 0, eval) == want
                        && s->stack->tt_clocks.max == (want ? 98 : 101);
                    ++cases;
                }
            }
        }
        const int alpha = eval + 5000;
        for (int guard = 0; guard < 16; ++guard) {
            const auto frame = *s->stack;
            const auto clock = s->brd.stack->fifty_count;
            s->broader_futility_enabled = s->ffp_enabled = true;
            if (guard == 0) { s->broader_futility_enabled = false; }
            if (guard == 1) { s->ffp_enabled = false; }
            if (guard == 2) { s->stack->in_check = true; }
            if (guard == 3) { s->stop_all = true; }
            if (guard == 4) { s->iid_active = true; }
            if (guard == 5) { s->verification_mode = verification_mode_t::null_move; }
            if (guard == 6) { s->verification_mode = verification_mode_t::excluded_move; }
            if (guard == 7) { s->stack->excluded_move = 1; }
            if (guard == 8) { s->stack->repetition_start = 1; }
            if (guard == 9) { s->brd.stack->fifty_count = 99; }
            ok &= !s->prune_futility(move, 1, guard == 10 ? 0 : 1,
                guard == 11 ? -score::TABLEBASE : 0,
                guard == 12 ? score::TABLEBASE : alpha,
                guard == 12 ? score::TABLEBASE + 1 : alpha + (guard == 13 ? 2 : 1),
                guard == 14 ? 1 : guard == 15 ? 2 : 0, eval);
            *s->stack = frame;
            s->brd.stack->fifty_count = clock;
            s->stop_all = s->iid_active = false;
            s->verification_mode = verification_mode_t::none;
        }
        ok &= s->brd.to_string() == fen;
    }
    // Certified capture and EP targets: include material gain and history.
    struct fixture_t { const char * fen; const char * last; const char * target; bool protect; };
    const fixture_t fixtures[] = {
        {"7k/pp6/8/3q4/8/8/PP6/K2R4 b - - 0 1", "h8g8", "d1d5", false},
        {"4k3/3p4/8/4P3/8/8/8/R3K3 b - - 0 1", "d7d5", "e5d6", false},
        {"4k3/p7/8/8/8/8/P7/R3K2R b KQ - 0 1", "a7a6", "e1g1", true},
        {"4k3/1P5p/8/8/8/8/8/R3K3 b - - 0 1", "h7h6", "b7b8q", true},
        {"4k3/7p/8/P7/8/8/8/R3K3 b - - 0 1", "h7h6", "a5a6", true},
        {"6k1/p7/8/8/8/8/4R3/4K3 b - - 0 1", "a7a6", "e2g2", true},
    };
    for (const auto & fixture : fixtures) {
        auto s = std::make_unique<search_t>(fixture.fen);
        move_t last{}, target{};
        last.set(&s->brd, fixture.last);
        ok &= s->brd.valid(&last) && s->brd.legal(&last);
        s->forward(&last, s->brd.gives_check(&last));
        target.set(&s->brd, fixture.target);
        const int eval = evaluate(s.get());
        ok &= s->brd.valid(&target) && s->brd.legal(&target);
        for (int depth : {1, 5, 6, 7, 8}) {
            for (int history : {-4096, 0, 4096}) {
                s->capture_history->entries[target.piece][target.tsq][capture_history_t::victim(target)] = history;
                const int margin = 200 * (1 + depth) + s->brd.max_gain(&target) + history / 64;
                for (int offset : {-1, 0, 1}) {
                    const int alpha = eval + margin + offset;
                    ok &= s->prune_futility(target, depth, 1, 0, alpha, alpha + 1,
                        s->brd.gives_check(&target), eval) == (!fixture.protect && depth < 8 && offset >= 0);
                    ++cases;
                }
            }
        }
    }
    std::cout << cases << " move-futility threshold cases\n";
    return check(ok, "move futility material/history thresholds or guards failed");
}

bool test_production() {
    bool ok = true;
    trans_table::enable();
    for (bool black : {false, true}) {
        for (bool broader : {false, true}) {
            for (bool pv : {false, true}) {
                for (int depth : {1, 4, 5, 6, 9}) {
                    for (int cancel : {0, 1, 2}) {
                        trans_table::clear();
                        auto s = std::make_unique<probe_t>(black);
                        s->beta_pruning = false;
                        s->broader_futility_enabled = broader;
                        s->child_value = 0;
                        s->cancel_call = cancel;
                        const int alpha = evaluate(s.get()) + 2000;
                        const auto fen = s->brd.to_string();
                        const auto key = s->brd.stack->tt_key;
                        const auto * frame = s->stack;
                        const int result = s->pvs(alpha, alpha + (pv ? 2 : 1), depth);
                        const bool prune = !pv && (broader ? depth < 6 : depth == 1);
                        const bool cancelled = cancel && (!prune || cancel == 1);
                        const bool before_ok = ok;
                        ok &= result == (cancelled ? alpha : 0) && s->stop_all == cancelled
                            && s->brd.to_string() == fen && s->brd.stack->tt_key == key
                            && s->stack == frame && s->children.size() >= 1;
                        if (!cancelled) {
                            ok &= prune ? s->children.size() < 10 : s->children.size() >= 10;
                            ok &= !broader || !prune || s->stack->tt_clocks.max == 98;
                            int value = 1, hint = 0, flag = 0;
                            ok &= trans_table::retrieve(key, s->brd.ply, depth, value, hint, flag,
                                s->tt_context(), s->brd.stack->fifty_count)
                                && flag == score::UPPERBOUND && value == 0;
                            if (broader && prune) {
                                ok &= !trans_table::retrieve(key, s->brd.ply, depth, value, hint, flag,
                                    s->tt_context(), 99);
                            }
                        }
                        if (before_ok && !ok) {
                            std::cerr << "production black=" << black << " broader=" << broader
                                << " pv=" << pv << " depth=" << depth << " cancel=" << cancel
                                << " result=" << result << " alpha=" << alpha
                                << " children=" << s->children.size() << " stop=" << s->stop_all << '\n';
                        }
                    }
                }
            }
        }
        // A real positive-depth node can now cut at depths four through eight.
        trans_table::clear();
        auto s = std::make_unique<probe_t>(black);
        s->ffp_enabled = false;
        const int eval = evaluate(s.get());
        ok &= s->pvs(eval - 1001, eval - 1000, 8) == eval - 1000 + 1000 / 3
            && s->children.empty() && s->stack->tt_clocks.max == 98;
    }
    for (bool enabled : {false, true}) {
        options::get_option("BroaderFutility")->value = enabled;
        auto s = std::make_unique<probe_t>();
        options::get_option("BroaderFutility")->value = !enabled;
        ok &= s->broader_futility_enabled == enabled;
    }
    options::get_option("BroaderFutility")->value = 1;
    auto losers = std::make_unique<w17_search_t>("7k/7p/8/8/8/8/P7/K7 w - - 0 1");
    ok &= !losers->broader_futility_enabled && options::get_option("BroaderFutility")->clears_tt;
    return check(ok, "production futility/PV/cancellation/restoration/worker policy failed");
}
}
int main() {
    magic::init();
    trans_table::set_size(4);
    trans_table::disable();
    const bool ok = test_reverse() & test_forward() & test_production();
    trans_table::enable();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
