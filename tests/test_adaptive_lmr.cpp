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
    move_t move;
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::abort();
    }
    search.forward(&move, search.brd.gives_check(&move));
}

bool test_reductions() {
    auto search = std::make_unique<search_t>(start);
    move_t move;
    move.set(&search->brd, "g1f3");
    bool ok = true;
    // Independent properties: monotonic base growth, context directions, history
    // influence, bounded child depth, and exact fixed/disabled policy controls.
    for (bool adaptive : {false, true}) {
        search->adaptive_lmr_enabled = adaptive;
        for (bool enabled : {false, true}) {
            search->lmr_enabled = enabled;
            for (bool pv : {false, true}) {
                for (bool cut : {false, true}) {
                    for (int history : {-16384, 0, 16384}) {
                        search->quiet_history->main[search->brd.us()][move.ssq][move.tsq] = history;
                        int last_depth = 0;
                        for (int depth = 1; depth <= MAX_PLY; ++depth) {
                            int last_move = 0;
                            for (int count = 0; count < move::MAX_MOVES; ++count) {
                                const int r = search->reduction(depth, count, false, move, pv, cut);
                                const bool eligible = enabled && depth >= 3 && count >= 3;
                                ok &= !eligible ? r == 0 : r >= 0 && r <= depth - 2;
                                ok &= adaptive || r == int(eligible);
                                ok &= r >= last_move;
                                last_move = r;
                                ok &= search->reduction(depth, count, true, move, pv, cut) == 0;
                            }
                            ok &= last_move >= last_depth;
                            last_depth = last_move;
                        }
                    }
                }
            }
        }
    }
    search->lmr_enabled = search->adaptive_lmr_enabled = true;
    auto r = [&](bool pv, bool cut, bool improving, int history) {
        search->stack->improving = improving;
        search->quiet_history->main[search->brd.us()][move.ssq][move.tsq] = history;
        return search->reduction(8, 15, false, move, pv, cut);
    };
    const int ordinary = r(false, false, false, 0);
    ok &= r(true, false, false, 0) < ordinary;
    ok &= r(false, true, false, 0) > ordinary;
    ok &= r(false, false, true, 0) < ordinary;
    ok &= r(false, false, false, 8192) < ordinary;
    ok &= r(false, false, false, -8192) > ordinary;
    // Picker offsets must never enter reduction policy, even when extreme.
    move.score = score::INF;
    ok &= r(false, false, false, 0) == ordinary;
    search->quiet_history_policy.main = false;
    ok &= r(false, false, false, 16384) == ordinary;
    return check(ok, "adaptive reduction range/context/history contract failed");
}

class probe_t : public search_t {
public:
    struct call_t { std::string move; int depth, alpha, beta; bool cut; };
    std::vector<call_t> calls;
    std::string target;
    int outcome = 0, cancel_call = 0;
    bool entry_active = false;
    probe_t() : search_t(start) {
        play(*this, "g1f3");
        play(*this, "g8f6");
        play(*this, "b1c3");
        play(*this, "b8c6");
        beta_pruning = null_enabled = ffp_enabled = iid_enabled = iir_enabled = correction_history_enabled = false;
        draw_contempt = tablebase_limit = 0;
    }
    int pvs(int alpha, int beta, int depth, bool cut = false) override {
        if (brd.ply == 4) {
            if (entry_active) {
                return 0;
            }
            entry_active = true;
            const int value = search_t::pvs(alpha, beta, depth, cut);
            entry_active = false;
            return value;
        }
        const auto name = (stack - 1)->current_move.to_string();
        const bool retry = !calls.empty() && calls.back().move == name;
        calls.push_back({name, depth, alpha, beta, cut});
        stack->pv_count = 0;
        // Both passes must contribute to the parent's clock contract.
        stack->tt_clocks = retry ? clock_range_t{2, 80} : clock_range_t{0, 90};
        if (cancel_call == int(calls.size())) {
            stop_all = true;
        }
        return name == target ? -(retry ? outcome : 1) : 0;
    }
};

bool test_search() {
    bool ok = true;
    int cases = 0;
    for (bool adaptive : {false, true}) {
        for (bool feedback : {false, true}) {
            for (bool pv : {false, true}) {
                for (bool cut : {false, true}) {
                    if (pv && cut) {
                        continue;
                    }
                    auto control = std::make_unique<probe_t>();
                    control->adaptive_lmr_enabled = adaptive;
                    control->lmr_history_enabled = feedback;
                    control->pvs(0, pv ? 3 : 1, 8, cut);
                    int index = -1;
                    for (int i = 3; i < int(control->calls.size()); ++i) {
                        move_t candidate;
                        candidate.set(&control->brd, control->calls[i].move.c_str());
                        if (control->calls[i].depth < 7 && candidate.piece != WPAWN) {
                            index = i;
                            break;
                        }
                    }
                    ok &= check(index >= 3, "controlled search never reduced");
                    if (index < 0) {
                        return false;
                    }
                    const auto target = control->calls[index].move;
                    for (int outcome : {0, 1, 4}) {
                        for (int cancel : {0, index + 1, index + 2}) {
                            auto search = std::make_unique<probe_t>();
                            search->adaptive_lmr_enabled = adaptive;
                            search->lmr_history_enabled = feedback;
                            search->target = target;
                            search->outcome = outcome;
                            search->cancel_call = cancel;
                            const auto fen = search->brd.to_string();
                            const auto key = search->brd.stack->tt_key;
                            const auto history = search->stack->tt_history;
                            const auto repetition = search->stack->repetition_start;
                            auto * frame = search->stack;
                            const int result = search->pvs(0, pv ? 3 : 1, 8, cut);
                            ok &= search->calls[index].move == target
                                && search->calls[index].depth == control->calls[index].depth
                                && search->calls[index].cut;
                            if (cancel != index + 1) {
                                const auto & full = search->calls[index + 1];
                                ok &= full.move == target && full.depth == 7
                                    && full.alpha == -(pv ? 3 : 1) && full.beta == 0
                                    && full.cut == (!pv && !cut);
                            }
                            ok &= search->brd.to_string() == fen && search->brd.stack->tt_key == key
                                && search->stack == frame && search->brd.ply == 4
                                && frame->tt_history == history && frame->repetition_start == repetition;
                            ok &= search->stop_all == bool(cancel) && result == (cancel ? 0 : outcome);
                            move_t move;
                            move.set(&search->brd, target.c_str());
                            const auto & previous = (frame - 1)->current_move;
                            const int learned = search->quiet_history->continuation_score(move, previous);
                            if (cancel || outcome == 0 || (pv && outcome == 1)) {
                                const bool penalty = feedback && !cancel && outcome == 0;
                                ok &= penalty ? learned < 0 : learned == 0;
                            } else {
                                ok &= learned > 0; // Ordinary cutoff learning also applies.
                            }
                            if (!cancel) {
                                ok &= frame->tt_clocks.min >= 1 && frame->tt_clocks.max <= 79;
                            }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                            const auto & counts = search->nnue_audit.counters;
                            ok &= counts.lmr_history_penalties == uint64_t(feedback && !cancel && outcome == 0);
                            ok &= counts.lmr_history_rewards == uint64_t(feedback && !cancel && outcome >= (pv ? 3 : 1));
#endif
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    std::cout << cases << " production PVS adaptive/feedback/window/cancellation cases\n";
    return check(ok, "adaptive PVS verification/feedback/restoration failed");
}

bool test_feedback_guards() {
    bool ok = true;
    int cases = 0;
    for (int guard = 0; guard < 12; ++guard) {
        for (bool continuation : {false, true}) {
            for (bool longer : {false, true}) {
                auto search = std::make_unique<probe_t>();
                search->quiet_history_policy.continuation = continuation;
                search->long_continuation_enabled = longer;
                move_t move;
                move.set(&search->brd, "a2a3");
                int value = 0;
                switch (guard) {
                    case 1: search->lmr_enabled = false; break;
                    case 2: search->lmr_history_enabled = false; break;
                    case 3: search->stop_all = true; break;
                    case 4: search->iid_active = true; break;
                    case 5: search->verification_mode = verification_mode_t::null_move; break;
                    case 6: search->stack->excluded_move = move.to_compact(); break;
                    case 7: search->stack->repetition_start = 1; break;
                    case 8: search->stack->in_check = true; break;
                    case 9: move.capture = BPAWN; break;
                    case 10: move.promotion = WQUEEN; break;
                    case 11: value = 1; break;
                }
                auto before = std::make_unique<quiet_history_t>(*search->quiet_history);
                search->update_lmr_history(move, 8, value, 0, 3);
                const bool changed = std::memcmp(before.get(), search->quiet_history.get(), sizeof(*before)) != 0;
                ok &= changed == (guard == 0 && (continuation || longer));
                ok &= std::memcmp(before->main, search->quiet_history->main, sizeof(before->main)) == 0;
                ok &= std::memcmp(before->pawn, search->quiet_history->pawn, sizeof(before->pawn)) == 0;
                ++cases;
            }
        }
    }
    for (int value : {0, 1, 4}) {
        auto search = std::make_unique<probe_t>();
        move_t move;
        move.set(&search->brd, "a2a3");
        search->update_lmr_history(move, 8, value, 0, 3);
        const int learned = search->quiet_history->continuation_score(move, (search->stack - 1)->current_move);
        ok &= value == 0 ? learned < 0 : value == 4 ? learned > 0 : learned == 0;
        // A null pass erases the usable continuation path, even with nonzero tables.
        search->forward();
        auto before = std::make_unique<quiet_history_t>(*search->quiet_history);
        search->update_lmr_history(move, 8, 4, 0, 3);
        ok &= std::memcmp(before.get(), search->quiet_history.get(), sizeof(*before)) == 0;
        search->backward();
    }
    for (const char * name : {"AdaptiveLMR", "LMRHistory"}) {
        for (int enabled : {0, 1}) {
            options::get_option(name)->value = enabled;
            auto search = std::make_unique<search_t>(start);
            options::get_option(name)->value = !enabled;
            ok &= (std::string(name) == "AdaptiveLMR" ? search->adaptive_lmr_enabled : search->lmr_history_enabled) == bool(enabled);
        }
        options::get_option(name)->value = 1;
    }
    auto losers = std::make_unique<search_t>(start, nullptr, nullptr, std::vector<U64>{}, 17);
    ok &= !losers->adaptive_lmr_enabled && !losers->lmr_history_enabled;
    std::cout << cases << " feedback eligibility/policy cases\n";
    return check(ok, "LMR feedback guard/policy contract failed");
}
}
int main() {
    magic::init();
    trans_table::set_size(4);
    trans_table::disable();
    const bool ok = test_reductions() & test_search() & test_feedback_guards();
    trans_table::enable();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
