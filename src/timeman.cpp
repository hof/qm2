/**
 * Maxima, a chess playing program. 
 * Copyright (C) 1996-2014 Erik van het Hof and Hermen Reitsma 
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
 * File: timeman.cpp
 * Time Manager, calculating how much time to spent on thinking, this is roughly 
 * the time left shared by the amount of moves left. The amount of moves
 * left is assumed to be no more than 30. 
 * In emergency situations more time can be used. For easy moves less time can be used.
 */

#include "timeman.h"

void time_manager_t::set(int my_time, int opp_time, int my_inc, int opp_inc, int moves_left) {
    int M = my_inc > 0 ? 25 : 30; //assume the game is decided after this amount of moves
    M = moves_left ? MIN(moves_left + 1, M) : M; //for classic time controls (X moves in Y minutes)
    int limit = my_time - time_man::OVERHEAD_TIME;

    /*
     * Base time is our time left shared by the amount of moves left
     */
    int result = limit / M;

    /*
     * Bonus/Penalty time when we have more/less time left than the opponent
     */
    if (my_time > opp_time && opp_time > 0 && my_inc >= opp_inc && M > 2) {
        double factor = MIN(10.0, (1.0 * my_time) / (1.0 * opp_time));
        result = factor * result;
    } else if (my_time < opp_time && opp_time > 0 && my_inc <= opp_inc && M > 2) {
        double factor = MAX(0.25, (1.0 * my_time) / (1.0 * opp_time) - 0.1);
        result = factor*result;
    }

    /*
     * Bonus time when we have an increment available
     */
    result += my_inc;

    /*
     * Make sure the time never exceeds the limit
     */
    result = MIN(result, limit);

    /*
     * Determine how much more time to give for difficult positions 
     * (never exceeding the limit)
     */
    int max_result = MIN(result * time_man::EMERGENCY_FACTOR, limit);
    this->set_end(result);
    this->set_max(max_result);
}

time_manager_t::time_manager_t() {
    start = 0;
    end = 0;
    max = 0;
}

bool time_manager_t::request_more() {
    clock_t ticks_available = max - end;
    if (ticks_available > ticks(500)) {
        end += (ticks_available / 2);
        return true;
    }
    end = max;
    return false;
}

bool time_manager_t::request_less() {
    if (max != end) {
        clock_t ticks_available = end - start;
        if (ticks_available > ticks(0)) {
            end -= ticks_available / 2;
            return true;
        }
    }
    return false;
}

namespace time_man {
    time_manager_t _timeman;
    
    time_manager_t * instance() {
        return & _timeman;
    }
    
    void set(int my_time, int opp_time, int my_inc, int opp_inc, int moves_left) {
        _timeman.set(my_time, opp_time, my_inc, opp_inc, moves_left);
    }
    
    bool request_more() {
        return _timeman.request_more();
    }
    
    bool request_less() {
        return _timeman.request_less();
    }
    
    bool time_is_up() {
        return _timeman.time_is_up();
    }
    
    int elapsed() {
        return _timeman.elapsed();
    }
};
