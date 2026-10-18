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
    if (!check("test sequence must contain legal moves",
        search.brd.valid(&move) && search.brd.legal(&move))) {
        std::exit(EXIT_FAILURE);
    }
    search.forward(&move, search.brd.gives_check(&move));
}

bool test_owned_history() {
    board_t board;
    board.init(start_fen);
    std::vector<U64> history;
    for (int occurrence = 0; occurrence < 2; ++occurrence) {
        for (const char * text : cycle) {
            history.push_back(board.stack->repetition_key());
            move_t move;
            move.set(&board, text);
            board.forward(&move);
        }
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
    const std::vector<U64> wrong_side_history = {
        key ^ 1, key, key ^ 1, key, key ^ 1, key, key ^ 1, key};
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
    bool success = check("first return to the root below root must score a draw",
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
    success &= check("rebasing must preserve twofold history without declaring a draw",
        search.brd.ply == 0 && !search.is_repetition());
    for (const char * text : cycle) {
        play(search, text);
    }
    success &= check("root and pre-root matches together must count as threefold",
        search.is_repetition());
    search.reset_stack();
    success &= check("rebasing the search stack must retain threefold history",
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
    success &= check("undoing null moves must restore first-repeat detection", search.is_repetition());
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

bool test_cycle_after_pawn_push() {
    search_t search(start_fen);
    play(search, "e2e4");
    const char * moves[] = {"g8f6", "g1f3", "f6g8", "f3g1"};
    for (const char * text : moves) {
        play(search, text);
    }
    bool success = check("an unavailable EP marker must not hide the first repeated position",
        search.is_repetition());
    std::vector<U64> history;
    for (int ply = 1; ply < search.brd.ply; ++ply) {
        history.push_back(search.brd._stack[ply].repetition_key());
    }
    search_t supplied(search.brd.to_string().c_str(), nullptr, nullptr, history);
    success &= check("a supplied twofold EP history must not declare a draw", !supplied.is_repetition());
    search.reset_stack();
    success &= check("rebasing turns a search cycle into twofold game history", !search.is_repetition());
    for (const char * text : moves) {
        play(search, text);
    }
    for (int ply = 0; ply < search.brd.ply; ++ply) {
        history.push_back(search.brd._stack[ply].repetition_key());
    }
    search_t supplied_threefold(search.brd.to_string().c_str(), nullptr, nullptr, history);
    success &= check("supplied threefold history must normalize unavailable EP",
        supplied_threefold.is_repetition());
    search.reset_stack();
    success &= check("rebasing must preserve normalized EP for genuine threefold", search.is_repetition());
    search_t available("7k/8/8/3pP3/8/8/8/K7 w - d6 0 1");
    for (int occurrence = 0; occurrence < 2; ++occurrence) {
        for (const char * text : {"a1a2", "h8h7", "a2a1", "h7h8"}) {
            play(available, text);
        }
        available.reset_stack();
    }
    success &= check("an expired legal EP capture must distinguish positions", !available.is_repetition());
    for (const char * text : {"a1a2", "h8h7", "a2a1", "h7h8"}) {
        play(available, text);
    }
    available.reset_stack();
    success &= check("three occurrences without legal EP must still count", available.is_repetition());
    return success;
}

bool test_pre_root_matches() {
    board_t board;
    board.init(start_fen);
    std::vector<U64> history;
    bool success = true;
    // Stop one move before returning to startpos. The matching occurrence is
    // before the search root, while the node being tested is inside the search.
    for (int occurrence = 0; occurrence < 2; ++occurrence) {
        for (int i = 0; i < 3; ++i) {
            history.push_back(board.stack->repetition_key());
            move_t move;
            move.set(&board, cycle[i]);
            board.forward(&move);
        }
        search_t search(board.to_string().c_str(), nullptr, nullptr, history);
        play(search, cycle[3]);
        success &= check("first pre-root match must draw a search descendant",
            search.is_repetition());
        search.draw_contempt = 0;
        success &= check("first pre-root repeat must score draw in quiescence and PVS",
            search.qsearch(-score::INF, score::INF, 0) == 0
            && search.pvs(-score::INF, score::INF, 1) == 0);
        history.push_back(board.stack->repetition_key());
        move_t move;
        move.set(&board, cycle[3]);
        board.forward(&move);
    }
    return success;
}

bool test_repetition_scores() {
    search_t search("7k/8/8/8/8/8/8/KR6 w - - 0 1");
    search.draw_contempt = 0;
    bool success = true;
    for (int occurrence = 0; occurrence < 2; ++occurrence) {
        for (const char * text : {"a1a2", "h8h7", "a2a1", "h7h8"}) {
            play(search, text);
        }
        search.reset_stack();
        search.init_root_moves();
        success &= check("only genuine threefold game history must score as a draw",
            search.is_draw() == (occurrence == 1));
        const std::string fen = search.brd.to_string();
        const U64 key = search.brd.stack->repetition_key();
        const int score = search.qsearch(-score::INF, score::INF, 0);
        success &= check(occurrence == 0
            ? "quiescence must retain a rook advantage after twofold repetition"
            : "quiescence must score genuine threefold repetition as a draw",
            occurrence == 0 ? score > 200 : score == 0);
        trans_table::clear();
        const int full_score = search.pvs(-score::INF, score::INF, 1);
        success &= check(occurrence == 0
            ? "one-ply search must retain a rook advantage after twofold repetition"
            : "one-ply search must score genuine threefold repetition as a draw",
            occurrence == 0 ? full_score > 200 : full_score == 0);
        success &= check("repetition score checks must finish and restore the root",
            !search.stop_all && search.brd.ply == 0
            && search.brd.to_string() == fen && search.brd.stack->repetition_key() == key
            && search.stack == search.root_stack);
    }
    return success;
}

bool matches_fresh(board_t & board) {
    board_t fresh;
    fresh.init(board.to_string().c_str());
    return check("incremental EP state must match a fresh FEN",
        board.stack->tt_key == fresh.stack->tt_key
        && board.stack->repetition_key() == fresh.stack->repetition_key()
        && board.stack->legal_enpassant == fresh.stack->legal_enpassant);
}

bool test_enpassant_identity() {
    struct ep_case_t {
        const char * position;
        const char * target;
        bool legal;
    };
    const ep_case_t cases[] = {
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq", "e3", false},
        {"rnbqkbnr/pppp1ppp/8/4p3/8/8/PPPPPPPP/RNBQKBNR w KQkq", "e6", false},
        {"7k/8/8/3pP3/8/8/8/K7 w -", "d6", true},
        {"k7/8/8/8/3Pp3/8/8/7K b -", "d3", true},
        {"k3r3/8/8/3pP3/8/8/8/4K3 w -", "d6", false},
        {"4k3/8/8/8/3Pp3/8/8/K3R3 b -", "d3", false},
        {"7k/8/8/r4pPK/8/8/8/8 w -", "f6", false},
        {"8/8/8/8/R4Ppk/8/8/7K b -", "f3", false},
        {"7k/6b1/8/3pP3/8/2K5/8/8 w -", "d6", false},
        {"k3r3/8/8/2PpP3/8/8/8/4K3 w -", "d6", true},
        {"7k/8/8/3pP3/4K3/8/8/8 w -", "d6", true},
    };
    bool success = true;
    for (const ep_case_t & test : cases) {
        const std::string fen = std::string(test.position) + " " + test.target + " 0 1";
        const std::string without_ep = std::string(test.position) + " - 0 1";
        board_t board, plain;
        board.init(fen.c_str());
        plain.init(without_ep.c_str());
        success &= check(fen.c_str(), board.stack->legal_enpassant == test.legal
            && (board.stack->repetition_key() != plain.stack->repetition_key()) == test.legal
            && board.to_string() == fen);
        const U64 key = board.stack->repetition_key();
        board.forward();
        success &= matches_fresh(board);
        board.backward();
        success &= check("undoing null must restore EP identity", board.stack->repetition_key() == key);
        board.do_flip();
        plain.do_flip();
        success &= check("flipped EP legality must match the cached flag",
            board.has_legal_enpassant() == board.stack->legal_enpassant);
        success &= check("flipping must preserve repetition identity with unavailable EP",
            (board.stack->repetition_key() != plain.stack->repetition_key()) == test.legal);
        board.do_flip();
        success &= matches_fresh(board);
    }
    for (bool pinned : {false, true}) {
        const char * fen = pinned ? "k3r3/3p4/8/4P3/8/8/8/4K3 b - - 0 1"
            : "k7/3p4/8/4P3/8/8/8/4K3 b - - 0 1";
        board_t board;
        board.init(fen);
        const U64 root_key = board.stack->repetition_key();
        move_t push, reply;
        push.set(&board, "d7d5");
        board.forward(&push);
        success &= check("double push must cache EP legality", board.stack->legal_enpassant == !pinned);
        success &= matches_fresh(board);
        reply.set(&board, pinned ? "e1f1" : "e5d6");
        success &= board.valid(&reply) && board.legal(&reply);
        board.forward(&reply);
        success &= matches_fresh(board);
        board.backward(&reply);
        success &= matches_fresh(board);
        board.backward(&push);
        success &= check("undoing the push must restore the root",
            board.to_string() == fen && board.stack->repetition_key() == root_key);
    }
    return success;
}

}

int main() {
    magic::init();
    bool success = test_owned_history();
    success &= test_search_path();
    success &= test_null_moves_and_bounds();
    success &= test_cycle_after_pawn_push();
    success &= test_enpassant_identity();
    success &= test_pre_root_matches();
    success &= test_repetition_scores();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
