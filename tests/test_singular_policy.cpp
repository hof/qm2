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
    move_t move{};
    move.set(&search.brd, text);
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        std::abort();
    }
    search.forward(&move, search.brd.gives_check(&move));
}
class probe_t : public search_t {
public:
    int alternative = 0, calls = 0, cancel = 0;
    bool valid = true, raise_exception = false;
    std::vector<int> depths, budgets;
    std::vector<uint16_t> moves;
    explicit probe_t(bool black = true, bool capture = false) : search_t(capture
            ? (black ? "r6k/8/8/8/8/8/P7/K7 w - - 0 1" : "k7/p7/8/8/8/8/8/R6K b - - 0 1")
            : (black ? start : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1")) {
        play(*this, capture ? (black ? "a2a3" : "a7a6") : (black ? "g1f3" : "g8f6"));
        stack->tt_move.set(&brd, capture ? (black ? "a8a3" : "a1a6") : (black ? "g8f6" : "g1f3"));
        beta_pruning = null_enabled = lmr_enabled = ffp_enabled = false;
        correction_history_enabled = upcoming_repetition_enabled = false;
        probcut_enabled = iid_enabled = iir_enabled = main_see_enabled = razoring_enabled = false;
        tablebase_limit = draw_contempt = 0;
        attempted_depth = 8;
    }
    void seed() {
        trans_table::clear();
        trans_table::store(brd.stack->tt_key, 0, brd.ply, 4, 100,
            stack->tt_move.to_compact(), score::LOWERBOUND, tt_context(), {0, 90});
    }
    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        if (brd.ply == 1 && verification_mode == verification_mode_t::excluded_move) {
            ++calls;
            valid &= alpha == 92 && beta == 93 && depth == 3 && !null_enabled
                && stack->excluded_move == stack->tt_move.to_compact();
            valid &= !singular_extension(-1000, 1000, 7, false).move;
            stack->multiple_extensions = 77;
            stack->move_list.stage = 99;
            stack->tt_clocks = {0, 80};
            stack->pv_count = 0;
            if (cancel == 1) {
                stop_all = true;
            }
            if (raise_exception) {
                throw std::runtime_error("singular policy fixture");
            }
            return alternative;
        }
        if (brd.ply > 1) {
            depths.push_back(depth);
            budgets.push_back(stack->multiple_extensions);
            moves.push_back((stack - 1)->current_move.to_compact());
            stack->tt_clocks = {0, 75};
            stack->pv_count = 0;
            if (cancel == 2) {
                stop_all = true;
            }
            return -200;
        }
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};
int expected_extension(int alternative, bool pv, bool cut, bool quiet, int budget, int beta) {
    if (alternative < 93) {
        if (!pv && budget < 12 && alternative < 77) {
            return 2 + int(quiet && alternative < 29);
        }
        return 1;
    }
    if (!pv && alternative >= beta && std::abs(alternative) < score::TABLEBASE - MAX_PLY) {
        return 0;
    }
    if (100 >= beta) {
        return -3 + int(pv);
    }
    return cut ? -2 : 0;
}
bool policy_boundaries() {
    bool ok = true;
    int cases = 0;
    for (bool black : {false, true}) {
        for (bool capture : {false, true}) {
            for (bool pv : {false, true}) {
                for (bool cut : {false, true}) {
                    if (pv && cut) {
                        continue;
                    }
                    for (int budget : {0, 11, 12, 13}) {
                        for (int beta : {90, 93, 100, 200}) {
                            for (int alternative : {-score::TABLEBASE, 28, 29, 76, 77, 92, 93, 99, 100, 200, int(score::TABLEBASE)}) {
                                auto p = std::make_unique<probe_t>(black, capture);
                                p->stack->multiple_extensions = budget;
                                p->alternative = alternative;
                                p->seed();
                                const auto saved = *p->stack;
                                const auto fen = p->brd.to_string();
                                const auto result = p->singular_extension(pv ? -1000 : beta - 1, beta, 7, cut);
                                const bool cutoff = !pv && alternative >= 93 && alternative >= beta
                                    && std::abs(alternative) < score::TABLEBASE - MAX_PLY;
                                const int extension = expected_extension(alternative, pv, cut, !capture, budget, beta);
                                auto expected = saved;
                                expected.tt_clocks.max = 80;
                                ok &= p->valid && p->calls == 1 && result.cutoff == cutoff
                                    && result.extension == extension && result.move == (extension ? saved.tt_move.to_compact() : 0)
                                    && (!cutoff || result.value == alternative)
                                    && std::memcmp(&expected, p->stack, sizeof(expected)) == 0
                                    && p->brd.to_string() == fen && p->verification_mode == verification_mode_t::none;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
                                const auto & c = p->nnue_audit.counters;
                                ok &= c.singular_double == unsigned(extension == 2)
                                    && c.singular_triple == unsigned(extension == 3)
                                    && c.singular_negative == unsigned(extension < 0)
                                    && c.singular_multicuts == unsigned(cutoff);
#endif
                                ++cases;
                            }
                        }
                    }
                }
            }
        }
    }
    std::cout << cases << " singular policy boundary cases\n";
    return check(ok, "multiple/negative/multicut thresholds, line budget, bounds or frame restoration failed");
}
bool child_contracts() {
    bool ok = true;
    for (bool black : {false, true}) {
        for (bool capture : {false, true}) {
            for (bool pv : {false, true}) {
                for (int alternative : {0, 28, 29, 76, 77, 92, 93, 100}) {
                    for (int beta : {100, 1000}) {
                        for (int cancel : {0, 1, 2}) {
                            bool case_ok = true;
                            auto p = std::make_unique<probe_t>(black, capture);
                            p->alternative = alternative;
                            p->cancel = cancel;
                            p->seed();
                            const auto candidate = p->stack->tt_move.to_compact();
                            const auto fen = p->brd.to_string();
                            const auto key = p->brd.stack->tt_key;
                            const int alpha = pv ? -1000 : beta - 1;
                            const int extension = expected_extension(alternative, pv, false, !capture, 0, beta);
                            const bool cutoff = !pv && alternative >= 93 && alternative >= beta;
                            const int result = p->pvs(alpha, beta, 7);
                            const bool stopped = cancel == 1 || (cancel == 2 && !cutoff);
                            case_ok &= p->valid && p->calls == 1 && p->stop_all == stopped
                                && result == (stopped ? alpha : cutoff ? alternative : 200)
                                && p->brd.to_string() == fen && p->brd.stack->tt_key == key
                                && p->stack == p->root_stack + 1 && p->stack->multiple_extensions == 0
                                && p->stack->tt_clocks.max <= 80 && p->verification_mode == verification_mode_t::none;
                            case_ok &= p->depths.empty() == (cancel == 1 || cutoff);
                            for (size_t i = 0; i < p->depths.size(); ++i) {
                                const bool nominated = p->moves[i] == candidate;
                                const int allocation = nominated ? extension : 0;
                                case_ok &= p->depths[i] == 6 + allocation
                                    && p->budgets[i] == int(nominated && extension >= 2);
                            }
                            int value = 0, move = 0, flag = 0;
                            const bool hit = trans_table::retrieve(key, 1, 7, value, move, flag, p->tt_context(), 1);
                            case_ok &= hit == (!stopped && !cutoff);
                            if (hit) {
                                case_ok &= value == 200 && flag == score::flags(200, alpha, beta);
                            }
                            if (!case_ok) {
                                std::cerr << "child case " << black << ' ' << capture << ' ' << pv
                                    << " alternative " << alternative << " beta " << beta << " cancel " << cancel
                                    << " result " << result << " stop " << p->stop_all << " calls " << p->calls
                                    << " depths " << p->depths.size() << " TT " << hit << '/' << value << '/' << flag << '\n';
                            }
                            ok &= case_ok;
                        }
                    }
                }
            }
        }
    }
    return check(ok, "singular allocation child depths, cancellation, TT bounds or line ownership failed");
}
bool only_legal_move() {
    auto p = std::make_unique<search_t>("8/8/8/8/8/1qk4P/8/K7 b - - 0 1");
    play(*p, "c3c2");
    p->stack->tt_move.set(&p->brd, "h3h4");
    p->tablebase_limit = p->draw_contempt = 0;
    trans_table::clear();
    const auto candidate = p->stack->tt_move.to_compact();
    trans_table::store(p->brd.stack->tt_key, 0, 1, 4, 100, candidate,
        score::LOWERBOUND, p->tt_context());
    const auto fen = p->brd.to_string();
    const auto result = p->singular_extension(999, 1000, 7, false);
    return check(result.move == candidate && result.extension == 1 && !result.cutoff
        && p->brd.to_string() == fen && p->stack->multiple_extensions == 0,
        "no legal alternative must allocate exactly one ply without a synthetic terminal score");
}
bool ownership_and_controls() {
    bool ok = true;
    for (bool enabled : {false, true}) {
        options::get_option("SingularPolicy")->value = enabled;
        auto p = std::make_unique<probe_t>();
        options::get_option("SingularPolicy")->value = !enabled;
        ok &= p->singular_policy_enabled == enabled;
        p->alternative = 0;
        p->seed();
        ok &= p->singular_extension(999, 1000, 7, false).extension == (enabled ? 3 : 1);
        p->singular_extensions_enabled = false;
        p->calls = 0;
        ok &= !p->singular_extension(999, 1000, 7, false).move && !p->calls;
    }
    options::get_option("SingularPolicy")->value = 1;
    auto p = std::make_unique<probe_t>();
    p->raise_exception = true;
    p->seed();
    const auto saved = *p->stack;
    bool threw = false;
    try {
        p->singular_extension(999, 1000, 7, false);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    auto expected = saved;
    expected.tt_clocks.max = 80;
    ok &= threw && std::memcmp(&expected, p->stack, sizeof(expected)) == 0
        && p->verification_mode == verification_mode_t::none;
    p->stack->multiple_extensions = 7;
    p->forward();
    ok &= p->stack->multiple_extensions == 7;
    p->backward();
    play(*p, "g8f6");
    ok &= p->stack->multiple_extensions == 7;
    p->reset_stack();
    ok &= p->stack->multiple_extensions == 0;
    auto royal = std::make_unique<search_t>(start, nullptr, nullptr, std::vector<U64>{}, 17);
    ok &= !royal->singular_policy_enabled;
#ifdef MAXIMA_NNUE_SEARCH_AUDIT
    for (int horizon : {1, 2}) {
        options::get_option("NNUEAuditHorizon")->value = horizon;
        auto audit = std::make_unique<search_t>(start);
        ok &= !audit->singular_policy_enabled;
    }
    options::get_option("NNUEAuditHorizon")->value = 0;
#endif
    return check(ok, "singular policy worker snapshots, master/variant/audit control, exception or budget reset failed");
}
}
int main() {
    magic::init();
    uci::silent(true);
    trans_table::set_size(4);
    const bool ok = policy_boundaries() & child_contracts() & only_legal_move() & ownership_and_controls();
    return ok ? 0 : 1;
}
