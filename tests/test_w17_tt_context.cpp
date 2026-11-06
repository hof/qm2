#include "engine.h"
#include "w17/w17_search.h"

namespace {

bool check(const char * name, bool success) {
    if (!success) {
        std::cerr << name << "\n";
    }
    return success;
}

struct result_t {
    int value;
    U64 nodes;
};

result_t run(const std::string & fen, int depth, const std::vector<U64> & history = {}, int contempt = 0) {
    auto search = std::make_unique<w17_search_t>(fen.c_str(), nullptr, nullptr, history);
    search->draw_contempt = contempt;
    search->stack->in_check = search->brd.in_check();
    const auto original_fen = search->brd.to_string();
    const U64 key = search->brd.stack->tt_key;
    const U64 context = position_key(search->brd);
    const bool repeated = search->is_repetition();
    const int value = search->w17_pvs(-score::INF, score::INF, depth);
    if (!check("losers search must finish and restore board, hashes, history and stacks",
            !search->stop_all && search->brd.to_string() == original_fen
            && search->brd.stack->tt_key == key
            && position_key(search->brd) == context && search->is_repetition() == repeated
            && search->brd.ply == 0 && search->stack == search->root_stack
            && search->brd.stack == search->brd._stack)) {
        std::exit(EXIT_FAILURE);
    }
    return {value, search->nodes};
}

bool test_cached_agree() {
    bool success = true;
    const char * positions[] = {
        "7k/7p/8/8/8/8/8/KR6 w - -",
        "r6k/6pp/8/8/8/8/P7/KR6 w - -",
        "kr6/8/8/8/8/8/7P/7K b - -",
        "kr6/p7/8/8/8/8/6PP/R6K b - -",
    };
    for (const char * position : positions) {
        for (int clock : {0, 99}) {
            const std::string fen = std::string(position) + " " + std::to_string(clock) + " 1";
            for (int depth : {0, 1, 2, 3}) {
                trans_table::clear();
                const int cold = run(fen, depth).value;
                const int warm = run(fen, depth).value;
                trans_table::disable();
                const int uncached = run(fen, depth).value;
                trans_table::enable();
                if (cold != uncached || warm != cold) {
                    std::cerr << "cache mismatch: " << fen << " depth=" << depth << " cold=" << cold
                        << " warm=" << warm << " uncached=" << uncached << "\n";
                    success = false;
                }
            }
        }
    }
    return success;
}

bool test_clock_pairs() {
    bool success = true;
    const char * position = "7k/7p/8/8/8/8/8/KR6 w - -";
    for (int writer_clock : {0, 99}) {
        const std::string writer_fen = std::string(position) + " " + std::to_string(writer_clock) + " 1";
        const std::string reader_fen = std::string(position) + " " + std::to_string(99 - writer_clock) + " 1";
        trans_table::clear();
        const int cold = run(reader_fen, 2).value;
        trans_table::clear();
        run(writer_fen, 2);
        success &= check("cached scores must stay safe across fifty-move clocks", run(reader_fen, 2).value == cold);
    }
    trans_table::clear();
    run(std::string(position) + " 0 1", 2);
    success &= check("a playable fifty-move draw must precede a cached score",
        run(std::string(position) + " 100 1", 2).value == 0);
    return success;
}

bool test_bound_cutoffs() {
    bool success = true;
    const char * fen = "7k/7p/8/8/8/8/8/KR6 w - - 4 1";
    for (int bound : {score::LOWERBOUND, score::UPPERBOUND, score::EXACT}) {
        auto reader = std::make_unique<w17_search_t>(fen);
        reader->draw_contempt = 0;
        reader->stack->in_check = reader->brd.in_check();
        const int count = reader->init_root_moves();
        if (!check("bound fixture must have legal moves", count > 0)) {
            return false;
        }
        trans_table::clear();
        const int expected = bound == score::UPPERBOUND ? -12345 : 12345;
        trans_table::store(reader->w17_tt_key(), 0, 0, 3, expected, reader->root.moves[0].move.to_compact(), bound);
        const int alpha = bound == score::EXACT ? -score::INF : bound == score::UPPERBOUND ? -1 : 0;
        const int beta = bound == score::EXACT ? score::INF : alpha + 1;
        const int actual = reader->w17_pvs(alpha, beta, 3);
        success &= check("losers PVS must cut on every bound type", actual == expected && reader->nodes == 0);
        success &= check("bound probes must finish at the original position",
            !reader->stop_all && reader->brd.to_string() == fen
            && reader->brd.ply == 0 && reader->stack == reader->root_stack);
    }
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_cached_agree();
    success &= test_clock_pairs();
    success &= test_bound_cutoffs();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
