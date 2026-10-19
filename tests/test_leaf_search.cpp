#include "engine.h"
#include "w17/w17_search.h"

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
    auto original_storage = std::make_unique<search_t>(fen);
    search_t & original = *original_storage;
    const bool success = search.brd.to_string() == original.brd.to_string()
        && search.brd.stack->tt_key == original.brd.stack->tt_key
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
    for (int depth : {0, -1, -2}) {
        auto quiet_storage = std::make_unique<search_t>(test.fen, &game);
        search_t & quiet_search = *quiet_storage;
        prepare_search(quiet_search);
        const int quiet_score = quiet_search.qsearch(-score::INF, score::INF, depth);
        // Quiet checks are intentionally omitted below depth zero.
        if (depth == 0 || test.legal_moves == 0 || test.expected_score == 0) {
            success &= check_score(test.name, "quiescence", quiet_score, test.expected_score);
        }
        success &= restored_position(quiet_search, test.fen);
        success &= !quiet_search.stop_all;
        if (test.legal_moves == 0) {
            for (int alpha : {-10000, -1, 0, 10000}) {
                auto bounded_storage = std::make_unique<search_t>(test.fen, &game);
                search_t & bounded = *bounded_storage;
                prepare_search(bounded);
                const int actual = bounded.qsearch(alpha, alpha + 1, depth);
                success &= check_score(test.name, "bounded quiescence", actual, test.expected_score);
                success &= restored_position(bounded, test.fen);
            }
        }
    }
    search_t full_search(test.fen, &game);
    prepare_search(full_search);
    const int full_score = full_search.pvs(-score::INF, score::INF, 1);
    success &= check_score(test.name, "one-ply search", full_score, test.expected_score);
    success &= restored_position(full_search, test.fen);
    if (full_search.stop_all) {
        std::cerr << test.name << ": search aborted\n";
        success = false;
    }
    return success;
}

bool test_pruning_terminals() {
    bool success = true;
    const char * stalemates[] = {
        "8/8/8/8/8/p1k5/P1P5/KB6 w - - 0 1",
        "kb6/p1p5/P1K5/8/8/8/8/8 b - - 0 1",
    };
    const char * parents[] = {
        "8/8/8/8/2k5/p7/P1P5/KB6 b - - 0 1",
        "kb6/p1p5/P7/2K5/8/8/8/8 w - - 0 1",
    };
    for (int color = 0; color < 2; ++color) {
        for (bool transition : {false, true}) {
            for (int configuration = 0; configuration < 4; ++configuration) {
                for (int depth : {1, 2, 3, 4, 6}) {
                    for (int alpha : {-score::INF, -10000, -1, 0, 10000}) {
                        const char * fen = transition ? parents[color] : stalemates[color];
                        auto storage = std::make_unique<search_t>(fen);
                        search_t & search = *storage;
                        prepare_search(search);
                        search.beta_pruning = configuration & 1;
                        search.null_enabled = configuration & 2;
                        search.lmr_enabled = true;
                        search.ffp_enabled = true;
                        move_t move;
                        if (transition) {
                            move.set(&search.brd, color == 0 ? "c4c3" : "c5c6");
                            success &= search.brd.valid(&move) && search.brd.legal(&move);
                            search.forward(&move, search.brd.gives_check(&move));
                        }
                        success &= !search.brd.in_check() && search.brd.has_pieces(search.brd.us())
                            && search.init_root_moves() == 0;
                        const int beta = alpha == -score::INF ? score::INF : alpha + 1;
                        success &= check_score("trapped-bishop stalemate", "pruning PVS",
                            search.pvs(alpha, beta, depth), 0);
                        success &= !search.stop_all;
                        if (transition) search.backward(&move);
                        success &= restored_position(search, fen);
                    }
                }
            }
        }
    }
    // Playable fail-highs must retain the useful static cutoff, including when
    // the king has no escape and the legal-move probe must find a rook move.
    for (const char * fen : {"7k/8/8/8/8/8/8/KR6 w - - 0 1",
                            "8/8/8/8/8/p1k5/P1P5/KB5R w - - 0 1"}) {
        auto storage = std::make_unique<search_t>(fen);
        search_t & search = *storage;
        prepare_search(search);
        search.beta_pruning = true;
        search.null_enabled = true;
        const int expected = evaluate(&search);
        success &= expected >= 101 && search.init_root_moves() > 0;
        success &= check_score("playable fail-high", "beta pruning", search.pvs(0, 1, 1), expected);
        success &= search.nodes == 1 && !search.stop_all;
        success &= restored_position(search, fen);
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
    bool success = quiet_score > static_score && quiet_score > 0
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
    bool success = after_recapture > before_recapture && search.stack->best_move.to_string() == "d8d5"
        && search.nodes > 1 && !search.stop_all;
    search.backward(&capture);
    success &= restored_position(search, fen);
    if (!success) {
        std::cerr << "rook recapture: static=" << before_recapture
            << ", quiescence=" << after_recapture << "\n";
    }
    return success;
}

bool test_capture_stalemate() {
    const char * fen = "7k/5K2/6n1/6Q1/8/8/8/8 w - - 0 1";
    search_t search(fen);
    prepare_search(search);
    move_t capture;
    capture.set(&search.brd, "g5g6");
    bool success = search.brd.valid(&capture) && search.brd.legal(&capture);
    search.forward(&capture, search.brd.gives_check(&capture));
    for (int depth : {-1, -2}) {
        success &= check_score("capture reaches stalemate", "quiescence",
            search.qsearch(-score::INF, score::INF, depth), 0);
    }
    search.backward(&capture);
    success &= restored_position(search, fen);
    return success;
}

bool test_recursive_stalemate() {
    bool success = true;
    for (const char * fen : {"7k/5K2/6q1/6Q1/8/8/8/8 w - - 0 1",
                            "8/8/8/8/6q1/6Q1/5k2/7K b - - 0 1"}) {
        for (int depth : {0, -1, -2}) {
            search_t search(fen);
            prepare_search(search);
            success &= search.stack->in_check;
            success &= check_score("capture evasion reaches stalemate", "quiescence",
                search.qsearch(-1, 0, depth), 0);
            // The first queen capture supplies a valid draw lower bound.
            success &= search.stack->best_move.to_string()
                == (search.brd.us() ? "g5g6" : "g4g3");
            success &= search.nodes > 1 && !search.stop_all;
            success &= restored_position(search, fen);
        }
    }
    return success;
}

bool test_quiet_playable() {
    bool success = true;
    for (const char * fen : {"7k/8/8/8/8/8/8/KR6 w - - 0 1",
                            "kr6/8/8/8/8/8/8/7K b - - 0 1",
                            "7k/p4K2/6Q1/8/8/8/8/8 b - - 0 1",
                            "8/8/8/8/8/6q1/P4k2/7K w - - 0 1"}) {
        for (int depth : {-1, -2}) {
            search_t search(fen);
            prepare_search(search);
            const int expected = evaluate(&search);
            if (expected == 0) {
                std::cerr << "quiet static score " << expected << ": " << fen << "\n";
                success = false;
            }
            success &= check_score("quiet playable", "quiescence",
                search.qsearch(-score::INF, score::INF, depth), expected);
            success &= restored_position(search, fen);
        }
    }
    return success;
}

bool test_losers_root_scores() {
    bool success = true;
    game_t game;
    game.max_depth = 1;
    const position_case_t cases[] = {
        {"white king-only win", "7k/7p/8/8/8/8/8/K7 w - - 0 1", false, 3, score::MATE},
        {"black king-only win", "k7/8/8/8/8/8/7P/7K b - - 0 1", false, 3, score::MATE},
        {"black king-only loss", "7k/7p/8/8/8/8/8/K7 b - - 0 1", false, 0, -score::MATE},
        {"white king-only loss", "k7/8/8/8/8/8/7P/7K w - - 0 1", false, 0, -score::MATE},
        {"both king-only", "7k/8/8/8/8/8/8/K7 w - - 0 1", false, 3, score::MATE},
        {"black checkmate win", "7k/6Q1/5K2/8/8/8/p7/8 b - - 100 1", true, 0, score::MATE},
        {"white checkmate win", "8/P7/8/8/8/5k2/6q1/7K w - - 100 1", true, 0, score::MATE},
        {"black stalemate win", "7k/5K2/6Q1/8/8/p7/P7/8 b - - 100 1", false, 0, score::MATE},
        {"white stalemate win", "8/p7/P7/8/8/6q1/5k2/7K w - - 100 1", false, 0, score::MATE},
    };
    for (const auto & test : cases) {
        w17_search_t search(test.fen, &game);
        const auto fen = search.brd.to_string();
        const U64 key = search.brd.stack->tt_key;
        search.go();
        success &= check_score(test.name, "root", search.result_score, test.expected_score);
        success &= !search.stack->best_move.piece && search.stack->pv_count == 0
            && search.brd.to_string() == fen && search.brd.stack->tt_key == key
            && search.brd.ply == 0 && search.stack == search.root_stack;
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
            const int reset_score = search.qsearch(-score::INF, score::INF, 0);
            if (reset_score >= 0 || search.stop_all) {
                std::cerr << "reset score " << reset_score << ": " << reset_fen << "\n";
                success = false;
            }
            search.backward(&move);
            success &= restored_position(search, reset_fen.c_str());
        }
    }
    const position_case_t last_move_mate = {"quiet move mates on halfmove 100",
        "7k/8/5KQ1/8/8/8/8/8 w - - 99 1", false, 23, score::MATE - 1};
    success &= test_position(last_move_mate);
    return success;
}

bool test_empty_tactical_bounds() {
    for (const char * fen : {"k3r3/8/8/8/8/3b4/4P3/4K3 w - - 0 1",
            "k3r3/8/8/8/8/8/4P3/4K3 w - - 0 1"}) {
        auto search = std::make_unique<search_t>(fen);
        prepare_search(*search);
        const int alpha = evaluate(search.get()) + 50;
        const int actual = search->qsearch(alpha, alpha + 100, -1);
        if (actual != alpha || !restored_position(*search, fen)) {
            std::cerr << "empty tactical fail-low bound: expected " << alpha
                << ", got " << actual << ": " << fen << '\n';
            return false;
        }
    }
    return true;
}

bool test_quiet_order() {
    const char * positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "1r5k/Pppppppp/8/8/8/8/1PPPPPPP/K7 w - - 0 1",
        "4r1k1/8/8/8/8/8/3B4/4K3 w - - 0 1",
        "7k/8/8/8/8/8/7B/K6R w - - 0 1",
        "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1",
        "7k/8/5KQ1/8/8/8/8/8 w - - 99 1"
    };
    uint32_t seed = 20261017;
    for (const char * fen : positions) {
        for (int pattern = 0; pattern < 16; ++pattern) {
            for (int hints = 0; hints < 2; ++hints) {
                auto storage = std::make_unique<search_t>(fen);
                auto & search = *storage;
                search.stack->in_check = search.brd.in_check();
                for (int from = 0; from < 64; ++from) {
                    for (int square = 0; square < 64; ++square) {
                        seed = seed * 1664525u + 1013904223u;
                        search.quiet_history->main[search.brd.us()][from][square] = pattern == 0 ? 0
                            : pattern == 1 ? 31 : (seed >> 16) % (pattern + 1);
                    }
                }
                move::list_t quiets;
                move::gen_quiet_moves(&search.brd, &quiets);
                if (hints) {
                    int count = 0;
                    for (auto * move = quiets.first; move != quiets.last && count < 3; ++move) {
                        if (!search.brd.legal(move)) continue;
                        if (count == 0) search.stack->tt_move.set(move);
                        else search.stack->killer[count - 1].set(move);
                        ++count;
                    }
                }
                std::vector<move_t> pending(quiets.first, quiets.last);
                for (auto & move : pending) move.score = search.quiet_score(move);
                std::vector<int> expected;
                // Independent original selection: first maximum, then swap-remove.
                while (!pending.empty()) {
                    size_t best = 0;
                    for (size_t index = 1; index < pending.size(); ++index)
                        if (pending[index].score > pending[best].score) best = index;
                    auto move = pending[best];
                    pending[best] = pending.back();
                    pending.pop_back();
                    if (!search.stack->tt_move.equals(&move) && !search.is_killer(&move)
                            && search.brd.legal(&move)) expected.push_back(move.to_int());
                }
                for (int depth : {0, 3, -1}) {
                    std::vector<int> actual;
                    for (auto * move = move::first(&search, depth); move; move = move::next(&search, depth)) {
                        if (!search.brd.legal(move)) {
                            continue;
                        }
                        if (!move->capture && !move->promotion && !move->castle
                                && !search.stack->tt_move.equals(move) && !search.is_killer(move)) {
                            actual.push_back(move->to_int());
                        }
                    }
                    const bool enabled = depth >= 0 || search.stack->in_check;
                    bool ordered = true;
                    int previous_score = std::numeric_limits<int>::max();
                    for (int code : actual) {
                        move_t move;
                        move.set(code);
                        const int value = search.quiet_score(move);
                        ordered &= value <= previous_score;
                        previous_score = value;
                    }
                    auto sorted_expected = enabled ? expected : std::vector<int>{};
                    std::sort(actual.begin(), actual.end());
                    std::sort(sorted_expected.begin(), sorted_expected.end());
                    if (!ordered || actual != sorted_expected) {
                        std::cerr << "quiet ordering changed at " << fen << " pattern " << pattern
                            << " hints " << hints << " depth " << depth << '\n';
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

}

int main() {
    magic::init();
    if (!test_empty_tactical_bounds()) {
        return EXIT_FAILURE;
    }
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
    success &= test_quiet_order();
    success &= test_pruning_terminals();
    success &= test_quiet_evasion();
    success &= test_capture();
    success &= test_capture_stalemate();
    success &= test_recursive_stalemate();
    success &= test_quiet_playable();
    success &= test_losers_root_scores();
    success &= test_recapture();
    success &= test_material_draws();
    success &= test_fifty_move_boundary();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
