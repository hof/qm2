#include "engine.h"
#include "w17/w17_search.h"

namespace {

const char * start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char * cycle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};

bool check(const char * name, bool success) {
    if (!success) {
        std::cerr << name << "\n";
    }
    return success;
}

void play(search_t & search, const char * text) {
    move_t move;
    move.set(&search.brd, text);
    search.forward(&move, search.brd.gives_check(&move));
}

bool test_owned_history() {
    board_t board;
    board.init(start_fen);
    std::vector<U64> history;
    for (const char * text : cycle) {
        history.push_back(board.stack->tt_key);
        move_t move;
        move.set(&board, text);
        board.forward(&move);
    }
    const std::string fen = board.to_string();
    search_t repeated(fen.c_str(), nullptr, nullptr, history);
    w17_search_t variant(fen.c_str(), nullptr, nullptr, history);
    repeated.init_root_moves();
    history.clear();
    search_t fresh(fen.c_str());
    fresh.init_root_moves();
    bool success = check("supplied history must survive caller mutation and another search",
        repeated.is_repetition() && variant.is_repetition() && !fresh.is_repetition());
    repeated.draw_contempt = 0;
    success &= check("quiescence must recognize supplied repetition history",
        repeated.qsearch(-score::INF, score::INF, 0) == 0 && !repeated.stop_all);
    const U64 key = board.stack->tt_key;
    const std::vector<U64> short_history = {key};
    search_t partial(fen.c_str(), nullptr, nullptr, short_history);
    success &= check("unknown positions before a partial history must not be consulted",
        !partial.is_repetition());
    const std::vector<U64> wrong_side_history = {key ^ 1, key, key ^ 1, key ^ 1};
    search_t wrong_side(fen.c_str(), nullptr, nullptr, wrong_side_history);
    success &= check("only positions with the same side to move can repeat",
        !wrong_side.is_repetition());
    repeated.init(fen.c_str(), nullptr);
    success &= check("reinitializing from a FEN must clear old history", !repeated.is_repetition());
    return success;
}

bool test_search_path() {
    search_t search(start_fen);
    search.init_root_moves();
    move_t moves[4];
    for (int i = 0; i < 4; ++i) {
        moves[i].set(&search.brd, cycle[i]);
        search.forward(&moves[i], search.brd.gives_check(&moves[i]));
    }
    // No PVS call populated search-stack keys: board history must be sufficient.
    bool success = check("a legal search cycle must be detected independently of PVS",
        search.is_repetition());
    for (int i = 3; i >= 0; --i) {
        search.backward(&moves[i]);
    }
    success &= check("undo must restore the root and its repetition state",
        search.brd.to_string() == start_fen && !search.is_repetition());
    for (const char * text : cycle) {
        play(search, text);
    }
    search.reset_stack();
    success &= check("rebasing the search stack must retain played history",
        search.brd.ply == 0 && search.is_repetition());
    play(search, "e2e4");
    success &= check("a pawn move must stop repetition scanning", !search.is_repetition());
    return success;
}

bool test_null_moves_and_bounds() {
    const char * fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 4 3";
    search_t search(fen);
    search.init_root_moves();
    for (int i = 0; i < 4; ++i) {
        search.forward();
    }
    bool success = check("artificial null moves must not manufacture repetition",
        !search.is_repetition());
    for (int i = 0; i < 4; ++i) {
        search.backward();
    }
    for (const char * text : cycle) {
        play(search, text);
    }
    success &= check("undoing null moves must restore legal cycle detection", search.is_repetition());
    for (int halfmoves : {4, 99, 100, 101, 150}) {
        const std::string position = "7k/8/8/8/8/8/8/K6R w - - "
            + std::to_string(halfmoves) + " 1";
        search_t fresh(position.c_str());
        fresh.init_root_moves();
        success &= check("a halfmove counter is not evidence of past positions", !fresh.is_repetition());
    }
    return success;
}

}

int main() {
    magic::init();
    bool success = test_owned_history();
    success &= test_search_path();
    success &= test_null_moves_and_bounds();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
