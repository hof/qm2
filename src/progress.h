#ifndef MAXIMA_PROGRESS_H
#define MAXIMA_PROGRESS_H

#include <algorithm>

// Ten-halfmove steps: 0..9 retain full evaluation, 10..19 retain 90%, etc.
// This is search policy; raw NNUE output and repetition identity stay unchanged.
inline int rule50_bucket(int halfmoves) {
    return std::clamp(halfmoves, 0, 100) / 10;
}

inline int damp_rule50(int value, int halfmoves) {
    return value * (100 - 10 * rule50_bucket(halfmoves)) / 100;
}

#endif
