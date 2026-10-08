#ifndef MAXIMA_TABLEBASE_H
#define MAXIMA_TABLEBASE_H

#include <string>

class board_t;

namespace tablebase {

// Configuration changes require all searches to have stopped.
bool set_path(const std::string & path);
int max_pieces();

// -3 means unavailable; otherwise -2 loss, -1 blessed loss, 0 draw,
// 1 cursed win, 2 win. WDL probing requires a zero fifty-move clock.
int probe_wdl(board_t & board, int limit, int depth, int probe_depth);

struct root_result_t {
    int from;
    int to;
    int promotion;
    int outcome; // -1 loss, 0 draw under the fifty-move rule, 1 win
};

// DTZ guides progress and accounts for the current fifty-move clock.
bool probe_root(board_t & board, int limit, root_result_t & result);

}
#endif
