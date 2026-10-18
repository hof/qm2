#ifndef MAXIMA_EVAL_H
#define MAXIMA_EVAL_H

#include "board.h"

class search_t;

struct search_metadata_t {
    U64 passed_pawns;
    bool endgame;
    bool operator==(const search_metadata_t &) const = default;
};

search_metadata_t search_metadata(const board_t & board);
int evaluate(search_t * search);

#endif
