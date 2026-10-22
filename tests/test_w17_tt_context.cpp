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
    const U64 context = search->tt_context();
    const bool repeated = search->is_repetition();
    const int value = search->w17_pvs(-score::INF, score::INF, mode, depth);
    if (!check("losers search must finish and restore board, hashes, history and stacks",
            !search->stop_all && search->brd.to_string() == original_fen
            && search->brd.stack->tt_key == key
            && search->tt_context() == context && search->is_repetition() == repeated
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
                trans_table::store(writer->brd.stack->tt_key, 0, 0, 3, expected,
                    hint, bound, writer->w17_tt_context(writer_mode), {4, 8});
                for (int depth : {1, 3, 4}) {
                    for (int clock : {4, 8, 99}) {
                        int value = 0, move = 0, flag = 0;
                        const bool usable = trans_table::retrieve(writer->brd.stack->tt_key,
                            0, depth, value, move, flag, writer->w17_tt_context(reader_mode), clock);
                        success &= check("each bound requires compatible mode, depth and clock",
                            usable == (writer_mode == reader_mode && depth <= 3 && clock <= 8));
                        move_t decoded;
                        decoded.set_compact(&writer->brd, move);
                        success &= check("incompatible scores must retain their legal move hint",
                            move == hint && writer->brd.valid(&decoded) && writer->brd.legal(&decoded));
                        if (usable) {
                            success &= check("compatible bounds must retain their value and type",
                                value == expected && flag == bound);
                        }
                    }
                }
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
    int value = 0, move = 0, flag = 0;
    bool success = check("auto-mode node must store under its incoming mode",
        trans_table::retrieve(search->brd.stack->tt_key, 0, 1, value, move, flag,
            search->w17_tt_context(-1), 0) && value == expected && flag == score::EXACT);
    success &= check("auto-to-forced transition must not relabel the parent's score",
        !trans_table::retrieve(search->brd.stack->tt_key, 0, 1, value, move, flag,
            search->w17_tt_context(search->brd.us()), 0) && move != 0);
    const auto matching = run(fen, -1, 1);
    success &= check("matching incoming mode must preserve useful one-node score reuse",
        matching.value == expected && matching.nodes == 0);
    trans_table::clear();
    const int depth = search->brd.max_mate_depth_us();
    const int deeper = search->w17_pvs(-score::INF, score::INF, -1, depth);
    success &= check("quiet depth reduction must preserve incoming mode and conservative depth",
        trans_table::retrieve(search->brd.stack->tt_key, 0, depth - 1, value, move, flag,
            search->w17_tt_context(-1), 0) && value == deeper
        && !trans_table::retrieve(search->brd.stack->tt_key, 0, depth, value, move, flag,
            search->w17_tt_context(-1), 0));
    return success;
}

bool test_history_pairs() {
    // Keep the repetition escape preferable to ordinary positional terms.
    constexpr int repetition_value = 200;
    board_t board;
    board.init("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const char * cycle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    std::vector<U64> history;
    for (int ply = 0; ply < 7; ++ply) {
        history.push_back(board.stack->repetition_key());
        move_t move;
        move.set(&board, cycle[ply % 4]);
        if (!check("history fixture must use legal moves", board.valid(&move) && board.legal(&move))) {
            return false;
        }
        board.forward(&move);
    }
    const auto fen = board.to_string();
    bool success = true;
    int cold[2][3];
    for (int repeated = 0; repeated < 2; ++repeated) {
        const std::vector<U64> supplied = repeated ? history : std::vector<U64>{};
        for (int mode = -1; mode <= 1; ++mode) {
            trans_table::clear();
            cold[repeated][mode + 1] = run(fen, mode, 2, supplied, repetition_value).value;
            trans_table::disable();
            const int uncached = run(fen, mode, 2, supplied, repetition_value).value;
            trans_table::enable();
            success &= check("history-aware cold and TT-disabled searches must agree",
                cold[repeated][mode + 1] == uncached);
        }
    }
    success &= check("history fixture must expose a score-sensitive repetition escape",
        cold[0][0] != cold[1][0] && cold[1][0] == repetition_value);
    for (int writer_history = 0; writer_history < 2; ++writer_history) {
        for (int reader_history = 0; reader_history < 2; ++reader_history) {
            for (int writer = -1; writer <= 1; ++writer) {
                for (int reader = -1; reader <= 1; ++reader) {
                    trans_table::clear();
                    run(fen, writer, 2, writer_history ? history : std::vector<U64>{}, repetition_value);
                    const int warm = run(fen, reader, 2, reader_history ? history : std::vector<U64>{}, repetition_value).value;
                    success &= check("mode compatibility must preserve repetition context",
                        warm == cold[reader_history][reader + 1]);
                }
            }
        }
    }
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(4);
    bool success = test_mode_pairs();
    success &= test_history_pairs();
    success &= test_clock_pairs();
    success &= test_bound_compatibility();
    success &= test_incoming_mode();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
