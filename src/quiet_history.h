#ifndef MAXIMA_QUIET_HISTORY_H
#define MAXIMA_QUIET_HISTORY_H

#include "board.h"
#include <algorithm>

// Worker-owned tables. Piece keys include color; pawn buckets intentionally
// share colliding structures, like other bounded history heuristics.
struct quiet_history_t {
    static constexpr int limit = 16384;
    static constexpr int pawn_buckets = 512;
    int16_t main[2][64][64] = {};
    int16_t continuation[BKING + 1][64][BKING + 1][64] = {};
    int16_t pawn[pawn_buckets][BKING + 1][64] = {};

    static void update_entry(int16_t & entry, int bonus) {
        bonus = std::clamp(bonus, -limit, limit);
        entry += bonus - int(entry) * std::abs(bonus) / limit;
    }

    int score(bool us, const move_t & move, const move_t * previous, U64 pawn_key) const {
        int value = main[us][move.ssq][move.tsq]
            + pawn[pawn_key % pawn_buckets][move.piece][move.tsq];
        if (previous && previous->piece) {
            value += continuation[previous->piece][previous->tsq][move.piece][move.tsq];
        }
        return value;
    }

    void update(bool us, const move_t & move, const move_t * previous, U64 pawn_key, int bonus,
            const move_t * protected_move = nullptr) {
        update_entry(main[us][move.ssq][move.tsq], bonus);
        if (protected_move && protected_move->piece == move.piece
                && protected_move->tsq == move.tsq) {
            return;
        }
        update_entry(pawn[pawn_key % pawn_buckets][move.piece][move.tsq], bonus);
        if (previous && previous->piece) {
            update_entry(continuation[previous->piece][previous->tsq][move.piece][move.tsq], bonus);
        }
    }
};

#endif
