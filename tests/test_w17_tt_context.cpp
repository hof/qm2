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

result_t run(const std::string & fen, int mode, int depth,
        const std::vector<U64> & history = {}, int contempt = 0) {
    auto search = std::make_unique<w17_search_t>(fen.c_str(), nullptr, nullptr, history);
    search->draw_contempt = contempt;
    search->stack->in_check = search->brd.in_check();
    const auto original_fen = search->brd.to_string();
    const U64 key = search->brd.stack->tt_key;
    const U64 context = search->position_key();
    const bool repeated = search->is_repetition();
    const int value = search->w17_pvs(-score::INF, score::INF, mode, depth);
    if (!check("losers search must finish and restore board, hashes, history and stacks",
            !search->stop_all && search->brd.to_string() == original_fen
            && search->brd.stack->tt_key == key
            && search->position_key() == context && search->is_repetition() == repeated
            && search->brd.ply == 0 && search->stack == search->root_stack
            && search->brd.stack == search->brd._stack)) {
        std::exit(EXIT_FAILURE);
    }
    return {value, search->nodes};
}

bool test_mode_pairs() {
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
            for (int depth : {1, 2, 3}) {
                int cold[3];
                for (int mode = -1; mode <= 1; ++mode) {
                    trans_table::clear();
                    cold[mode + 1] = run(fen, mode, depth).value;
                    trans_table::disable();
                    const auto uncached = run(fen, mode, depth);
                    trans_table::enable();
                    success &= check("cold and TT-disabled losers searches must agree",
                        cold[mode + 1] == uncached.value);
                }
                for (int writer = -1; writer <= 1; ++writer) {
                    for (int reader = -1; reader <= 1; ++reader) {
                        trans_table::clear();
                        run(fen, writer, depth);
                        const auto warm = run(fen, reader, depth);
                        if (warm.value != cold[reader + 1]) {
                            std::cerr << "mode mismatch: " << fen << " depth=" << depth
                                << " writer=" << writer << " reader=" << reader
                                << " cold=" << cold[reader + 1] << " warm=" << warm.value << "\n";
                            success = false;
                        }
                    }
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
        for (int reader_mode = -1; reader_mode <= 1; ++reader_mode) {
            trans_table::clear();
            const int cold = run(reader_fen, reader_mode, 2).value;
            for (int writer_mode = -1; writer_mode <= 1; ++writer_mode) {
                trans_table::clear();
                run(writer_fen, writer_mode, 2);
                success &= check("mode compatibility must preserve cross-clock score safety",
                    run(reader_fen, reader_mode, 2).value == cold);
            }
        }
    }
    for (int mode = -1; mode <= 1; ++mode) {
        trans_table::clear();
        run(std::string(position) + " 0 1", mode, 2);
        success &= check("a playable fifty-move draw must precede every cached mode",
            run(std::string(position) + " 100 1", mode, 2).value == 0);
    }
    return success;
}

bool test_bound_compatibility() {
    bool success = true;
    const char * fen = "7k/7p/8/8/8/8/8/KR6 w - - 4 1";
    auto writer = std::make_unique<w17_search_t>(fen);
    writer->draw_contempt = 0;
    const int count = writer->init_root_moves();
    if (!check("bound fixture must have legal moves", count > 0)) {
        return false;
    }
    const int hint = writer->root.moves[0].move.to_compact();
    for (int writer_mode = -1; writer_mode <= 1; ++writer_mode) {
        for (int reader_mode = -1; reader_mode <= 1; ++reader_mode) {
            for (int bound : {score::LOWERBOUND, score::UPPERBOUND, score::EXACT}) {
                trans_table::clear();
                const int expected = bound == score::UPPERBOUND ? -12345 : 12345;
                trans_table::store(writer->w17_tt_key(writer_mode), 0, 0, 3, expected, hint, bound);
                tt_hit_t hit;
                const bool found = trans_table::probe(writer->w17_tt_key(reader_mode), 0, 4, hit);
                success &= check("each mode must have its own entries",
                    found == (writer_mode == reader_mode)
                    && (!found || (hit.score == expected && hit.bound == bound && hit.move == hint)));
                // Exercise all three actual cutoff paths, not just the table API.
                auto reader = std::make_unique<w17_search_t>(fen);
                reader->draw_contempt = 0;
                reader->stack->in_check = reader->brd.in_check();
                const int alpha = bound == score::EXACT ? -score::INF : bound == score::UPPERBOUND ? -1 : 0;
                const int beta = bound == score::EXACT ? score::INF : alpha + 1;
                const int actual = reader->w17_pvs(alpha, beta, reader_mode, 3);
                success &= check("losers PVS must use mode compatibility for every cutoff type",
                    writer_mode == reader_mode ? actual == expected && reader->nodes == 0 : actual != expected);
                success &= check("bound probes must finish at the original position",
                    !reader->stop_all && reader->brd.to_string() == fen
                    && reader->brd.ply == 0 && reader->stack == reader->root_stack);
            }
        }
    }
    return success;
}

bool test_incoming_mode() {
    const char * fen = "7k/7p/8/8/8/8/8/KR6 w - - 0 1";
    auto search = std::make_unique<w17_search_t>(fen);
    search->draw_contempt = 0;
    search->stack->in_check = search->brd.in_check();
    trans_table::clear();
    const int expected = search->w17_pvs(-score::INF, score::INF, -1, 1);
    tt_hit_t hit;
    bool success = check("auto-mode node must store under its incoming mode",
        trans_table::probe(search->w17_tt_key(-1), 0, 0, hit) && hit.depth >= 1
        && hit.score == expected && hit.bound == score::EXACT);
    success &= check("auto-to-forced transition must not relabel the parent's score",
        !trans_table::probe(search->w17_tt_key(search->brd.us()), 0, 0, hit));
    const auto matching = run(fen, -1, 1);
    success &= check("matching incoming mode must preserve useful one-node score reuse",
        matching.value == expected && matching.nodes == 0);
    trans_table::clear();
    const int depth = search->brd.max_mate_depth_us();
    const int deeper = search->w17_pvs(-score::INF, score::INF, -1, depth);
    success &= check("quiet depth reduction must preserve incoming mode and conservative depth",
        trans_table::probe(search->w17_tt_key(-1), 0, 0, hit) && hit.score == deeper
        && hit.depth == depth - 1);
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_mode_pairs();
    success &= test_clock_pairs();
    success &= test_bound_compatibility();
    success &= test_incoming_mode();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
