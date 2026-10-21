#include "engine.h"
#include <algorithm>
#include <vector>

namespace {
constexpr auto white_fen = "rnbqk1nr/pp1pb1pp/2p2p2/4p3/3Q4/8/PPP1PPPP/RNB1KBNR b KQkq - 0 1";
constexpr auto black_fen = "rnb1kbnr/ppp1pppp/8/3q4/4P3/2P2P2/PP1PB1PP/RNBQK1NR w KQkq - 0 1";
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
class probe_t : public search_t {
public:
    std::vector<std::string> children;
    int cancel_call = 0, child_score = 0;
    explicit probe_t(bool black = false) : search_t(black ? black_fen : white_fen) {
        move_t move{};
        move.set(&brd, black ? "a2a3" : "a7a6");
        forward(&move, brd.gives_check(&move));
        beta_pruning = null_enabled = ffp_enabled = lmr_enabled = false;
        // Isolate move-level SEE from same-position qsearch node pruning.
        razoring_enabled = iir_enabled = iid_enabled = correction_history_enabled = false;
        // Keep this mechanism probe independent of quiet-pruning policy.
        lmp_enabled = history_pruning_enabled = false;
        tablebase_limit = draw_contempt = 0;
        evaluate(this);
        stack->killer[0].set(&brd, black ? "g8f6" : "g1f3");
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply == 1) {
            return search_t::pvs(alpha, beta, depth, cut_node);
        }
        children.push_back((stack - 1)->current_move.to_string());
        stack->pv_count = 0;
        stack->tt_clocks = {0, 101};
        stop_all = cancel_call && int(children.size()) == cancel_call;
        return child_score;
    }
};
bool test_thresholds_and_guards() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (bool capture : {false, true}) {
            for (int depth = 0; depth <= MAX_PLY; ++depth) {
                auto s = std::make_unique<probe_t>(black);
                move_t move{};
                move.set(&s->brd, black ? (capture ? "d5e4" : "d5d4")
                    : (capture ? "d4e5" : "d4d5"));
                const auto fen = s->brd.to_string();
                const auto key = s->brd.stack->tt_key;
                const bool want = depth >= 1 && depth <= 6
                    && !s->brd.see(&move, capture ? -100 * depth : -50 * depth * depth);
                const bool before_ok = ok;
                const bool pruned = s->prune_main_see(move, depth, 1, 0, 100, 101, 0);
                ok &= pruned == want && s->brd.to_string() == fen
                    && s->brd.stack->tt_key == key && s->brd.ply == 1
                    && (!pruned || s->stack->tt_clocks.max == 98);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                const auto & c = s->nnue_audit.counters;
                ok &= c.main_see_candidates == uint64_t(depth >= 1 && depth <= 6)
                    && c.main_see_prunes == uint64_t(want)
                    && c.main_see_captures == uint64_t(want && capture)
                    && c.main_see_quiets == uint64_t(want && !capture);
#endif
                if (before_ok && !ok) {
                    std::cerr << "policy " << black << '/' << capture << '/' << depth
                        << " wanted/pruned " << want << '/' << pruned
                        << " clock " << int(s->stack->tt_clocks.max) << '\n';
                }
            }
        }
        for (int guard = 0; guard < 16; ++guard) {
            auto s = std::make_unique<probe_t>(black);
            move_t move{};
            move.set(&s->brd, black ? "d5d4" : "d4d5");
            int searched = 1, best = 0, alpha = 100, beta = 101, gives_check = 0;
            if (guard == 0) {
                s->main_see_enabled = false;
            } else if (guard == 1) {
                searched = 0;
            } else if (guard == 2) {
                beta = 102;
            } else if (guard == 3) {
                s->stack->in_check = true;
            } else if (guard == 4) {
                gives_check = 1;
            } else if (guard == 5) {
                s->iid_active = true;
            } else if (guard == 6) {
                s->verification_mode = verification_mode_t::null_move;
            } else if (guard == 7) {
                s->verification_mode = verification_mode_t::excluded_move;
            } else if (guard == 8) {
                s->stack->excluded_move = 1;
            } else if (guard == 9) {
                s->brd.stack->fifty_count = 99;
            } else if (guard == 10) {
                best = -score::TABLEBASE + MAX_PLY;
            } else if (guard == 11) {
                alpha = score::TABLEBASE - MAX_PLY;
                beta = alpha + 1;
            } else if (guard == 12) {
                beta = -score::TABLEBASE + MAX_PLY;
                alpha = beta - 1;
            } else if (guard == 13) {
                s->stack->metadata.endgame = true;
            } else if (guard == 14) {
                move.promotion = QUEEN[s->brd.us()];
            } else {
                move.castle = 1;
            }
            ok &= !s->prune_main_see(move, 1, searched, best, alpha, beta, gives_check);
        }
    }
    // Independent exchange certificate: pawn100 - queen925 = -825.
    // Equality is inclusive in the threshold SEE used by pruning.
    auto s = std::make_unique<probe_t>();
    move_t capture{};
    capture.set(&s->brd, "d4e5");
    ok &= s->brd.see(&capture) == -825 && s->brd.see(&capture, -825)
        && !s->brd.see(&capture, -824);
    return check(ok, "main SEE threshold, guard or state contract failed");
}
bool test_search() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        for (bool enabled : {false, true}) {
            for (int depth : {1, 2, 4, 6, 7}) {
                for (bool pv : {false, true}) {
                    for (int cancel : {0, 1, 2}) {
                        auto s = std::make_unique<probe_t>(black);
                        s->main_see_enabled = enabled;
                        s->cancel_call = cancel;
                        const auto fen = s->brd.to_string();
                        const auto key = s->brd.stack->tt_key;
                        auto * frame = s->stack;
                        trans_table::clear();
                        const bool before_ok = ok;
                        const int result = s->pvs(100, pv ? 200 : 101, depth);
                        ok &= result == (cancel ? 100 : 0)
                            && s->stop_all == bool(cancel)
                            && s->brd.to_string() == fen && s->brd.stack->tt_key == key
                            && s->stack == frame && s->brd.ply == 1
                            && s->children.front() == (black ? "g8f6" : "g1f3");
                        if (!cancel) {
                            for (const auto & name : {black ? "d5d4" : "d4d5", black ? "d5e4" : "d4e5"}) {
                                move_t move{};
                                move.set(&s->brd, name);
                                const bool pruned = enabled && !pv && depth <= 6
                                    && !s->brd.see(&move, move.capture ? -100 * depth : -50 * depth * depth);
                                ok &= (std::find(s->children.begin(), s->children.end(), name)
                                    == s->children.end()) == pruned;
                            }
                        }
                        int value = 0, hint = 0, flag = 0;
                        const bool hit = trans_table::retrieve(key, 1, depth, value, hint, flag,
                            s->tt_context(), s->brd.stack->fifty_count);
                        ok &= hit == !bool(cancel);
                        if (hit) {
                            ok &= value == 0 && flag == score::UPPERBOUND;
                            move_t best{};
                            best.set_compact(&s->brd, hint);
                            ok &= s->brd.valid(&best) && s->brd.legal(&best);
                            if (enabled && !pv && depth <= 4) {
                                ok &= s->stack->tt_clocks.max <= 98
                                    && !trans_table::retrieve(key, 1, depth, value, hint, flag,
                                        s->tt_context(), 99);
                            }
                        }
                        if (before_ok && !ok) {
                            std::cerr << "search " << black << '/' << enabled << '/' << depth
                                << '/' << pv << '/' << cancel << " result " << result
                                << " kids " << s->children.size() << " first "
                                << (s->children.empty() ? "<none>" : s->children.front())
                                << " clock " << int(s->stack->tt_clocks.max) << " hit " << hit
                                << " value/flag " << value << '/' << flag << '\n';
                        }
                        ++cases;
                    }
                }
            }
        }
    }
    std::cout << cases << " controlled main SEE PVS cases\n";
    return check(ok, "main SEE child set, bounds, cancellation or restoration failed");
}
bool test_legal_and_pawn_guards() {
    bool ok = true;
    struct fixture_t { const char * fen; const char * last; const char * move; int kind; };
    const fixture_t fixtures[] = {
        {"rnbqkbnr/2pppppp/P7/1p6/8/N7/1PPPPPPP/R1BQKBNR b KQkq - 0 1", "h7h6", "a6a7", 0},
        {"4k3/pp6/8/8/8/1r6/P1P5/4K3 b - - 0 1", "e8f8", "a2a3", 1},
        {"4r1k1/8/8/8/8/1p6/4R3/4K3 b - - 0 1", "g8h8", "e2c2", 2},
    };
    for (const auto & fixture : fixtures) {
        auto s = std::make_unique<search_t>(fixture.fen);
        const bool before_ok = ok;
        move_t last{}, move{};
        last.set(&s->brd, fixture.last);
        ok &= s->brd.valid(&last) && s->brd.legal(&last);
        s->forward(&last, s->brd.gives_check(&last));
        evaluate(s.get());
        move.set(&s->brd, fixture.move);
        ok &= s->brd.valid(&move) && !s->brd.see(&move, -50);
        // Isolate pawn-only and self-check guards from endgame protection.
        s->stack->metadata.endgame = false;
        if (fixture.kind == 0) {
            ok &= s->is_advanced_passed_pawn(&move) && s->brd.legal(&move);
        } else if (fixture.kind == 1) {
            ok &= !s->brd.has_pieces(s->brd.us()) && s->brd.legal(&move);
        } else {
            ok &= !s->brd.legal(&move);
        }
        const auto key = s->brd.stack->tt_key;
        const auto fen = s->brd.to_string();
        ok &= !s->prune_main_see(move, 1, 1, 0, 100, 101, 0)
            && s->brd.to_string() == fen && s->brd.stack->tt_key == key;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= s->nnue_audit.counters.main_see_prunes == 0;
#endif
        if (before_ok && !ok) {
            std::cerr << "legal/pawn fixture " << fixture.kind << " SEE " << s->brd.see(&move)
                << " passed " << s->is_advanced_passed_pawn(&move)
                << " legal " << s->brd.legal(&move) << '\n';
        }
    }
    for (bool black : {false, true}) {
        auto s = std::make_unique<probe_t>(black);
        const char * name = black ? "d5d4" : "d4d5";
        s->stack->killer[0].set(&s->brd, name);
        trans_table::clear();
        ok &= s->pvs(100, 101, 1) == 0 && s->children.front() == name;
        // The original node stays PV even after alpha rises to beta-1.
        auto pv = std::make_unique<probe_t>(black);
        pv->child_score = -199;
        trans_table::clear();
        ok &= pv->pvs(100, 200, 1) == 199
            && std::find(pv->children.begin(), pv->children.end(), name) != pv->children.end();
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= pv->nnue_audit.counters.main_see_candidates == 0;
#endif
    }
    return check(ok, "main SEE legality, passed-pawn, pawn-only, first-move or original-PV protection failed");
}
bool test_worker_and_terminals() {
    bool ok = true;
    for (bool enabled : {false, true}) {
        options::get_option("MainSEE")->value = enabled;
        auto s = std::make_unique<probe_t>();
        options::get_option("MainSEE")->value = !enabled;
        ok &= s->main_see_enabled == enabled;
        auto royal = std::make_unique<search_t>(white_fen, nullptr, nullptr, std::vector<U64>{}, 17);
        ok &= !royal->main_see_enabled;
    }
    options::get_option("MainSEE")->value = 1;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto s = std::make_unique<probe_t>();
        ok &= !s->main_see_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    for (const char * text : {"g5g6", "g5g7"}) {
        auto s = std::make_unique<search_t>("7k/8/5K2/6Q1/8/8/8/8 w - - 0 1");
        move_t move{};
        move.set(&s->brd, text);
        s->forward(&move, s->brd.gives_check(&move));
        s->tablebase_limit = s->draw_contempt = 0;
        trans_table::clear();
        const auto fen = s->brd.to_string();
        const int result = s->pvs(100, 101, 2);
        ok &= result == (std::string(text) == "g5g6" ? 0 : -score::MATE + 1)
            && s->brd.to_string() == fen;
    }
    auto root = std::make_unique<search_t>(white_fen);
    move_t move{};
    move.set(&root->brd, "a7a6");
    ok &= !root->prune_main_see(move, 1, 1, 0, 100, 101, 0);
    return check(ok, "main SEE worker, root or terminal contract failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    options::get_option("OwnBook")->value = 0;
    bool ok = test_thresholds_and_guards();
    ok &= test_search();
    ok &= test_legal_and_pawn_guards();
    ok &= test_worker_and_terminals();
    return ok ? 0 : 1;
}
