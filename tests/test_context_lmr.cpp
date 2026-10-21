#include "engine.h"
#include "w17/w17_search.h"
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {
constexpr auto start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
struct fixture_t { const char * fen; bool in_check; const char * moves; };
const fixture_t fixtures[] = {
#include "data/search/selectivity.inc"
};
bool check(bool ok, const char * message) {
    if (!ok) {
        std::cerr << message << '\n';
    }
    return ok;
}

bool properties() {
    auto search = std::make_unique<search_t>(start);
    move_t move;
    move.set(&search->brd, "g1f3");
    search->lmr_enabled = search->adaptive_lmr_enabled = search->context_lmr_enabled = true;
    bool ok = true;
    for (bool root : {false, true}) {
        for (int depth = 1; depth <= MAX_PLY; ++depth) {
            int last = 0;
            for (int ordinal = 0; ordinal < move::MAX_MOVES; ++ordinal) {
                const int r = search->reduction(depth, ordinal, true, move, false, true, 0, root);
                ok &= r >= last && r <= std::max(0, depth - 2);
                ok &= (depth >= 3 && ordinal >= (root ? 2 : 1)) || r == 0;
                last = r;
            }
        }
    }
    auto reduced = [&]() { return search->reduction(16, 12, false, move, false, true); };
    const int ordinary = reduced();
    ok &= search->reduction(16, 1, true, move, false, true) > 0;
    ok &= search->reduction(16, 12, true, move, false, true, 1) < ordinary;
    search->stack->in_check = true;
    ok &= reduced() < ordinary;
    search->stack->in_check = false;
    search->stack->tt_pv = true;
    ok &= reduced() < ordinary;
    search->stack->tt_pv = false;
    search->stack->lmr_tt_tactical = true;
    ok &= reduced() > ordinary;
    search->stack->lmr_tt_tactical = false;
    search->stack->lmr_correction = 1;
    ok &= reduced() < ordinary;
    search->stack->lmr_correction = 0;
    search->stack->lmr_alpha_raises = 2;
    ok &= reduced() > ordinary;
    search->stack->lmr_alpha_raises = 0;
    search->stack->killer[0] = move;
    ok &= reduced() < ordinary;
    search->killers_enabled = false;
    ok &= reduced() == ordinary;
    search->stack->killer[0].clear();
    move.score = score::INF;
    ok &= reduced() == ordinary;
    search->quiet_history->main[search->brd.us()][move.ssq][move.tsq] = 8192;
    ok &= reduced() < ordinary;
    search->quiet_history->main[search->brd.us()][move.ssq][move.tsq] = -8192;
    ok &= reduced() > ordinary;
    search->quiet_history->main[search->brd.us()][move.ssq][move.tsq] = 0;
    for (bool context : {false, true}) {
        search->context_lmr_enabled = context;
        search->lmr_enabled = false;
        ok &= reduced() == 0;
        search->lmr_enabled = true;
        search->adaptive_lmr_enabled = false;
        ok &= search->reduction(16, 12, true, move, false, true) == 0;
        ok &= reduced() == 1;
        search->adaptive_lmr_enabled = true;
    }
    for (auto mode : {verification_mode_t::null_move, verification_mode_t::excluded_move}) {
        search->verification_mode = mode;
        ok &= search->reduction(16, 1, false, move, false, true) == 0;
        ok &= search->reduction(16, 12, true, move, false, true) == 0;
    }
    search->verification_mode = verification_mode_t::none;
    search->stack->excluded_move = move.to_compact();
    ok &= reduced() == 0;
    return check(ok, "context LMR eligibility/range/context properties failed");
}

// Production move-loop contracts, independent of neural score quality. Repeat
// for all legal certified move types and both root and interior callers.
class probe_t : public search_t {
public:
    struct call_t { std::string move; int depth, alpha, beta; bool cut; };
    std::vector<call_t> calls;
    std::string target;
    int outcome = 0, cancel_call = 0;
    bool active = false;
    explicit probe_t(const char * fen) : search_t(fen) {
        beta_pruning = null_enabled = ffp_enabled = iid_enabled = iir_enabled = false;
        lmp_enabled = history_pruning_enabled = main_see_enabled = false;
        probcut_enabled = singular_extensions_enabled = razoring_enabled = false;
        correction_history_enabled = false;
        tablebase_limit = draw_contempt = 0;
        brd.stack->fifty_count = 8;
        lmr_enabled = adaptive_lmr_enabled = context_lmr_enabled = true;
    }
    int pvs(int alpha, int beta, int depth, bool cut = false) override {
        if (!brd.ply) {
            if (active) {
                return 0;
            }
            active = true;
            const int value = search_t::pvs(alpha, beta, depth, cut);
            active = false;
            return value;
        }
        const auto name = (stack - 1)->current_move.to_string();
        const bool retry = !calls.empty() && calls.back().move == name;
        calls.push_back({name, depth, alpha, beta, cut});
        stack->pv_count = 0;
        stack->tt_clocks = retry ? clock_range_t{uint8_t(brd.stack->fifty_count >= 2 ? 2 : 0), 80} : clock_range_t{0, 90};
        if (cancel_call == int(calls.size())) {
            stop_all = true;
        }
        return name == target ? -(retry ? outcome : 4) : 0;
    }
    int run(bool root, bool pv, bool cut) {
        if (root) {
            init_root_moves();
            return pvs_root(0, pv ? 3 : 1, 12);
        }
        return pvs(0, pv ? 3 : 1, 12, cut);
    }
};

bool searches() {
    bool ok = true;
    int cases = 0, covered_flags = 0, evasions = 0, noisy_history = 0;
    for (const auto & fixture : fixtures) {
        std::map<std::string, int> certified;
        std::istringstream input(fixture.moves);
        for (std::string token; input >> token;) {
            const auto colon = token.find(':');
            certified.emplace(token.substr(0, colon), std::stoi(token.substr(colon + 1)));
        }
        for (bool root : {false, true}) {
            for (int window = 0; window < 3; ++window) {
                const bool pv = window == 0, cut = window == 2;
                auto baseline = std::make_unique<probe_t>(fixture.fen);
                baseline->run(root, pv, cut);
                std::set<std::string> seen;
                for (const auto & call : baseline->calls) {
                    seen.insert(call.move);
                }
                ok &= check(seen.size() == certified.size(), "new LMR skipped a legal alternative");
                for (size_t i = 0; i < baseline->calls.size(); ++i) {
                    const auto & call = baseline->calls[i];
                    move_t move;
                    move.set(&baseline->brd, call.move.c_str());
                    const int gives_check = baseline->brd.gives_check(&move);
                    const int extend = root ? 0 : baseline->extension(&move, gives_check);
                    const int full_depth = 11 + extend;
                    ok &= call.depth >= 1 && call.depth <= full_depth;
                    if (i < size_t(root ? 2 : 1)) {
                        ok &= call.depth == full_depth;
                    }
                    if (call.depth == full_depth) {
                        continue;
                    }
                    covered_flags |= certified.at(call.move);
                    evasions += fixture.in_check;
                    // Per-type history must affect genuine tactical candidates,
                    // including EP and noncapturing underpromotions.
                    if (move.capture || move.promotion) {
                        auto history = std::make_unique<probe_t>(fixture.fen);
                        evaluate(history.get());
                        history->capture_history->entries[move.piece][move.tsq][capture_history_t::victim(move)] = 8192;
                        const int good = history->reduction(12, 15, true, move, pv, cut, gives_check);
                        history->capture_history_enabled = false;
                        ok &= good < history->reduction(12, 15, true, move, pv, cut, gives_check);
                        ++noisy_history;
                    }
                    for (int outcome : {0, 4}) {
                        for (int cancel : {0, int(i) + 1, int(i) + 2}) {
                            auto search = std::make_unique<probe_t>(fixture.fen);
                            search->target = call.move;
                            search->outcome = outcome;
                            search->cancel_call = cancel;
                            const auto fen = search->brd.to_string();
                            const auto key = search->brd.stack->tt_key;
                            const auto path = search->stack->tt_history;
                            const auto repetition = search->stack->repetition_start;
                            const int result = search->run(root, pv, cut);
                            ok &= search->calls[i].depth == call.depth && search->calls[i].move == call.move;
                            if (cancel != int(i) + 1) {
                                const auto & retry = search->calls[i + 1];
                                ok &= retry.move == call.move && retry.depth == full_depth
                                    && retry.alpha == -(pv ? 3 : 1) && retry.beta == 0
                                    && retry.cut == (!root && !pv && !cut);
                            }
                            ok &= result == (cancel ? 0 : outcome) && search->stop_all == bool(cancel);
                            ok &= search->brd.to_string() == fen && search->brd.stack->tt_key == key
                                && search->brd.ply == 0 && search->stack == search->root_stack
                                && search->stack->tt_history == path
                                && search->stack->repetition_start == repetition;
                            if (!cancel && !move.capture && move.piece != WPAWN && move.piece != BPAWN) {
                                ok &= search->stack->tt_clocks.min >= 1 && search->stack->tt_clocks.max <= 79;
                            }
                            ++cases;
                        }
                    }
                }
            }
        }
    }
    ok &= (covered_flags & 31) == 31 && evasions > 0 && noisy_history > 0;
    std::cout << cases << " root/interior tactical verification/cancellation cases; flags "
        << covered_flags << ", evasions " << evasions << ", noisy history " << noisy_history << '\n';
    return check(ok, "context LMR production verification/restoration failed");
}

bool production_context() {
    bool ok = true;
    trans_table::enable();
    for (bool pv : {false, true}) {
        trans_table::clear();
        auto search = std::make_unique<probe_t>(fixtures[2].fen);
        move_t hint;
        hint.set(&search->brd, "c3d5");
        const auto key = search->brd.stack->tt_key;
        const auto context = search->tt_context();
        trans_table::store(key, 0, 0, 2, -50, hint.to_compact(), score::LOWERBOUND,
            context, {0, 99}, tt_mode_t::main_search, pv);
        search->run(false, false, false);
        ok &= search->stack->tt_pv == pv && search->stack->lmr_tt_tactical;
        int value = 0, move = 0, flag = 0;
        bool cached_pv = false;
        ok &= trans_table::retrieve(key, 0, 12, value, move, flag, context, 8,
            nullptr, tt_mode_t::main_search, &cached_pv) && cached_pv == pv;
    }
    trans_table::clear();
    trans_table::disable();
    for (bool root : {false, true}) {
        auto search = std::make_unique<probe_t>(start);
        search->correction_history_enabled = true;
        constexpr int mask = correction_history_t::size - 1;
        search->correction_history->entries[search->brd.us()][search->brd.stack->pawn_key & mask]
            = 250 * correction_history_t::grain;
        search->run(root, false, false);
        ok &= search->stack->lmr_correction == 2;
        // Reset rebases a played position; its board copy requires a distinct
        // source stack slot. Seed root metadata to detect stale-field leakage.
        search->stack->tt_pv = search->stack->lmr_tt_tactical = true;
        search->stack->lmr_alpha_raises = 3;
        move_t move;
        move.set(&search->brd, "g1f3");
        search->forward(&move, search->brd.gives_check(&move));
        search->reset_stack();
        ok &= !search->stack->tt_pv && !search->stack->lmr_tt_tactical
            && search->stack->lmr_correction == 0 && search->stack->lmr_alpha_raises == 0;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
        ok &= search->nnue_audit.counters.lmr_context == 0;
#endif
    }
    return check(ok, "production TT-PV/tactical hint/correction/frame reset failed");
}

bool metadata() {
    trans_table_t table(1);
    const U64 key = 0x1234567812345678ULL;
    constexpr U64 context = 123;
    constexpr auto main = tt_mode_t::main_search;
    constexpr auto qsearch = tt_mode_t::quiescence_entry;
    int value = 0, move = 0, flag = 0;
    bool pv = true, ok = true;
    table.retrieve(key, 0, 8, value, move, flag, context, 0, nullptr, main, &pv);
    ok &= !pv;
    table.store(key, 0, 0, 6, 20, 42, score::LOWERBOUND, context, {2, 90}, main, true);
    ok &= !table.retrieve(key, 0, 8, value, move, flag, context, 4, nullptr, main, &pv) && pv;
    ok &= table.retrieve(key, 0, 6, value, move, flag, context, 4, nullptr, main, &pv) && pv && value == 20;
    for (int clock : {0, 91}) {
        table.retrieve(key, 0, 1, value, move, flag, context, clock, nullptr, main, &pv);
        ok &= !pv;
    }
    table.retrieve(key, 0, 1, value, move, flag, context + 1, 4, nullptr, main, &pv);
    ok &= !pv;
    table.retrieve(key, 0, 1, value, move, flag, context, 4, nullptr, qsearch, &pv);
    ok &= !pv;
    // A move-only or shallower rejected store cannot rewrite score provenance.
    table.store(key, 0, 0, 1, 0, 43, 0, context, {2, 90});
    table.store(key, 0, 0, 2, 0, 44, score::UPPERBOUND, context, {2, 90});
    ok &= table.retrieve(key, 0, 6, value, move, flag, context, 4, nullptr, main, &pv)
        && pv && value == 20 && move == 44;
    table.store_eval(key, 321, 200, 0);
    ok &= table.retrieve(key, 0, 6, value, move, flag, context, 4, nullptr, main, &pv) && pv;
    table.store(key, 0, 0, 8, 30, 45, score::EXACT, context, {2, 90});
    ok &= table.retrieve(key, 0, 8, value, move, flag, context, 4, nullptr, main, &pv) && !pv;
    table.clear();
    table.store(key, 0, 0, 1, 0, 42, 0, context, {2, 90}, main, true);
    table.retrieve(key, 0, 1, value, move, flag, context, 4, nullptr, main, &pv);
    ok &= !pv;
    table.store(key, 0, 0, 2, 30, 43, score::EXACT, context, {2, 90}, qsearch, true);
    table.retrieve(key, 0, 2, value, move, flag, context, 4, nullptr, qsearch, &pv);
    ok &= !pv;
    return check(ok, "TT-PV metadata domain/replacement contract failed");
}

bool policies() {
    bool ok = true;
    for (int enabled : {0, 1}) {
        options::get_option("ContextLMR")->value = enabled;
        auto search = std::make_unique<search_t>(start);
        options::get_option("ContextLMR")->value = !enabled;
        ok &= search->context_lmr_enabled == bool(enabled);
    }
    options::get_option("ContextLMR")->value = 1;
    auto royal = std::make_unique<w17_search_t>(start);
    ok &= !royal->context_lmr_enabled;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (const char * horizon : {"fixed", "quiet"}) {
        uci::in(std::string("setoption name NNUEAuditHorizon value ") + horizon);
        auto search = std::make_unique<search_t>(start);
        ok &= !search->context_lmr_enabled;
        uci::in("setoption name NNUEAuditHorizon value normal");
    }
#endif
    return check(ok, "context LMR worker/variant/audit policy failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    trans_table::disable();
    const bool ok = properties() & searches() & metadata() & production_context() & policies();
    trans_table::enable();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
