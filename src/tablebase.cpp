#include "tablebase.h"
#include "board.h"
#include "tbprobe.h"
#include <algorithm>

namespace tablebase {
namespace {

struct resources_t {
    ~resources_t() { tb_free(); }
};

bool eligible(board_t & board, int limit) {
    return limit > 0 && board.stack->castling_flags == 0
        && popcnt0(board.all()) <= std::min(limit, int(TB_LARGEST))
        && is_1(board.bb[WKING]) && is_1(board.bb[BKING])
        && !(board.bb[WPAWN] & (RANK_1 | RANK_8))
        && !(board.bb[BPAWN] & (RANK_1 | RANK_8)) && board.legal();
}

}

bool set_path(const std::string & path) {
    static resources_t resources;
    return tb_init(path.empty() || path == "<empty>" ? "" : path.c_str());
}

int max_pieces() {
    return int(TB_LARGEST);
}

int probe_wdl(board_t & board, int limit, int depth, int probe_depth) {
    if (board.stack->fifty_count != 0 || !eligible(board, limit)) {
        return -3;
    }
    if (popcnt0(board.all()) == std::min(limit, max_pieces()) && depth < probe_depth) {
        return -3;
    }
    const unsigned result = tb_probe_wdl(board.all(WHITE), board.all(BLACK),
        board.bb[WKING] | board.bb[BKING], board.bb[WQUEEN] | board.bb[BQUEEN],
        board.bb[WROOK] | board.bb[BROOK], board.bb[WBISHOP] | board.bb[BBISHOP],
        board.bb[WKNIGHT] | board.bb[BKNIGHT], board.bb[WPAWN] | board.bb[BPAWN], 0, 0,
        board.stack->enpassant_sq, board.us());
    return result == TB_RESULT_FAILED ? -3 : int(result) - 2;
}

bool probe_root(board_t & board, int limit, root_result_t & result) {
    if (!eligible(board, limit) || board.stack->fifty_count >= 100) {
        return false;
    }
    TbRootMoves moves;
    // Always rank wins by progress rather than treating all winning moves as equal.
    if (!tb_probe_root_dtz(board.all(WHITE), board.all(BLACK),
        board.bb[WKING] | board.bb[BKING], board.bb[WQUEEN] | board.bb[BQUEEN],
        board.bb[WROOK] | board.bb[BROOK], board.bb[WBISHOP] | board.bb[BBISHOP],
        board.bb[WKNIGHT] | board.bb[BKNIGHT], board.bb[WPAWN] | board.bb[BPAWN], board.stack->fifty_count, 0,
            board.stack->enpassant_sq, board.us(), true, true, &moves) || moves.size == 0) {
        return false;
    }
    const TbRootMove * best = &moves.moves[0];
    for (unsigned i = 1; i < moves.size; ++i) {
        if (moves.moves[i].tbRank > best->tbRank) {
            best = &moves.moves[i];
        }
    }
    // Rounded DTZ can be ambiguous exactly at the fifty-move boundary.
    if (best->tbRank == 900 || best->tbRank == -900) {
        return false;
    }
    const int promotions[] = {0, QUEEN[board.us()], ROOK[board.us()],
        BISHOP[board.us()], KNIGHT[board.us()]};
    const unsigned promotion = TB_MOVE_PROMOTES(best->move);
    if (promotion > 4) {
        return false;
    }
    result = {int(TB_MOVE_FROM(best->move)), int(TB_MOVE_TO(best->move)),
        promotions[promotion], best->tbRank > 900 ? 1 : best->tbRank < -900 ? -1 : 0};
    return true;
}

}
