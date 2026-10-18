#ifndef MAXIMA_ENDGAME_H
#define MAXIMA_ENDGAME_H
#include "board.h"
namespace eg {
    // -1 outside KPK, 0 draw, 1 pawn-side win; no fifty-move distance data.
    int probe_kpk(board_t & board);
}
#endif
