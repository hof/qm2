#ifndef MAXIMA_EVAL_H
#define MAXIMA_EVAL_H

#include "board.h"

class search_t;

int scale_network_score(int raw, const board_t & board, int halfmoves);
int evaluate(search_t * search);

#endif
