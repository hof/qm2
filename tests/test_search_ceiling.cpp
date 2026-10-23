#include "engine.h"
#include "w17/w17_search.h"
#include "w17/w17_eval.h"

namespace {

struct ceiling_case_t {
    const char * position;
    bool terminal;
    bool checked;
    int standard_sign;
    int losers_sign;
};

// Synthetic interior placement isolates the stack boundary without inventing
// legal game history. Repetition cannot scan the unpopulated earlier slots.
void place_at_ply(search_t & search, int ply) {
    search.brd._stack[ply].copy(search.brd.stack);
    search.brd.stack = &search.brd._stack[ply];
    search.brd.ply = ply;
    search.stack = &search._stack[ply];
    search.stack->in_check = search.brd.in_check();
    search.stack->repetition_start = ply;
    search.draw_contempt = 0;
}

bool run_case(const ceiling_case_t & test, int clock, bool losers, int mode,
        int depth, int alpha, int table_state) {
    const std::string fen = std::string(test.position) + " " + std::to_string(clock) + " 1";
    std::unique_ptr<search_t> storage;
    if (losers) {
        storage = std::make_unique<w17_search_t>(fen.c_str());
    } else {
        storage = std::make_unique<search_t>(fen.c_str());
    }
    search_t & search = *storage;
    const int ply = losers ? MAX_PLY : MAX_PLY - 1;
    place_at_ply(search, ply);
    const auto original_fen = search.brd.to_string();
    const auto key = search.brd.stack->tt_key;
    const auto context = search.position_key();
    const auto history_start = search.stack->repetition_start;
    const bool repeated = search.is_repetition();
    const int sign = losers ? test.losers_sign : test.standard_sign;
    const int expected = test.terminal ? sign * (score::MATE - ply)
        : clock >= 100 ? 0 : losers ? w17_evaluate(static_cast<w17_search_t *>(&search)) : evaluate(&search);
    trans_table::clear();
    if (table_state == 1) {
        const U64 tt_key = losers ? static_cast<w17_search_t &>(search).w17_tt_key(mode) : context;
        // A stale cached evaluation must not hide the ceiling's terminal result.
        trans_table::store(tt_key, 0, ply, 6, 1234, 0, score::EXACT);
    }
    if (table_state == 2) {
        trans_table::disable();
    }
    const int beta = alpha == -score::INF ? score::INF : alpha + 1;
    const int actual = losers ? static_cast<w17_search_t &>(search).w17_pvs(alpha, beta, mode, depth)
        : depth <= 0 ? search.qsearch(alpha, beta, depth) : search.pvs(alpha, beta, depth);
    trans_table::enable();
    const bool success = actual == expected && !search.stop_all && search.nodes == 0
        && search.brd.in_check() == test.checked && search.brd.to_string() == original_fen
        && search.brd.ply == ply && search.stack == &search._stack[ply]
        && search.brd.stack == &search.brd._stack[ply]
        && search.brd.stack->tt_key == key
        && search.position_key() == context
        && search.stack->repetition_start == history_start && search.is_repetition() == repeated;
    if (!success) {
        std::cerr << "ceiling: " << fen << " losers=" << losers << " mode=" << mode
            << " depth=" << depth << " alpha=" << alpha << " TT=" << table_state
            << " expected=" << expected << " actual=" << actual << "\n";
    }
    return success;
}

bool test_transition(bool losers) {
    // A real legal move reaches mate exactly on the last usable stack slot.
    const char * fen = "7k/8/5KQ1/8/8/8/p7/8 w - - 99 1";
    std::unique_ptr<search_t> storage;
    if (losers) {
        storage = std::make_unique<w17_search_t>(fen);
    } else {
        storage = std::make_unique<search_t>(fen);
    }
    search_t & search = *storage;
    const int ply = losers ? MAX_PLY - 1 : MAX_PLY - 2;
    place_at_ply(search, ply);
    const auto original_fen = search.brd.to_string();
    const auto key = search.brd.stack->tt_key;
    const auto context = search.position_key();
    move_t move;
    move.set(&search.brd, "g6g7");
    if (!search.brd.valid(&move) || !search.brd.legal(&move)) {
        return false;
    }
    search.forward(&move, search.brd.gives_check(&move));
    const int expected = (losers ? 1 : -1) * (score::MATE - ply - 1);
    const int actual = search.pvs(-score::INF, score::INF, 1);
    search.backward(&move);
    const bool success = actual == expected && !search.stop_all
        && search.brd.to_string() == original_fen && search.brd.stack->tt_key == key
        && search.position_key() == context && search.brd.ply == ply
        && search.stack == &search._stack[ply] && search.brd.stack == &search.brd._stack[ply];
    if (!success) {
        std::cerr << "ceiling transition losers=" << losers
            << " expected=" << expected << " actual=" << actual << "\n";
    }
    return success;
}

bool test_interruption(bool losers) {
    const char * fen = "7k/7p/8/8/8/8/P7/KR6 w - - 0 1";
    std::unique_ptr<search_t> storage;
    if (losers) {
        storage = std::make_unique<w17_search_t>(fen);
    } else {
        storage = std::make_unique<search_t>(fen);
    }
    search_t & search = *storage;
    place_at_ply(search, losers ? MAX_PLY : MAX_PLY - 1);
    const auto original_fen = search.brd.to_string();
    const auto key = search.brd.stack->tt_key;
    // A spent node budget must interrupt before the ceiling evaluation.
    search.game.max_nodes = 1;
    search.nodes = 1;
    const int interrupted = search.pvs(-7, -6, 1);
    bool success = interrupted == -7 && search.stop_all && search.nodes == 1;
    search.game.max_nodes = 0;
    search.stop_all = false;
    const int expected = losers ? w17_evaluate(static_cast<w17_search_t *>(&search)) : evaluate(&search);
    success &= search.pvs(-score::INF, score::INF, 1) == expected && !search.stop_all
        && search.nodes == 1 && search.brd.to_string() == original_fen
        && search.brd.stack->tt_key == key;
    if (!success) {
        std::cerr << "interrupted ceiling restart losers=" << losers << "\n";
    }
    return success;
}

bool test_repetition(bool losers) {
    const char * fen = "6nk/7p/8/8/8/8/P7/KN1R4 w - - 0 1";
    std::unique_ptr<search_t> storage;
    if (losers) {
        storage = std::make_unique<w17_search_t>(fen);
    } else {
        storage = std::make_unique<search_t>(fen);
    }
    search_t & search = *storage;
    const int ply = (losers ? MAX_PLY : MAX_PLY - 1) - 4;
    place_at_ply(search, ply);
    const auto original_fen = search.brd.to_string();
    const auto key = search.brd.stack->tt_key;
    const auto context = search.position_key();
    const char * cycle[] = {"b1c3", "g8f6", "c3b1", "f6g8"};
    move_t moves[4];
    for (int i = 0; i < 4; ++i) {
        moves[i].set(&search.brd, cycle[i]);
        if (!search.brd.valid(&moves[i]) || !search.brd.legal(&moves[i])) {
            return false;
        }
        search.forward(&moves[i], search.brd.gives_check(&moves[i]));
    }
    bool success = search.is_repetition()
        && search.pvs(-score::INF, score::INF, 1) == 0 && !search.stop_all;
    if (!losers) {
        success &= search.qsearch(-score::INF, score::INF, -1) == 0;
    }
    for (int i = 3; i >= 0; --i) search.backward(&moves[i]);
    success &= search.brd.to_string() == original_fen && search.brd.stack->tt_key == key
        && search.position_key() == context && !search.is_repetition()
        && search.brd.ply == ply && search.stack == &search._stack[ply];
    if (!success) {
        std::cerr << "ceiling repetition losers=" << losers << "\n";
    }
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(1);
    // Terminal expectations come from chess rules, never static evaluation.
    // Both sides retain a pawn so losers mate/stalemate is not king-only.
    const ceiling_case_t cases[] = {
        {"7k/6Q1/5K2/8/8/p7/P7/8 b - -", true, true, -1, 1},
        {"8/p7/P7/8/8/5k2/6q1/7K w - -", true, true, -1, 1},
        {"7k/5K2/6Q1/8/8/p7/P7/8 b - -", true, false, 0, 1},
        {"8/p7/P7/8/8/6q1/5k2/7K w - -", true, false, 0, 1},
        {"R6k/8/5K2/8/8/p7/P7/8 b - -", false, true, 0, 0},
        {"8/p7/P7/8/8/5k2/8/r6K w - -", false, true, 0, 0},
        {"7k/7p/8/8/8/8/P7/KR6 w - -", false, false, 0, 0},
        {"kr6/p7/8/8/8/8/7P/7K b - -", false, false, 0, 0},
    };
    bool success = true;
    for (const auto & test : cases) {
        for (int clock : {0, 99, 100, 101}) {
            for (int table_state : {0, 1, 2}) {
                for (int alpha : {-score::INF, -10000, -1, 0, 10000}) {
                    for (int depth : {-2, 0, 1, 3}) {
                        success &= run_case(test, clock, false, -1, depth, alpha, table_state);
                        for (int mode : {-1, 0, 1}) {
                            success &= run_case(test, clock, true, mode, depth, alpha, table_state);
                        }
                    }
                }
            }
        }
    }
    success &= test_transition(false);
    success &= test_transition(true);
    success &= test_interruption(false);
    success &= test_interruption(true);
    success &= test_repetition(false);
    success &= test_repetition(true);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
