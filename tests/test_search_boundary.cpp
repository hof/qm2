#include "engine.h"
#include <set>
#include <sstream>

namespace {

struct certificate_t {
    const char * fen;
    int expected;
    const char * legal_moves;
    const char * mating_moves;
};

const certificate_t certificates[] = {
#include "data/search/boundary.inc"
};

std::set<std::string> words(const char * text) {
    std::istringstream input(text);
    std::set<std::string> result;
    for (std::string word; input >> word;) result.insert(word);
    return result;
}

bool check(bool ok, const char * message, const char * fen) {
    if (!ok) {
        std::cerr << message << ": " << fen << '\n';
    }
    return ok;
}

void prepare(search_t & search, int configuration) {
    search.draw_contempt = 0;
    search.tablebase_limit = 0;
    search.stack->in_check = search.brd.in_check();
    // 0: production defaults; 1: unpruned; 2..5: one heuristic enabled.
    if (configuration) {
        search.null_allowed = configuration == 3;
    }
}

bool restored(search_t & search, search_t & original, const char * fen) {
    return check(search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.stack->repetition_start == original.stack->repetition_start
        && search.brd.ply == 0 && search.stack == search.root_stack
        && !search.stop_all, "state restoration failed", fen);
}

bool validate_certificate(const certificate_t & test) {
    auto search = std::make_unique<search_t>(test.fen);
    auto original = std::make_unique<search_t>(test.fen);
    prepare(*search, 0);
    std::set<std::string> legal;
    search->init_root_moves();
    for (int i = 0; i < search->root.move_count; ++i)
        legal.insert(search->root.moves[i].move.to_string());
    bool ok = check(legal == words(test.legal_moves), "independent legal set differs", test.fen);
    const auto mates = words(test.mating_moves);
    for (const auto & text : legal) {
        move_t move;
        move.set(&search->brd, text.c_str());
        search->forward(&move, search->brd.gives_check(&move));
        const int expected = mates.count(text) ? -score::MATE + 1 : 0;
        for (int depth : {0, 1, 3}) {
            const int actual = search->pvs(-score::INF, score::INF, depth);
            ok &= check(actual == expected, "independent child terminal differs", test.fen);
        }
        search->backward(&move);
        ok &= restored(*search, *original, test.fen);
    }
    return ok;
}

bool valid_bound(int result, int expected, int alpha, int beta) {
    if (result <= alpha) {
        return expected <= result;
    }
    if (result >= beta) {
        return expected >= result;
    }
    return result == expected;
}

bool test_windows(const certificate_t & test, int & probes) {
    bool ok = true;
    for (int configuration = 0; configuration < 6; ++configuration) {
        for (int depth : {1, 2, 3, 4}) {
            for (auto window : {std::pair{-score::INF, int(score::INF)},
                    std::pair{test.expected - 1, test.expected},
                    std::pair{test.expected, test.expected + 1},
                    std::pair{-1, 0}, std::pair{0, 1}}) {
                for (int cache = 0; cache < 3; ++cache) {
                    trans_table::clear();
                    if (cache == 2) {
                        trans_table::disable();
                    } else {
                        trans_table::enable();
                    }
                    // Warm with each bound direction as well as an exact score.
                    if (cache == 1) {
                        auto writer = std::make_unique<search_t>(test.fen);
                        prepare(*writer, configuration);
                        writer->pvs(window.first, window.second, depth);
                    }
                    auto search = std::make_unique<search_t>(test.fen);
                    auto original = std::make_unique<search_t>(test.fen);
                    prepare(*search, configuration);
                    const int result = search->pvs(window.first, window.second, depth);
                    ++probes;
                    if (!valid_bound(result, test.expected, window.first, window.second)) {
                        std::cerr << "invalid bound: expected=" << test.expected << " result=" << result
                            << " window=" << window.first << ',' << window.second
                            << " depth=" << depth << " config=" << configuration << " cache=" << cache << '\n';
                        ok &= check(false, "terminal oracle mismatch", test.fen);
                    }
                    ok &= restored(*search, *original, test.fen);
                }
            }
        }
    }
    trans_table::enable();
    return ok;
}

bool test_interruption() {
    bool ok = true;
    for (const auto & test : certificates) {
        if (!*test.legal_moves) {
            continue;
        }
        for (U64 budget : {1, 2}) {
            trans_table::clear();
            auto search = std::make_unique<search_t>(test.fen);
            auto original = std::make_unique<search_t>(test.fen);
            prepare(*search, 0);
            search->game.max_nodes = budget;
            search->pvs(-score::INF, score::INF, 2);
            // A search smaller than the budget completes; one that stops must
            // stop exactly at the budget.
            ok &= check(search->stop_all ? search->nodes == budget : search->nodes < budget,
                "node-budget interruption differs", test.fen);
            search->stop_all = false;
            ok &= restored(*search, *original, test.fen);
            search->game.max_nodes = 0;
            ok &= check(search->pvs(-score::INF, score::INF, 2) == test.expected,
                "restart after interruption differs from oracle", test.fen);
            ok &= restored(*search, *original, test.fen);
        }
    }
    return ok;
}

// Polling is periodic, yet a budget between polls must still stop exactly.
bool test_large_budget() {
    const char * fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    trans_table::clear();
    auto search = std::make_unique<search_t>(fen);
    prepare(*search, 0);
    search->game.max_nodes = 7777;
    search->pvs(-score::INF, score::INF, 12);
    return check(search->stop_all && search->nodes == 7777, "a budget between polls is not exact", fen);
}

bool test_clock_resets() {
    bool ok = true;
    for (const char * fen : {"7k/4P3/8/8/8/8/8/KR6 w - - 99 1",
                            "7k/8/8/3q4/8/8/8/K2R4 w - - 99 1"}) {
        trans_table::clear();
        auto search = std::make_unique<search_t>(fen);
        auto original = std::make_unique<search_t>(fen);
        prepare(*search, 0);
        ok &= check(search->pvs(-score::INF, score::INF, 1) > 0,
            "clock-resetting winning move was lost", fen);
        move_t best;
        best.set(&search->stack->best_move);
        if (!best.piece) {
            ok &= check(false, "missing clock-resetting move", fen);
            continue;
        }
        search->forward(&best, search->brd.gives_check(&best));
        ok &= check(search->brd.stack->fifty_count == 0,
            "winning move must reset the draw clock", fen);
        search->backward(&best);
        ok &= restored(*search, *original, fen);
    }
    return ok;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool ok = true;
    int probes = 0;
    for (const auto & test : certificates) {
        trans_table::clear();
        ok &= validate_certificate(test);
        ok &= test_windows(test, probes);
    }
    ok &= test_interruption();
    ok &= test_large_budget();
    ok &= test_clock_resets();
    std::cout << probes << " independent terminal/window/cache probes\n";
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
