#include "engine.h"
#include "eval_material.h"
#include <map>
#include <sstream>
#include <vector>

namespace {
struct fixture_t { const char * fen; bool in_check; const char * moves; };
const fixture_t fixtures[] = {
#include "data/search/selectivity.inc"
};
struct move_info_t { int flags, gain, extend; };
struct call_t { std::string move; int depth, alpha, beta; };

bool check(bool ok, const char * message, const char * fen) {
    if (!ok) std::cerr << message << ": " << fen << '\n';
    return ok;
}

// Only the entry node runs production PVS. Child contracts expose move guards
// and depth/window transitions without evaluator or qsearch accuracy assumptions.
class selective_search_t : public search_t {
public:
    std::vector<call_t> calls;
    int parent_value = 0;
    std::string target;
    bool improve = false;
    int cancel_call = 0;
    bool entry_active = false;

    explicit selective_search_t(const char * fen) : search_t(fen) {
        beta_pruning = null_enabled = false;
        tablebase_limit = draw_contempt = 0;
        stack->in_check = brd.in_check();
    }

    int pvs(int alpha, int beta, int depth) override {
        if (brd.ply == 0) {
            // Isolate LMR from the parent's PV internal iterative deepening.
            if (entry_active) return parent_value;
            entry_active = true;
            const int result = search_t::pvs(alpha, beta, depth);
            entry_active = false;
            return result;
        }
        auto name = (stack - 1)->current_move.to_string();
        calls.push_back({name, depth, alpha, beta});
        ++nodes;
        stack->pv_count = 0;
        stack->tt_clocks = {0, 101};
        if (cancel_call == static_cast<int>(calls.size())) stop_all = true;
        // A reduced improvement is deliberately refuted by the full-depth pass.
        const bool first_target = calls.size() < 2 || calls[calls.size() - 2].move != name;
        return -(parent_value + (improve && name == target && first_target ? 1 : 0));
    }
};

bool restored(search_t & search, search_t & original, const char * fen) {
    return check(search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.brd.stack->pawn_hash == original.brd.stack->pawn_hash
        && search.brd.stack->material_hash == original.brd.stack->material_hash
        && search.stack->tt_history == original.stack->tt_history
        && search.stack->repetition_start == original.stack->repetition_start
        && search.brd.ply == 0 && search.stack == search.root_stack,
        "selective search restoration failed", fen);
}

bool certify(selective_search_t & search, const fixture_t & fixture,
             std::map<std::string, move_info_t> & moves) {
    std::map<std::string, int> certified;
    std::istringstream input(fixture.moves);
    for (std::string token; input >> token;) {
        auto colon = token.find(':');
        certified.emplace(token.substr(0, colon), std::stoi(token.substr(colon + 1)));
    }
    bool ok = check(search.brd.in_check() == fixture.in_check, "check certificate differs", fixture.fen);
    evaluate(&search);
    search.init_root_moves();
    for (int i = 0; i < search.root.move_count; ++i) {
        auto & move = search.root.moves[i].move;
        const int gives_check = search.brd.gives_check(&move);
        const int flags = (move.capture ? 1 : 0) | (move.promotion ? 2 : 0)
            | (move.castle ? 4 : 0) | (gives_check ? 8 : 0)
            | (search.is_advanced_passed_pawn(&move) ? 16 : 0);
        moves.emplace(move.to_string(), move_info_t{flags, search.brd.max_gain(&move),
            search.extension(&move, gives_check)});
        auto found = certified.find(move.to_string());
        ok &= check(found != certified.end() && found->second == flags,
            "legal/tactical certificate differs", fixture.fen);
    }
    return check(moves.size() == certified.size(), "legal count differs", fixture.fen) && ok;
}

bool test_futility(const fixture_t & fixture, int & probes, int & skipped, int & protected_flags,
                   int & tactical_wins) {
    bool ok = true;
    auto original = std::make_unique<search_t>(fixture.fen);
    for (int depth : {1, 8, 9}) {
        for (int margin : {-1, 0, 50, 1000}) {
            for (bool enabled : {false, true}) {
                auto search = std::make_unique<selective_search_t>(fixture.fen);
                std::map<std::string, move_info_t> moves;
                ok &= certify(*search, fixture, moves);
                search->ffp_enabled = enabled;
                search->lmr_enabled = false;
                const int eval = fixture.in_check ? 0 : evaluate(search.get());
                const int alpha = eval + 40 * depth + margin;
                search->parent_value = alpha;
                search->pvs(alpha, alpha + 1, depth);
                std::map<std::string, int> observed;
                for (auto & call : search->calls) ++observed[call.move];
                const bool active = enabled && depth <= 8 && !fixture.in_check
                    && !material::is_eg(search.get()) && margin >= 0;
                ok &= check(!search->calls.empty(), "first legal move pruned", fixture.fen);
                const std::string first = search->calls.empty() ? "" : search->calls.front().move;
                int expected_skips = 0;
                for (auto & [name, info] : moves) {
                    const bool dangerous = fixture.in_check || info.flags;
                    const bool prune = active && name != first && (!dangerous
                        || (!(info.flags & 8) && (info.flags & 3)
                            && info.gain + eval + 40 * depth + 50 <= alpha));
                    expected_skips += prune;
                    ok &= check(observed[name] == (prune ? 0 : 1), "futility guard differs", fixture.fen);
                    if (active && name != first && !prune) protected_flags |= info.flags;
                    if (active && margin == 0 && name != first && !prune && info.flags) {
                        auto tactic = std::make_unique<selective_search_t>(fixture.fen);
                        tactic->ffp_enabled = true;
                        tactic->lmr_enabled = false;
                        tactic->parent_value = alpha;
                        tactic->target = name;
                        tactic->improve = true;
                        ok &= check(tactic->pvs(alpha, alpha + 1, depth) == alpha + 1,
                            "protected tactical improvement lost", fixture.fen);
                        ok &= restored(*tactic, *original, fixture.fen);
                        ++tactical_wins;
                    }
                }
                ok &= check(search->pruned_nodes == static_cast<U64>(expected_skips),
                    "pruned work differs", fixture.fen);
                skipped += expected_skips;
                ++probes;
                ok &= restored(*search, *original, fixture.fen);
            }
        }
    }
    // Mate windows must retain every legal move even with a hopeless static score.
    auto search = std::make_unique<selective_search_t>(fixture.fen);
    search->ffp_enabled = true;
    search->lmr_enabled = false;
    search->parent_value = score::MATE - 10;
    search->pvs(search->parent_value, search->parent_value + 1, 1);
    ok &= check(static_cast<int>(search->calls.size()) == original->init_root_moves(), "mate futility guard differs", fixture.fen);
    ok &= restored(*search, *original, fixture.fen);
    return ok;
}

bool test_lmr(const fixture_t & fixture, int & probes, int & reductions, int & restarts) {
    bool ok = true;
    auto original = std::make_unique<search_t>(fixture.fen);
    for (int depth : {1, 2, 3, 6, 9, 15}) {
        for (bool enabled : {false, true}) {
            for (bool pv : {false, true}) {
                auto search = std::make_unique<selective_search_t>(fixture.fen);
                std::map<std::string, move_info_t> moves;
                ok &= certify(*search, fixture, moves);
                search->ffp_enabled = false;
                search->lmr_enabled = enabled;
                const int beta = pv ? 2 : 1;
                search->pvs(0, beta, depth);
                ok &= check(search->calls.size() == moves.size(), "LMR skipped a move", fixture.fen);
                std::string target;
                int target_call = 0, reduced_depth = 0, full_depth = 0;
                for (size_t i = 0; i < search->calls.size(); ++i) {
                    auto & call = search->calls[i];
                    const auto info = moves.at(call.move);
                    const int reduce = !enabled || fixture.in_check || info.flags || i < 3
                        ? 0 : i < 6 ? 1 : depth / 3;
                    ok &= check(call.depth == depth - 1 + info.extend - reduce,
                        "LMR child depth differs", fixture.fen);
                    if (reduce) {
                        ++reductions;
                        target = call.move;
                        target_call = static_cast<int>(i) + 1;
                        reduced_depth = call.depth;
                        full_depth = depth - 1 + info.extend;
                    }
                }
                ++probes;
                ok &= restored(*search, *original, fixture.fen);
                if (target.empty()) continue;
                for (int cancel : {0, target_call, target_call + 1}) {
                    auto retry = std::make_unique<selective_search_t>(fixture.fen);
                    retry->ffp_enabled = false;
                    retry->lmr_enabled = enabled;
                    retry->target = target;
                    retry->improve = true;
                    retry->cancel_call = cancel;
                    const int result = retry->pvs(0, beta, depth);
                    const int expected_calls = cancel ? cancel : static_cast<int>(moves.size()) + 1;
                    ok &= check(static_cast<int>(retry->calls.size()) == expected_calls,
                        "reduced re-search call count differs", fixture.fen);
                    ok &= check(retry->calls[target_call - 1].depth == reduced_depth,
                        "improving reduced depth differs", fixture.fen);
                    if (cancel != target_call && retry->calls.size() > static_cast<size_t>(target_call)) {
                        auto & full = retry->calls[target_call];
                        ok &= check(full.move == target && full.depth == full_depth
                            && full.alpha == -beta && full.beta == 0,
                            "improvement was not verified at full depth", fixture.fen);
                    }
                    ok &= check(result == 0 && retry->stop_all == (cancel != 0),
                        "refuted/cancelled result differs", fixture.fen);
                    ok &= restored(*retry, *original, fixture.fen);
                    if (cancel) {
                        retry->stop_all = false;
                        retry->cancel_call = 0;
                        retry->calls.clear();
                        ok &= check(retry->pvs(0, beta, depth) == 0 && !retry->stop_all,
                            "cancelled re-search restart differs", fixture.fen);
                        ok &= restored(*retry, *original, fixture.fen);
                        ++restarts;
                    }
                }
            }
        }
    }
    return ok;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    trans_table::disable();
    bool ok = true;
    int futility = 0, skipped = 0, protected_flags = 0, tactical_wins = 0;
    int lmr = 0, reductions = 0, restarts = 0;
    for (auto & fixture : fixtures) {
        ok &= test_futility(fixture, futility, skipped, protected_flags, tactical_wins);
        ok &= test_lmr(fixture, lmr, reductions, restarts);
    }
    ok &= check(skipped > 0 && protected_flags == 31 && tactical_wins > 0
        && reductions > 0 && restarts > 0,
        "missing selective guard coverage", "all fixtures");
    std::cout << futility << " futility probes, " << skipped << " skipped moves, "
        << lmr << " LMR probes, " << reductions << " reduced calls, " << restarts
        << " cancelled restarts, " << tactical_wins << " protected tactical improvements, flags "
        << protected_flags << '\n';
    trans_table::enable();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
