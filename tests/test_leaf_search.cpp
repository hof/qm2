#include "engine.h"

namespace {

struct position_case_t {
    const char * name;
    const char * fen;
    bool in_check;
    int legal_moves;
    int expected_score;
};

bool check_score(const char * name, const char * method, int actual, int expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << name << " (" << method << "): expected " << expected
        << ", got " << actual << "\n";
    return false;
}

void prepare_search(search_t & search) {
    trans_table::clear();
    search.draw_contempt = 0;
    // Direct interior-node calls need the check flag normally supplied by forward().
    search.stack->in_check = search.brd.in_check();
    search.null_enabled = false;
    search.beta_pruning = false;
    search.lmr_enabled = false;
    search.ffp_enabled = false;
}

bool restored_position(search_t & search, const char * fen) {
    search_t original(fen);
    const bool success = search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
        && search.brd.stack->pawn_hash == original.brd.stack->pawn_hash
        && search.brd.stack->material_hash == original.brd.stack->material_hash
        && search.brd.ply == 0 && search.stack == search.root_stack;
    if (!success) {
        std::cerr << "search did not restore board, hashes, or stack: " << fen << "\n";
    }
    return success;
}

bool test_position(const position_case_t & test) {
    game_t game;
    search_t search(test.fen, &game);
    prepare_search(search);
    const int legal_moves = search.init_root_moves();
    bool success = search.brd.in_check() == test.in_check
        && legal_moves == test.legal_moves;
    if (!success) {
        std::cerr << test.name << ": unexpected check status or legal move count: "
            << search.brd.in_check() << ", " << legal_moves << "\n";
    }
    search_t quiet_search(test.fen, &game);
    prepare_search(quiet_search);
    const int quiet_score = quiet_search.qsearch(-score::INF, score::INF, 0);
    success &= check_score(test.name, "quiescence", quiet_score, test.expected_score);
    success &= restored_position(quiet_search, test.fen);
    search_t full_search(test.fen, &game);
    prepare_search(full_search);
    const int full_score = full_search.pvs(-score::INF, score::INF, 1);
    success &= check_score(test.name, "one-ply search", full_score, test.expected_score);
    success &= restored_position(full_search, test.fen);
    if (quiet_search.stop_all || full_search.stop_all) {
        std::cerr << test.name << ": search aborted\n";
        success = false;
    }
    return success;
}

bool test_quiet_evasion() {
    game_t game;
    search_t search("R6k/8/5K2/8/8/8/8/8 b - - 0 1", &game);
    prepare_search(search);
    bool success = search.brd.in_check() && search.init_root_moves() == 1;
    move_t escape;
    escape.set(&search.brd, "h8h7");
    search.forward(&escape, search.brd.gives_check(&escape));
    const int expected_score = -search.qsearch(-score::INF, score::INF, -1);
    search.backward(&escape);
    search_t checked_search("R6k/8/5K2/8/8/8/8/8 b - - 0 1", &game);
    prepare_search(checked_search);
    const int actual_score = checked_search.qsearch(-score::INF, score::INF, -1);
    success &= check_score("only quiet check evasion", "quiescence", actual_score, expected_score);
    success &= checked_search.nodes > 1 && !checked_search.stop_all;
    if (!success) {
        std::cerr << "only quiet check evasion: must search h8h7 instead of standing pat\n";
    }
    return success;
}

bool test_capture() {
    game_t game;
    const char * fen = "7k/8/8/3q4/8/8/8/K2R4 w - - 0 1";
    search_t search(fen, &game);
    prepare_search(search);
    const int static_score = evaluate(&search);
    const int quiet_score = search.qsearch(-score::INF, score::INF, 0);
    bool success = static_score < -200 && quiet_score > 200
        && search.stack->best_move.to_string() == "d1d5" && !search.stop_all;
    if (!success) {
        std::cerr << "hanging queen: static=" << static_score << ", quiescence="
            << quiet_score << ", move=" << search.stack->best_move.to_string() << "\n";
    }
    success &= restored_position(search, fen);
    return success;
}

bool test_recapture() {
    game_t game;
    const char * fen = "3r3k/8/8/3q4/8/8/8/K2R4 w - - 0 1";
    search_t search(fen, &game);
    prepare_search(search);
    move_t capture;
    capture.set(&search.brd, "d1d5");
    search.forward(&capture, search.brd.gives_check(&capture));
    const int before_recapture = evaluate(&search);
    const int after_recapture = search.qsearch(-score::INF, score::INF, -1);
    bool success = before_recapture > -200 && before_recapture < 200
        && after_recapture > 200 && search.stack->best_move.to_string() == "d8d5"
        && search.nodes > 1 && !search.stop_all;
    search.backward(&capture);
    success &= restored_position(search, fen);
    if (!success) {
        std::cerr << "rook recapture: static=" << before_recapture
            << ", quiescence=" << after_recapture << "\n";
    }
    return success;
}

}

int main() {
    magic::init();
    // Terminal results are exact; heuristic evaluation is never the mate/draw oracle.
    const position_case_t cases[] = {
        {"black checkmated", "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", true, 0, -score::MATE},
        {"white checkmated", "8/8/8/8/8/5k2/6q1/7K w - - 0 1", true, 0, -score::MATE},
        {"black stalemated", "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1", false, 0, 0},
        {"white stalemated", "8/8/8/8/8/6q1/5k2/7K w - - 0 1", false, 0, 0},
        {"mate in one", "7k/8/5KQ1/8/8/8/8/8 w - - 0 1", false, 23, score::MATE - 1},
    };
    bool success = true;
    for (const position_case_t & test : cases) {
        success &= test_position(test);
    }
    success &= test_quiet_evasion();
    success &= test_capture();
    success &= test_recapture();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
