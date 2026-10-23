#include "engine.h"
#include <map>
#include <set>
#include <sstream>

namespace {
struct node_t {
    const char * fen;
    bool in_check;
    int terminal;
    const char * legal;
    const char * entry;
    const char * tactical;
};
const node_t certificates[] = {
#include "data/search/horizon.inc"
};
const char * roots[] = {
#include "data/search/horizon_roots.inc"
};
struct history_t { const char * initial; const char * fen; const char * moves; bool repeated; };
const history_t histories[] = {
#include "data/search/horizon_history.inc"
};
std::map<std::string, const node_t *> oracle;
bool valid = true;
int checked_nodes = 0, quiescent_nodes = 0;

bool check(bool ok, const char * message, const char * fen) {
    if (!ok) {
        std::cerr << message << ": " << fen << '\n';
    }
    return ok;
}

std::set<std::string> words(const char * text) {
    std::istringstream input(text);
    std::set<std::string> result;
    for (std::string word; input >> word;) result.insert(word);
    return result;
}

void prepare(search_t & search, bool selective) {
    search.stack->in_check = search.brd.in_check();
    search.draw_contempt = search.tablebase_limit = 0;
    search.null_enabled = search.beta_pruning = search.ffp_enabled = search.lmr_enabled = selective;
}

// Exhaustive certified topology, without alpha-beta, TT, move ordering or
// pruning. Static labels and the quiescence SEE bound are explicit engine
// dependencies; this validates search integration, not those functions.
int reference(search_t & search, int mode) {
    const auto fen = search.brd.to_string();
    auto found = oracle.find(fen);
    valid &= check(found != oracle.end(), "uncertified reference node", fen.c_str());
    if (found == oracle.end()) {
        return 0;
    }
    const auto & node = *found->second;
    if (node.terminal != score::INVALID) {
        return node.terminal == -score::MATE ? -score::MATE + search.brd.ply : node.terminal;
    }
    valid &= check(search.stack->in_check == node.in_check, "reference check differs", node.fen);
    const int eval = node.in_check ? -score::INF : evaluate(&search);
    quiescent_nodes += mode <= 0 && !node.in_check;
    checked_nodes += node.in_check;
    int best = mode == 1 || node.in_check ? -score::INF : eval;
    const char * moves = mode == 1 ? node.legal : mode == 0 ? node.entry : node.tactical;
    for (const auto & name : words(moves)) {
        move_t move;
        move.set(&search.brd, name.c_str());
        valid &= check(search.brd.valid(&move) && search.brd.legal(&move), "reference move invalid", node.fen);
        if (mode <= 0 && !node.in_check && !search.brd.see(&move, quiescence_see_threshold)) {
            continue;
        }
        const int gives_check = search.brd.gives_check(&move);
        const int child_mode = mode == 1 ? 0 : -1;
        search.forward(&move, gives_check);
        best = std::max(best, -reference(search, child_mode));
        search.backward(&move);
    }
    return best;
}

bool restored(search_t & search, search_t & original, const char * fen) {
    return check(search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.stack->tt_history == original.stack->tt_history
        && search.stack->repetition_start == original.stack->repetition_start
        && search.brd.ply == 0 && search.stack == search.root_stack,
        "horizon state restoration failed", fen);
}

bool valid_bound(int result, int value, int alpha, int beta) {
    if (result <= alpha) {
        return value <= result;
    }
    if (result >= beta) {
        return value >= result;
    }
    return result == value;
}

int run(search_t & search, int alpha, int beta, int mode, bool direct) {
    return direct ? search.qsearch(alpha, beta, mode) : search.pvs(alpha, beta, mode);
}

bool validate_nodes() {
    bool ok = true;
    auto search = std::make_unique<search_t>(roots[0]);
    for (auto & node : certificates) {
        search->init(node.fen, nullptr);
        prepare(*search, false);
        search->init_root_moves();
        std::set<std::string> legal;
        for (int i = 0; i < search->root.move_count; ++i)
            legal.insert(search->root.moves[i].move.to_string());
        ok &= check(legal == words(node.legal) && search->brd.in_check() == node.in_check,
            "independent legal/check certificate differs", node.fen);
    }
    return ok;
}

bool test_root(const char * fen, int & probes, int & interrupted, int & exact_reuses,
               int & horizon_differences, int & seeded_probes) {
    bool ok = true;
    auto original = std::make_unique<search_t>(fen);
    for (int mode : {-2, -1, 0, 1}) {
        auto model = std::make_unique<search_t>(fen);
        prepare(*model, false);
        const int value = reference(*model, mode);
        ok &= restored(*model, *original, fen);
        if (mode == 0) {
            auto parent = std::make_unique<search_t>(fen);
            prepare(*parent, false);
            horizon_differences += reference(*parent, 1) != value;
        }
        const std::pair<int, int> windows[] = {{-score::INF, score::INF},
            {value - 1, value}, {value, value + 1}, {value - 17, value + 17}};
        for (bool selective : {false, true}) {
            for (bool direct : {false, true}) {
                if (direct && mode > 0) {
                    continue;
                }
                if (!direct && mode < 0) {
                    continue; // PVS always enters qsearch at depth zero.
                }
                for (int cache = 0; cache < 3; ++cache) {
                    for (auto warm : windows) {
                        for (auto window : windows) {
                            trans_table::clear();
                            if (cache == 2) {
                                trans_table::disable();
                            } else {
                                trans_table::enable();
                            }
                            if (cache == 1) {
                                auto writer = std::make_unique<search_t>(fen);
                                prepare(*writer, selective);
                                // A genuine one-ply entry must never become a qsearch score.
                                writer->pvs(warm.first, warm.second, 1);
                            }
                            auto search = std::make_unique<search_t>(fen);
                            prepare(*search, selective);
                            const int result = run(*search, window.first, window.second, mode, direct);
                            ++probes;
                            if (cache == 1 && mode == 1 && !direct
                                    && warm.first == -score::INF && warm.second == score::INF
                                    && window.first == value - 1 && window.second == value
                                    && oracle.at(fen)->terminal == score::INVALID) {
                                // Only non-PV windows take cached score cutoffs.
                                ++exact_reuses;
                                ok &= check(search->nodes == 0, "nonterminal exact TT was not reused", fen);
                            }
                            // Selective pruning gives approximate bounds, and a warm
                            // TT hint may add a move to quiescence; only the unpruned
                            // search without hints must reproduce the exhaustive model.
                            if (!selective && cache != 1) {
                                ok &= check(valid_bound(result, value, window.first, window.second),
                                    "real horizon bound differs", fen);
                            }
                            ok &= check(!search->stop_all, "unexpected horizon cancellation", fen);
                            ok &= restored(*search, *original, fen);
                        }
                    }
                }
            }
        }
        trans_table::enable();
        if (mode <= 0) {
            for (int flag : {score::EXACT, score::LOWERBOUND, score::UPPERBOUND}) {
                trans_table::clear();
                auto search = std::make_unique<search_t>(fen);
                prepare(*search, false);
                trans_table::store(search->brd.stack->tt_key, 0, 0, 8, value + 500, 0,
                    flag, search->tt_context());
                ok &= check(run(*search, -score::INF, score::INF, mode, mode < 0) == value,
                    "deep TT entry contaminated quiescence", fen);
                ok &= restored(*search, *original, fen);
                ++seeded_probes;
            }
        }
        for (U64 budget : {1, 2, 5}) {
            trans_table::clear();
            auto search = std::make_unique<search_t>(fen);
            prepare(*search, false);
            search->game.max_nodes = budget;
            run(*search, -score::INF, score::INF, mode, mode < 0);
            if (!search->stop_all) {
                continue;
            }
            ++interrupted;
            ok &= check(search->nodes == budget, "horizon node budget overrun", fen);
            ok &= restored(*search, *original, fen);
            search->game.max_nodes = 0;
            search->stop_all = false;
            ok &= check(run(*search, -score::INF, score::INF, mode, mode < 0) == value && !search->stop_all,
                "horizon interrupted restart differs", fen);
            ok &= restored(*search, *original, fen);
        }
    }
    return ok;
}

bool test_histories(int & probes) {
    bool ok = true;
    for (auto & fixture : histories) {
        board_t board;
        board.init(fixture.initial);
        std::vector<U64> history;
        std::istringstream input(fixture.moves);
        for (std::string name; input >> name;) {
            history.push_back(board.stack->repetition_key());
            move_t move;
            move.set(&board, name.c_str());
            ok &= check(board.valid(&move) && board.legal(&move), "history move differs", fixture.fen);
            board.forward(&move);
        }
        ok &= check(board.to_string() == fixture.fen, "history FEN differs", fixture.fen);
        auto original = std::make_unique<search_t>(fixture.fen, nullptr, nullptr, history);
        for (int mode : {-1, 0, 1}) {
            auto model = std::make_unique<search_t>(fixture.fen);
            prepare(*model, false);
            const int fresh_value = reference(*model, mode);
            const int value = fixture.repeated ? 0 : fresh_value;
            ok &= check(fresh_value != 0, "repetition fixture lacks non-draw control", fixture.fen);
            for (int cache = 0; cache < 3; ++cache) {
                for (auto window : {std::pair<int, int>{-score::INF, score::INF},
                        std::pair{value - 1, value}, std::pair{value, value + 1}}) {
                    trans_table::clear();
                    trans_table::enable();
                    if (cache == 1) {
                        auto writer = std::make_unique<search_t>(fixture.fen);
                        prepare(*writer, false);
                        writer->pvs(-score::INF, score::INF, 1);
                    }
                    if (cache == 2) {
                        trans_table::disable();
                    }
                    auto search = std::make_unique<search_t>(fixture.fen, nullptr, nullptr, history);
                    prepare(*search, false);
                    ok &= check(search->is_repetition() == fixture.repeated,
                        "independent repetition certificate differs", fixture.fen);
                    const int result = run(*search, window.first, window.second, mode, mode < 0);
                    // A warm TT hint may add a move to quiescence.
                    ok &= check(cache == 1 || valid_bound(result, value, window.first, window.second),
                        "history horizon bound differs", fixture.fen);
                    if (mode == 1 && cache == 1 && !fixture.repeated
                            && window.first == -score::INF && window.second == score::INF) {
                        ok &= check(search->nodes > 0, "history-incompatible exact TT reused", fixture.fen);
                    }
                    ok &= restored(*search, *original, fixture.fen);
                    ++probes;
                }
            }
        }
    }
    trans_table::enable();
    return ok;
}
}

int main() {
    magic::init();
    trans_table::set_size(4);
    for (auto & node : certificates) oracle.emplace(node.fen, &node);
    bool ok = validate_nodes();
    int probes = 0, interrupted = 0, exact_reuses = 0, horizon_differences = 0, seeded_probes = 0;
    for (auto fen : roots)
        ok &= test_root(fen, probes, interrupted, exact_reuses, horizon_differences, seeded_probes);
    int history_probes = 0;
    ok &= test_histories(history_probes);
    ok &= valid && check(checked_nodes > 0 && quiescent_nodes > 0
        && interrupted > 0 && exact_reuses > 0 && horizon_differences > 0 && seeded_probes > 0,
        "missing real horizon coverage", "all fixtures");
    trans_table::enable();
    std::cout << std::size(certificates) << " certified nodes, " << probes << " real horizon probes, "
        << interrupted << " interrupted restarts, " << exact_reuses << " exact TT reuses, "
        << horizon_differences << " differing PVS/qsearch horizons, " << seeded_probes
        << " deep-TT rejection probes, " << history_probes << " history probes\n";
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
