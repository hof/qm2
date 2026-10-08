#include "engine.h"

namespace {

// Force a late quiet cutoff while running the real parent's move ordering and
// history updates. Child scores are controlled to make the boundary reproducible.
class cutoff_search_t : public search_t {
    int _cutoff_after;

public:
    int child_calls = 0;

    explicit cutoff_search_t(int cutoff_after)
        : search_t("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
          _cutoff_after(cutoff_after) {
        null_enabled = false;
        beta_pruning = false;
        lmr_enabled = false;
        ffp_enabled = false;
        stack->in_check = brd.in_check();
        for (int piece = WPAWN; piece <= BKING; ++piece) {
            for (int square = 0; square < 64; ++square) {
                history[piece][square] = 1024;
            }
        }
    }

    int pvs(int, int, int) override {
        return child_calls++ == _cutoff_after ? -1 : 0;
    }
};

bool test_late_cutoff(int searched_moves) {
    trans_table::clear();
    cutoff_search_t search(searched_moves);
    const int result = search.search_t::pvs(0, 1, 1);
    const move_t & best = search.stack->best_move;
    bool success = result == 1 && search.child_calls == searched_moves + 1
        && !best.capture && !best.promotion && !best.castle && !search.stop_all;
    int quiet_moves = 0;
    for (int i = 0; i < searched_moves; ++i) {
        const move_t & move = search.stack->searched[i];
        if (!move.capture && !move.promotion && !move.castle) {
            ++quiet_moves;
            // A history value below 2048 must vanish after at least 31 halvings.
            success &= search.history[move.piece][move.tsq] == 0;
        }
    }
    success &= quiet_moves > 0 && search.brd.ply == 0 && search.stack == search.root_stack;
    if (!success) {
        std::cerr << "history penalty after " << searched_moves
            << " moves: score=" << result << ", child calls=" << search.child_calls
            << ", quiet moves=" << quiet_moves << "\n";
    }
    return success;
}

}

int main() {
    magic::init();
    bool success = true;
    for (int count : {31, 32, 33, 40}) {
        success &= test_late_cutoff(count);
    }
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
