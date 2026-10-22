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
void play(search_t & search, const char * text) {
    move_t move{};
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::abort();
    }
    search.forward(&move, search.brd.gives_check(&move));
}
bool test_feedback() {
    bool ok = true;
    for (bool black : {false, true}) {
        auto search = std::make_unique<search_t>(start);
        if (black) {
            play(*search, "g1f3");
        }
        play(*search, black ? "g8f6" : "g1f3");
        auto & parent = *(search->stack - 1);
        const auto move = parent.current_move;
        auto & entry = search->quiet_history->main[!search->brd.us()][move.ssq][move.tsq];
        const auto fen = search->brd.to_string();
        const auto key = search->brd.stack->tt_key;
        for (int previous : {-10000, -100, 0, 100, 10000}) {
            for (int child : {-10000, -100, -17, -16, -15, 0, 100, 10000}) {
                parent.static_eval = previous;
                entry = 0;
                search->update_eval_history(child);
                const int sum = previous + child;
                // Independent certificates for the sign, neutral point and bound.
                ok &= entry >= -94 && entry <= 94
                    && ((sum < -16 && entry > 0) || (sum == -16 && entry == 0)
                        || (sum > -16 && entry < 0));
                if (sum <= -30) {
                    ok &= entry == 94;
                }
                if (sum >= -2) {
                    ok &= entry == -94;
                }
            }
        }
        parent.static_eval = 0;
        entry = 16000;
        search->update_eval_history(-100);
        ok &= entry > 16000 && entry <= quiet_history_t::limit;
        for (int guard = 0; guard < 16; ++guard) {
            auto frame = *search->stack;
            auto previous = parent;
            const int clock = search->brd.stack->fifty_count;
            search->eval_history_enabled = search->quiet_history_policy.main = true;
            if (guard == 0) { search->eval_history_enabled = false; }
            if (guard == 1) { search->quiet_history_policy.main = false; }
            if (guard == 2) { search->stack->in_check = true; }
            if (guard == 3) { parent.in_check = true; }
            if (guard == 4) { parent.current_move.clear(); }
            if (guard == 5) { parent.current_move.capture = WPAWN; }
            if (guard == 6) { parent.current_move.promotion = WQUEEN; }
            if (guard == 7) { parent.static_eval = score::INVALID; }
            if (guard == 8) { search->stop_all = true; }
            if (guard == 9) { search->iid_active = true; }
            if (guard == 10) { search->verification_mode = verification_mode_t::null_move; }
            if (guard == 11) { search->verification_mode = verification_mode_t::excluded_move; }
            if (guard == 12) { search->stack->repetition_start = 1; }
            if (guard == 13) { search->brd.stack->fifty_count = 90; }
            if (guard == 14) { search->stack->excluded_move = 1; }
            if (guard == 15) { parent.static_eval = score::TABLEBASE; }
            entry = 0;
            search->update_eval_history(-100);
            ok &= entry == 0;
            *search->stack = frame;
            parent = previous;
            search->brd.stack->fifty_count = clock;
            search->stop_all = search->iid_active = false;
            search->verification_mode = verification_mode_t::none;
        }
        search->eval_history_enabled = search->quiet_history_policy.main = true;
        entry = 0;
        search->update_eval_history(score::TABLEBASE);
        search->update_eval_history(-score::TABLEBASE);
        ok &= entry == 0 && search->brd.to_string() == fen && search->brd.stack->tt_key == key;
        // The child owns its fresh snapshot; undo preserves the parent's value.
        search->stack->static_eval = 123;
        play(*search, black ? "b1c3" : "b8c6");
        ok &= search->stack->static_eval == score::INVALID;
        const auto last = (search->stack - 1)->current_move;
        move_t undo = last;
        search->backward(&undo);
        ok &= search->stack->static_eval == 123;
    }
    return check(ok, "evaluation feedback sign, saturation, ownership or helper guards failed");
}
bool test_snapshot_and_activation() {
    bool ok = true;
    options::get_option("EvalHistory")->value = 0;
    auto off = std::make_unique<search_t>(start);
    options::get_option("EvalHistory")->value = 1;
    auto on = std::make_unique<search_t>(start);
    on->context_lmr_enabled = false;
    auto royal = std::make_unique<w17_search_t>(start);
    auto master_off = std::make_unique<search_t>(start);
    master_off->quiet_history_policy.main = false;
    master_off->context_lmr_enabled = false;
    ok &= !off->eval_history_enabled && on->eval_history_enabled && !royal->eval_history_enabled;
    for (auto * search : {off.get(), on.get(), master_off.get()}) {
        trans_table::clear();
        search->draw_contempt = search->tablebase_limit = 0;
        search->game.max_depth = 4;
        search->go();
        ok &= search->completed_depth == 4 && !search->stop_all && search->brd.ply == 0
            && search->brd.legal(&search->stack->best_move);
        if (search == on.get()) {
            ok &= search->stack->static_eval != score::INVALID;
        }
        if (search == master_off.get()) {
            ok &= search->stack->static_eval == score::INVALID;
        }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= (search->nnue_audit.counters.eval_history_updates > 0) == (search->eval_history_enabled && search->quiet_history_policy.main);
#endif
    }
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int mode : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = mode;
        auto search = std::make_unique<search_t>(start);
        ok &= !search->eval_history_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    return check(ok, "evaluation feedback worker, production activation or variant isolation failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(16);
    options::get_option("OwnBook")->value = 0;
    const bool ok = test_feedback() & test_snapshot_and_activation();
    return ok ? 0 : 1;
}
