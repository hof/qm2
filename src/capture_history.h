#ifndef MAXIMA_CAPTURE_HISTORY_H
#define MAXIMA_CAPTURE_HISTORY_H

#include "board.h"
#include <algorithm>

// Worker-owned history. The move records the EP victim even on an empty target;
// non-capturing promotions use EMPTY, and the mover remains the original pawn.
struct capture_history_t {
    static constexpr int limit = 16384;
    int16_t entries[BKING + 1][64][WKING + 1] = {};

    static int victim(const move_t & move) {
        return move.capture > WKING ? move.capture - WKING : move.capture;
    }

    static bool same_entry(const move_t & a, const move_t & b) {
        return a.piece == b.piece && a.tsq == b.tsq && victim(a) == victim(b);
    }

    int score(const move_t & move) const {
        return entries[move.piece][move.tsq][victim(move)];
    }

    void update(const move_t & move, int bonus) {
        auto & entry = entries[move.piece][move.tsq][victim(move)];
        bonus = std::clamp(bonus, -limit, limit);
        entry += bonus - int(entry) * std::abs(bonus) / limit;
    }
};

#endif
