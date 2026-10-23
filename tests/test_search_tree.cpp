#include "engine.h"
#include <map>
#include <set>
#include <sstream>

namespace {
struct node_t {
    const char * fen;
    int ply;
    int value;
    const char * legal;
};
const node_t certificates[] = {
#include "data/search/tree.inc"
};
std::map<std::string, const node_t *> oracle;

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

class tree_search_t : public search_t {
public:
    bool valid = true;
    bool cancel_research = false;
    int researches = 0;
    std::set<std::string> narrow;

    explicit tree_search_t(const char * fen) : search_t(fen) {
        beta_pruning = null_enabled = ffp_enabled = lmr_enabled = false;
        // This artificial child contract isolates bounds/history from pruning.
        lmp_enabled = history_pruning_enabled = main_see_enabled = razoring_enabled = false;
        tablebase_limit = draw_contempt = 0;
        stack->in_check = brd.in_check();
    }

    int pvs(int alpha, int beta, int depth, bool cut_node = false) override {
        const auto fen = brd.to_string();
        if (brd.ply > 0 && beta - alpha > 1 && narrow.erase(fen)) {
            ++researches;
            if (cancel_research) {
                stop_all = true;
                return alpha;
            }
        } else if (beta - alpha == 1) {
            narrow.insert(fen);
        }
        if (brd.ply == 2) {
            // Explicit two-ply contract replaces qsearch. This isolates PVS
            // bounds; it does not certify chess evaluation.
            stack->pv_count = 0;
            stack->best_move.clear();
            if (abort(false)) {
                return alpha;
            }
            auto found = oracle.find(fen);
            valid &= check(found != oracle.end() && found->second->ply == 2,
                "uncertified leaf", fen.c_str());
            return found == oracle.end() ? 0 : found->second->value;
        }
        valid &= check(depth > 0, "unexpected early horizon", fen.c_str());
        return search_t::pvs(alpha, beta, depth, cut_node);
    }
};

bool restored(tree_search_t & search, search_t & original, const char * fen) {
    return check(search.valid && search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.stack->repetition_start == original.stack->repetition_start
        && search.brd.ply == 0 && search.stack == search.root_stack,
        "state restoration failed", fen);
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

bool validate_nodes() {
    bool ok = true;
    for (const auto & node : certificates) {
        auto search = std::make_unique<search_t>(node.fen);
        std::set<std::string> legal;
        search->init_root_moves();
        for (int i = 0; i < search->root.move_count; ++i)
            legal.insert(search->root.moves[i].move.to_string());
        ok &= check(legal == words(node.legal), "independent legal set differs", node.fen);
    }
    return ok;
}

bool test_root(const node_t & node, int & probes, int & interruptions, int & researches, int & cancelled_researches) {
    bool ok = true;
    auto original = std::make_unique<search_t>(node.fen);
    const std::pair<int, int> windows[] = {{-score::INF, score::INF},
        {node.value - 1, node.value}, {node.value, node.value + 1},
        {-1, 0}, {0, 1}, {node.value - 17, node.value + 17}};
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
                    auto writer = std::make_unique<tree_search_t>(node.fen);
                    writer->pvs(warm.first, warm.second, 2);
                    ok &= restored(*writer, *original, node.fen);
                }
                auto search = std::make_unique<tree_search_t>(node.fen);
                const int value = search->pvs(window.first, window.second, 2);
                ++probes;
                researches += search->researches;
                if (cache == 1 && *node.legal
                        && warm.first == -score::INF && warm.second == score::INF
                        && window.first == node.value - 1 && window.second == node.value) {
                    // Only non-PV windows take cached score cutoffs.
                    ok &= check(search->nodes == 0, "warm exact entry was not reused", node.fen);
                }
                ok &= check(valid_bound(value, node.value, window.first, window.second),
                    "independent minimax bound differs", node.fen);
                ok &= restored(*search, *original, node.fen);
                ok &= check(!search->stop_all, "unexpected cancellation", node.fen);
            }
        }
    }
    trans_table::enable();
    for (U64 budget : {1, 2, 5, 10}) {
        if (!*node.legal) {
            continue;
        }
        trans_table::clear();
        auto search = std::make_unique<tree_search_t>(node.fen);
        search->game.max_nodes = budget;
        search->pvs(-score::INF, score::INF, 2);
        if (search->stop_all) {
            ++interruptions;
            ok &= check(search->nodes == budget, "budget overrun", node.fen);
            ok &= restored(*search, *original, node.fen);
            search->game.max_nodes = 0;
            search->stop_all = false;
            ok &= check(search->pvs(-score::INF, score::INF, 2) == node.value,
                "budget restart differs", node.fen);
            ok &= restored(*search, *original, node.fen);
        }
    }
    trans_table::clear();
    auto search = std::make_unique<tree_search_t>(node.fen);
    search->cancel_research = true;
    search->pvs(-score::INF, score::INF, 2);
    if (search->stop_all) {
        ++interruptions;
        ++cancelled_researches;
        ok &= check(search->researches == 1, "re-search cancellation overrun", node.fen);
        ok &= restored(*search, *original, node.fen);
        search->stop_all = search->cancel_research = false;
        ok &= check(search->pvs(-score::INF, score::INF, 2) == node.value,
            "re-search restart differs", node.fen);
        ok &= restored(*search, *original, node.fen);
    }
    return ok;
}
}

int main() {
    magic::init();
    trans_table::set_size(4);
    for (const auto & node : certificates) oracle.emplace(node.fen, &node);
    bool ok = validate_nodes();
    int probes = 0, interruptions = 0, researches = 0, cancelled_researches = 0;
    for (const auto & node : certificates) {
        if (node.ply == 0) {
            ok &= test_root(node, probes, interruptions, researches, cancelled_researches);
        }
    }
    ok &= check(researches > 0 && interruptions > 0 && cancelled_researches > 0,
        "missing re-search/interruption coverage", "all fixtures");
    std::cout << std::size(certificates) << " certified legal nodes, " << probes
        << " window/cache probes, " << researches << " re-searches, "
        << interruptions << " interrupted restarts (" << cancelled_researches
        << " during re-search)\n";
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
