#ifndef MAXIMA_REPETITION_H
#define MAXIMA_REPETITION_H

#include "board.h"

namespace repetition {
// Decode a reversible non-pawn move delta; accept only a legal quiet move.
bool find_move(board_t & board, U64 delta, move_t & move);
}

#endif
