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
    // Separate domains keep unlike features and continuation distances independent.
    int16_t pieces[4][2][size] = {}; //white/black nonpawn, minor, major
    int16_t continuation[2][2][size] = {};

    static int bonus_for(int depth, int error) {
        return std::clamp(std::clamp(error, -32768, 32768) * std::clamp(depth, 1, 16) / 8,
            -limit / 4, limit / 4);
    }

    static bool update_entry(int16_t & entry, int bonus) {
        const int before = entry;
        entry += bonus - int(entry) * std::abs(bonus) / limit;
        return entry != before;
    }

    int correction(bool side, U64 pawn_key) const {
        return entries[side][pawn_key & (size - 1)] / grain;
    }

    bool update(bool side, U64 pawn_key, int depth, int error) {
        return update_entry(entries[side][pawn_key & (size - 1)], bonus_for(depth, error));
    }
};

#endif
