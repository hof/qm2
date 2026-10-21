#include "engine.h"
#include "progress.h"
#include "w17/w17_search.h"

namespace {
constexpr auto position = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool check(bool value, const char * message) {
    if (!value) {
        std::cerr << message << '\n';
    }
    return value;
}

bool table_contract() {
    trans_table_t table(1);
    constexpr U64 key = 123, model = 456, context = 789;
    int value = 321, move = 0, flag = 0;
    bool ok = !table.retrieve_eval(key, model, value) && value == 321;
    for (int raw : {-27999, -1, 0, 1, 27999}) {
        table.clear();
        table.store_eval(key, model, raw, 1);
        ok &= table.retrieve_eval(key, model, value) && value == raw;
        ok &= !table.retrieve_eval(key, model + 1, value);
        ok &= !table.retrieve_eval(key + 1, model, value);
        ok &= table.retrieve(key, 3, 1, value, move, flag) && flag == 0 && move == 0;
        for (auto mode : {tt_mode_t::main_search, tt_mode_t::quiescence_entry, tt_mode_t::quiescence_tactical}) {
            table.clear();
            table.store_eval(key, model, raw, 1);
            table.store(key, 1, 3, 8, score::MATE - 5, 1234, score::EXACT, context, {10, 19}, mode);
            ok &= table.retrieve_eval(key, model, value) && value == raw;
            table.store_eval(key, model, raw, 2);
            clock_range_t clocks{};
            ok &= table.retrieve(key, 5, 8, value, move, flag, context, 15, &clocks, mode)
                && value == score::MATE - 7 && move == 1234 && flag == score::EXACT
                && clocks.min == 10 && clocks.max == 19;
            ok &= !table.retrieve(key, 5, 8, value, move, flag, context + 1, 15, nullptr, mode);
            ok &= !table.retrieve(key, 5, 8, value, move, flag, context, 99, nullptr, mode);
            ok &= table.retrieve_eval(key, model, value) && value == raw;
        }
        table.enabled = false;
        ok &= !table.retrieve_eval(key, model, value);
        table.store_eval(key, model, 27, 3);
        table.enabled = true;
        ok &= table.retrieve_eval(key, model, value) && value == raw;
        table.clear();
        ok &= !table.retrieve_eval(key, model, value);
    }
    // Small keys deliberately share the multiply-high cluster. A bound-only
    // replacement must not inherit another position's evaluation.
    table.clear();
    for (U64 k = 1; k <= 4; ++k) {
        table.store_eval(k, model, int(k), 1);
        table.store(k, 1, 0, 4, 10, 0, score::EXACT);
    }
    table.store_eval(5, model, 55, 1);
    ok &= !table.retrieve_eval(5, model, value);
    for (U64 k = 1; k <= 4; ++k) {
        ok &= table.retrieve_eval(k, model, value) && value == int(k);
    }
    table.store(5, 2, 0, 8, 20, 0, score::EXACT);
    ok &= !table.retrieve_eval(5, model, value);
    table.store_eval(5, model + 1, 55, 2);
    ok &= !table.retrieve_eval(5, model, value)
        && table.retrieve_eval(5, model + 1, value) && value == 55;
    table.set_size(2);
    ok &= !table.retrieve_eval(5, model + 1, value);
    return check(ok, "raw evaluation identity, bounds, replacement, disable, clear or resize");
}

bool evaluation_contract() {
    bool ok = true;
    trans_table::clear();
    auto cold = std::make_unique<search_t>(position);
    cold->tt_eval_enabled = false;
    const int raw = cold->neural_static_score();
    nnue::accumulator_t fresh;
    ok &= raw == nnue::to_centipawns(cold->evaluation_network->evaluate(cold->brd, fresh));
    const U64 key = cold->brd.stack->tt_key, model = cold->evaluation_network->fingerprint();
    for (int clock : {0, 9, 10, 89, 90, 99, 100}) {
        for (bool enabled : {false, true}) {
            trans_table::clear();
            auto search = std::make_unique<search_t>(position);
            search->tt_eval_enabled = enabled;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
            search->nnue_audit.mode = nnue::audit_mode_t::off;
#endif
            search->brd.stack->fifty_count = clock;
            search->correction_history->update(search->brd.us(), search->brd.stack->pawn_key, 8, 1024);
            // A different full model identity must always miss.
            trans_table::store_eval(key, model ^ 1, raw + 100, 0);
            const auto before = search->brd.to_string();
            ok &= evaluate(search.get()) == damp_rule50(raw, clock) + damp_rule50(8, clock);
            ok &= search->brd.to_string() == before;
            int stored;
            ok &= trans_table::retrieve_eval(key, enabled ? model : model ^ 1, stored)
                && stored == (enabled ? raw : raw + 100);
        }
        // Distinct workers and clock/repetition contexts reuse undamped raw
        // scores but apply their own correction; frame metadata remains fresh.
        trans_table::clear();
        trans_table::store_eval(key, model, raw, 0);
        auto warm = std::make_unique<search_t>(position);
        warm->brd.stack->fifty_count = clock;
        warm->correction_history->update(warm->brd.us(), warm->brd.stack->pawn_key, 8, -1024);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        warm->nnue_audit.mode = nnue::audit_mode_t::off;
#endif
        ok &= evaluate(warm.get()) == damp_rule50(raw, clock) + damp_rule50(-8, clock);
        ok &= warm->stack->eval_result == damp_rule50(raw, clock)
            && warm->stack->metadata == search_metadata(warm->brd);
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= warm->nnue_audit.counters.tt_eval_hits == 1
            && warm->nnue_audit.counters.neural_evaluations == 0;
#endif
        warm->correction_history->update(warm->brd.us(), warm->brd.stack->pawn_key, 8, 1024);
        ok &= evaluate(warm.get()) == warm->corrected_static_eval(damp_rule50(raw, clock));
    }
    options::get_option("TTEval")->value = 0;
    auto off = std::make_unique<search_t>(position);
    options::get_option("TTEval")->value = 1;
    auto on = std::make_unique<search_t>(position);
    auto losers = std::make_unique<w17_search_t>(position);
    ok &= !off->tt_eval_enabled && on->tt_eval_enabled && !losers->tt_eval_enabled
        && options::get_option("TTEval")->clears_tt;
    on->stack->in_check = true;
    on->stack->eval_result = score::INVALID;
    ok &= evaluate(on.get()) == score::INVALID;
    return check(ok, "evaluation clock/correction/model, metadata, worker or checked contracts");
}
}

int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(1);
    const bool table = table_contract();
    const bool evaluation = evaluation_contract();
    return table && evaluation ? EXIT_SUCCESS : EXIT_FAILURE;
}
