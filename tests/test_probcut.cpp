#include "engine.h"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
bool check(bool ok, const char * text) {
    if (!ok) {
        std::cerr << text << '\n';
    }
    return ok;
}
class probe_t : public search_t {
public:
    struct call_t { int alpha, beta, depth; bool cut; };
    std::vector<call_t> calls;
    std::vector<int> results{250};
    bool cancel = false, throws = false;
    explicit probe_t(bool black = false)
        : search_t(black ? "k2r4/pp6/8/8/3Q4/8/PP6/7K w - - 0 1"
            : "7k/pp6/8/3q4/8/8/PP6/K2R4 b - - 0 1") {
        move_t move{};
        move.set(&brd, black ? "h1g1" : "h8g8");
        forward(&move, false);
        beta_pruning = null_enabled = ffp_enabled = lmr_enabled = false;
        razoring_enabled = main_see_enabled = iir_enabled = iid_enabled = false;
        correction_history_enabled = singular_extensions_enabled = false;
        tablebase_limit = draw_contempt = 0;
        evaluate(this);
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        calls.push_back({alpha, beta, depth, cut_node});
        stack->tt_clocks = {0, 80};
        stack->pv_count = 0;
        if (throws) {
            throw std::runtime_error("child");
        }
        stop_all = cancel;
        return -results[std::min(calls.size() - 1, results.size() - 1)];
    }
    void seed(int qvalue) {
        trans_table::clear();
        move_t move{};
        move.set(&brd, brd.us() ? "d1d5" : "d8d4");
        forward(&move, brd.gives_check(&move));
        trans_table::store(brd.stack->tt_key, 0, brd.ply, 1, -qvalue, 0,
            score::EXACT, tt_context(), {0, 80}, tt_mode_t::quiescence_entry);
        backward(&move);
    }
};
bool test_verification() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (bool improving : {false, true}) {
            const int threshold = improving ? 200 : 250;
            for (int qdelta : {-1, 0, 1}) {
                for (int delta : {-1, 0, 1}) {
                    auto s = std::make_unique<probe_t>(black);
                    s->stack->improving = improving;
                    s->seed(threshold + qdelta);
                    s->results = {threshold + delta};
                    s->stack->move_list.stage = 77;
                    s->stack->tt_move.set(&s->brd, black ? "d8d4" : "d1d5");
                    s->stack->best_move = s->stack->tt_move;
                    s->stack->pv_count = 1;
                    s->stack->pv_moves[0] = s->stack->tt_move;
                    const auto frame = *s->stack;
                    const auto fen = s->brd.to_string();
                    const auto key = s->brd.stack->tt_key;
                    const auto quiet = std::make_unique<quiet_history_t>(*s->quiet_history);
                    const auto capture = std::make_unique<capture_history_t>(*s->capture_history);
                    const auto correction = std::make_unique<correction_history_t>(*s->correction_history);
                    int value = -999;
                    const bool accepted = s->probcut(-1, 0, 5, 0, true, value);
                    const bool wanted = qdelta >= 0 && delta >= 0;
                    auto after = *s->stack;
                    after.tt_clocks = frame.tt_clocks;
                    ok &= accepted == wanted && s->calls.size() == unsigned(qdelta >= 0)
                        && (!wanted || value == delta) && s->brd.to_string() == fen
                        && s->brd.stack->tt_key == key && s->stack == &s->_stack[1]
                        && std::memcmp(&frame, &after, sizeof(frame)) == 0
                        && std::memcmp(quiet.get(), s->quiet_history.get(), sizeof(quiet_history_t)) == 0
                        && std::memcmp(capture.get(), s->capture_history.get(), sizeof(capture_history_t)) == 0
                        && std::memcmp(correction.get(), s->correction_history.get(), sizeof(correction_history_t)) == 0;
                    if (!s->calls.empty()) {
                        const auto & c = s->calls[0];
                        ok &= c.alpha == -threshold && c.beta == 1 - threshold && c.depth == 2 && !c.cut;
                    }
                    int score = 0, hint = 0, bound = 0;
                    clock_range_t clocks;
                    const bool hit = trans_table::retrieve(key, 1, 3, score, hint, bound,
                        s->tt_context(), 0, &clocks);
                    ok &= hit == wanted && (!hit || (score == threshold + delta
                        && bound == score::LOWERBOUND && hint == s->stack->tt_move.to_compact()
                        && clocks.max == 98 && s->stack->tt_clocks.max == 98));
                    ok &= !trans_table::retrieve(key, 1, 5, score, hint, bound, s->tt_context());
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                    const auto & counts = s->nnue_audit.counters;
                    ok &= counts.probcut_attempts == 1 && counts.probcut_candidates == 1
                        && counts.probcut_q_passes == unsigned(qdelta >= 0)
                        && counts.probcut_cutoffs == unsigned(wanted);
#endif
                }
            }
        }
    }
    return check(ok, "ProbCut screen, verification, TT depth/bound or frame/history contract failed");
}
bool test_adaptive() {
    bool ok = true;
    for (bool black : {false, true}) {
        // D8 base5, qscore850 reduces by2 and raises beta by400. A reduced
        // fail-low retries at base depth and the original raised threshold.
        for (bool fallback : {false, true}) {
            auto s = std::make_unique<probe_t>(black);
            s->seed(850);
            s->results = fallback ? std::vector<int>{649, 250} : std::vector<int>{650};
            int value = -999;
            ok &= s->probcut(-1, 0, 8, 0, false, value) && value == 0
                && s->calls.size() == unsigned(fallback ? 2 : 1)
                && s->calls[0].alpha == -650 && s->calls[0].depth == 3 && s->calls[0].cut;
            if (fallback) {
                ok &= s->calls[1].alpha == -250 && s->calls[1].depth == 5;
            }
            int score = 0, hint = 0, bound = 0;
            ok &= trans_table::retrieve(s->brd.stack->tt_key, 1, fallback ? 6 : 4,
                score, hint, bound, s->tt_context()) && score == (fallback ? 250 : 650);
        }
        // A high static score can make qsearch alone sufficient; the stored
        // depth is then one ply, never the requested D8.
        auto s = std::make_unique<probe_t>(black);
        s->seed(250);
        int value = -999;
        ok &= s->probcut(-1, 0, 8, 1500, false, value) && value == 0 && s->calls.empty();
        int score = 0, hint = 0, bound = 0;
        ok &= trans_table::retrieve(s->brd.stack->tt_key, 1, 1,
            score, hint, bound, s->tt_context()) && score == 250;
        ok &= !trans_table::retrieve(s->brd.stack->tt_key, 1, 2,
            score, hint, bound, s->tt_context());
    }
    return check(ok, "ProbCut adaptive depth, raised window, fallback or q-only depth failed");
}
bool test_guards_and_tt() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int guard = 0; guard < 12; ++guard) {
            auto s = std::make_unique<probe_t>(black);
            s->seed(250);
            int alpha = -1, beta = 0, depth = 5, eval = 0, value = -999;
            if (guard == 0) {
                s->probcut_enabled = false;
            } else if (guard == 1) {
                depth = 4;
            } else if (guard == 2) {
                alpha = -2;
            } else if (guard == 3) {
                s->stack->in_check = true;
            } else if (guard == 4) {
                s->iid_active = true;
            } else if (guard == 5) {
                s->verification_mode = verification_mode_t::null_move;
            } else if (guard == 6) {
                s->verification_mode = verification_mode_t::excluded_move;
            } else if (guard == 7) {
                s->stack->excluded_move = 1;
            } else if (guard == 8) {
                s->brd.stack->fifty_count = 99;
            } else if (guard == 9) {
                beta = score::TABLEBASE - MAX_PLY - 249;
                alpha = beta - 1;
            } else if (guard == 10) {
                beta = -score::TABLEBASE + MAX_PLY;
                alpha = beta - 1;
            } else {
                eval = score::TABLEBASE;
            }
            const bool accepted = s->probcut(alpha, beta, depth, eval, false, value);
            const bool good = !accepted && s->nodes == 0 && s->calls.empty() && value == -999;
            if (!good) {
                std::cerr << "guard " << black << '/' << guard << " accepted " << accepted
                    << " nodes " << s->nodes << " value " << value << '\n';
            }
            ok &= good;
        }
        for (int bound : {score::EXACT, score::UPPERBOUND, score::LOWERBOUND}) {
            for (int mismatch = 0; mismatch < 5; ++mismatch) {
                auto s = std::make_unique<probe_t>(black);
                s->seed(250);
                trans_table::store(s->brd.stack->tt_key, 0, 1, mismatch == 1 ? 1 : 2,
                    249, 0, bound, s->tt_context() ^ (mismatch == 2 ? 1 : 0),
                    mismatch == 3 ? clock_range_t{2, 80} : clock_range_t{0, 80},
                    mismatch == 4 ? tt_mode_t::quiescence_entry : tt_mode_t::main_search);
                int value = -999;
                const bool veto = mismatch == 0 && bound != score::LOWERBOUND;
                const bool accepted = s->probcut(-1, 0, 5, 0, false, value);
                const bool good = accepted != veto && s->calls.size() == unsigned(!veto);
                if (!good) {
                    std::cerr << "veto " << black << '/' << bound << '/' << mismatch
                        << " accepted " << accepted << " calls " << s->calls.size() << '\n';
                }
                ok &= good;
            }
        }
    }
    return check(ok, "ProbCut policy guard or compatible directional TT veto failed");
}
bool test_cancellation() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (int kind = 0; kind < 4; ++kind) {
            auto s = std::make_unique<probe_t>(black);
            s->seed(250);
            s->cancel = kind == 1;
            s->throws = kind == 2;
            if (kind == 0) {
                s->game.max_nodes = 1;
            }
            if (kind == 3) {
                s->results = {score::TABLEBASE};
            }
            const auto fen = s->brd.to_string();
            const auto key = s->brd.stack->tt_key;
            const auto frame = *s->stack;
            int value = -999;
            bool caught = false, accepted = false;
            try {
                accepted = s->probcut(-1, 0, 5, 0, false, value);
            } catch (const std::runtime_error &) {
                caught = true;
            }
            auto after = *s->stack;
            after.tt_clocks = frame.tt_clocks;
            ok &= !accepted && caught == (kind == 2) && s->stop_all == (kind < 2)
                && s->brd.to_string() == fen && s->brd.stack->tt_key == key
                && s->stack == &s->_stack[1] && std::memcmp(&frame, &after, sizeof(frame)) == 0;
            int score = 0, hint = 0, bound = 0;
            ok &= !trans_table::retrieve(key, 1, 1, score, hint, bound, s->tt_context());
        }
    }
    return check(ok, "ProbCut cancellation, exception undo or decisive-score rejection failed");
}
bool test_cold_q_contract() {
    bool ok = true;
    for (bool black : {false, true}) {
        auto reference = std::make_unique<probe_t>(black);
        reference->q_tt_enabled = false;
        move_t move{};
        move.set(&reference->brd, black ? "d8d4" : "d1d5");
        reference->forward(&move, reference->brd.gives_check(&move));
        const int eval = evaluate(reference.get());
        const auto fen = reference->brd.to_string();
        for (int depth : {0, -1}) {
            const int soft = reference->qsearch(eval + 400, eval + 401, depth);
            ok &= soft == eval && reference->brd.to_string() == fen;
        }
        const int tactical_score = -reference->qsearch(-score::INF, score::INF, 0);
        auto s = std::make_unique<probe_t>(black);
        s->q_tt_enabled = false;
        trans_table::clear();
        const int beta = tactical_score - 1100;
        s->results = {tactical_score - 250};
        int value = -999;
        // No seeded q-TT scores: a real fail-low must trigger the full adaptive
        // reduction and its raised main-search threshold on a cold search.
        ok &= s->probcut(beta - 1, beta, 8, beta, false, value) && value == beta
            && s->calls.size() == 1 && s->calls[0].depth == 2
            && s->calls[0].alpha == -tactical_score + 250;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= s->nnue_audit.counters.probcut_adaptive == 1;
#endif
    }
    return check(ok, "ProbCut cold fail-soft qscreen or real adaptive activation failed");
}

bool test_tactical_candidates() {
    class tactical_probe_t : public search_t {
    public:
        int children = 0;
        explicit tactical_probe_t(const char * fen) : search_t(fen) {}
        int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
            (void)alpha; (void)beta; (void)depth; (void)cut_node;
            ++children;
            stack->tt_clocks = {0, 101};
            return -250;
        }
    };
    bool ok = true;
    struct fixture_t { const char * fen; const char * last; const char * candidate; };
    for (const auto & f : {
            fixture_t{"7k/8/8/8/3p4/8/4P3/K2R4 w - - 0 1", "e2e4", "d4e3"},
            fixture_t{"7k/Pp6/8/8/8/8/8/K7 b - - 0 1", "h8g8", "a7a8n"},
            fixture_t{"7k/Pp6/8/8/8/8/8/K7 b - - 0 1", "h8g8", "a7a8q"}}) {
        auto s = std::make_unique<tactical_probe_t>(f.fen);
        s->tablebase_limit = s->draw_contempt = 0;
        move_t move{};
        move.set(&s->brd, f.last);
        s->forward(&move, s->brd.gives_check(&move));
        evaluate(s.get());
        move.set(&s->brd, f.candidate);
        ok &= s->brd.valid(&move) && s->brd.legal(&move);
        const auto compact = move.to_compact();
        trans_table::clear();
        trans_table::store(s->brd.stack->tt_key, 0, s->brd.ply, 1, 0, compact,
            score::LOWERBOUND, s->tt_context());
        s->forward(&move, s->brd.gives_check(&move));
        trans_table::store(s->brd.stack->tt_key, 0, s->brd.ply, 1, -250, 0,
            score::EXACT, s->tt_context(), {0, 101}, tt_mode_t::quiescence_entry);
        s->backward(&move);
        const auto fen = s->brd.to_string();
        int value = -999;
        const bool accepted = s->probcut(-1, 0, 8, 1000, true, value);
        const bool good = accepted && value == 0 && s->children == 1 && s->brd.to_string() == fen;
        if (!good) {
            std::cerr << "tactical " << f.candidate << " accepted " << accepted
                << " value " << value << " children " << s->children << '\n';
        }
        ok &= good;
        int score = 0, hint = 0, bound = 0;
        ok &= trans_table::retrieve(s->brd.stack->tt_key, s->brd.ply, 3,
            score, hint, bound, s->tt_context()) && hint == compact;
    }
    // A pinned rook's apparently profitable queen capture is not a candidate.
    auto pinned = std::make_unique<tactical_probe_t>("3r3k/pp1p4/8/8/8/3Rq3/8/3K4 b - - 0 1");
    move_t last{};
    last.set(&pinned->brd, "h8g8");
    pinned->forward(&last, false);
    pinned->tablebase_limit = pinned->draw_contempt = 0;
    evaluate(pinned.get());
    trans_table::clear();
    int value = -999;
    ok &= !pinned->probcut(-1, 0, 5, 0, false, value) && pinned->children == 0;
    return check(ok, "ProbCut en passant, promotion, tactical TT hint or pinned legality failed");
}

bool test_worker_and_pvs() {
    bool ok = options::get_value("ProbCut") == 1
        && std::string(options::get_uci_option("ProbCut")) == "type check default true";
    for (bool enabled : {false, true}) {
        options::get_option("ProbCut")->value = enabled;
        auto s = std::make_unique<probe_t>();
        options::get_option("ProbCut")->value = !enabled;
        ok &= s->probcut_enabled == enabled;
        auto royal = std::make_unique<search_t>("4k3/8/8/8/8/8/8/4K3 w - - 0 1",
            nullptr, nullptr, std::vector<U64>{}, 17);
        ok &= !royal->probcut_enabled;
    }
    options::get_option("ProbCut")->value = 1;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto s = std::make_unique<probe_t>();
        ok &= !s->probcut_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    for (bool black : {false, true}) {
        auto s = std::make_unique<probe_t>(black);
        const int eval = evaluate(s.get());
        const int beta = eval + 1;
        const int threshold = beta + 250;
        s->seed(threshold);
        s->results = {threshold};
        const auto fen = s->brd.to_string();
        const int value = s->search_t::pvs(beta - 1, beta, 5, true);
        ok &= value == beta && s->calls.size() == 1 && s->brd.to_string() == fen;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= s->nnue_audit.counters.probcut_cutoffs == 1;
#endif
    }
    return check(ok, "ProbCut worker snapshot, variant/audit isolation or production PVS activation failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    options::get_option("OwnBook")->value = 0;
    trans_table::set_size(4);
    bool ok = test_verification();
    ok &= test_adaptive();
    ok &= test_guards_and_tt();
    ok &= test_cancellation();
    ok &= test_cold_q_contract();
    ok &= test_tactical_candidates();
    ok &= test_worker_and_pvs();
    return ok ? 0 : 1;
}
