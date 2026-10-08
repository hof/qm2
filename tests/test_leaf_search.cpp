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

bool test_material_draws() {
    struct material_case_t {
        const char * name;
        const char * fen;
        bool dead;
    };
    const material_case_t cases[] = {
        {"white knight against bare king", "7k/8/8/8/8/8/8/KN6 w - - 0 1", true},
        {"black knight against bare king", "kn6/8/8/8/8/8/8/7K b - - 0 1", true},
        {"white bishop against bare king", "7k/8/8/8/8/8/8/KB6 w - - 0 1", true},
        {"black bishop against bare king", "kb6/8/8/8/8/8/8/7K b - - 0 1", true},
        {"same-color bishops, edge kings", "k7/8/2b5/8/8/5B2/8/7K w - - 0 1", true},
        {"same-color bishops, central kings", "8/8/2b1k3/8/8/3K1B2/8/8 b - - 0 1", true},
        {"promoted bishops on one color", "k7/8/2b5/8/4B3/5B2/8/7K w - - 0 1", true},
        {"two white knights, central kings", "8/8/8/4k3/8/3KNN2/8/8 w - - 0 1", false},
        {"two black knights, central kings", "8/8/8/3knn2/8/4K3/8/8 b - - 0 1", false},
        {"knight against knight", "8/8/2n1k3/8/8/3K1N2/8/8 w - - 0 1", false},
        {"bishop against knight", "8/8/2n1k3/8/8/3K1B2/8/8 w - - 0 1", false},
        {"opposite-color bishops", "8/8/1b2k3/8/8/3K1B2/8/8 w - - 0 1", false},
        {"bishop and knight", "8/8/4k3/8/8/3KNB2/8/8 w - - 0 1", false},
        {"bishops on both colors", "8/8/4k3/8/8/3KBB2/8/8 w - - 0 1", false},
        {"three knights", "8/8/4k3/8/8/3KNNN1/8/8 w - - 0 1", false},
        {"pawn remains", "7k/8/8/8/8/8/P7/K7 w - - 0 1", false},
        {"rook remains", "7k/8/8/8/8/8/8/KR6 w - - 0 1", false},
        {"queen remains", "7k/8/8/8/8/8/8/KQ6 w - - 0 1", false},
    };
    bool success = true;
    for (const material_case_t & test : cases) {
        search_t search(test.fen);
        prepare_search(search);
        if (search.brd.is_draw() != test.dead || search.is_draw() != test.dead) {
            std::cerr << test.name << ": incorrect material draw classification\n";
            success = false;
        }
        if (test.dead) {
            success &= check_score(test.name, "quiescence",
                search.qsearch(-score::INF, score::INF, 0), 0);
            success &= check_score(test.name, "one-ply search",
                search.pvs(-score::INF, score::INF, 1), 0);
            success &= restored_position(search, test.fen);
        }
    }
    return success;
}

bool test_fifty_move_boundary() {
    bool success = true;
    for (int halfmoves : {99, 100, 101, 150}) {
        const std::string suffix = " " + std::to_string(halfmoves) + " 1";
        const std::string mate_name = "checkmate at halfmove " + std::to_string(halfmoves);
        for (const char * position : {
                "7k/6Q1/5K2/8/8/8/8/8 b - -",
                "8/8/8/8/8/5k2/6q1/7K w - -"}) {
            const std::string fen = position + suffix;
            const position_case_t test = {mate_name.c_str(),
                fen.c_str(), true, 0, -score::MATE};
            success &= test_position(test);
        }
        const std::string fen = "R6k/8/5K2/8/8/8/8/8 b - -" + suffix;
        for (bool quiescence : {false, true}) {
            search_t search(fen.c_str());
            prepare_search(search);
            success &= check_score("quiet escape at the fifty-move boundary", "draw detection",
                search.is_draw(), halfmoves >= 100);
            const int actual = quiescence ? search.qsearch(-score::INF, score::INF, 0)
                : search.pvs(-score::INF, score::INF, 1);
            success &= check_score("quiet escape at the fifty-move boundary", "search", actual, 0);
            // At 99 the escape reaches the limit; at 100 a draw is already available.
            success &= (halfmoves >= 100 || search.nodes > 1) && !search.stop_all;
            success &= restored_position(search, fen.c_str());
        }
        const std::string capture_fen = "7k/6Q1/8/8/8/8/8/K7 b - -" + suffix;
        search_t capture_evasion(capture_fen.c_str());
        prepare_search(capture_evasion);
        success &= check_score("capture escape at the fifty-move boundary", "draw detection",
            capture_evasion.is_draw(), halfmoves >= 100);
        const position_case_t capture_case = {"capture escape at the fifty-move boundary",
            capture_fen.c_str(), true, 1, 0};
        success &= test_position(capture_case);
        for (bool capture : {false, true}) {
            const std::string reset_fen = std::string(capture
                ? "7k/8/8/3q4/8/8/8/K2R4 w - -"
                : "7k/8/8/8/8/8/P7/KR6 w - -") + suffix;
            search_t search(reset_fen.c_str());
            prepare_search(search);
            move_t move;
            move.set(&search.brd, capture ? "d1d5" : "a2a3");
            success &= search.brd.valid(&move) && search.brd.legal(&move);
            search.forward(&move, search.brd.gives_check(&move));
            success &= search.brd.stack->fifty_count == 0 && !search.is_draw();
            // The side that just moved retains winning material after resetting the clock.
            success &= search.qsearch(-score::INF, score::INF, 0) < -200 && !search.stop_all;
            search.backward(&move);
            success &= restored_position(search, reset_fen.c_str());
        }
    }
    const position_case_t last_move_mate = {"quiet move mates on halfmove 100",
        "7k/8/5KQ1/8/8/8/8/8 w - - 99 1", false, 23, score::MATE - 1};
    success &= test_position(last_move_mate);
    return success;
}

}

int main() {
    magic::init();
    // Terminal results are exact; heuristic evaluation is never the mate/draw oracle.
    const position_case_t cases[] = {
        {"black checkmated", "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", true, 0, -score::MATE},
        {"white checkmated", "8/8/8/8/8/5k2/6q1/7K w - - 0 1", true, 0, -score::MATE},
        {"black checkmated by two knights", "7k/5K2/5NN1/8/8/8/8/8 b - - 0 1", true, 0, -score::MATE},
        {"white checkmated by two knights", "8/8/8/8/8/5nn1/5k2/7K w - - 0 1", true, 0, -score::MATE},
        {"black stalemated", "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1", false, 0, 0},
        {"white stalemated", "8/8/8/8/8/6q1/5k2/7K w - - 0 1", false, 0, 0},
        {"bare kings on edges", "7k/8/8/8/8/8/8/K7 w - - 0 1", false, 3, 0},
        {"bare kings in center", "8/8/4k3/8/8/3K4/8/8 w - - 0 1", false, 8, 0},
        {"capture is the only check evasion", "7k/6Q1/8/8/8/8/8/K7 b - - 0 1", true, 1, 0},
        {"mate in one", "7k/8/5KQ1/8/8/8/8/8 w - - 0 1", false, 23, score::MATE - 1},
    };
    bool success = true;
    for (const position_case_t & test : cases) {
        success &= test_position(test);
    }
    success &= test_quiet_evasion();
    success &= test_capture();
    success &= test_recapture();
    success &= test_material_draws();
    success &= test_fifty_move_boundary();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
