#ifndef MAXIMA_PROGRESS_H
#define MAXIMA_PROGRESS_H

// Halfmove clocks 0..13 share score bounds; later clocks use buckets of eight.
// Scores within a bucket reuse each other; clock ranges still guard draws.
inline int rule50_bucket(int halfmoves) {
    return halfmoves < 14 ? 0 : (halfmoves - 14) / 8 + 1;
}

#endif
