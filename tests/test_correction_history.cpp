#include "engine.h"
#include "progress.h"
#include "w17/w17_search.h"
#include <cstring>
#include <limits>

namespace {
constexpr auto position = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool check(bool value, const char * message) {
    if (!value) {
        std::cerr << message << '\n';
    }
    return value;
}

bool table_contract() {
    correction_history_t history;
    const U64 key = 123;
    bool ok = history.correction(true, key) == 0;
    history.update(true, key, 8, 1024);
    ok &= history.entries[1][key] == 1024 && history.correction(true, key) == 8;
    history.update(true, key, 8, -512);
    ok &= history.entries[1][key] == 1024 - 512 - 1024 * 512 / 16384;
    ok &= history.correction(false, key) == 0 && history.correction(true, key + 1) == 0;
    ok &= history.correction(true, key + correction_history_t::size) == history.correction(true, key);
    for (int error : {32768, -32768, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()}) {
        for (int i = 0; i < 1000; ++i) {
            history.update(true, key, 1000, error);
            ok &= std::abs(int(history.entries[1][key])) <= correction_history_t::limit;
        }
    }
    return check(ok, "correction table arithmetic, bounds or indexing");
}

bool evaluation_contract() {
    bool ok = true;
    for (int clock : {0, 9, 10, 89, 90, 99, 100}) {
        std::string fen = position;
        fen.replace(fen.size() - 3, 1, std::to_string(clock));
        auto search = std::make_unique<search_t>(fen.c_str());
        search->correction_history_enabled = false;
        const int raw = evaluate(search.get());
        const int neural = search->neural_static_score();
        auto & entry = search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & 16383];
        for (int stored : {-16384, -1024, 1024, 16384}) {
            entry = stored;
            search->correction_history_enabled = true;
            const int corrected = evaluate(search.get());
            ok &= corrected == raw + damp_rule50(stored / 128, clock);
            ok &= search->stack->eval_result == raw && search->neural_static_score() == neural;
            // A cached frame must see subsequent table changes immediately.
            search->correction_history_enabled = false;
            ok &= evaluate(search.get()) == raw;
        }
        search->correction_history_enabled = true;
        constexpr int bound = score::TABLEBASE - MAX_PLY - 1;
        ok &= search->corrected_static_eval(score::MATE) == bound;
        ok &= search->corrected_static_eval(-score::MATE) == -bound;
    }
    auto search = std::make_unique<search_t>("4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
    search->stack->in_check = true;
    ok &= evaluate(search.get()) == score::INVALID;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    search = std::make_unique<search_t>(position);
    search->correction_history->update(search->brd.us(), search->brd.stack->pawn_key, 8, 1024);
    for (bool fixed : {false, true}) {
        search->nnue_audit.fixed_horizon = fixed;
        search->nnue_audit.aligned_quiescence = !fixed;
        ok &= evaluate(search.get()) == search->stack->eval_result;
    }
#endif
    return check(ok, "raw caches, corrected reads, checks, clock damping or clamp");
}

bool update_contract() {
    bool ok = true;
    for (int bound : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
        for (int error : {-1024, 0, 1024}) {
            for (int guard = 0; guard < 16; ++guard) {
                auto search = std::make_unique<search_t>(position);
                search->stack->best_move.set(&search->brd, "g1f3");
                search->stack->eval_result = 0;
                int depth = 8;
                int value = error;
                switch (guard) {
                    case 1: search->correction_history_enabled = false; break;
                    case 2: search->stop_all = true; break;
                    case 3: search->stack->in_check = true; break;
                    case 4: search->iid_active = true; break;
                    case 5: search->verification_mode = verification_mode_t::null_move; break;
                    case 6: search->verification_mode = verification_mode_t::excluded_move; break;
                    case 7: search->stack->excluded_move = 123; break;
                    case 8: search->stack->best_move.capture = BPAWN; break;
                    case 9: search->stack->best_move.promotion = WQUEEN; break;
                    case 10: search->stack->best_move.clear(); break;
                    case 11: search->stack->eval_result = score::INVALID; break;
                    case 12: value = score::TABLEBASE; break;
                    case 13: search->brd.stack->fifty_count = 90; break;
                    case 14: depth = 0; break;
                    case 15: search->stack->repetition_start = 2; break;
                }
                const bool eligible = !guard && error && (bound == score::EXACT
                    || (bound == score::LOWERBOUND && error > 0)
                    || (bound == score::UPPERBOUND && error < 0));
                search->update_correction_history(value, bound, depth);
                const int actual = search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & 16383];
                ok &= actual == (eligible ? error : 0);
            }
        }
    }
    // Children can change the bucket after parent entry: train the fresh error.
    auto search = std::make_unique<search_t>(position);
    search->stack->best_move.set(&search->brd, "g1f3");
    search->stack->eval_result = 0;
    search->correction_history->update(search->brd.us(), search->brd.stack->pawn_key, 8, 1024);
    search->update_correction_history(8, score::EXACT, 8);
    ok &= search->correction_history->correction(search->brd.us(), search->brd.stack->pawn_key) == 8;
    return check(ok, "correction update eligibility, direction or fresh-error contract");
}

// Independent child scores isolate the real parent's bound and update paths.
class probe_t : public search_t {
public:
    int child_value = 0;
    bool cancel = false;
    probe_t() : search_t(position) {
        null_enabled = beta_pruning = ffp_enabled = iid_enabled = lmr_enabled = false;
        upcoming_repetition_enabled = false;
        tablebase_limit = 0;
    }
    int pvs(int, int, int, bool = false) override {
        if (!brd.legal()) {
            std::abort();
        }
        stop_all = cancel;
        return child_value;
    }
};

bool search_contract() {
    bool ok = true;
    for (int mode = 0; mode < 6; ++mode) {
        trans_table::clear();
        auto search = std::make_unique<probe_t>();
        const auto before = search->brd.to_string();
        const U64 key = search->brd.stack->tt_key;
        const int eval = evaluate(search.get());
        const int value = eval + (mode == 1 ? -1024 : 1024);
        search->child_value = -value;
        search->cancel = mode == 3;
        search->iid_active = mode == 4;
        search->verification_mode = mode == 5 ? verification_mode_t::excluded_move : verification_mode_t::none;
        const int alpha = mode == 1 ? eval : mode == 2 ? value - 1 : eval;
        const int beta = mode == 2 ? value + 1 : alpha + 1;
        search->search_t::pvs(alpha, beta, 1);
        const int entry = search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & 16383];
        ok &= mode < 3 ? (mode == 1 ? entry < 0 : entry > 0) : entry == 0;
        ok &= search->brd.to_string() == before && search->brd.stack->tt_key == key
            && search->stack == search->root_stack && search->brd.ply == 0;
    }
    // Cached, legal terminal and qsearch results never learn.
    for (const char * fen : {position, "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"}) {
        for (int depth : {0, 2}) {
            trans_table::clear();
            auto search = std::make_unique<search_t>(fen);
            search->tablebase_limit = 0;
            if (fen == position && depth) {
                trans_table::store(search->brd.stack->tt_key, 0, 0, depth, 123, 0,
                    score::EXACT, search->tt_context());
            }
            search->pvs(-score::INF, score::INF, depth);
            const correction_history_t empty;
            ok &= std::memcmp(&empty, search->correction_history.get(), sizeof(empty)) == 0;
        }
    }
    return check(ok, "real PVS learning/nonlearning paths or restoration");
}

bool worker_contract() {
    bool ok = true;
    for (bool enabled : {false, true}) {
        options::get_option("CorrectionHistory")->value = enabled;
        auto search = std::make_unique<search_t>(position);
        options::get_option("CorrectionHistory")->value = !enabled;
        ok &= search->correction_history_enabled == enabled;
    }
    options::get_option("CorrectionHistory")->value = 1;
    auto previous = std::make_unique<search_t>(position);
    previous->correction_history->update(true, 123, 8, 1024);
    auto * address = previous->correction_history.get();
    auto next = std::make_unique<search_t>(position);
    next->take_history(*previous);
    ok &= !previous->correction_history && next->correction_history.get() == address;
    auto fresh = std::make_unique<search_t>(position);
    ok &= fresh->correction_history->correction(true, 123) == 0;
    // A different model snapshot discards errors, independently of move history.
    next->evaluation_network.reset();
    fresh->take_history(*next);
    ok &= fresh->correction_history->correction(true, 123) == 0 && next->correction_history;
    auto losers = std::make_unique<w17_search_t>(position);
    ok &= !losers->correction_history_enabled && !losers->correction_history;
    return check(ok, "worker option, transfer, model compatibility or royal policy");
}
}

int main() {
    magic::init();
    trans_table::set_size(16);
    return table_contract() && evaluation_contract() && update_contract()
        && search_contract() && worker_contract() ? EXIT_SUCCESS : EXIT_FAILURE;
}
