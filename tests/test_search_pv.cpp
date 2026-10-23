#include "engine.h"

namespace {

const char * start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char * pv_moves[] = {"e2e4", "e7e5", "g1f3", "b8c6"};
const char * stale_moves[] = {"d2d4", "e7e6", "b1c3", "g8f6"};

enum class table_state_t { empty, stale, matching };

bool test_store_pv(table_state_t state) {
    trans_table::clear();
    search_t search(start_fen);
    board_t board;
    board.init(start_fen);
    for (int i = 0; i < 4; ++i) {
        move_t & move = search.stack->pv_moves[i];
        move.set(&board, pv_moves[i]);
        if (!board.valid(&move) || !board.legal(&move)) {
            std::cerr << "invalid test PV move: " << pv_moves[i] << "\n";
            return false;
        }
        if (i > 0 && state != table_state_t::empty) {
            move_t stored_move;
            stored_move.set(&board, state == table_state_t::matching ? pv_moves[i] : stale_moves[i]);
            trans_table::store(board.stack->tt_key, 0, 0, 7, 123 + i,
                stored_move.to_compact(), score::EXACT);
        }
        board.forward(&move);
    }
    search.stack->pv_count = 4;
    const U64 root_key = search.brd.stack->tt_key;
    search.store_pv();
    bool success = search.brd.to_string() == start_fen
        && search.brd.stack->tt_key == root_key && search.brd.ply == 0
        && search.stack == search.root_stack && search.stack->pv_count == 4;
    board.init(start_fen);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            tt_hit_t hit;
            const bool found = trans_table::probe(board.stack->tt_key, 0, 0, hit);
            const int score = hit.score, move = hit.move, flag = hit.bound;
            const bool correct_move = found && move == search.stack->pv_moves[i].to_compact();
            // A changed hint does not invalidate an existing score for this board.
            // A new hint alone must not invent a scored result.
            const bool correct_bound = state != table_state_t::empty
                ? score == 123 + i && flag == score::EXACT
                : flag == 0;
            success &= correct_move && correct_bound;
            if (!correct_move || !correct_bound) {
                std::cerr << "PV storage case " << static_cast<int>(state)
                    << ", ply " << i << ": expected " << pv_moves[i]
                    << ", stored move=" << move << ", score=" << score
                    << ", flag=" << flag << "\n";
            }
            success &= state != table_state_t::empty ? hit.depth == 7 : hit.depth == 0;
        }
        board.forward(&search.stack->pv_moves[i]);
    }
    return success;
}

// Fixed child results isolate root reporting from timing, TT state and evaluation.
class reporting_search_t : public search_t {
    int interrupt_depth;
    int completed_children;
    int children = 0;
public:
    reporting_search_t(int depth, int count) : search_t(start_fen),
        interrupt_depth(depth), completed_children(count) {
        game.max_depth = 2;
        aspiration_enabled = false;
    }
    int pvs(int, int, int depth, bool = false) override {
        nodes += 10;
        pruned_nodes += 7;
        stack->pv_count = 0;
        if (depth + 1 == interrupt_depth && children++ == completed_children) {
            stop_all = true;
            return 0;
        }
        return depth == 0 ? -100 : -200;
    }
};

bool test_root_reporting(int interrupt_depth, int children, int expected_depth,
        const char * provenance, const char * expected_score) {
    reporting_search_t search(interrupt_depth, children);
    std::ostringstream output;
    auto * original = std::cout.rdbuf(output.rdbuf());
    search.go();
    std::cout.rdbuf(original);
    std::istringstream lines(output.str());
    std::string line, final_report;
    bool success = search.stack->best_move.piece != 0;
    while (std::getline(lines, line)) {
        if (line.starts_with("info depth ")) {
            final_report = line;
        }
    }
    success &= final_report.starts_with("info depth " + std::to_string(expected_depth) + " ");
    success &= final_report.find(" nodes " + std::to_string(search.nodes) + " ") != std::string::npos;
    success &= output.str().find(std::string("info string search_result ") + provenance) != std::string::npos;
    if (expected_score) {
        success &= final_report.find(expected_score) != std::string::npos;
    } else {
        success &= final_report.find(" score ") == std::string::npos;
    }
    if (!success) {
        std::cerr << "incorrect root reporting:\n" << output.str();
    }
    return success;
}

class bounded_search_t : public search_t {
    int child_score;
public:
    explicit bounded_search_t(int value) : search_t(start_fen), child_score(value) {}
    int pvs(int, int, int, bool = false) override {
        ++nodes;
        stack->pv_count = 0;
        return -child_score;
    }
};

bool test_partial_bounds(int value, bool fail_low) {
    bounded_search_t search(value);
    search.init_root_moves();
    std::ostringstream output;
    auto * original = std::cout.rdbuf(output.rdbuf());
    search.pvs_root(fail_low ? value + 1 : value - 2, fail_low ? value + 2 : value - 1, 6);
    std::cout.rdbuf(original);
    // A fail-low result for just the first move says nothing about other moves.
    const bool success = fail_low ? output.str().find(" score ") == std::string::npos
        : output.str().find(" lowerbound ") != std::string::npos;
    if (!success) {
        std::cerr << "incorrect partial aspiration bound:\n" << output.str();
    }
    return success;
}

}

int main() {
    options::get_option("OwnBook")->value = 0;
    magic::init();
    trans_table::set_size(1);
    bool success = test_store_pv(table_state_t::empty);
    success &= test_store_pv(table_state_t::stale);
    success &= test_store_pv(table_state_t::matching);
    success &= test_root_reporting(1, 0, 0, "fallback", nullptr);
    success &= test_root_reporting(1, 1, 1, "partial", "score cp 100 lowerbound");
    success &= test_root_reporting(2, 0, 1, "complete", "score cp 100 nodes");
    success &= test_root_reporting(2, 1, 2, "partial", "score cp 200 lowerbound");
    success &= test_root_reporting(3, 0, 2, "complete", "score cp 200 nodes");
    for (int value : {100, -100, score::MATE - 3, -score::MATE + 3}) {
        success &= test_partial_bounds(value, false);
        success &= test_partial_bounds(value, true);
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
