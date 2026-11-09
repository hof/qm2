/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2026 Erik van het Hof and Hermen Reitsma 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, If not, see <http://www.gnu.org/licenses/>.
 *  
 * File: timeman.h
 * Time Manager, calculating how much time to spent on thinking, this is roughly 
 * the time left shared by the amount of moves left. The amount of moves
 * left is assumed to be no more than 30. 
 * In emergency situations more time can be used. For easy moves less time can be used.
 *
 * Created on 16 mei 2011, 22:37
 */

#ifndef TIMEMAN_H
#define	TIMEMAN_H

#include "bits.h"
#include <algorithm>
#include <chrono>
#include <limits>

class time_manager_t;

namespace time_man {
    const int INFINITE_TIME = 24 * 60 * 60 * 1000;
    const int M = 16; //assume game is decided after M moves from now (UCI TimeMoves); budget clock/2M, cap clock/(M/2)
    const int LOW_TIME = 120000; //two minutes, the budget shrinks gradually below this
    const int MIN_SEARCH_TIME = 50; //floor on the budget, never above 1/16 of the clock

    /*
     * Expected moves remaining in a standard game, from 302 Lichess bot games of 2026-11-09: about 50 early,
     * 32 at move 40, 27 at move 60 and 16-18 from move 80 on. Over three pawns the game is decided sooner;
     * between half a pawn and three, one side presses and it lasts longer. Opponent strength barely mattered.
     */
    inline int horizon(int move_number, int score) {
        const int moves = MAX(18, 50 - move_number * 2 / 5);
        const int size = ABS(score);
        const int scaled = size > 300 ? moves * 3 / 5 : size > 50 ? moves * 6 / 5 : moves;
        return MIN(50, MAX(16, scaled));
    }
};

class time_manager_t {
private:
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point max;
    int tot_min;
    int tot_max;
    int horizon_moves;

public:
    time_manager_t();
    void clear();
    void set(const int my_time, const int opp_time, const int my_inc, const int opp_inc, const int moves_left,
        const int move_overhead = 50, const int time_factor = 100, const int m = time_man::M, const int lag = 0,
        const int horizon = 0);
    
    void set_start(std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now()) {
        start = started;
    }

    void set_min(const int ms) {
        tot_min = ms;
    }

    void set_max(const int ms) {
        tot_max = ms;
        max = start + std::chrono::milliseconds(ms);
    }

    bool time_is_up() const {
        return std::chrono::steady_clock::now() >= max;
    }

    // Poll timing only when due; the caller owns these per-worker counters.
    bool handle_abort(bool pondering, int & next_poll);

    int remaining() const {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            max - std::chrono::steady_clock::now()).count();
        return static_cast<int>(std::clamp<int64_t>(ms,
            std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    }

    int elapsed() const {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        return static_cast<int>(std::min<int64_t>(ms, std::numeric_limits<int>::max()));
    }

    int reserved_min() const {
        return tot_min;
    }

    int reserved_max() const {
        return tot_max;
    }

    int horizon() const {
        return horizon_moves;
    }
};

#endif /* TIMEMANAGER_H */
