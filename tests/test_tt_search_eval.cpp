#include "engine.h"
#include "w17/w17_search.h"

namespace {
constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}
bool test_compatible_shallow_bounds() {
    trans_table_t table(0);
    bool ok = true;
    for (auto stored : {tt_mode_t::main_search, tt_mode_t::quiescence_entry,
            tt_mode_t::quiescence_tactical}) {
        for (int bound : {0, int(score::EXACT), int(score::LOWERBOUND), int(score::UPPERBOUND)}) {
            table.clear();
            table.store(123, 0, 2, 2, 77, 42, bound, 456, {7, 80}, stored);
            for (auto mode : {tt_mode_t::main_search, tt_mode_t::quiescence_entry,
                    tt_mode_t::quiescence_tactical}) {
                for (int clock : {0, 7, 80, 81, 100}) {
                    for (int depth : {1, 2, 3, 10}) {
                        for (U64 context : {U64(456), U64(457)}) {
                            int value = 0, move = 0, flags = 0;
                            bool compatible = true;
                            clock_range_t clocks{1, 2};
                            const bool found = table.retrieve(123, 2, depth, value, move, flags,
                                context, clock, &clocks, mode, nullptr, &compatible);
                            const bool domain = mode == stored && context == 456 && clock >= 7 && clock <= 80;
                            ok &= found == (domain && depth <= 2) && compatible == (domain && bound)
                                && move == 42 && value == 77 && flags == bound;
                            if (domain && (depth <= 2 || bound)) {
                                ok &= clocks.min == 7 && clocks.max == 80;
                            } else {
                                ok &= clocks.min == 1 && clocks.max == 2;
                            }
                        }
                    }
                }
            }
        }
    }
    int value = 0, move = 0, flags = 0;
    bool compatible = true;
    ok &= !table.retrieve(999, 0, 1, value, move, flags, 0, 0, nullptr,
        tt_mode_t::main_search, nullptr, &compatible) && !compatible && !move;
    return check(ok, "shallow TT estimate must retain bound, mode, context, depth and clock distinctions");
}
bool test_direction_and_guards() {
    auto search = std::make_unique<search_t>(start);
    bool ok = true;
    for (int eval : {-1000, 0, 1000}) {
        for (int value : {-2000, -1000, 0, 1000, 2000}) {
            for (int bound : {0, int(score::EXACT), int(score::LOWERBOUND), int(score::UPPERBOUND)}) {
                int expected = eval;
                if (bound == score::EXACT) {
                    expected = value;
                } else if (bound == score::LOWERBOUND) {
                    expected = std::max(eval, value);
                } else if (bound == score::UPPERBOUND) {
                    expected = std::min(eval, value);
                }
                ok &= search->tt_pruning_eval(eval, value, bound, true) == expected;
                ok &= search->tt_pruning_eval(eval, value, bound, false) == eval;
            }
        }
    }
    for (int guard = 0; guard < 9; ++guard) {
        const auto frame = *search->stack;
        search->tt_search_eval_enabled = true;
        if (guard == 0) { search->tt_search_eval_enabled = false; }
        if (guard == 1) { search->stack->in_check = true; }
        if (guard == 2) { search->stop_all = true; }
        if (guard == 3) { search->iid_active = true; }
        if (guard == 4) { search->verification_mode = verification_mode_t::null_move; }
        if (guard == 5) { search->verification_mode = verification_mode_t::excluded_move; }
        if (guard == 6) { search->stack->excluded_move = 1; }
        if (guard == 7) { search->stack->repetition_start = 1; }
        ok &= search->tt_pruning_eval(0, guard == 8 ? score::TABLEBASE : 100,
            score::LOWERBOUND, true) == 0;
        *search->stack = frame;
        search->stop_all = search->iid_active = false;
        search->verification_mode = verification_mode_t::none;
    }
    search->tt_search_eval_enabled = true;
    ok &= search->tt_pruning_eval(score::INVALID, 100, score::EXACT, true) == score::INVALID;
    return check(ok, "TT estimate must follow bound direction and protect invalid or helper contexts");
}
class probe_t : public search_t {
public:
    int children = 0, base_eval = 0;
    explicit probe_t(bool enabled) : search_t(start) {
        move_t move{};
        move.set(&brd, "g1f3");
        forward(&move, false);
        null_enabled = lmr_enabled = ffp_enabled = lmp_enabled = history_pruning_enabled = false;
        iid_enabled = iir_enabled = razoring_enabled = main_see_enabled = false;
        probcut_enabled = singular_extensions_enabled = upcoming_repetition_enabled = false;
        correction_history_enabled = eval_history_enabled = false;
        draw_contempt = tablebase_limit = 0;
        tt_search_eval_enabled = enabled;
        base_eval = evaluate(this);
    }
    int pvs(int alpha, int beta, int depth, bool cut = false) override {
        if (brd.ply == 1) {
            return search_t::pvs(alpha, beta, depth, cut);
        }
        ++children;
        stack->pv_count = 0;
        stack->tt_clocks = {0, 101};
        return -base_eval;
    }
};
bool test_real_consumers() {
    bool ok = true;
    for (bool enabled : {false, true}) {
        trans_table::clear();
        auto search = std::make_unique<probe_t>(enabled);
        const auto fen = search->brd.to_string();
        trans_table::store(search->brd.stack->tt_key, 0, search->brd.ply, 1,
            search->base_eval + 1000, 0, score::LOWERBOUND, search->tt_context(), {0, 80});
        const int value = search->pvs(search->base_eval - 1, search->base_eval, 3);
        // The shallow bound cannot resolve a D3 node directly: RFP consumes it
        // with the existing damped return, while the disabled control searches.
        ok &= value == search->base_eval + (enabled ? 333 : 0) && (search->children == 0) == enabled
            && search->stack->tt_clocks.max == (enabled ? 80 : 99)
            && search->stack->static_eval == search->base_eval && search->stack->eval_result == search->base_eval
            && search->brd.to_string() == fen && search->brd.ply == 1;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= search->nnue_audit.counters.tt_search_eval_hits == unsigned(enabled);
#endif
        for (int bound : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
            for (int cached : {-100, 100}) {
                trans_table::clear();
                auto quiet = std::make_unique<search_t>(start);
                quiet->draw_contempt = quiet->tablebase_limit = 0;
                quiet->correction_history_enabled = false;
                quiet->tt_search_eval_enabled = enabled;
                const int base_eval = evaluate(quiet.get());
                trans_table::store(quiet->brd.stack->tt_key, 0, 0, 1, base_eval + cached, 0,
                    bound, quiet->tt_context(), {0, 80}, tt_mode_t::quiescence_entry);
                // A full window avoids direct TT cutoffs. With no tactics at
                // startpos, only eligible stand-pat substitution changes score.
                const bool use = enabled && (bound == score::EXACT
                    || (bound == score::LOWERBOUND && cached > 0)
                    || (bound == score::UPPERBOUND && cached < 0));
                const int qvalue = quiet->qsearch(base_eval - 50, base_eval + 50, 0);
                ok &= qvalue == base_eval + (use ? cached : 0) && quiet->nodes == 1
                    && quiet->stack->eval_result == base_eval && quiet->stack->tt_clocks.max == (use ? 80 : 99)
                    && quiet->brd.to_string() == start;
            }
        }
    }
    return check(ok, "production RFP/standpat estimate must retain static scores and propagate TT clocks");
}
bool test_worker_controls() {
    options::get_option("TTSearchEval")->value = 0;
    auto off = std::make_unique<search_t>(start);
    options::get_option("TTSearchEval")->value = 1;
    auto on = std::make_unique<search_t>(start);
    auto royal = std::make_unique<w17_search_t>(start);
    bool ok = !off->tt_search_eval_enabled && on->tt_search_eval_enabled && !royal->tt_search_eval_enabled;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int mode : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = mode;
        auto search = std::make_unique<search_t>(start);
        ok &= !search->tt_search_eval_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    return check(ok, "TT searched estimate must be worker-owned and isolated from royal/audit policies");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(16);
    options::get_option("OwnBook")->value = 0;
    return test_compatible_shallow_bounds() & test_direction_and_guards()
        & test_real_consumers() & test_worker_controls() ? 0 : 1;
}
