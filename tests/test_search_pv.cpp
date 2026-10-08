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
                stored_move.to_int(), score::EXACT);
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
            int score = 0, move = 0, flag = 0;
            const bool found = trans_table::retrieve(board.stack->tt_key, 0, 1, score, move, flag);
            const bool correct_move = found && move == search.stack->pv_moves[i].to_int();
            // Matching entries retain their real search bounds. New/replaced moves
            // are ordering hints and must not masquerade as exact evaluations.
            const bool correct_bound = state == table_state_t::matching
                ? score == 123 + i && flag == score::EXACT
                : score == 0 && flag == 0;
            success &= correct_move && correct_bound;
            if (!correct_move || !correct_bound) {
                std::cerr << "PV storage case " << static_cast<int>(state)
                    << ", ply " << i << ": expected " << pv_moves[i]
                    << ", stored move=" << move << ", score=" << score
                    << ", flag=" << flag << "\n";
            }
            if (state == table_state_t::matching) {
                success &= trans_table::retrieve(board.stack->tt_key, 0, 7, score, move, flag);
            }
        }
        board.forward(&search.stack->pv_moves[i]);
    }
    return success;
}

}

int main() {
    magic::init();
    trans_table::set_size(1);
    bool success = test_store_pv(table_state_t::empty);
    success &= test_store_pv(table_state_t::stale);
    success &= test_store_pv(table_state_t::matching);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
