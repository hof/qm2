#pragma once

#include "engine.h"

inline void start_new_search(search_t & search, std::vector<U64> & position_history) {
    for (int ply = 0; ply < search.brd.ply; ++ply) {
        position_history.push_back(search.brd._stack[ply].repetition_key());
    }

    const auto fen = search.brd.to_string();
    search.init(fen.c_str(), nullptr, nullptr, position_history);
}
