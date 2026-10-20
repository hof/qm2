#ifndef MAXIMA_CORRECTION_HISTORY_H
#define MAXIMA_CORRECTION_HISTORY_H

#include "board.h"
#include <algorithm>

// Side-to-move pawn patterns share a bounded estimate of static-eval error.
// Collisions are intentional; this is a heuristic, never a score certificate.
struct correction_history_t {
    static constexpr int size = 16384;
    static constexpr int limit = 16384;
    static constexpr int grain = 128;
    int16_t entries[2][size] = {};

    int correction(bool side, U64 pawn_key) const {
        return entries[side][pawn_key & (size - 1)] / grain;
    }

    bool update(bool side, U64 pawn_key, int depth, int error) {
        const int bonus = std::clamp(std::clamp(error, -32768, 32768) * std::clamp(depth, 1, 16) / 8,
            -limit / 4, limit / 4);
        auto & entry = entries[side][pawn_key & (size - 1)];
        const int before = entry;
        entry += bonus - int(entry) * std::abs(bonus) / limit;
        return entry != before;
    }
};

#endif
